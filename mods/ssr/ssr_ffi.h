#ifndef SSR_FFI_H
#define SSR_FFI_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "ssr_item_types.h"

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

typedef struct RenderItemRequest {
 const char *module;
 const char *action;
 const char *id;
 const char *query;
 const char *json;
 const char *remote_user;
 const struct ModuleEntryFfi *modules;
 size_t modules_len;
} RenderItemRequest;

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

extern struct RenderResult ssr_render_ffi(const struct RenderRequest *request);

extern struct RenderResult ssr_render_item_ffi(const struct RenderItemRequest *request);

extern
struct RenderResult ssr_render_song_detail_ffi(const SongItemFfi *payload,
                                               const struct ItemContext *ictx);

extern
struct RenderResult ssr_render_poem_detail_ffi(const PoemItemFfi *payload,
                                               const struct ItemContext *ictx);

extern
struct RenderResult ssr_render_poem_edit_ffi(const PoemItemFfi *payload,
                                             const struct ItemContext *ictx);

extern
struct RenderResult ssr_render_delete_ffi(const char *module,
                                          const DeleteItemFfi *payload,
                                          const struct ItemContext *ictx);

extern void ssr_free_result_ffi(struct RenderResult *result);

#endif  /* SSR_FFI_H */
