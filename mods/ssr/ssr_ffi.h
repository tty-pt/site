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

typedef struct RenderRequest {
 const char *method;
 const char *path;
 const char *query;
 const unsigned char *body;
 size_t body_len;
 const char *remote_user;
 const char *forwarded_host;
 const char *modules_header;
} RenderRequest;

extern struct RenderResult ssr_render_ffi(const struct RenderRequest *request);

extern
struct RenderResult ssr_render_item_ffi(const char *module,
                                        const char *action,
                                        const char *id,
                                        const char *query,
                                        const char *json,
                                        const char *remote_user,
                                        const char *modules_header);

extern void ssr_free_result_ffi(struct RenderResult *result);

#endif  /* SSR_FFI_H */
