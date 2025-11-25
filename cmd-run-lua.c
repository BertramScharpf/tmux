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
#include "luaif.h"

/*
 * Runs a Lua expression
 */

static enum cmd_retval	cmd_run_lua_exec(struct cmd *,
			    struct cmdq_item *);

const struct cmd_entry cmd_run_lua_entry = {
	.name = "run-lua",
	.alias = "lua",

	.args = { "npFc:t:", 1, 1, NULL },
	.usage = "[-npF] [-c target-client] "
		 CMD_TARGET_PANE_USAGE " [message]",

	.target = { 't', CMD_FIND_PANE, CMD_FIND_CANFAIL },

	.flags = CMD_CLIENT_CFLAG|CMD_CLIENT_CANFAIL,
	.exec = cmd_run_lua_exec
};


static enum cmd_retval
cmd_run_lua_exec(struct cmd *self, struct cmdq_item *item)
{
	struct args		*args = cmd_get_args(self);
	struct client		*tc = cmdq_get_target_client(item);
	const char		*expr;
	char			*exprf = NULL;
	char			*eval;
	int			 err;

	expr = args_string(args, 0);
	if (args_has(args, 'F')) {
		struct format_tree	*ft;

		ft = format_create_from_target(item);
		expr = exprf = format_expand(ft, expr);
		format_free(ft);
	}

	if (args_has(args, 'n')) {
		eval = luaif_exec_str(expr);
		err = 1;
	} else {
		eval = luaif_eval_str(expr, &err);
	}
	free(exprf);
	if (eval) {
		if (args_has(args, 'p')) {
			if (err)
				cmdq_print(item, "Lua Error");
			cmdq_print(item, "%s", eval);
		} else if (err || tc == NULL)
			cmdq_error(item, "%s", eval);
		else if (tc->flags & CLIENT_CONTROL) {
			struct evbuffer		*evb;

			evb = evbuffer_new();
			if (evb == NULL)
				fatalx("out of memory");
			evbuffer_add_printf(evb, "%%lua-result %s", eval);
			server_client_print(tc, 0, evb);
			evbuffer_free(evb);
		} else
			status_message_set(tc, -1, 0, 0, 0, "%s", eval);
		free(eval);
	}

	return (CMD_RETURN_NORMAL);
}
