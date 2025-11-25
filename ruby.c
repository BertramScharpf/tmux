/*
 * Copyright (c) 2025 Bertram Scharpf <software@bertram-scharpf.de>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "ruby.h"

#include "tmux.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>


static void		 ruby_set_env(void);
static void		 ruby_spawn(void);
static void		 ruby_open(void);
static int		 ruby_write_u32(unsigned long);
static unsigned long	 ruby_read_u32(void);
static int		 ruby_write_query(const char *);
static char		*ruby_read_answer(int *);


static const char ruby_env[] = "TMUX_RUBY";

static const char ruby_command[] =
/* ruby -ne 'next if ~/^$/ ; puts "\t" + $_.inspect' ruby_server.rb */
/* ruby -ne 'next $_ unless ~%r/^static .* ruby_command.*=/ ... ~%r/^\s*"";$/ ; puts eval $_ if ~/^\s*".*\\n"$/' ruby.c >ruby_server.rb */
	"require 'socket'\n"
	"class BasicSocket\n"
	"  private def with_close socket\n"
	"    if block_given? then\n"
	"      begin\n"
	"        yield socket\n"
	"      ensure\n"
	"        socket.close\n"
	"      end\n"
	"    else\n"
	"      socket\n"
	"    end\n"
	"  end\n"
	"end\n"
	"class TCPServer\n"
	"  alias accept_orig accept\n"
	"  private :accept_orig\n"
	"  def accept &block ; with_close accept_orig, &block ; end\n"
	"end\n"
	"class UNIXServer\n"
	"  alias accept_orig accept\n"
	"  private :accept_orig\n"
	"  def accept &block ; with_close accept_orig, &block ; end\n"
	"end\n"
	"module Tmux\n"
	"  class Server\n"
	"    class Timeout < Exception ; end\n"
	"    LIMIT = 1024\n"
	"    def run\n"
	"      sockfile = ENV['TMUX_RUBY']\n"
	"      File.unlink sockfile rescue nil\n"
	"      UNIXServer.open sockfile do |srv|\n"
	"        srv.accept do |acc|\n"
	"          loop do\n"
	"            qu = read_expr acc\n"
	"            qu or break\n"
	"            re = eval_expr qu\n"
	"            write_result acc, *re\n"
	"          rescue\n"
	"            break\n"
	"          end\n"
	"        end\n"
	"      end\n"
	"    rescue Errno::EPIPE  # SIGPIPE is disabled by the parent process.\n"
	"    ensure\n"
	"      File.unlink sockfile rescue nil\n"
	"    end\n"
	"    private\n"
	"    def read_expr conn\n"
	"      rel = conn.read 4\n"
	"      len, = rel.to_s.unpack 'L<'\n"
	"      return if len == 0xffffffff\n"
	"      conn.read len\n"
	"    end\n"
	"    def eval_expr str\n"
	"      to = Thread.new do sleep 1 ; Thread.main.raise Timeout end\n"
	"      begin\n"
	"        res = TOPLEVEL_BINDING.eval str\n"
	"      rescue Exception\n"
	"        res, err = $!, true\n"
	"      end\n"
	"      to.kill\n"
	"      res = res.to_s.slice 0, LIMIT\n"
	"      [ res, err]\n"
	"    end\n"
	"    def write_result conn, res, err\n"
	"      conn.write [(err ? 1 : 0)].pack 'C'\n"
	"      conn.write [res.bytesize].pack 'L<'\n"
	"      conn.write res\n"
	"    end\n"
	"  end\n"
	"  Server.new.run\n"
	"end\n"
	"";

static pid_t ruby_pid = 0;
static int   ruby_sfd = -1;


void
ruby_start_server(void)
{
	if (ruby_pid != 0)
		return;
	ruby_set_env();
	ruby_spawn();
	ruby_open();
}

void
ruby_set_env(void)
{
	char *ruby_socket;

	xasprintf(&ruby_socket, "%s-ruby", socket_path);
	setenv(ruby_env, ruby_socket, 1);
	free(ruby_socket);
}

