/*
 * ssr_item_types.h — hand-written C structs mirroring the #[repr(C)] types in
 * mods/ssr/rust-renderer/src/lib.rs.  These are kept in sync manually; the
 * cbindgen-generated ssr_ffi.h provides the function declarations that use them.
 *
 * C callers (song.c, poem.c, index.c, …) include this header to fill a payload
 * struct on the stack and pass it to an ssr_render_* hook without any heap
 * allocation or JSON serialisation.
 */
#ifndef SSR_ITEM_TYPES_H
#define SSR_ITEM_TYPES_H

#include <stdbool.h>
#include <stdint.h>

struct SongItemFfi {
	const char *title;
	const char *data;
	const char *yt;
	const char *audio;
	const char *pdf;
	const char *categories;
	const char *author;
	int32_t     original_key;
	int32_t     viewer_zoom;
	bool        show_media;
	bool        viewer_bemol;
	bool        viewer_latin;
	bool        owner;
};
typedef struct SongItemFfi SongItemFfi;

struct PoemItemFfi {
	const char *title;
	const char *head_content;
	const char *body_content;
	bool        owner;
};
typedef struct PoemItemFfi PoemItemFfi;

struct DeleteItemFfi {
	const char *title;
};
typedef struct DeleteItemFfi DeleteItemFfi;

#endif /* SSR_ITEM_TYPES_H */
