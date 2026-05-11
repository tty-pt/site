#ifndef SSR_MOD_H
#define SSR_MOD_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <ttypt/ndx-mod.h>

/* ── FFI structs (layout must match Rust #[repr(C)] definitions in lib.rs) ──
 */

typedef struct ModuleEntryFfi {
	const char *id;
	const char *title;
	uint32_t flags;
} ModuleEntryFfi;

typedef struct SongDetailRenderFfi {
	const char *title;
	const char *data;
	const char *yt;
	const char *audio;
	const char *pdf;
	const char *categories;
	const char *author;
	int32_t original_key;
	int32_t viewer_zoom;
	bool show_media;
	bool viewer_bemol;
	bool viewer_latin;
	bool owner;
	const char *id;
	const char *query;
	const char *remote_user;
	const ModuleEntryFfi *modules;
	size_t modules_len;
	const char *csrf_token;
} SongDetailRenderFfi;

typedef struct PoemRenderFfi {
	const char *title;
	const char *head_content;
	const char *body_content;
	bool owner;
	const char *id;
	const char *query;
	const char *remote_user;
	const ModuleEntryFfi *modules;
	size_t modules_len;
	const char *csrf_token;
} PoemRenderFfi;

typedef struct SongbookSongFfi {
	const char *chord_id;
	const char *format;
	const char *chord_title;
	const char *chord_data;
	int32_t transpose;
	int32_t original_key;
} SongbookSongFfi;

typedef struct SongbookDetailRenderFfi {
	const char *sb_title;
	const char *owner;
	const char *choir;
	int32_t viewer_zoom;
	const SongbookSongFfi *songs;
	size_t songs_len;
	const char *id;
	const char *query;
	const char *remote_user;
	const ModuleEntryFfi *modules;
	size_t modules_len;
	const char *csrf_token;
} SongbookDetailRenderFfi;

typedef struct ChoirSongFfi {
	const char *id;
	const char *title;
	const char *format;
	int32_t preferred_key;
	int32_t original_key;
} ChoirSongFfi;

typedef struct ChoirEntryFfi {
	const char *id;
	const char *title;
} ChoirEntryFfi;

typedef struct ChoirDetailRenderFfi {
	const char *title;
	const char *owner_name;
	const char *formats;
	const ChoirSongFfi *songs;
	size_t songs_len;
	const ChoirEntryFfi *all_songs;
	size_t all_songs_len;
	const ChoirEntryFfi *songbooks;
	size_t songbooks_len;
	const char *id;
	const char *query;
	const char *remote_user;
	const ModuleEntryFfi *modules;
	size_t modules_len;
	const char *csrf_token;
} ChoirDetailRenderFfi;

/* ── Helper macro to snapshot the module list ───────────────────────────────
 */

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

/* ── Hook declarations (used by song.c, poem.c, choir.c, songbook.c, etc.) ──
 */

NDX_HOOK_DECL(int, ssr_render,
	int, fd,
	const char *, method,
	const char *, path,
	const char *, query,
	const char *, body,
	size_t, body_len,
	const char *, remote_user);

NDX_HOOK_DECL(int, ssr_render_poem_detail, int, fd, const char *, id);

NDX_HOOK_DECL(int, ssr_render_poem_edit, int, fd, const char *, id);

NDX_HOOK_DECL(int, ssr_render_delete,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, title);

NDX_HOOK_DECL(int, ssr_render_choir_detail,
	int, fd,
	const ChoirDetailRenderFfi *, req);

#endif
