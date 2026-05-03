#ifndef SSR_FFI_H
#define SSR_FFI_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct RenderResult {
 uint16_t status;
 char *content_type;
 char *location;
 char *body;
} RenderResult;

typedef struct ModuleEntryFfi {
 const char *id;
 const char *title;
 uint32_t flags;
} ModuleEntryFfi;

/**
 * Generic page render (home, login, register, error pages).
 */
typedef struct PageRenderFfi {
 const char *method;
 const char *path;
 const char *query;
 const unsigned char *body;
 size_t body_len;
 const char *remote_user;
 const struct ModuleEntryFfi *modules;
 size_t modules_len;
} PageRenderFfi;

typedef struct DeleteRenderFfi {
 const char *module;
 const char *title;
 const char *id;
 const char *query;
 const char *remote_user;
 const struct ModuleEntryFfi *modules;
 size_t modules_len;
} DeleteRenderFfi;

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
 const struct ChoirSongFfi *songs;
 size_t songs_len;
 const struct ChoirEntryFfi *all_songs;
 size_t all_songs_len;
 const struct ChoirEntryFfi *songbooks;
 size_t songbooks_len;
 const char *id;
 const char *query;
 const char *remote_user;
 const struct ModuleEntryFfi *modules;
 size_t modules_len;
} ChoirDetailRenderFfi;

/**
 * Reused for both poem detail and poem edit.
 */
typedef struct PoemRenderFfi {
 const char *title;
 const char *head_content;
 const char *body_content;
 bool owner;
 const char *id;
 const char *query;
 const char *remote_user;
 const struct ModuleEntryFfi *modules;
 size_t modules_len;
} PoemRenderFfi;

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
 const struct ModuleEntryFfi *modules;
 size_t modules_len;
} SongDetailRenderFfi;

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
 const struct SongbookSongFfi *songs;
 size_t songs_len;
 const char *id;
 const char *query;
 const char *remote_user;
 const struct ModuleEntryFfi *modules;
 size_t modules_len;
} SongbookDetailRenderFfi;

extern struct RenderResult ssr_render_page_ffi(const struct PageRenderFfi *req);

extern struct RenderResult ssr_render_delete_ffi(const struct DeleteRenderFfi *req);

extern void ssr_free_result_ffi(struct RenderResult *result);

extern struct RenderResult ssr_render_choir_detail_ffi(const struct ChoirDetailRenderFfi *req);

extern struct RenderResult ssr_render_poem_detail_ffi(const struct PoemRenderFfi *req);

extern struct RenderResult ssr_render_poem_edit_ffi(const struct PoemRenderFfi *req);

extern struct RenderResult ssr_render_song_detail_ffi(const struct SongDetailRenderFfi *req);

extern
struct RenderResult ssr_render_songbook_detail_ffi(const struct SongbookDetailRenderFfi *req);

#endif  /* SSR_FFI_H */
