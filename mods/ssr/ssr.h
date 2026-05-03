#ifndef SSR_MOD_H
#define SSR_MOD_H

#include <stddef.h>
#include <ttypt/ndx-mod.h>
#include "ssr_ffi.h"

#define SSR_FILL_MODULES(snap_, len_out_)                                      \
	do {                                                                   \
		size_t _i;                                                     \
		(len_out_) = index_get_module_count(0);                        \
		if ((len_out_) > 64)                                           \
			(len_out_) = 64;                                       \
		for (_i = 0; _i < (len_out_); _i++) {                          \
			(snap_)[_i].id = index_get_module_id(_i);              \
			(snap_)[_i].title = index_get_module_title(_i);        \
			(snap_)[_i].flags = index_get_module_flags(_i);        \
		}                                                              \
	} while (0)

#ifndef SSR_IMPL
NDX_HOOK_DECL(int, ssr_render,
	int, fd,
	const char *, method,
	const char *, path,
	const char *, query,
	const char *, body,
	size_t, body_len,
	const char *, remote_user);

NDX_HOOK_DECL(int, ssr_render_song_detail,
	int, fd,
	const SongDetailRenderFfi *, req);

NDX_HOOK_DECL(int, ssr_render_poem_detail, int, fd, const PoemRenderFfi *, req);

NDX_HOOK_DECL(int, ssr_render_poem_edit, int, fd, const PoemRenderFfi *, req);

NDX_HOOK_DECL(int, ssr_render_delete,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, title);

NDX_HOOK_DECL(int, ssr_render_songbook_detail,
	int, fd,
	const SongbookDetailRenderFfi *, req);

NDX_HOOK_DECL(int, ssr_render_choir_detail,
	int, fd,
	const ChoirDetailRenderFfi *, req);
#endif

#endif
