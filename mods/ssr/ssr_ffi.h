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

typedef struct RenderRequest {
 const char *method;
 const char *path;
 const char *query;
 const unsigned char *body;
 size_t body_len;
 const char *remote_user;
 const struct ModuleEntryFfi *modules;
 size_t modules_len;
} RenderRequest;

typedef struct SongItemFfi {
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
} SongItemFfi;

/**
 * Shared context passed to every per-module item FFI entry point.
 */
typedef struct ItemContext {
 const char *id;
 const char *query;
 const char *remote_user;
 const struct ModuleEntryFfi *modules;
 size_t modules_len;
} ItemContext;

typedef struct PoemItemFfi {
 const char *title;
 const char *head_content;
 const char *body_content;
 bool owner;
} PoemItemFfi;

/**
 * Used by the generic delete confirmation page (all modules).
 */
typedef struct DeleteItemFfi {
 const char *title;
} DeleteItemFfi;

typedef struct SongbookSongFfi {
 const char *chord_id;
 const char *format;
 const char *chord_title;
 const char *chord_data;
 int32_t transpose;
 int32_t original_key;
} SongbookSongFfi;

typedef struct SongbookItemFfi {
 const char *title;
 const char *owner;
 const char *choir;
 int32_t viewer_zoom;
 const struct SongbookSongFfi *songs;
 size_t songs_len;
} SongbookItemFfi;

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

typedef struct ChoirItemFfi {
 const char *title;
 const char *owner_name;
 const char *counter;
 const char *formats;
 const struct ChoirSongFfi *songs;
 size_t songs_len;
 const struct ChoirEntryFfi *all_songs;
 size_t all_songs_len;
 const struct ChoirEntryFfi *songbooks;
 size_t songbooks_len;
} ChoirItemFfi;

extern struct RenderResult ssr_render_ffi(const struct RenderRequest *request);

extern
struct RenderResult ssr_render_song_detail_ffi(const struct SongItemFfi *payload,
                                               const struct ItemContext *ictx);

extern
struct RenderResult ssr_render_poem_detail_ffi(const struct PoemItemFfi *payload,
                                               const struct ItemContext *ictx);

extern
struct RenderResult ssr_render_poem_edit_ffi(const struct PoemItemFfi *payload,
                                             const struct ItemContext *ictx);

extern
struct RenderResult ssr_render_delete_ffi(const char *module,
                                          const struct DeleteItemFfi *payload,
                                          const struct ItemContext *ictx);

extern
struct RenderResult ssr_render_songbook_detail_ffi(const struct SongbookItemFfi *payload,
                                                   const struct ItemContext *ictx);

extern
struct RenderResult ssr_render_choir_detail_ffi(const struct ChoirItemFfi *payload,
                                                const struct ItemContext *ictx);

extern void ssr_free_result_ffi(struct RenderResult *result);

#endif  /* SSR_FFI_H */
