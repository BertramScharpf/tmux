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

#include "luaif.h"
#include "tmux.h"

#include <ctype.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>


static const char	*luaif_only_message(const char *);
static const char	*luaif_after_location(const char *);
static void		 luaif_start(void);

static lua_State *L;



const char *
luaif_after_location(const char *p)
{
	int n;

	for (;;) {
		while (*p && *p != ':')
			++p;
		if (!*p)
			break;
		n = 0;
		++p;
		while (isdigit(*p))
			++p, ++n;
		if (n > 0 && *p == ':') {
			++p;
			if (*p == ' ') {
				++p;
				return p;
			}
		}
	}
	return NULL;
}

const char *
luaif_only_message(const char *p)
{
	const char *q;

	while ((q = luaif_after_location(p)) != NULL)
		p = q;
	return p;
}


void
luaif_start(void)
{
	if (L == NULL) {
		L = luaL_newstate();
		luaL_openlibs(L);
	}
}

void
luaif_finish(void)
{
	if (L != NULL)
		lua_close(L);
}

char *
luaif_exec_str(const char *str)
{
	char *ret = NULL;
	luaif_start();

	if (luaL_loadstring(L, str) || lua_pcall(L, 0, 0, 0)) {
		ret = xstrdup(luaif_only_message(lua_tostring(L, -1)));
		lua_pop(L, 1);
	}
	return ret;
}


char *
luaif_eval_str(const char *str, int *perr)
{
	char *code;
	int evl;
	const char *res;
	char *ret;
	int err;

	luaif_start();

	xasprintf(&code, "return tostring(%s)", str);
	evl = luaL_loadstring(L, code);
	free(code);
	if (evl || lua_pcall(L, 0, 1, 0)) {
		err = 1;
		res = luaif_only_message(lua_tostring(L, -1));
	} else {
		err = 0;
		res = lua_tostring(L, -1);
	}
	ret = xstrdup(res);
	lua_pop(L, 1);

	if (perr)
		*perr = err;
	return ret;
}
