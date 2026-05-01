#ifndef SSR_MOD_H
#define SSR_MOD_H

#include <stddef.h>
#include <ttypt/ndx-mod.h>
#include "ssr_item_types.h"

#ifndef SSR_IMPL
NDX_HOOK_DECL(int, ssr_render,
	int, fd,
	const char *, method,
	const char *, path,
	const char *, query,
	const char *, body,
	size_t, body_len,
	const char *, remote_user);

NDX_HOOK_DECL(int, ssr_render_item,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, action,
	const char *, json);

NDX_HOOK_DECL(int, ssr_render_song_detail,
	int, fd,
	const struct SongItemFfi *, payload);

NDX_HOOK_DECL(int, ssr_render_poem_detail,
	int, fd,
	const struct PoemItemFfi *, payload);

NDX_HOOK_DECL(int, ssr_render_poem_edit,
	int, fd,
	const struct PoemItemFfi *, payload);

NDX_HOOK_DECL(int, ssr_render_delete,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, title);
#endif

#endif
