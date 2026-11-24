#ifndef BUD_LAMINA_H
#define BUD_LAMINA_H

#include <bud/bud.h>
#include <ttypt/lam.h>
#include <ttypt/lam-ui.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bud_lamina bud_lamina_t;

typedef bud_node *(*bud_lamina_render_fn)(void *ctx);
typedef void (*bud_lamina_term_input_fn)(void *ctx, const char *data, size_t len);

/* Lifecycle */
bud_lamina_t *bud_lamina_create(int vw, int vh);
void          bud_lamina_destroy(bud_lamina_t *bl);

/* Configuration */
int  bud_lamina_load_css_file(bud_lamina_t *bl, const char *path);
int  bud_lamina_load_css(bud_lamina_t *bl, const char *css, size_t len);
void bud_lamina_set_fonts(bud_lamina_t *bl,
                          uint32_t sans, uint32_t sans_bold,
                          uint32_t mono, uint32_t mono_bold);
void bud_lamina_set_asset_root(bud_lamina_t *bl, const char *htdocs_dir);
void bud_lamina_set_render(bud_lamina_t *bl, bud_lamina_render_fn fn, void *ctx);
void bud_lamina_set_term_input(bud_lamina_t *bl, bud_lamina_term_input_fn fn, void *ctx);
void bud_lamina_set_dark_mode(bud_lamina_t *bl, int dark);
void bud_lamina_set_active_global(bud_lamina_t *bl);

/* State & viewport */
void bud_lamina_mark_dirty(bud_lamina_t *bl);
int  bud_lamina_is_dirty(const bud_lamina_t *bl);
void bud_lamina_resize(bud_lamina_t *bl, int vw, int vh);

/* Frame loop */
int  bud_lamina_sync(bud_lamina_t *bl);
void bud_lamina_render(bud_lamina_t *bl);

/* Input & events */
void bud_lamina_attach_input(bud_lamina_t *bl);
void bud_lamina_handle_event(bud_lamina_t *bl, const lam_event_t *ev);
int  bud_lamina_dispatch_root(bud_lamina_t *bl, const char *event_name, void *user);

/* WebGL2 3D stage surface bridge */
int  bud_lamina_webgl_size(const bud_lamina_t *bl, int *w_out, int *h_out);
int  bud_lamina_webgl_begin(bud_lamina_t *bl);
void bud_lamina_webgl_end(bud_lamina_t *bl);

/* Embedded ANSI terminal (#term) */
void        bud_lamina_term_write(bud_lamina_t *bl, const char *data, size_t len);
int         bud_lamina_term_line_count(const bud_lamina_t *bl);
const char *bud_lamina_term_line_text(const bud_lamina_t *bl, int idx_from_bottom,
                                      char *buf, size_t cap);

/* Introspection (useful for tests & debugging) */
lam_ui_div_t *bud_lamina_ui_root(const bud_lamina_t *bl);
bud_node     *bud_lamina_bud_root(const bud_lamina_t *bl);
lam_ui_div_t *bud_lamina_find_by_id(const bud_lamina_t *bl, const char *id);
int           bud_lamina_is_term_focused(const bud_lamina_t *bl);

#ifdef __cplusplus
}
#endif

#endif /* BUD_LAMINA_H */
