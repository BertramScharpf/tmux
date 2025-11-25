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

#include <sys/types.h>

#include <stdlib.h>
#include <time.h>

#include "tmux.h"
#include "ruby.h"

/*
 * Runs a Ruby expression
 */

static enum cmd_retval	cmd_run_ruby_exec(struct cmd *,
			    struct cmdq_item *);

const struct cmd_entry cmd_run_ruby_entry = {
	.name = "run-ruby",
	.alias = "ruby",

	.args = { "npFc:t:", 1, 1, NULL },
	.usage = "[-npF] [-c target-client] "
		 CMD_TARGET_PANE_USAGE " [message]",

	.target = { 't', CMD_FIND_PANE, CMD_FIND_CANFAIL },

	.flags = CMD_CLIENT_CFLAG|CMD_CLIENT_CANFAIL,
	.exec = cmd_run_ruby_exec
};


static enum cmd_retval
cmd_run_ruby_exec(struct cmd *self, struct cmdq_item *item)
{
	struct args		*args = cmd_get_args(self);
	struct client		*tc = cmdq_get_target_client(item);
	const char		*expr;
	char			*exprf = NULL;
	char			*eval, *res;
	int			 err;

	expr = args_string(args, 0);
	if (args_has(args, 'F')) {
		struct format_tree	*ft;

		ft = format_create_from_target(item);
		expr = exprf = format_expand(ft, expr);
		format_free(ft);
	}
	ruby_start_server();
	eval = ruby_eval_str(expr, &err);
	free(exprf);

	res = !args_has(args, 'n') || err ? eval : NULL;
	if (res) {
		if (args_has(args, 'p')) {
			if (err)
				cmdq_print(item, "Ruby Error");
			cmdq_print(item, "%s", res);
		} else if (err || tc == NULL)
			cmdq_error(item, "%s", res);
		else if (tc->flags & CLIENT_CONTROL) {
			struct evbuffer		*evb;

			evb = evbuffer_new();
			if (evb == NULL)
				fatalx("out of memory");
			evbuffer_add_printf(evb, "%%ruby-result %s", res);
			server_client_print(tc, 0, evb);
			evbuffer_free(evb);
		} else
			status_message_set(tc, -1, 0, 0, 0, "%s", res);
	}
	free(eval);

	return (CMD_RETURN_NORMAL);
}