void
ruby_spawn(void)
{
	int stdin_pipe[2];

	if (pipe(stdin_pipe) != 0)
		return;
	ruby_pid = fork();
	if (ruby_pid < 0)
		return;
	if (ruby_pid == 0) {
		int dev_null;

		dup2(stdin_pipe[0], STDIN_FILENO);
		close(stdin_pipe[1]);
		dev_null = open("/dev/null", O_WRONLY);
		dup2(dev_null, STDOUT_FILENO);
		dup2(dev_null, STDERR_FILENO);
		close(dev_null);
		execlp(RUBYPROG, "ruby", NULL);
		exit(1);
	}
	close(stdin_pipe[0]);
	if (write(stdin_pipe[1], ruby_command, strlen(ruby_command)) <= 0)
		return;
	close(stdin_pipe[1]);
}

void
ruby_open(void)
{
	char *ruby_socket;
	struct sockaddr_un addr;
	unsigned int j;

	ruby_sfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (ruby_sfd == -1)
		return;

	memset(&addr, 0, sizeof (struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	ruby_socket = getenv(ruby_env);
	for (j = 0; j < sizeof addr.sun_path && (addr.sun_path[ j] = ruby_socket[ j]); ++j)
		;
	/* Sorry, I rather preferred the strncpy-strlen solution, but I couldn't
	** get rid of the GCC stringop-truncation warning.
	**
	** strncpy(addr.sun_path, ruby_socket, strlen(ruby_socket) + 1);
	**/
	if (j < sizeof addr.sun_path) {
		for (int i = 100; i > 0; --i) {
			int c;
			usleep(10000);
			c = connect(ruby_sfd, (const struct sockaddr *) &addr, sizeof (struct sockaddr_un));
			if (c >= 0)
				return;
		}
	}
	ruby_sfd = -1;
}

void
ruby_kill_server(void)
{
	int status;

	if (ruby_pid == 0)
		return;
	ruby_write_u32(0xffffffff);
	close(ruby_sfd);
	ruby_sfd = -1;
	for (int i = 100; i > 0; --i) {
		int w;
		usleep(1000);
		w = waitpid(ruby_pid, &status, WNOHANG);
		if (w > 0)
			break;
	}
	unsetenv(ruby_env);
	ruby_pid = 0;
}


int
ruby_write_u32(unsigned long l)
{
	unsigned int u;
	unsigned char lc[4];

	u = l;
	for (int i = 0; i < 4; i++) {
		lc[i] = u & 0xff;
		u >>= 8;
	}
	return write(ruby_sfd, lc, 4);
}

unsigned long
ruby_read_u32(void)
{
	unsigned char lc[4];
	int l = 0;

	if (read(ruby_sfd, lc, 4) >= 0) {
		for (int j = 4; j;) {
			l <<= 8;
			l |= lc[--j];
		}
	}
	return l;
}

int
ruby_write_query(const char *str)
{
	unsigned int len;
	int ret;

	len = strlen(str);
	ret = ruby_write_u32(len);  /* SIGPIPE is disabled generally by Tmux. */
	if (ret < 0) {
		ruby_kill_server();
		return ret;
	}
	return write(ruby_sfd, str, len);
}

char *
ruby_read_answer(int *err)
{
	unsigned char ech;
	char *res = NULL;

	if (read(ruby_sfd, &ech, 1) >= 0) {
		unsigned int len;

		if (err != NULL)
			*err = ech;
		len = ruby_read_u32();
		res = xmalloc(len + 1);
		if (read(ruby_sfd, res, len) >= 0)
			res[len] = '\0';
		else {
			free(res);
			res = NULL;
		}
	}
	return res;
}

char *
ruby_eval_str(const char *str, int *err)
{
	char *res = NULL;

	if (ruby_write_query(str) >= 0)
		res = ruby_read_answer(err);
	if (!res)
		res = xstrdup("");
	return res;
}
