#ifndef BUD_APP_H
#define BUD_APP_H

#include "bud.h"
#include "bud_jsx.h"

#ifdef __wasm__

extern bud_node *bud_app_render(void);

#define BUD_APP_ROUTE(render_fn)                                               \
	bud_node *bud_app_render(void)                                         \
	{                                                                      \
		return render_fn();                                            \
	}

__attribute__((import_module("env"), import_name("bud_host_emit_patch"))) void
bud_host_emit_patch(
        const char *op,
        size_t op_len,
        const char *a,
        size_t a_len,
        const char *b,
        size_t b_len,
        const char *c,
        size_t c_len);

void bud_patch_attr(bud_node *node, const char *name, const char *value);
void bud_patch_text(bud_node *node, const char *value);
bud_runtime *wasm_get_runtime(void);

#else

/* Native stub — provided by libbud.so */
void bud_host_emit_patch(
        const char *op,
        size_t op_len,
        const char *a,
        size_t a_len,
        const char *b,
        size_t b_len,
        const char *c,
        size_t c_len);

void bud_patch_attr(bud_node *node, const char *name, const char *value);
void bud_patch_text(bud_node *node, const char *value);
bud_runtime *wasm_get_runtime(void);

#endif

#endif
