#ifndef LAM_WEBGL_NATIVE
#define LAM_WEBGL_NATIVE 1
#endif
#ifndef LAM_NATIVE_CLIENT
#define LAM_NATIVE_CLIENT 1
#endif

#include "bud_lamina.h"
#include "bl_img_decode.h"
#include "term_ansi.h"

#include <ttypt/lam-font.h>
#include <ttypt/webgl.h>

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Standard GLFW key codes (keeps bridge independent of GLFW headers). */
#define K_SPACE      32
#define K_APOSTROPHE 39
#define K_COMMA      44
#define K_MINUS      45
#define K_PERIOD     46
#define K_SLASH      47
#define K_0          48
#define K_9          57
#define K_SEMICOLON  59
#define K_EQUAL      61
#define K_A          65
#define K_Z          90
#define K_LBRACKET   91
#define K_BACKSLASH  92
#define K_RBRACKET   93
#define K_GRAVE      96
#define K_ESCAPE     256
#define K_ENTER      257
#define K_TAB        258
#define K_BACKSPACE  259
#define K_INSERT     260
#define K_DELETE     261
#define K_RIGHT      262
#define K_LEFT       263
#define K_DOWN       264
#define K_UP         265
#define K_PAGE_UP    266
#define K_PAGE_DOWN  267
#define K_HOME       268
#define K_END        269
#define K_F1         290
#define K_F12        301
#define K_KP_0       320
#define K_KP_9       329
#define K_KP_SUB     333
#define K_KP_ADD     334
#define K_KP_ENTER   335
#define K_LEFT_SHIFT 340
#define K_RIGHT_SHIFT 344

#define MOD_SHIFT    1
#define MOD_CTRL     2
#define MOD_ALT      4

#define BL_KEY_MAX    96
#define BL_INPUT_MAX  512
#define BL_VIDEO_MAX  16

typedef enum {
	W_NONE = 0,
	W_WEBGL_CANVAS,
	W_SW_CANVAS,
	W_IMG,
	W_VIDEO,
	W_INPUT_TEXT,
	W_INPUT_RANGE,
	W_INPUT_CHECKBOX,
	W_INPUT_COLOR,
	W_SWITCH_TRACK,
	W_SELECT,
	W_SVG,
	W_TERM
} bl_widget_kind_t;

typedef struct {
	char     id[BL_KEY_MAX];
	uint32_t gl_tex;
	uint32_t w;
	uint32_t h;
} bl_video_slot_t;

typedef struct {
	char     key[BL_KEY_MAX];
	int32_t  scroll_x;
	int32_t  scroll_y;
	char     text[BL_INPUT_MAX];
	int      cursor;
	int      has_text;
	double   range_val;
	int      has_range;
} bl_state_t;

typedef struct {
	lam_ui_div_t    *div;
	bud_node        *bud;
	char             key[BL_KEY_MAX];
	bl_widget_kind_t kind;
	uint32_t         img_tex;
	int              img_valid;
	int              is_password;
	int              is_multiline;
	int              is_checked;
	double           range_min;
	double           range_max;
	double           range_step;
	double           range_val;
	uint32_t         color_val;
	float            svg_vb_x;
	float            svg_vb_y;
	float            svg_vb_w;
	float            svg_vb_h;
	char             switch_cb_key[BL_KEY_MAX];
	char             placeholder[96];
	char             select_label[96];
} bl_entry_t;

typedef struct {
	char    *src;
	uint32_t tex;
	uint32_t w;
	uint32_t h;
	int      state; /* 0 = failed, 1 = ready, 2 = pending */
} bl_img_entry_t;

typedef struct {
	uint64_t hash;
	uint32_t w;
	uint32_t h;
	uint32_t tex;
} bl_svg_cache_t;

struct bud_lamina {
	int vw, vh;
	uint32_t ss;
	uint32_t font_sans;
	uint32_t font_sans_bold;
	uint32_t font_mono;
	uint32_t font_mono_bold;
	char asset_root[512];

	bud_lamina_render_fn     render_fn;
	void                    *render_ctx;
	bud_lamina_term_input_fn term_input_fn;
	void                    *term_input_ctx;
	bud_lamina_img_fetch_fn  img_fetch_fn;
	void                    *img_fetch_ctx;

	bud_runtime  *rt;
	lam_ui_div_t *ui_root;

	bl_entry_t *entries;
	size_t      n_entries;
	size_t      cap_entries;

	bl_state_t *states;
	size_t      n_states;
	size_t      cap_states;

	bl_img_entry_t *imgs;
	size_t          n_imgs;
	size_t          cap_imgs;

	bl_svg_cache_t *svgs;
	size_t          n_svgs;
	size_t          cap_svgs;

	bl_video_slot_t videos[BL_VIDEO_MAX];
	size_t          n_videos;

	lam_ui_div_t        *webgl_div;
	lam_webgl_surface_t *webgl_surf;
	int                  webgl_w;
	int                  webgl_h;

	term_ansi_t *term;

	int dirty;
	int dark_mode;

	/* Interactive UI state persisted by stable key */
	char          hover_key[BL_KEY_MAX];
	char          active_key[BL_KEY_MAX];
	char          focus_key[BL_KEY_MAX];
	char          drag_slider_key[BL_KEY_MAX];
	lam_ui_div_t *hover_div;
	lam_ui_div_t *active_div;
	char          press_key[BL_KEY_MAX];
	int           press_x;
	int           press_y;
	int           press_btn;
	int           press_dragged;
	int           term_focused;

	int          mouse_x;
	int          mouse_y;
	int          has_mouse;
	uint32_t     mouse_buttons;
	uint64_t     last_click_ms;
	int          last_click_x;
	int          last_click_y;
	lam_cursor_t last_cursor;
};

static bud_lamina_t *g_active_bl = NULL;

/* Strong definition overriding BUD_WEAK bud_app_set_state() in libbud. */
void bud_app_set_state(void)
{
	if (g_active_bl)
		g_active_bl->dirty = 1;
}

static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int ci_eq(const char *a, const char *b)
{
	if (!a || !b) return 0;
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return 0;
		a++;
		b++;
	}
	return *a == '\0' && *b == '\0';
}

/* ── Persistent per-key widget state table ──────────────────────────────── */

static bl_state_t *state_find(bud_lamina_t *bl, const char *key, int create)
{
	if (!bl || !key || !key[0]) return NULL;
	for (size_t i = 0; i < bl->n_states; i++) {
		if (strcmp(bl->states[i].key, key) == 0)
			return &bl->states[i];
	}
	if (!create) return NULL;
	if (bl->n_states >= bl->cap_states) {
		size_t nc = bl->cap_states ? bl->cap_states * 2 : 32;
		bl_state_t *ns = realloc(bl->states, nc * sizeof(bl_state_t));
		if (!ns) return NULL;
		bl->states = ns;
		bl->cap_states = nc;
	}
	bl_state_t *st = &bl->states[bl->n_states++];
	memset(st, 0, sizeof(*st));
	snprintf(st->key, sizeof(st->key), "%s", key);
	return st;
}

static bl_entry_t *entry_add(bud_lamina_t *bl)
{
	if (bl->n_entries >= bl->cap_entries) {
		size_t nc = bl->cap_entries ? bl->cap_entries * 2 : 128;
		bl_entry_t *ne = realloc(bl->entries, nc * sizeof(bl_entry_t));
		if (!ne) return NULL;
		bl->entries = ne;
		bl->cap_entries = nc;
	}
	bl_entry_t *e = &bl->entries[bl->n_entries++];
	memset(e, 0, sizeof(*e));
	return e;
}

static bl_entry_t *entry_find_by_div(bud_lamina_t *bl, const lam_ui_div_t *div)
{
	if (!bl || !div) return NULL;
	for (size_t i = 0; i < bl->n_entries; i++) {
		if (bl->entries[i].div == div)
			return &bl->entries[i];
	}
	return NULL;
}

static bl_entry_t *entry_find_by_key(bud_lamina_t *bl, const char *key)
{
	if (!bl || !key || !key[0]) return NULL;
	for (size_t i = 0; i < bl->n_entries; i++) {
		if (strcmp(bl->entries[i].key, key) == 0)
			return &bl->entries[i];
	}
	return NULL;
}

/* ── Image texture cache (local files, data: URIs, and async web images) ─ */

static bl_img_entry_t *img_cache_find(bud_lamina_t *bl, const char *src)
{
	for (size_t i = 0; i < bl->n_imgs; i++) {
		if (strcmp(bl->imgs[i].src, src) == 0)
			return &bl->imgs[i];
	}
	return NULL;
}

static bl_img_entry_t *img_cache_upsert(bud_lamina_t *bl, const char *src,
                                        uint32_t tex, uint32_t w, uint32_t h, int state)
{
	bl_img_entry_t *e = img_cache_find(bl, src);
	if (e) {
		e->tex = tex;
		e->w = w;
		e->h = h;
		e->state = state;
		return e;
	}
	if (bl->n_imgs >= bl->cap_imgs) {
		size_t nc = bl->cap_imgs ? bl->cap_imgs * 2 : 16;
		bl_img_entry_t *ni = realloc(bl->imgs, nc * sizeof(bl_img_entry_t));
		if (!ni) return NULL;
		bl->imgs = ni;
		bl->cap_imgs = nc;
	}
	char *dup = strdup(src);
	if (!dup) return NULL;
	e = &bl->imgs[bl->n_imgs++];
	e->src = dup;
	e->tex = tex;
	e->w = w;
	e->h = h;
	e->state = state;
	return e;
}

static int b64_val(unsigned char c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+' || c == '-') return 62;
	if (c == '/' || c == '_') return 63;
	return -1;
}

static uint8_t *decode_base64(const char *in, size_t *out_len)
{
	size_t slen = strlen(in);
	size_t cap = (slen / 4u + 2u) * 3u;
	uint8_t *out = malloc(cap);
	if (!out) return NULL;
	size_t n = 0;
	uint32_t acc = 0;
	int bits = 0;
	for (size_t i = 0; i < slen; i++) {
		unsigned char c = (unsigned char)in[i];
		if (c == '=') break;
		int v = b64_val(c);
		if (v < 0) continue;
		acc = (acc << 6) | (uint32_t)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out[n++] = (uint8_t)((acc >> bits) & 0xFFu);
		}
	}
	if (out_len) *out_len = n;
	return out;
}

int bud_lamina_provide_img(bud_lamina_t *bl, const char *src,
                           const uint8_t *bytes, size_t len, const char *mime)
{
	(void)mime;
	if (!bl || !src || !src[0]) return 0;
	if (!bytes || len < 12) {
		img_cache_upsert(bl, src, 0, 0, 0, 0);
		return 0;
	}
	uint32_t w = 0, h = 0;
	uint8_t *rgba = bl_img_decode_rgba(bytes, len, &w, &h);
	if (!rgba) {
		img_cache_upsert(bl, src, 0, 0, 0, 0);
		return 0;
	}
	uint32_t tex = lam_tex_create_rgba(src, rgba, w, h);
	free(rgba);
	if (tex == UINT32_MAX) {
		img_cache_upsert(bl, src, 0, 0, 0, 0);
		return 0;
	}
	img_cache_upsert(bl, src, tex, w, h, 1);
	bl->dirty = 1;
	return 1;
}

int bud_lamina_img_ready(const bud_lamina_t *bl, const char *src,
                         uint32_t *w_out, uint32_t *h_out)
{
	if (!bl || !src || !src[0]) return 0;
	for (size_t i = 0; i < bl->n_imgs; i++) {
		if (strcmp(bl->imgs[i].src, src) == 0) {
			if (bl->imgs[i].state == 1) {
				if (w_out) *w_out = bl->imgs[i].w;
				if (h_out) *h_out = bl->imgs[i].h;
				return 1;
			}
			return 0;
		}
	}
	return 0;
}

static int load_image_cached(bud_lamina_t *bl, const char *src,
                             uint32_t *tex_out, uint32_t *w_out, uint32_t *h_out)
{
	if (!bl || !src || !src[0]) return 0;
	bl_img_entry_t *cached = img_cache_find(bl, src);
	if (cached) {
		if (cached->state == 1) {
			if (tex_out) *tex_out = cached->tex;
			if (w_out)   *w_out   = cached->w;
			if (h_out)   *h_out   = cached->h;
		}
		return cached->state == 1;
	}

	/* Inline data:image/...;base64,... URIs */
	if (strncmp(src, "data:", 5) == 0) {
		const char *b64 = strstr(src, ";base64,");
		if (!b64) {
			img_cache_upsert(bl, src, 0, 0, 0, 0);
			return 0;
		}
		size_t raw_len = 0;
		uint8_t *raw = decode_base64(b64 + 8, &raw_len);
		int ok = 0;
		if (raw) {
			ok = bud_lamina_provide_img(bl, src, raw, raw_len, NULL);
			free(raw);
		} else {
			img_cache_upsert(bl, src, 0, 0, 0, 0);
		}
		if (ok) {
			cached = img_cache_find(bl, src);
			if (cached && cached->state == 1) {
				if (tex_out) *tex_out = cached->tex;
				if (w_out)   *w_out   = cached->w;
				if (h_out)   *h_out   = cached->h;
			}
		}
		return ok;
	}

	/* Remote http(s):// URLs via host async fetcher */
	if (strncmp(src, "http://", 7) == 0 || strncmp(src, "https://", 8) == 0) {
		if (bl->img_fetch_fn) {
			img_cache_upsert(bl, src, 0, 0, 0, 2);
			bl->img_fetch_fn(bl->img_fetch_ctx, src);
		} else {
			img_cache_upsert(bl, src, 0, 0, 0, 0);
		}
		return 0;
	}

	/* Local asset file relative to asset_root, with async web fetch fallback */
	char rel[384];
	const char *p = src;
	while (*p == '/') p++;
	size_t rlen = 0;
	while (p[rlen] && p[rlen] != '?' && p[rlen] != '#' && rlen + 1 < sizeof(rel)) {
		rel[rlen] = p[rlen];
		rlen++;
	}
	rel[rlen] = '\0';

	if (rlen > 0 && strstr(rel, "..") == NULL) {
		char full[1024];
		snprintf(full, sizeof(full), "%s/%s",
		         bl->asset_root[0] ? bl->asset_root : "htdocs", rel);
		FILE *f = fopen(full, "rb");
		if (f) {
			fseek(f, 0, SEEK_END);
			long sz = ftell(f);
			fseek(f, 0, SEEK_SET);
			if (sz > 12 && sz <= 16L * 1024L * 1024L) {
				uint8_t *buf = malloc((size_t)sz);
				if (buf) {
					size_t got = fread(buf, 1, (size_t)sz, f);
					fclose(f);
					int ok = bud_lamina_provide_img(bl, src, buf, got, NULL);
					free(buf);
					if (ok) {
						cached = img_cache_find(bl, src);
						if (cached && cached->state == 1) {
							if (tex_out) *tex_out = cached->tex;
							if (w_out)   *w_out   = cached->w;
							if (h_out)   *h_out   = cached->h;
						}
					}
					return ok;
				}
			}
			fclose(f);
		}
	}

	if (bl->img_fetch_fn) {
		img_cache_upsert(bl, src, 0, 0, 0, 2);
		bl->img_fetch_fn(bl->img_fetch_ctx, src);
		return 0;
	}

	img_cache_upsert(bl, src, 0, 0, 0, 0);
	return 0;
}

/* ── Custom widget draw callbacks ───────────────────────────────────────── */

static void draw_webgl_canvas(lam_ui_div_t *div, int32_t x, int32_t y,
                              int32_t w, int32_t h, void *user)
{
	(void)div;
	bud_lamina_t *bl = (bud_lamina_t *)user;
	if (!bl || !bl->webgl_surf || w <= 0 || h <= 0)
		return;
	uint32_t tex = lam_webgl_surface_tex(bl->webgl_surf);
	if (tex)
		lam_tex_draw_gl(tex, x, y, (uint32_t)w, (uint32_t)h, 1);
}

static void draw_sw_canvas(lam_ui_div_t *div, int32_t x, int32_t y,
                           int32_t w, int32_t h, void *user)
{
	(void)div;
	(void)user;
	if (w <= 0 || h <= 0) return;
	uint32_t cw = 0, ch = 0;
	uint32_t tex = lam_canvas_gl_tex(&cw, &ch);
	if (tex && cw > 0 && ch > 0)
		lam_tex_draw_gl(tex, x, y, (uint32_t)w, (uint32_t)h, 0);
}

static void draw_img_widget(lam_ui_div_t *div, int32_t x, int32_t y,
                            int32_t w, int32_t h, void *user)
{
	bud_lamina_t *bl = (bud_lamina_t *)user;
	bl_entry_t *e = entry_find_by_div(bl, div);
	if (!e || w <= 0 || h <= 0) return;
	const lam_ui_style_t *st = lam_ui_get_style(div);
	float max_r = (float)(w < h ? w : h) * 0.5f;
	float tl = 0.0f, tr = 0.0f, br = 0.0f, blr = 0.0f;
	if (st) {
		if (st->border_radius_pct) {
			tl = tr = br = blr = max_r;
		} else {
			tl  = (float)st->border_radius_top_left;
			tr  = (float)st->border_radius_top_right;
			br  = (float)st->border_radius_bottom_right;
			blr = (float)st->border_radius_bottom_left;
			if (tl  > max_r) tl  = max_r;
			if (tr  > max_r) tr  = max_r;
			if (br  > max_r) br  = max_r;
			if (blr > max_r) blr = max_r;
		}
	}
	if (e->img_valid) {
		uint32_t tw = 0, th = 0;
		lam_tex_size(&tw, &th, e->img_tex);
		if (tw > 0 && th > 0) {
			int fit_mode = 0;
			if (st) {
				if (st->object_fit == LAM_UI_FIT_COVER)
					fit_mode = 1;
				else if (st->object_fit == LAM_UI_FIT_CONTAIN)
					fit_mode = 2;
			}
			float opacity = st ? st->opacity : 1.0f;
			lam_tex_draw_box(e->img_tex, x, y, (uint32_t)w, (uint32_t)h,
			                 tl, tr, br, blr, fit_mode, 0, 0, opacity);
		}
	} else {
		/* Subtle placeholder badge for missing art */
		if (tl > 0.0f || tr > 0.0f || br > 0.0f || blr > 0.0f) {
			lam_border_radius(LAM_RGBA(40, 44, 58, 160), 0, x, y,
			                  (uint32_t)w, (uint32_t)h, tl, tr, br, blr, 0.0f);
		} else {
			lam_fill(x, y, w, h, LAM_RGBA(40, 44, 58, 160));
		}
	}
}

static void draw_range_widget(lam_ui_div_t *div, int32_t x, int32_t y,
                              int32_t w, int32_t h, void *user)
{
	bud_lamina_t *bl = (bud_lamina_t *)user;
	bl_entry_t *e = entry_find_by_div(bl, div);
	if (!e || w <= 4 || h <= 2) return;

	double span = e->range_max - e->range_min;
	double t = (span > 0.0) ? ((e->range_val - e->range_min) / span) : 0.0;
	if (t < 0.0) t = 0.0;
	if (t > 1.0) t = 1.0;

	int track_h = 4;
	int track_y = y + (h - track_h) / 2;
	int fill_w  = (int)((double)w * t);
	lam_border_radius(LAM_RGBA(80, 84, 108, 180), 0, x, track_y,
	                  (uint32_t)w, (uint32_t)track_h, 2, 2, 2, 2, 0);
	if (fill_w > 0) {
		lam_border_radius(LAM_RGB(0x95, 0x89, 0xc5), 0, x, track_y,
		                  (uint32_t)fill_w, (uint32_t)track_h, 2, 2, 2, 2, 0);
	}
	int thumb_r = 7;
	int tx = x + fill_w - thumb_r;
	if (tx < x) tx = x;
	if (tx > x + w - thumb_r * 2) tx = x + w - thumb_r * 2;
	int ty = y + (h - thumb_r * 2) / 2;
	lam_border_radius(LAM_RGB(0xe8, 0xe4, 0xff), LAM_RGB(0x55, 0x4b, 0x88),
	                  tx, ty, (uint32_t)(thumb_r * 2), (uint32_t)(thumb_r * 2),
	                  (float)thumb_r, (float)thumb_r, (float)thumb_r, (float)thumb_r, 1.5f);
}

static void draw_video_widget(lam_ui_div_t *div, int32_t x, int32_t y,
                              int32_t w, int32_t h, void *user)
{
	bud_lamina_t *bl = (bud_lamina_t *)user;
	bl_entry_t *e = entry_find_by_div(bl, div);
	if (!e || w <= 0 || h <= 0) return;

	uint32_t tex = 0, tw = 0, th = 0;
	for (size_t i = 0; i < bl->n_videos; i++) {
		if (strcmp(bl->videos[i].id, e->key) == 0) {
			tex = bl->videos[i].gl_tex;
			tw = bl->videos[i].w;
			th = bl->videos[i].h;
			break;
		}
	}
	lam_fill((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, LAM_RGB(0x0c, 0x0d, 0x10));
	if (tex && tw > 0 && th > 0) {
		double scale_x = (double)w / (double)tw;
		double scale_y = (double)h / (double)th;
		double scale = scale_x < scale_y ? scale_x : scale_y;
		uint32_t dw = (uint32_t)(tw * scale);
		uint32_t dh = (uint32_t)(th * scale);
		int32_t dx = x + (w - (int32_t)dw) / 2;
		int32_t dy = y + (h - (int32_t)dh) / 2;
		lam_tex_draw_gl(tex, dx, dy, dw, dh, 0);
	}
}

static void draw_checkbox_widget(lam_ui_div_t *div, int32_t x, int32_t y,
                                 int32_t w, int32_t h, void *user)
{
	bud_lamina_t *bl = (bud_lamina_t *)user;
	bl_entry_t *e = entry_find_by_div(bl, div);
	if (!e || w <= 2 || h <= 2) return;
	uint32_t bg = e->is_checked ? LAM_RGB(0xe2, 0xb6, 0x57) : LAM_RGBA(60, 62, 74, 220);
	float r = (float)(h < w ? h : w) * 0.5f;
	lam_border_radius(bg, LAM_RGBA(120, 120, 140, 160),
	                  x, y, (uint32_t)w, (uint32_t)h, r, r, r, r, 1.0f);
	int32_t knob = (h > 6) ? (h - 4) : h;
	int32_t kx = e->is_checked ? (x + w - knob - 2) : (x + 2);
	int32_t ky = y + (h - knob) / 2;
	float kr = (float)knob * 0.5f;
	lam_border_radius(LAM_RGB(0xf5, 0xf5, 0xf7), LAM_RGBA(0, 0, 0, 80),
	                  kx, ky, (uint32_t)knob, (uint32_t)knob, kr, kr, kr, kr, 1.0f);
}

static void draw_color_input_widget(lam_ui_div_t *div, int32_t x, int32_t y,
                                    int32_t w, int32_t h, void *user)
{
	bud_lamina_t *bl = (bud_lamina_t *)user;
	bl_entry_t *e = entry_find_by_div(bl, div);
	if (!e || w <= 2 || h <= 2) return;
	uint32_t col = e->color_val ? e->color_val : LAM_RGB(0xe2, 0xb6, 0x57);
	lam_border_radius(col, LAM_RGBA(255, 255, 255, 90),
	                  x, y, (uint32_t)w, (uint32_t)h, 4, 4, 4, 4, 1.0f);
}

static void draw_switch_track_widget(lam_ui_div_t *div, int32_t x, int32_t y,
                                     int32_t w, int32_t h, void *user)
{
	bud_lamina_t *bl = (bud_lamina_t *)user;
	bl_entry_t *e = entry_find_by_div(bl, div);
	if (!e || w <= 4 || h <= 4) return;
	int checked = 0;
	if (e->switch_cb_key[0]) {
		bl_entry_t *cb = entry_find_by_key(bl, e->switch_cb_key);
		if (cb) checked = cb->is_checked;
	}
	const lam_ui_style_t *st = lam_ui_get_style(div);
	float r = (float)(h < w ? h : w) * 0.5f;
	uint32_t track_bg = checked ? LAM_RGB(0xe2, 0xb6, 0x57)
		: (st && st->background_color ? st->background_color : LAM_RGB(0x34, 0x20, 0x60));
	uint32_t track_bd = checked ? LAM_RGB(0xe2, 0xb6, 0x57)
		: (st && st->border_color ? st->border_color : LAM_RGB(0x6a, 0x4c, 0x96));
	lam_border_radius(track_bg, track_bd,
	                  x, y, (uint32_t)w, (uint32_t)h, r, r, r, r, 1.0f);
	int32_t knob = (h > 6) ? (h - 6) : (h - 2);
	if (knob < 4) knob = 4;
	int32_t kx = checked ? (x + w - knob - 3) : (x + 3);
	int32_t ky = y + (h - knob) / 2;
	float kr = (float)knob * 0.5f;
	uint32_t knob_col = checked ? LAM_RGB(0x0e, 0x07, 0x1f) : LAM_RGB(0xcb, 0xb8, 0xe0);
	lam_border_radius(knob_col, 0,
	                  kx, ky, (uint32_t)knob, (uint32_t)knob, kr, kr, kr, kr, 0.0f);
}

static void draw_select_widget(lam_ui_div_t *div, int32_t x, int32_t y,
                               int32_t w, int32_t h, void *user)
{
	bud_lamina_t *bl = (bud_lamina_t *)user;
	bl_entry_t *e = entry_find_by_div(bl, div);
	if (!e || w <= 4 || h <= 4) return;
	const lam_ui_style_t *st = lam_ui_get_style(div);
	uint32_t bg = (st && st->background_color) ? st->background_color : LAM_RGBA(24, 26, 36, 220);
	uint32_t bd = (st && st->border_color) ? st->border_color : LAM_RGBA(120, 120, 140, 160);
	uint32_t fg = (st && st->color) ? st->color : LAM_RGB(0xf0, 0xf2, 0xf8);
	float rad = (st && st->border_radius_top_left) ? (float)st->border_radius_top_left : 4.0f;
	lam_border_radius(bg, bd, x, y, (uint32_t)w, (uint32_t)h, rad, rad, rad, rad, 1.0f);
	uint32_t font = (bl->font_sans != UINT32_MAX) ? bl->font_sans : bl->font_mono;
	if (font == UINT32_MAX) return;
	uint32_t fpx = (st && st->font_size_is_px && st->font_size > 0) ? st->font_size : 13;
	uint32_t lh = lam_font_line_height(font, fpx);
	int ty = y + (h - (int)lh) / 2;
	if (ty < y + 2) ty = y + 2;
	const char *lbl = e->select_label[0] ? e->select_label : "Predefinido";
	lam_tint(fg);
	lam_font_draw_px(font, lbl, (uint32_t)(x + 8), (uint32_t)ty,
	                 (uint32_t)(x + w - 18 > x + 8 ? x + w - 18 : x + w),
	                 (uint32_t)(y + h), fpx,
	                 LAM_UI_WS_NOWRAP, LAM_UI_WB_NORMAL);
	if (w > 22) {
		lam_tint(LAM_RGBA(200, 185, 220, 180));
		lam_font_draw_px(font, "v", (uint32_t)(x + w - 14), (uint32_t)ty,
		                 (uint32_t)(x + w - 2), (uint32_t)(y + h), fpx > 2 ? fpx - 2 : fpx,
		                 LAM_UI_WS_NOWRAP, LAM_UI_WB_NORMAL);
	}
	lam_tint(0xFFFFFFFFu);
}

static void draw_text_input_widget(lam_ui_div_t *div, int32_t x, int32_t y,
                                   int32_t w, int32_t h, void *user)
{
	bud_lamina_t *bl = (bud_lamina_t *)user;
	bl_entry_t *e = entry_find_by_div(bl, div);
	if (!e || w <= 4 || h <= 4) return;

	int focused = (bl->focus_key[0] && strcmp(bl->focus_key, e->key) == 0);
	bl_state_t *st = state_find(bl, e->key, 0);
	const char *txt = (st && st->has_text) ? st->text : "";

	char masked[BL_INPUT_MAX];
	const char *disp = txt;
	if (e->is_password && txt[0]) {
		size_t n = strlen(txt);
		if (n >= sizeof(masked)) n = sizeof(masked) - 1;
		memset(masked, '*', n);
		masked[n] = '\0';
		disp = masked;
	}

	const lam_ui_style_t *cs = lam_ui_get_style(div);
	uint32_t bg = (cs && cs->background_color) ? cs->background_color : LAM_RGBA(24, 26, 36, 220);
	uint32_t bd_default = (cs && cs->border_color) ? cs->border_color : LAM_RGBA(120, 120, 140, 160);
	uint32_t border = focused ? LAM_RGB(0xe2, 0xb6, 0x57) : bd_default;
	uint32_t fg = (cs && cs->color) ? cs->color : LAM_RGB(0xf0, 0xf2, 0xf8);
	float rad = (cs && cs->border_radius_top_left) ? (float)cs->border_radius_top_left : 6.0f;
	lam_border_radius(bg, border,
	                  x, y, (uint32_t)w, (uint32_t)h, rad, rad, rad, rad, 1.0f);

	uint32_t font = (bl->font_sans != UINT32_MAX) ? bl->font_sans : bl->font_mono;
	if (font == UINT32_MAX) return;
	uint32_t fpx = (cs && cs->font_size_is_px && cs->font_size > 0) ? cs->font_size : 14;
	uint32_t lh = lam_font_line_height(font, fpx);
	int ty = (e->is_multiline && h > (int)lh + 16) ? (y + 8) : (y + (h - (int)lh) / 2);
	if (ty < y + 2) ty = y + 2;

	int pad_l = (cs && cs->padding_left > 0) ? (int)cs->padding_left : 8;
	if (pad_l > w / 3) pad_l = 6;
	uint32_t x0 = (uint32_t)(x + pad_l > 0 ? x + pad_l : 0);
	uint32_t y0 = (uint32_t)(ty > 0 ? ty : 0);
	uint32_t x1 = (uint32_t)(x + w - 4 > (int)x0 ? x + w - 4 : (int)x0);
	uint32_t y1 = (uint32_t)(y + h > (int)y0 ? y + h : (int)y0);

	if (disp[0]) {
		lam_tint(fg);
		lam_font_draw_px(font, disp, x0, y0, x1, y1, fpx,
		                 e->is_multiline ? LAM_UI_WS_PRE_WRAP : LAM_UI_WS_NOWRAP,
		                 LAM_UI_WB_NORMAL);
	} else if (e->placeholder[0]) {
		lam_tint(LAM_RGBA(141, 116, 168, 220));
		lam_font_draw_px(font, e->placeholder, x0, y0, x1, y1, fpx,
		                 LAM_UI_WS_NOWRAP, LAM_UI_WB_NORMAL);
	}

	if (focused) {
		int cur = st ? st->cursor : 0;
		int tlen = (int)strlen(txt);
		if (cur < 0) cur = 0;
		if (cur > tlen) cur = tlen;
		if (cur >= BL_INPUT_MAX) cur = BL_INPUT_MAX - 1;
		char pre[BL_INPUT_MAX];
		if (cur > 0)
			memcpy(pre, txt, (size_t)cur);
		pre[cur] = '\0';
		uint32_t pw = 0, ph = 0;
		if (cur > 0) {
			lam_font_measure_px(&pw, &ph, font, pre, 0, 0, 100000, 1000, fpx,
			                    LAM_UI_WS_NOWRAP, LAM_UI_WB_NORMAL);
		}
		lam_fill(x + pad_l + (int)pw, ty, 2, (int)lh, LAM_RGB(0xe2, 0xb6, 0x57));
	}
	lam_tint(0xFFFFFFFFu);
}

/* ── Pure-C Anti-Aliased SVG Icon Rasterizer (W_SVG) ────────────────────── */

#define SVG_MAX_SEGS 2048

typedef struct {
	float x0, y0, x1, y1;
} svg_seg_t;

typedef struct {
	svg_seg_t segs[SVG_MAX_SEGS];
	int       n;
} svg_seglist_t;

static void svg_push_seg(svg_seglist_t *sl, float x0, float y0, float x1, float y1)
{
	if (sl->n >= SVG_MAX_SEGS) return;
	if (fabsf(x1 - x0) < 1e-5f && fabsf(y1 - y0) < 1e-5f) return;
	sl->segs[sl->n].x0 = x0;
	sl->segs[sl->n].y0 = y0;
	sl->segs[sl->n].x1 = x1;
	sl->segs[sl->n].y1 = y1;
	sl->n++;
}

static void svg_skip_sep(const char **pp)
{
	const char *p = *pp;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')
		p++;
	*pp = p;
}

static int svg_has_num(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')
		p++;
	return (*p == '-' || *p == '+' || *p == '.' || (*p >= '0' && *p <= '9'));
}

static int svg_read_num(const char **pp, float *out)
{
	svg_skip_sep(pp);
	const char *p = *pp;
	if (!(*p == '-' || *p == '+' || *p == '.' || (*p >= '0' && *p <= '9')))
		return 0;
	char *ep = NULL;
	float v = strtof(p, &ep);
	if (ep == p) return 0;
	*out = v;
	*pp = ep;
	return 1;
}

static int svg_read_flag(const char **pp, int *out)
{
	svg_skip_sep(pp);
	const char *p = *pp;
	if (*p == '0' || *p == '1') {
		*out = (*p == '1') ? 1 : 0;
		*pp = p + 1;
		return 1;
	}
	return 0;
}

/* W3C SVG 1.1 F.6.5 endpoint-to-center arc subdivision */
static void svg_emit_arc(svg_seglist_t *sl,
                         float x1, float y1, float rx, float ry,
                         float phi_deg, int large_arc, int sweep,
                         float x2, float y2)
{
	if (fabsf(x1 - x2) < 1e-5f && fabsf(y1 - y2) < 1e-5f) return;
	rx = fabsf(rx);
	ry = fabsf(ry);
	if (rx < 1e-5f || ry < 1e-5f) {
		svg_push_seg(sl, x1, y1, x2, y2);
		return;
	}
	float phi = phi_deg * (3.14159265358979323846f / 180.0f);
	float cp = cosf(phi), sp = sinf(phi);
	float dx2 = (x1 - x2) * 0.5f;
	float dy2 = (y1 - y2) * 0.5f;
	float x1p =  cp * dx2 + sp * dy2;
	float y1p = -sp * dx2 + cp * dy2;

	float rx2 = rx * rx, ry2 = ry * ry;
	float x1p2 = x1p * x1p, y1p2 = y1p * y1p;
	float lam = x1p2 / rx2 + y1p2 / ry2;
	if (lam > 1.0f) {
		float sc = sqrtf(lam);
		rx *= sc; ry *= sc;
		rx2 = rx * rx; ry2 = ry * ry;
	}

	float num = rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2;
	float den = rx2 * y1p2 + ry2 * x1p2;
	float coef = (den > 1e-9f && num > 0.0f) ? sqrtf(num / den) : 0.0f;
	if (large_arc == sweep) coef = -coef;

	float cxp =  coef * (rx * y1p / ry);
	float cyp = -coef * (ry * x1p / rx);
	float cx = cp * cxp - sp * cyp + (x1 + x2) * 0.5f;
	float cy = sp * cxp + cp * cyp + (y1 + y2) * 0.5f;

	float ux = (x1p - cxp) / rx, uy = (y1p - cyp) / ry;
	float vx = (-x1p - cxp) / rx, vy = (-y1p - cyp) / ry;

	float t1 = atan2f(uy, ux);
	float dot = ux * vx + uy * vy;
	float det = ux * vy - uy * vx;
	float dt = atan2f(det, dot);
	if (!sweep && dt > 0.0f) dt -= 2.0f * 3.14159265358979323846f;
	if ( sweep && dt < 0.0f) dt += 2.0f * 3.14159265358979323846f;

	int steps = (int)ceilf(fabsf(dt) / (3.14159265358979323846f / 10.0f));
	if (steps < 4) steps = 4;
	if (steps > 36) steps = 36;

	float px = x1, py = y1;
	for (int i = 1; i <= steps; i++) {
		float a = t1 + dt * ((float)i / (float)steps);
		float ca = cosf(a), sa = sinf(a);
		float nx = cp * (rx * ca) - sp * (ry * sa) + cx;
		float ny = sp * (rx * ca) + cp * (ry * sa) + cy;
		svg_push_seg(sl, px, py, nx, ny);
		px = nx; py = ny;
	}
}

static void svg_parse_path(svg_seglist_t *sl, const char *d)
{
	if (!d) return;
	const char *p = d;
	float cx = 0, cy = 0, sx = 0, sy = 0;
	char cmd = 0;
	while (*p) {
		svg_skip_sep(&p);
		if (!*p) break;
		if (isalpha((unsigned char)*p)) {
			cmd = *p++;
		} else if (!cmd) {
			break;
		}
		switch (cmd) {
		case 'M': case 'm': {
			float x, y;
			if (!svg_read_num(&p, &x) || !svg_read_num(&p, &y)) return;
			if (cmd == 'm') { cx += x; cy += y; } else { cx = x; cy = y; }
			sx = cx; sy = cy;
			cmd = (cmd == 'm') ? 'l' : 'L';
			break;
		}
		case 'L': case 'l': {
			float x, y;
			if (!svg_read_num(&p, &x) || !svg_read_num(&p, &y)) return;
			float nx = (cmd == 'l') ? cx + x : x;
			float ny = (cmd == 'l') ? cy + y : y;
			svg_push_seg(sl, cx, cy, nx, ny);
			cx = nx; cy = ny;
			break;
		}
		case 'H': case 'h': {
			float x;
			if (!svg_read_num(&p, &x)) return;
			float nx = (cmd == 'h') ? cx + x : x;
			svg_push_seg(sl, cx, cy, nx, cy);
			cx = nx;
			break;
		}
		case 'V': case 'v': {
			float y;
			if (!svg_read_num(&p, &y)) return;
			float ny = (cmd == 'v') ? cy + y : y;
			svg_push_seg(sl, cx, cy, cx, ny);
			cy = ny;
			break;
		}
		case 'C': case 'c': {
			float x1, y1, x2, y2, x, y;
			if (!svg_read_num(&p, &x1) || !svg_read_num(&p, &y1) ||
			    !svg_read_num(&p, &x2) || !svg_read_num(&p, &y2) ||
			    !svg_read_num(&p, &x)  || !svg_read_num(&p, &y)) return;
			if (cmd == 'c') {
				x1 += cx; y1 += cy; x2 += cx; y2 += cy; x += cx; y += cy;
			}
			float px = cx, py = cy;
			for (int i = 1; i <= 10; i++) {
				float t = (float)i / 10.0f, it = 1.0f - t;
				float nx = it*it*it*cx + 3*it*it*t*x1 + 3*it*t*t*x2 + t*t*t*x;
				float ny = it*it*it*cy + 3*it*it*t*y1 + 3*it*t*t*y2 + t*t*t*y;
				svg_push_seg(sl, px, py, nx, ny);
				px = nx; py = ny;
			}
			cx = x; cy = y;
			break;
		}
		case 'Q': case 'q': {
			float x1, y1, x, y;
			if (!svg_read_num(&p, &x1) || !svg_read_num(&p, &y1) ||
			    !svg_read_num(&p, &x)  || !svg_read_num(&p, &y)) return;
			if (cmd == 'q') { x1 += cx; y1 += cy; x += cx; y += cy; }
			float px = cx, py = cy;
			for (int i = 1; i <= 8; i++) {
				float t = (float)i / 8.0f, it = 1.0f - t;
				float nx = it*it*cx + 2*it*t*x1 + t*t*x;
				float ny = it*it*cy + 2*it*t*y1 + t*t*y;
				svg_push_seg(sl, px, py, nx, ny);
				px = nx; py = ny;
			}
			cx = x; cy = y;
			break;
		}
		case 'A': case 'a': {
			float rx, ry, rot, x, y;
			int large = 0, sweep = 0;
			if (!svg_read_num(&p, &rx) || !svg_read_num(&p, &ry) ||
			    !svg_read_num(&p, &rot) || !svg_read_flag(&p, &large) ||
			    !svg_read_flag(&p, &sweep) || !svg_read_num(&p, &x) ||
			    !svg_read_num(&p, &y)) return;
			float nx = (cmd == 'a') ? cx + x : x;
			float ny = (cmd == 'a') ? cy + y : y;
			svg_emit_arc(sl, cx, cy, rx, ry, rot, large, sweep, nx, ny);
			cx = nx; cy = ny;
			break;
		}
		case 'Z': case 'z':
			svg_push_seg(sl, cx, cy, sx, sy);
			cx = sx; cy = sy;
			cmd = 0;
			break;
		default:
			if (!svg_has_num(p)) cmd = 0;
			else return;
			break;
		}
	}
}

static void svg_emit_rect(svg_seglist_t *sl, float x, float y, float w, float h, float rx)
{
	if (w <= 0.0f || h <= 0.0f) return;
	if (rx < 0.0f) rx = 0.0f;
	if (rx > w * 0.5f) rx = w * 0.5f;
	if (rx > h * 0.5f) rx = h * 0.5f;
	if (rx < 1e-4f) {
		svg_push_seg(sl, x, y, x + w, y);
		svg_push_seg(sl, x + w, y, x + w, y + h);
		svg_push_seg(sl, x + w, y + h, x, y + h);
		svg_push_seg(sl, x, y + h, x, y);
		return;
	}
	svg_push_seg(sl, x + rx, y, x + w - rx, y);
	svg_emit_arc(sl, x + w - rx, y, rx, rx, 0, 0, 1, x + w, y + rx);
	svg_push_seg(sl, x + w, y + rx, x + w, y + h - rx);
	svg_emit_arc(sl, x + w, y + h - rx, rx, rx, 0, 0, 1, x + w - rx, y + h);
	svg_push_seg(sl, x + w - rx, y + h, x + rx, y + h);
	svg_emit_arc(sl, x + rx, y + h, rx, rx, 0, 0, 1, x, y + h - rx);
	svg_push_seg(sl, x, y + h - rx, x, y + rx);
	svg_emit_arc(sl, x, y + rx, rx, rx, 0, 0, 1, x + rx, y);
}

static uint64_t fnv1a_mix(uint64_t h, const void *data, size_t n)
{
	const uint8_t *b = (const uint8_t *)data;
	for (size_t i = 0; i < n; i++) {
		h ^= (uint64_t)b[i];
		h *= 1099511628211ULL;
	}
	return h;
}

static float point_seg_dist(float px, float py, float x0, float y0, float x1, float y1)
{
	float dx = x1 - x0, dy = y1 - y0;
	float l2 = dx * dx + dy * dy;
	float t = (l2 > 1e-9f) ? (((px - x0) * dx + (py - y0) * dy) / l2) : 0.0f;
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	float qx = x0 + t * dx - px;
	float qy = y0 + t * dy - py;
	return sqrtf(qx * qx + qy * qy);
}

static void draw_svg_widget(lam_ui_div_t *div, int32_t x, int32_t y,
                            int32_t w, int32_t h, void *user)
{
	bud_lamina_t *bl = (bud_lamina_t *)user;
	bl_entry_t *e = entry_find_by_div(bl, div);
	if (!e || w <= 1 || h <= 1) return;

	const lam_ui_style_t *st = lam_ui_get_style(div);
	uint8_t do_fill   = st ? st->svg_fill : 1;
	uint8_t do_stroke = st ? st->svg_stroke : 0;
	float   stroke_w  = (st && st->svg_stroke_width > 0.0f) ? st->svg_stroke_width : 1.5f;
	if (!do_fill && !do_stroke) return;

	float vb_x = e->svg_vb_x;
	float vb_y = e->svg_vb_y;
	float vb_w = e->svg_vb_w > 0.0f ? e->svg_vb_w : 24.0f;
	float vb_h = e->svg_vb_h > 0.0f ? e->svg_vb_h : 24.0f;

	/* Build hash of visible children & raster parameters */
	uint64_t hash = 14695981039346656037ULL;
	hash = fnv1a_mix(hash, &vb_x, sizeof(vb_x));
	hash = fnv1a_mix(hash, &vb_y, sizeof(vb_y));
	hash = fnv1a_mix(hash, &vb_w, sizeof(vb_w));
	hash = fnv1a_mix(hash, &vb_h, sizeof(vb_h));
	hash = fnv1a_mix(hash, &do_fill, sizeof(do_fill));
	hash = fnv1a_mix(hash, &do_stroke, sizeof(do_stroke));
	hash = fnv1a_mix(hash, &stroke_w, sizeof(stroke_w));

	for (lam_ui_div_t *ch = lam_ui_first_child(div); ch; ch = lam_ui_next_sibling(ch)) {
		const lam_ui_style_t *cst = lam_ui_get_style(ch);
		if (cst && cst->display == LAM_UI_DISPLAY_NONE) continue;
		const char *ctag = lam_ui_get_tag(ch);
		if (!ctag) continue;
		hash = fnv1a_mix(hash, ctag, strlen(ctag) + 1);
		if (ci_eq(ctag, "path")) {
			const char *d = lam_ui_get_attr(ch, "d");
			if (d) hash = fnv1a_mix(hash, d, strlen(d) + 1);
		} else if (ci_eq(ctag, "rect")) {
			const char *a;
			if ((a = lam_ui_get_attr(ch, "x")))      hash = fnv1a_mix(hash, a, strlen(a) + 1);
			if ((a = lam_ui_get_attr(ch, "y")))      hash = fnv1a_mix(hash, a, strlen(a) + 1);
			if ((a = lam_ui_get_attr(ch, "width")))  hash = fnv1a_mix(hash, a, strlen(a) + 1);
			if ((a = lam_ui_get_attr(ch, "height"))) hash = fnv1a_mix(hash, a, strlen(a) + 1);
			if ((a = lam_ui_get_attr(ch, "rx")))     hash = fnv1a_mix(hash, a, strlen(a) + 1);
		} else if (ci_eq(ctag, "circle")) {
			const char *a;
			if ((a = lam_ui_get_attr(ch, "cx"))) hash = fnv1a_mix(hash, a, strlen(a) + 1);
			if ((a = lam_ui_get_attr(ch, "cy"))) hash = fnv1a_mix(hash, a, strlen(a) + 1);
			if ((a = lam_ui_get_attr(ch, "r")))  hash = fnv1a_mix(hash, a, strlen(a) + 1);
		} else if (ci_eq(ctag, "line")) {
			const char *a;
			if ((a = lam_ui_get_attr(ch, "x1"))) hash = fnv1a_mix(hash, a, strlen(a) + 1);
			if ((a = lam_ui_get_attr(ch, "y1"))) hash = fnv1a_mix(hash, a, strlen(a) + 1);
			if ((a = lam_ui_get_attr(ch, "x2"))) hash = fnv1a_mix(hash, a, strlen(a) + 1);
			if ((a = lam_ui_get_attr(ch, "y2"))) hash = fnv1a_mix(hash, a, strlen(a) + 1);
		}
	}

	uint32_t rw = (uint32_t)w;
	uint32_t rh = (uint32_t)h;
	if (rw > 128) rw = 128;
	if (rh > 128) rh = 128;

	uint32_t tex = UINT32_MAX;
	for (size_t i = 0; i < bl->n_svgs; i++) {
		if (bl->svgs[i].hash == hash && bl->svgs[i].w == rw && bl->svgs[i].h == rh) {
			tex = bl->svgs[i].tex;
			break;
		}
	}

	if (tex == UINT32_MAX) {
		svg_seglist_t sl;
		sl.n = 0;
		for (lam_ui_div_t *ch = lam_ui_first_child(div); ch; ch = lam_ui_next_sibling(ch)) {
			const lam_ui_style_t *cst = lam_ui_get_style(ch);
			if (cst && cst->display == LAM_UI_DISPLAY_NONE) continue;
			const char *ctag = lam_ui_get_tag(ch);
			if (!ctag) continue;
			if (ci_eq(ctag, "path")) {
				svg_parse_path(&sl, lam_ui_get_attr(ch, "d"));
			} else if (ci_eq(ctag, "rect")) {
				const char *ax = lam_ui_get_attr(ch, "x");
				const char *ay = lam_ui_get_attr(ch, "y");
				const char *aw = lam_ui_get_attr(ch, "width");
				const char *ah = lam_ui_get_attr(ch, "height");
				const char *ar = lam_ui_get_attr(ch, "rx");
				svg_emit_rect(&sl,
				              ax ? strtof(ax, NULL) : 0.0f,
				              ay ? strtof(ay, NULL) : 0.0f,
				              aw ? strtof(aw, NULL) : 0.0f,
				              ah ? strtof(ah, NULL) : 0.0f,
				              ar ? strtof(ar, NULL) : 0.0f);
			} else if (ci_eq(ctag, "circle")) {
				const char *acx = lam_ui_get_attr(ch, "cx");
				const char *acy = lam_ui_get_attr(ch, "cy");
				const char *ar  = lam_ui_get_attr(ch, "r");
				float cx0 = acx ? strtof(acx, NULL) : 0.0f;
				float cy0 = acy ? strtof(acy, NULL) : 0.0f;
				float rr  = ar  ? strtof(ar,  NULL) : 0.0f;
				if (rr > 0.0f) {
					svg_emit_arc(&sl, cx0 - rr, cy0, rr, rr, 0, 1, 1, cx0 + rr, cy0);
					svg_emit_arc(&sl, cx0 + rr, cy0, rr, rr, 0, 1, 1, cx0 - rr, cy0);
				}
			} else if (ci_eq(ctag, "line")) {
				const char *a1 = lam_ui_get_attr(ch, "x1");
				const char *b1 = lam_ui_get_attr(ch, "y1");
				const char *a2 = lam_ui_get_attr(ch, "x2");
				const char *b2 = lam_ui_get_attr(ch, "y2");
				svg_push_seg(&sl,
				             a1 ? strtof(a1, NULL) : 0.0f,
				             b1 ? strtof(b1, NULL) : 0.0f,
				             a2 ? strtof(a2, NULL) : 0.0f,
				             b2 ? strtof(b2, NULL) : 0.0f);
			}
		}

		/* Map viewBox coords -> pixel coords preserving aspect */
		float sx = (float)rw / vb_w;
		float sy = (float)rh / vb_h;
		float sc = sx < sy ? sx : sy;
		float ox = ((float)rw - vb_w * sc) * 0.5f - vb_x * sc;
		float oy = ((float)rh - vb_h * sc) * 0.5f - vb_y * sc;
		for (int i = 0; i < sl.n; i++) {
			sl.segs[i].x0 = sl.segs[i].x0 * sc + ox;
			sl.segs[i].y0 = sl.segs[i].y0 * sc + oy;
			sl.segs[i].x1 = sl.segs[i].x1 * sc + ox;
			sl.segs[i].y1 = sl.segs[i].y1 * sc + oy;
		}

		uint8_t *rgba = calloc((size_t)rw * (size_t)rh, 4);
		if (!rgba) return;
		float half_sw = stroke_w * sc * 0.5f;
		if (half_sw < 0.65f) half_sw = 0.65f;

		for (uint32_t py = 0; py < rh; py++) {
			for (uint32_t px = 0; px < rw; px++) {
				float cov = 0.0f;
				if (do_fill && sl.n > 0) {
					int hits = 0;
					for (int syi = 0; syi < 4; syi++) {
						float fy = (float)py + ((float)syi + 0.5f) * 0.25f;
						for (int sxi = 0; sxi < 4; sxi++) {
							float fx = (float)px + ((float)sxi + 0.5f) * 0.25f;
							int inside = 0;
							for (int i = 0; i < sl.n; i++) {
								float y0 = sl.segs[i].y0, y1 = sl.segs[i].y1;
								if ((y0 > fy) != (y1 > fy)) {
									float xint = sl.segs[i].x0 +
										(fy - y0) * (sl.segs[i].x1 - sl.segs[i].x0) / (y1 - y0);
									if (fx < xint) inside = !inside;
								}
							}
							if (inside) hits++;
						}
					}
					cov = (float)hits / 16.0f;
				}
				if (do_stroke && sl.n > 0) {
					float cxp = (float)px + 0.5f;
					float cyp = (float)py + 0.5f;
					float mind = 1e9f;
					for (int i = 0; i < sl.n; i++) {
						float dd = point_seg_dist(cxp, cyp,
							sl.segs[i].x0, sl.segs[i].y0,
							sl.segs[i].x1, sl.segs[i].y1);
						if (dd < mind) mind = dd;
					}
					float scov = half_sw + 0.6f - mind;
					if (scov < 0.0f) scov = 0.0f;
					if (scov > 1.0f) scov = 1.0f;
					if (scov > cov) cov = scov;
				}
				if (cov > 0.003f) {
					size_t idx = ((size_t)py * (size_t)rw + (size_t)px) * 4;
					rgba[idx + 0] = 255;
					rgba[idx + 1] = 255;
					rgba[idx + 2] = 255;
					rgba[idx + 3] = (uint8_t)lroundf(cov * 255.0f);
				}
			}
		}

		char keybuf[64];
		snprintf(keybuf, sizeof(keybuf), "bl_svg:%016llx:%ux%u",
		         (unsigned long long)hash, rw, rh);
		tex = lam_tex_create_rgba(keybuf, rgba, rw, rh);
		free(rgba);
		if (tex == UINT32_MAX) return;

		if (bl->n_svgs >= bl->cap_svgs) {
			size_t nc = bl->cap_svgs ? bl->cap_svgs * 2 : 32;
			bl_svg_cache_t *na = realloc(bl->svgs, nc * sizeof(*na));
			if (na) {
				bl->svgs = na;
				bl->cap_svgs = nc;
			}
		}
		if (bl->n_svgs < bl->cap_svgs) {
			bl->svgs[bl->n_svgs].hash = hash;
			bl->svgs[bl->n_svgs].w = rw;
			bl->svgs[bl->n_svgs].h = rh;
			bl->svgs[bl->n_svgs].tex = tex;
			bl->n_svgs++;
		}
	}

	uint32_t col = (st && st->color) ? st->color : 0xFFFFFFFFu;
	float op = st ? st->opacity : 1.0f;
	uint32_t a = (uint32_t)lroundf((float)LAM_COLOR_A(col) * op);
	if (a > 255) a = 255;
	uint32_t tint = (col & 0x00FFFFFFu) | (a << 24);
	lam_tint(tint);
	lam_tex_draw(tex, x, y, (uint32_t)w, (uint32_t)h);
	lam_tint(0xFFFFFFFFu);
}

static void draw_term_widget(lam_ui_div_t *div, int32_t x, int32_t y,
                             int32_t w, int32_t h, void *user)
{
	bud_lamina_t *bl = (bud_lamina_t *)user;
	if (!bl || !bl->term) return;
	const lam_ui_style_t *st = lam_ui_get_style(div);
	/* If CSS explicitly set background: transparent (e.g. #rpg-chat #term), let
	 * the parent panel's frosted glass backdrop show through with shadowed text. */
	if (st && (st->set_mask[0] & 1ULL) && st->background_color == 0)
		term_ansi_set_style(bl->term, 0, 1);
	else
		term_ansi_set_style(bl->term, LAM_RGBA(0, 0, 0, 128), 0);
	uint32_t mf = (bl->font_mono != UINT32_MAX) ? bl->font_mono : bl->font_sans;
	term_ansi_render(bl->term, mf, 13, x, y, w, h, bl->term_focused);
}

/* ── RAW_HTML helper (strips inline HTML spans & unescapes entities) ────── */

static char *strip_raw_html(const char *raw)
{
	if (!raw) return NULL;
	size_t n = strlen(raw);
	char *out = malloc(n + 1);
	if (!out) return NULL;
	size_t w = 0;
	for (size_t i = 0; i < n; ) {
		if (raw[i] == '<') {
			while (i < n && raw[i] != '>') i++;
			if (i < n) i++;
		} else if (raw[i] == '&') {
			if (strncmp(raw + i, "&lt;", 4) == 0) {
				out[w++] = '<'; i += 4;
			} else if (strncmp(raw + i, "&gt;", 4) == 0) {
				out[w++] = '>'; i += 4;
			} else if (strncmp(raw + i, "&amp;", 5) == 0) {
				out[w++] = '&'; i += 5;
			} else if (strncmp(raw + i, "&quot;", 6) == 0) {
				out[w++] = '"'; i += 6;
			} else if (strncmp(raw + i, "&nbsp;", 6) == 0) {
				out[w++] = ' '; i += 6;
			} else {
				out[w++] = raw[i++];
			}
		} else {
			out[w++] = raw[i++];
		}
	}
	out[w] = '\0';
	return out;
}

/* ── Bud → Lamina DOM reconciliation ────────────────────────────────────── */

static void make_node_key(const bud_node *node, const char *parent_key,
                          size_t child_idx, char out[BL_KEY_MAX])
{
	const char *k = bud_get_key(node);
	if (k && k[0]) {
		snprintf(out, BL_KEY_MAX, "k:%s", k);
		return;
	}
	const char *id = bud_get_attr(node, "id");
	if (id && id[0]) {
		snprintf(out, BL_KEY_MAX, "i:%s", id);
		return;
	}
	const char *tag = bud_node_tag(node);
	snprintf(out, BL_KEY_MAX, "%s/%s#%zu",
	         parent_key ? parent_key : "", tag ? tag : "t", child_idx);
}

static int is_all_whitespace(const char *s)
{
	if (!s) return 1;
	while (*s) {
		if (!isspace((unsigned char)*s)) return 0;
		s++;
	}
	return 1;
}

static void reconcile_node(bud_lamina_t *bl, const bud_node *bnode,
                           lam_ui_div_t *parent_div,
                           const char *parent_key, size_t child_idx)
{
	if (!bnode || !parent_div) return;
	bud_node_kind kind = bud_node_kind_of(bnode);

	if (kind == BUD_NODE_FRAGMENT) {
		size_t nc = bud_node_child_count(bnode);
		for (size_t i = 0; i < nc; i++) {
			reconcile_node(bl, bud_node_child(bnode, i),
			               parent_div, parent_key, child_idx * 31 + i);
		}
		return;
	}

	if (kind == BUD_NODE_TEXT) {
		const char *txt = bud_node_text(bnode);
		if (!txt || !txt[0] || is_all_whitespace(txt))
			return;
		lam_ui_div_t *td = lam_ui_new(parent_div, NULL);
		lam_ui_set_tag(td, "span");
		lam_ui_text(td, txt);
		return;
	}

	if (kind == BUD_NODE_RAW_HTML) {
		const char *raw = bud_node_text(bnode);
		char *plain = strip_raw_html(raw);
		if (plain && !is_all_whitespace(plain)) {
			lam_ui_div_t *td = lam_ui_new(parent_div, NULL);
			lam_ui_set_tag(td, "span");
			lam_ui_text(td, plain);
		}
		free(plain);
		return;
	}

	/* BUD_NODE_ELEMENT */
	const char *tag = bud_node_tag(bnode);
	if (!tag) tag = "div";

	/* Skip non-visual tags (<script>, <style>, <link>, <meta>, <title>, <svg> icons if empty) */
	if (ci_eq(tag, "script") || ci_eq(tag, "style") ||
	    ci_eq(tag, "link") || ci_eq(tag, "meta") || ci_eq(tag, "head"))
		return;

	lam_ui_div_t *div = lam_ui_new(parent_div, NULL);
	lam_ui_set_tag(div, tag);

	char key[BL_KEY_MAX];
	make_node_key(bnode, parent_key, child_idx, key);

	bl_entry_t *ent = entry_add(bl);
	if (ent) {
		ent->div = div;
		ent->bud = (bud_node *)bnode;
		snprintf(ent->key, sizeof(ent->key), "%s", key);
	}
	lam_ui_set_user_data(div, (void *)bnode);

	/* Copy all attributes */
	uint32_t pseudo = 0;
	const char *id_val = NULL;
	const char *type_val = NULL;
	const char *src_val = NULL;
	const char *val_attr = NULL;
	const char *min_attr = NULL;
	const char *max_attr = NULL;
	const char *step_attr = NULL;
	const char *ph_attr = NULL;
	const char *w_attr = NULL;
	const char *h_attr = NULL;
	int is_webgl = 0;
	int is_sw_canvas = 0;
	int is_term = 0;

	size_t na = bud_node_attr_count(bnode);
	for (size_t i = 0; i < na; i++) {
		const char *an = bud_node_attr_name(bnode, i);
		const char *av = bud_node_attr_value(bnode, i);
		if (!an) continue;
		const char *v = av ? av : "";

		if (strcmp(an, "id") == 0) {
			lam_ui_set_id(div, v);
			id_val = v;
			if (strcmp(v, "term") == 0) is_term = 1;
			if (strcmp(v, "seven-stage-canvas") == 0) is_webgl = 1;
		} else if (strcmp(an, "class") == 0) {
			lam_ui_class(div, v);
		} else if (strcmp(an, "style") == 0) {
			lam_ui_set_inline_style(div, v);
		} else {
			lam_ui_set_attr(div, an, v);
			if (strcmp(an, "disabled") == 0)
				pseudo |= LAM_PSEUDO_DISABLED;
			else if (strcmp(an, "checked") == 0)
				pseudo |= LAM_PSEUDO_CHECKED;
			else if (strcmp(an, "type") == 0)
				type_val = v;
			else if (strcmp(an, "src") == 0)
				src_val = v;
			else if (strcmp(an, "value") == 0)
				val_attr = v;
			else if (strcmp(an, "min") == 0)
				min_attr = v;
			else if (strcmp(an, "max") == 0)
				max_attr = v;
			else if (strcmp(an, "step") == 0)
				step_attr = v;
			else if (strcmp(an, "placeholder") == 0)
				ph_attr = v;
			else if (strcmp(an, "width") == 0)
				w_attr = v;
			else if (strcmp(an, "height") == 0)
				h_attr = v;
			else if (strcmp(an, "data-bud-webgl") == 0 && v[0] && strcmp(v, "0") != 0)
				is_webgl = 1;
			else if (strcmp(an, "data-bud-canvas") == 0 && v[0] && strcmp(v, "0") != 0)
				is_sw_canvas = 1;
			else if (strcmp(an, "data-bud-terminal") == 0 && v[0] && strcmp(v, "0") != 0)
				is_term = 1;
		}
	}
	(void)id_val;

	/* Restore focus pseudo state (hover/active ancestor chains are applied post-layout) */
	if (bl->focus_key[0] && strcmp(bl->focus_key, key) == 0)
		pseudo |= LAM_PSEUDO_FOCUS;
	if (pseudo)
		lam_ui_set_pseudo(div, pseudo);

	/* Restore persisted scroll position */
	bl_state_t *pst = state_find(bl, key, 0);
	if (pst && (pst->scroll_x || pst->scroll_y))
		lam_ui_set_scroll(div, pst->scroll_x, pst->scroll_y);

	/* Attach special native widgets */
	if (ent) {
		if (is_term) {
			ent->kind = W_TERM;
			lam_ui_set_custom_draw(div, draw_term_widget, bl);
		} else if (ci_eq(tag, "canvas") && is_webgl) {
			ent->kind = W_WEBGL_CANVAS;
			bl->webgl_div = div;
			lam_ui_set_custom_draw(div, draw_webgl_canvas, bl);
		} else if (ci_eq(tag, "canvas") && is_sw_canvas) {
			ent->kind = W_SW_CANVAS;
			lam_ui_set_custom_draw(div, draw_sw_canvas, bl);
		} else if (ci_eq(tag, "svg")) {
			ent->kind = W_SVG;
			ent->svg_vb_x = 0.0f;
			ent->svg_vb_y = 0.0f;
			ent->svg_vb_w = 24.0f;
			ent->svg_vb_h = 24.0f;
			const char *vb = bud_get_attr(bnode, "viewBox");
			if (!vb) vb = bud_get_attr(bnode, "viewbox");
			if (vb) {
				float vx = 0, vy = 0, vw = 24, vh = 24;
				if (sscanf(vb, "%f %f %f %f", &vx, &vy, &vw, &vh) == 4 &&
				    vw > 0.0f && vh > 0.0f) {
					ent->svg_vb_x = vx;
					ent->svg_vb_y = vy;
					ent->svg_vb_w = vw;
					ent->svg_vb_h = vh;
				}
			}
			lam_ui_set_intrinsic_size(div, 20, 20);
			lam_ui_set_custom_draw(div, draw_svg_widget, bl);
		} else if (ci_eq(tag, "i")) {
			const char *ptag = lam_ui_get_tag(parent_div);
			if (ptag && ci_eq(ptag, "label")) {
				for (lam_ui_div_t *sib = lam_ui_first_child(parent_div);
				     sib; sib = lam_ui_next_sibling(sib)) {
					bl_entry_t *se = entry_find_by_div(bl, sib);
					if (se && se->kind == W_INPUT_CHECKBOX) {
						ent->kind = W_SWITCH_TRACK;
						snprintf(ent->switch_cb_key, sizeof(ent->switch_cb_key),
						         "%s", se->key);
						lam_ui_set_intrinsic_size(div, 34, 18);
						lam_ui_set_custom_draw(div, draw_switch_track_widget, bl);
						break;
					}
				}
			}
		} else if (ci_eq(tag, "img")) {
			ent->kind = W_IMG;
			uint32_t iw = 0, ih = 0;
			if (src_val && load_image_cached(bl, src_val, &ent->img_tex, &iw, &ih))
				ent->img_valid = 1;
			if (iw == 0 && ih == 0 && w_attr && h_attr) {
				int pw = atoi(w_attr);
				int ph = atoi(h_attr);
				if (pw > 0 && ph > 0) {
					iw = (uint32_t)pw;
					ih = (uint32_t)ph;
				}
			}
			if (iw > 0 && ih > 0)
				lam_ui_set_intrinsic_size(div, iw, ih);
			lam_ui_set_custom_draw(div, draw_img_widget, bl);
		} else if (ci_eq(tag, "video")) {
			ent->kind = W_VIDEO;
			lam_ui_set_custom_draw(div, draw_video_widget, bl);
		} else if (ci_eq(tag, "textarea")) {
			ent->kind = W_INPUT_TEXT;
			ent->is_multiline = 1;
			if (ph_attr)
				snprintf(ent->placeholder, sizeof(ent->placeholder), "%s", ph_attr);
			bl_state_t *ist = state_find(bl, key, 1);
			if (ist && !ist->has_text) {
				if (val_attr) {
					snprintf(ist->text, sizeof(ist->text), "%s", val_attr);
					ist->cursor = (int)strlen(ist->text);
					ist->has_text = 1;
				} else if (bud_node_child_count(bnode) > 0) {
					const bud_node *ch0 = bud_node_child(bnode, 0);
					const char *ct = bud_node_text(ch0);
					if (ct && ct[0]) {
						snprintf(ist->text, sizeof(ist->text), "%s", ct);
						ist->cursor = (int)strlen(ist->text);
						ist->has_text = 1;
					}
				}
			}
			lam_ui_set_intrinsic_size(div, 180, 42);
			lam_ui_set_custom_draw(div, draw_text_input_widget, bl);
			return;
		} else if (ci_eq(tag, "select")) {
			ent->kind = W_SELECT;
			if (val_attr && val_attr[0])
				snprintf(ent->select_label, sizeof(ent->select_label), "%s", val_attr);
			/* Extract selected <option> label text (or first <option>) */
			size_t onc = bud_node_child_count(bnode);
			const char *first_lbl = NULL;
			const char *sel_lbl = NULL;
			for (size_t oi = 0; oi < onc; oi++) {
				const bud_node *opt = bud_node_child(bnode, oi);
				if (bud_node_kind_of(opt) != BUD_NODE_ELEMENT) continue;
				const char *ot = bud_node_tag(opt);
				if (!ot || !ci_eq(ot, "option")) continue;
				const char *olbl = NULL;
				if (bud_node_child_count(opt) > 0)
					olbl = bud_node_text(bud_node_child(opt, 0));
				if (!olbl || !olbl[0]) olbl = bud_get_attr(opt, "value");
				if (!olbl || !olbl[0]) continue;
				if (!first_lbl) first_lbl = olbl;
				if (bud_get_attr(opt, "selected") != NULL) {
					sel_lbl = olbl;
					break;
				}
				if (val_attr && val_attr[0]) {
					const char *ov = bud_get_attr(opt, "value");
					if (ov && strcmp(ov, val_attr) == 0)
						sel_lbl = olbl;
				}
			}
			if (sel_lbl)
				snprintf(ent->select_label, sizeof(ent->select_label), "%s", sel_lbl);
			else if (!ent->select_label[0] && first_lbl)
				snprintf(ent->select_label, sizeof(ent->select_label), "%s", first_lbl);
			lam_ui_set_intrinsic_size(div, 150, 30);
			lam_ui_set_custom_draw(div, draw_select_widget, bl);
			return;
		} else if (ci_eq(tag, "input")) {
			if (type_val && ci_eq(type_val, "hidden")) {
				lam_ui_set_inline_style(div, "display:none;");
			} else if (type_val && ci_eq(type_val, "checkbox")) {
				ent->kind = W_INPUT_CHECKBOX;
				ent->is_checked = (pseudo & LAM_PSEUDO_CHECKED) != 0;
				lam_ui_set_intrinsic_size(div, 34, 18);
				lam_ui_set_custom_draw(div, draw_checkbox_widget, bl);
			} else if (type_val && ci_eq(type_val, "color")) {
				ent->kind = W_INPUT_COLOR;
				ent->color_val = LAM_RGB(0xe2, 0xb6, 0x57);
				if (val_attr && val_attr[0] == '#' && strlen(val_attr) >= 7) {
					unsigned int hex = 0;
					if (sscanf(val_attr + 1, "%6x", &hex) == 1) {
						ent->color_val = LAM_RGB((hex >> 16) & 0xFF,
						                         (hex >> 8) & 0xFF,
						                         hex & 0xFF);
					}
				}
				lam_ui_set_intrinsic_size(div, 32, 24);
				lam_ui_set_custom_draw(div, draw_color_input_widget, bl);
			} else if (type_val && ci_eq(type_val, "range")) {
				ent->kind = W_INPUT_RANGE;
				ent->range_min  = min_attr ? atof(min_attr) : 0.0;
				ent->range_max  = max_attr ? atof(max_attr) : 100.0;
				ent->range_step = step_attr ? atof(step_attr) : 1.0;
				if (ent->range_step <= 0.0) ent->range_step = 1.0;
				ent->range_val  = val_attr ? atof(val_attr) : ent->range_min;
				if (pst && pst->has_range &&
				    bl->drag_slider_key[0] &&
				    strcmp(bl->drag_slider_key, key) == 0) {
					ent->range_val = pst->range_val;
				}
				lam_ui_set_intrinsic_size(div, 120, 18);
				lam_ui_set_custom_draw(div, draw_range_widget, bl);
			} else if (!type_val || ci_eq(type_val, "text") ||
			           ci_eq(type_val, "search") || ci_eq(type_val, "password")) {
				ent->kind = W_INPUT_TEXT;
				ent->is_password = (type_val && ci_eq(type_val, "password")) ? 1 : 0;
				if (ph_attr)
					snprintf(ent->placeholder, sizeof(ent->placeholder), "%s", ph_attr);
				bl_state_t *ist = state_find(bl, key, 1);
				if (ist && !ist->has_text && val_attr) {
					snprintf(ist->text, sizeof(ist->text), "%s", val_attr);
					ist->cursor = (int)strlen(ist->text);
					ist->has_text = 1;
				}
				lam_ui_set_intrinsic_size(div, 160, 30);
				lam_ui_set_custom_draw(div, draw_text_input_widget, bl);
			}
		}
	}

	/* Children reconciliation: inline single/pure-text children onto div directly */
	size_t nc = bud_node_child_count(bnode);
	if (nc == 0) return;

	int only_text = 1;
	for (size_t i = 0; i < nc; i++) {
		bud_node_kind ck = bud_node_kind_of(bud_node_child(bnode, i));
		if (ck != BUD_NODE_TEXT && ck != BUD_NODE_RAW_HTML) {
			only_text = 0;
			break;
		}
	}

	if (only_text && (!ent || ent->kind == W_NONE)) {
		size_t total = 0;
		for (size_t i = 0; i < nc; i++) {
			const bud_node *ch = bud_node_child(bnode, i);
			const char *t = bud_node_text(ch);
			if (t) total += strlen(t);
		}
		if (total > 0) {
			char *buf = malloc(total + 1);
			if (buf) {
				size_t pos = 0;
				for (size_t i = 0; i < nc; i++) {
					const bud_node *ch = bud_node_child(bnode, i);
					const char *t = bud_node_text(ch);
					if (!t) continue;
					if (bud_node_kind_of(ch) == BUD_NODE_RAW_HTML) {
						char *plain = strip_raw_html(t);
						if (plain) {
							size_t pl = strlen(plain);
							memcpy(buf + pos, plain, pl);
							pos += pl;
							free(plain);
						}
					} else {
						size_t tl = strlen(t);
						memcpy(buf + pos, t, tl);
						pos += tl;
					}
				}
				buf[pos] = '\0';
				if (!is_all_whitespace(buf))
					lam_ui_text(div, buf);
				free(buf);
			}
		}
		return;
	}

	for (size_t i = 0; i < nc; i++) {
		reconcile_node(bl, bud_node_child(bnode, i), div, key, i);
	}
}

static void reconcile_body_root(bud_lamina_t *bl, const bud_node *bnode,
                                lam_ui_div_t *body)
{
	bl_entry_t *ent = entry_add(bl);
	if (ent) {
		ent->div = body;
		ent->bud = (bud_node *)bnode;
		snprintf(ent->key, sizeof(ent->key), "body");
	}
	lam_ui_set_user_data(body, (void *)bnode);

	size_t na = bud_node_attr_count(bnode);
	for (size_t i = 0; i < na; i++) {
		const char *an = bud_node_attr_name(bnode, i);
		const char *av = bud_node_attr_value(bnode, i);
		if (!an) continue;
		const char *v = av ? av : "";
		if (strcmp(an, "id") == 0)
			lam_ui_set_id(body, v);
		else if (strcmp(an, "class") == 0)
			lam_ui_class(body, v);
		else if (strcmp(an, "style") == 0)
			lam_ui_set_inline_style(body, v);
		else
			lam_ui_set_attr(body, an, v);
	}

	size_t nc = bud_node_child_count(bnode);
	for (size_t i = 0; i < nc; i++) {
		reconcile_node(bl, bud_node_child(bnode, i), body, "body", i);
	}
}

/* ── Public Lifecycle & Configuration ───────────────────────────────────── */

bud_lamina_t *bud_lamina_create(int vw, int vh)
{
	bud_lamina_t *bl = calloc(1, sizeof(*bl));
	if (!bl) return NULL;
	bl->vw = vw > 0 ? vw : 1280;
	bl->vh = vh > 0 ? vh : 800;
	bl->font_sans = UINT32_MAX;
	bl->font_sans_bold = UINT32_MAX;
	bl->font_mono = UINT32_MAX;
	bl->font_mono_bold = UINT32_MAX;
	bl->dirty = 1;
	bl->dark_mode = 1;
	bl->press_btn = -1;
	bl->last_cursor = LAM_CURSOR_DEFAULT;
	snprintf(bl->asset_root, sizeof(bl->asset_root), "htdocs");

	lam_ui_init((uint32_t)bl->vw, (uint32_t)bl->vh);
	lam_ui_set_asset_root(bl->asset_root);
	lam_ui_set_dark_mode(bl->dark_mode);
	bl->ss = lam_ui_stylesheet_init();
	bl->term = term_ansi_create();
	return bl;
}

static int set_pseudo_chain(lam_ui_div_t *old_leaf, lam_ui_div_t *new_leaf, uint32_t bit);
static lam_ui_div_t *update_hover_state(bud_lamina_t *bl, int x, int y);

void bud_lamina_destroy(bud_lamina_t *bl)
{
	if (!bl) return;
	if (g_active_bl == bl)
		g_active_bl = NULL;
	if (bl->webgl_surf)
		lam_webgl_surface_destroy(bl->webgl_surf);
	if (bl->ui_root)
		lam_ui_destroy(bl->ui_root);
	if (bl->rt) {
		bud_node *old_bud = bud_runtime_root(bl->rt);
		bud_runtime_free(bl->rt);
		bud_free(old_bud);
	}
	if (bl->term)
		term_ansi_destroy(bl->term);
	for (size_t i = 0; i < bl->n_imgs; i++)
		free(bl->imgs[i].src);
	free(bl->imgs);
	free(bl->svgs);
	free(bl->entries);
	free(bl->states);
	lam_css_sheet_free(bl->ss);
	free(bl);
}

int bud_lamina_load_css_file(bud_lamina_t *bl, const char *path)
{
	if (!bl || !path) return -1;
	bl->dirty = 1;
	return lam_css_load_file(bl->ss, path);
}

int bud_lamina_load_css(bud_lamina_t *bl, const char *css, size_t len)
{
	if (!bl || !css) return -1;
	bl->dirty = 1;
	return lam_css_parse(bl->ss, css, len);
}

int bud_lamina_set_css_var(bud_lamina_t *bl, const char *name, const char *val)
{
	if (!bl || !name) return -1;
	bl->dirty = 1;
	return lam_css_set_var(bl->ss, name, val);
}

void bud_lamina_set_video_tex(bud_lamina_t *bl, const char *id,
                              uint32_t gl_tex, uint32_t w, uint32_t h)
{
	if (!bl || !id || !id[0]) return;
	for (size_t i = 0; i < bl->n_videos; i++) {
		if (strcmp(bl->videos[i].id, id) == 0) {
			bl->videos[i].gl_tex = gl_tex;
			bl->videos[i].w = w;
			bl->videos[i].h = h;
			return;
		}
	}
	if (bl->n_videos < BL_VIDEO_MAX) {
		bl_video_slot_t *s = &bl->videos[bl->n_videos++];
		snprintf(s->id, sizeof(s->id), "%s", id);
		s->gl_tex = gl_tex;
		s->w = w;
		s->h = h;
	}
}

void bud_lamina_set_fonts(bud_lamina_t *bl,
                          uint32_t sans, uint32_t sans_bold,
                          uint32_t mono, uint32_t mono_bold)
{
	if (!bl) return;
	bl->font_sans = sans;
	bl->font_sans_bold = sans_bold;
	bl->font_mono = mono;
	bl->font_mono_bold = mono_bold;
	lam_ui_set_fonts(sans, sans_bold, mono, mono_bold);
	bl->dirty = 1;
}

void bud_lamina_set_asset_root(bud_lamina_t *bl, const char *htdocs_dir)
{
	if (!bl || !htdocs_dir) return;
	snprintf(bl->asset_root, sizeof(bl->asset_root), "%s", htdocs_dir);
	lam_ui_set_asset_root(bl->asset_root);
}

void bud_lamina_set_img_fetch(bud_lamina_t *bl, bud_lamina_img_fetch_fn fn, void *ctx)
{
	if (!bl) return;
	bl->img_fetch_fn = fn;
	bl->img_fetch_ctx = ctx;
}

void bud_lamina_set_render(bud_lamina_t *bl, bud_lamina_render_fn fn, void *ctx)
{
	if (!bl) return;
	bl->render_fn = fn;
	bl->render_ctx = ctx;
	bl->dirty = 1;
}

void bud_lamina_set_term_input(bud_lamina_t *bl, bud_lamina_term_input_fn fn, void *ctx)
{
	if (!bl) return;
	bl->term_input_fn = fn;
	bl->term_input_ctx = ctx;
}

void bud_lamina_set_dark_mode(bud_lamina_t *bl, int dark)
{
	if (!bl) return;
	bl->dark_mode = dark ? 1 : 0;
	lam_ui_set_dark_mode(bl->dark_mode);
	bl->dirty = 1;
}

void bud_lamina_set_active_global(bud_lamina_t *bl)
{
	g_active_bl = bl;
}

void bud_lamina_mark_dirty(bud_lamina_t *bl)
{
	if (bl) bl->dirty = 1;
}

int bud_lamina_is_dirty(const bud_lamina_t *bl)
{
	return bl ? bl->dirty : 0;
}

void bud_lamina_resize(bud_lamina_t *bl, int vw, int vh)
{
	if (!bl || vw <= 0 || vh <= 0) return;
	if (bl->vw == vw && bl->vh == vh) return;
	bl->vw = vw;
	bl->vh = vh;
	lam_ui_resize((uint32_t)vw, (uint32_t)vh);
	if (bl->ui_root) {
		lam_ui_set_size(bl->ui_root, vw, vh);
		lam_ui_apply_styles(bl->ui_root, bl->ss);
		lam_ui_layout(bl->ui_root);
		if (bl->has_mouse)
			update_hover_state(bl, bl->mouse_x, bl->mouse_y);
	}
}

/* ── Frame Sync & Render ────────────────────────────────────────────────── */

int bud_lamina_sync(bud_lamina_t *bl)
{
	if (!bl || !bl->dirty || !bl->render_fn)
		return 0;

	bl->dirty = 0;

	bud_node *new_bud = bl->render_fn(bl->render_ctx);
	if (!new_bud) return 0;

	bud_runtime *new_rt = bud_runtime_new(new_bud);
	if (!new_rt) {
		bud_free(new_bud);
		return -1;
	}

	/* Tear down previous UI & Bud runtime cleanly (0 leaks) */
	bl->hover_div = NULL;
	bl->active_div = NULL;
	if (bl->ui_root) {
		lam_ui_destroy(bl->ui_root);
		bl->ui_root = NULL;
	}
	if (bl->rt) {
		bud_node *old_bud = bud_runtime_root(bl->rt);
		bud_runtime_free(bl->rt);
		bud_free(old_bud);
		bl->rt = NULL;
	}
	bl->n_entries = 0;
	bl->webgl_div = NULL;
	bl->rt = new_rt;

	/* Synthetic <body> container sized to viewport.
	 * If Bud root is already <body>, reconcile its attrs & children directly
	 * onto `body` instead of nesting <body> inside <body>. */
	lam_ui_div_t *body = lam_ui_new(NULL, NULL);
	lam_ui_set_tag(body, "body");
	lam_ui_set_size(body, bl->vw, bl->vh);
	bl->ui_root = body;

	if (bud_node_kind_of(new_bud) == BUD_NODE_ELEMENT &&
	    bud_node_tag(new_bud) && ci_eq(bud_node_tag(new_bud), "body"))
		reconcile_body_root(bl, new_bud, body);
	else
		reconcile_node(bl, new_bud, body, "body", 0);

	/* Restore :active ancestor chain by key before initial cascade */
	if (bl->active_key[0]) {
		bl_entry_t *ae = entry_find_by_key(bl, bl->active_key);
		if (ae && ae->div) {
			set_pseudo_chain(NULL, ae->div, LAM_PSEUDO_ACTIVE);
			bl->active_div = ae->div;
		}
	}

	lam_ui_apply_styles(bl->ui_root, bl->ss);
	lam_ui_layout(bl->ui_root);

	/* Re-apply live :hover ancestor chain & OS cursor across DOM rebuilds */
	if (bl->has_mouse) {
		update_hover_state(bl, bl->mouse_x, bl->mouse_y);
	} else if (bl->hover_key[0]) {
		bl_entry_t *he = entry_find_by_key(bl, bl->hover_key);
		if (he && he->div) {
			set_pseudo_chain(NULL, he->div, LAM_PSEUDO_HOVER);
			bl->hover_div = he->div;
			lam_ui_apply_styles(bl->ui_root, bl->ss);
			lam_ui_layout(bl->ui_root);
		}
	}
	return 1;
}

void bud_lamina_render(bud_lamina_t *bl)
{
	if (!bl || !bl->ui_root)
		return;
	lam_ui_render(bl->ui_root);
}

/* ── WebGL2 Stage Surface Bridge ────────────────────────────────────────── */

int bud_lamina_webgl_size(const bud_lamina_t *bl, int *w_out, int *h_out)
{
	if (!bl || !bl->webgl_div)
		return 0;
	int w = lam_ui_get_width(bl->webgl_div);
	int h = lam_ui_get_height(bl->webgl_div);
	if (w <= 0) w = bl->vw;
	if (h <= 0) h = bl->vh;
	float s = lam_get_dpi_scale();
	if (s > 1.001f) {
		w = (int)lroundf((float)w * s);
		h = (int)lroundf((float)h * s);
	}
	if (w_out) *w_out = w;
	if (h_out) *h_out = h;
	return 1;
}

int bud_lamina_webgl_begin(bud_lamina_t *bl)
{
	int w = 0, h = 0;
	if (!bud_lamina_webgl_size(bl, &w, &h))
		return 0;
	if (w < 16) w = 16;
	if (h < 16) h = 16;

	if (!bl->webgl_surf) {
		bl->webgl_surf = lam_webgl_surface_create((uint32_t)w, (uint32_t)h);
		if (!bl->webgl_surf) return 0;
		bl->webgl_w = w;
		bl->webgl_h = h;
	} else if (bl->webgl_w != w || bl->webgl_h != h) {
		lam_webgl_surface_resize(bl->webgl_surf, (uint32_t)w, (uint32_t)h);
		bl->webgl_w = w;
		bl->webgl_h = h;
	}

	lam_webgl_begin(bl->webgl_surf);
	return 1;
}

void bud_lamina_webgl_end(bud_lamina_t *bl)
{
	if (!bl || !bl->webgl_surf) return;
	lam_webgl_end();
}

/* ── Embedded ANSI Terminal ─────────────────────────────────────────────── */

void bud_lamina_term_write(bud_lamina_t *bl, const char *data, size_t len)
{
	if (!bl || !bl->term) return;
	term_ansi_write(bl->term, data, len);
}

int bud_lamina_term_line_count(const bud_lamina_t *bl)
{
	return (bl && bl->term) ? term_ansi_line_count(bl->term) : 0;
}

const char *bud_lamina_term_line_text(const bud_lamina_t *bl, int idx_from_bottom,
                                      char *buf, size_t cap)
{
	if (!bl || !bl->term) {
		if (buf && cap > 0) buf[0] = '\0';
		return "";
	}
	return term_ansi_get_line(bl->term, idx_from_bottom, buf, cap);
}

/* ── Event Helpers & GLFW Key → W3C Key Mapping ─────────────────────────── */

static const char *glfw_key_to_w3c(uint32_t code, int mods, char scratch[4])
{
	int shift = (mods & MOD_SHIFT) != 0;
	if (code >= K_A && code <= K_Z) {
		scratch[0] = (char)(shift ? code : (code + 32));
		scratch[1] = '\0';
		return scratch;
	}
	if (code >= K_0 && code <= K_9) {
		static const char shifted[11] = ")!@#$%^&*(";
		scratch[0] = shift ? shifted[code - K_0] : (char)code;
		scratch[1] = '\0';
		return scratch;
	}
	if (code >= K_KP_0 && code <= K_KP_9) {
		scratch[0] = (char)('0' + (code - K_KP_0));
		scratch[1] = '\0';
		return scratch;
	}
	switch (code) {
	case K_SPACE:      return " ";
	case K_ENTER:
	case K_KP_ENTER:   return "Enter";
	case K_ESCAPE:     return "Escape";
	case K_TAB:        return "Tab";
	case K_BACKSPACE:  return "Backspace";
	case K_DELETE:     return "Delete";
	case K_UP:         return "ArrowUp";
	case K_DOWN:       return "ArrowDown";
	case K_LEFT:       return "ArrowLeft";
	case K_RIGHT:      return "ArrowRight";
	case K_HOME:       return "Home";
	case K_END:        return "End";
	case K_PAGE_UP:    return "PageUp";
	case K_PAGE_DOWN:  return "PageDown";
	case K_MINUS:      return shift ? "_" : "-";
	case K_EQUAL:      return shift ? "+" : "=";
	case K_KP_ADD:     return "+";
	case K_KP_SUB:     return "-";
	case K_COMMA:      return shift ? "<" : ",";
	case K_PERIOD:     return shift ? ">" : ".";
	case K_SLASH:      return shift ? "?" : "/";
	case K_LBRACKET:   return shift ? "{" : "[";
	case K_RBRACKET:   return shift ? "}" : "]";
	case K_SEMICOLON:  return shift ? ":" : ";";
	case K_APOSTROPHE: return shift ? "\"" : "'";
	case K_GRAVE:      return shift ? "~" : "`";
	case K_BACKSLASH:  return shift ? "|" : "\\";
	case K_LEFT_SHIFT:
	case K_RIGHT_SHIFT: return "Shift";
	default:
		if (code >= K_F1 && code <= K_F12) {
			static char fbuf[4];
			snprintf(fbuf, sizeof(fbuf), "F%u", code - K_F1 + 1);
			return fbuf;
		}
		return NULL;
	}
}

static int node_has_listener(const bud_node *node, const char *ev_name)
{
	if (!node || !ev_name) return 0;
	size_t n = bud_node_listener_count(node);
	for (size_t i = 0; i < n; i++) {
		const char *en = bud_node_listener_event(node, i);
		if (en && strcmp(en, ev_name) == 0)
			return 1;
	}
	return 0;
}

static int bl_dispatch_bud(bud_lamina_t *bl, bud_node *target,
                           const char *ev_name, void *user)
{
	if (!bl || !bl->rt || !target || !ev_name) return -1;
	int wg = bud_lamina_webgl_begin(bl);
	int rc = bud_runtime_dispatch(bl->rt, target, ev_name, user);
	if (wg)
		bud_lamina_webgl_end(bl);
	return rc;
}

static void dispatch_window_event(bud_lamina_t *bl, const char *ev_name, void *user)
{
	if (!bl || !bl->rt || !ev_name) return;
	/* Dispatch to root first (most @window listeners live on root),
	 * then any other element carrying that @window listener. */
	bud_node *root = bud_runtime_root(bl->rt);
	if (root && node_has_listener(root, ev_name))
		bl_dispatch_bud(bl, root, ev_name, user);

	for (size_t i = 0; i < bl->n_entries; i++) {
		bud_node *bn = bl->entries[i].bud;
		if (bn && bn != root && node_has_listener(bn, ev_name)) {
			bl_dispatch_bud(bl, bn, ev_name, user);
			/* Stop if handler invalidated and we only want current tree */
			if (!bl->rt) break;
		}
	}
}

static void find_holder_box(const bud_lamina_t *bl,
                            int *hx, int *hy, int *hw, int *hh)
{
	*hx = 0;
	*hy = 0;
	*hw = bl ? bl->vw : 0;
	*hh = bl ? bl->vh : 0;
	if (!bl) return;
	for (size_t i = 0; i < bl->n_entries; i++) {
		const lam_ui_div_t *d = bl->entries[i].div;
		if (!d) continue;
		const char *id = lam_ui_get_id(d);
		if (id && strcmp(id, "holder") == 0) {
			int w = lam_ui_get_width(d);
			int h = lam_ui_get_height(d);
			if (w > 0 && h > 0) {
				*hx = lam_ui_get_x(d);
				*hy = lam_ui_get_y(d);
				*hw = w;
				*hh = h;
			}
			return;
		}
	}
}

static void resolve_target_rect(const bud_lamina_t *bl, const lam_ui_div_t *div,
                                int *rx, int *ry, int *rw, int *rh,
                                int *hx_out, int *hy_out, int *hw_out, int *hh_out)
{
	int hx = 0, hy = 0, hw = 0, hh = 0;
	find_holder_box(bl, &hx, &hy, &hw, &hh);
	if (hx_out) *hx_out = hx;
	if (hy_out) *hy_out = hy;
	if (hw_out) *hw_out = hw;
	if (hh_out) *hh_out = hh;

	/* Match bud-cap.js: climb to [data-bud-persist-key] or [data-win-id]
	 * (excluding drag handles carrying [data-win-drag]). */
	const lam_ui_div_t *cur = div;
	const lam_ui_div_t *ref = div;
	while (cur) {
		const char *drag = lam_ui_get_attr(cur, "data-win-drag");
		if (!drag || !drag[0]) {
			const char *pk  = lam_ui_get_attr(cur, "data-bud-persist-key");
			const char *wid = lam_ui_get_attr(cur, "data-win-id");
			if ((pk && pk[0]) || (wid && wid[0])) {
				ref = cur;
				break;
			}
		}
		cur = lam_ui_parent(cur);
	}
	if (ref) {
		*rx = lam_ui_get_x(ref) - hx;
		*ry = lam_ui_get_y(ref) - hy;
		*rw = lam_ui_get_width(ref);
		*rh = lam_ui_get_height(ref);
	} else {
		*rx = -1; *ry = -1; *rw = 0; *rh = 0;
	}
}

static void update_slider_from_mouse(bud_lamina_t *bl, bl_entry_t *e,
                                     int mx, const char *ev_name)
{
	if (!bl || !e || e->kind != W_INPUT_RANGE || !e->div) return;
	int x = lam_ui_get_x(e->div);
	int w = lam_ui_get_width(e->div);
	if (w <= 0) return;

	double t = (double)(mx - x) / (double)w;
	if (t < 0.0) t = 0.0;
	if (t > 1.0) t = 1.0;
	double raw = e->range_min + t * (e->range_max - e->range_min);
	double step = e->range_step > 0.0 ? e->range_step : 1.0;
	double val = e->range_min + floor((raw - e->range_min) / step + 0.5) * step;
	if (val < e->range_min) val = e->range_min;
	if (val > e->range_max) val = e->range_max;

	e->range_val = val;
	bl_state_t *st = state_find(bl, e->key, 1);
	if (st) {
		st->range_val = val;
		st->has_range = 1;
	}

	char vbuf[64];
	if (fabs(val - floor(val + 0.5)) < 1e-6)
		snprintf(vbuf, sizeof(vbuf), "%d", (int)floor(val + 0.5));
	else
		snprintf(vbuf, sizeof(vbuf), "%.3f", val);

	if (bl->rt && e->bud)
		bl_dispatch_bud(bl, e->bud, ev_name, vbuf);
}

static void set_pseudo_by_key(bud_lamina_t *bl, const char *old_key,
                              const char *new_key, uint32_t bit, int restyle)
{
	if (strcmp(old_key, new_key) == 0) return;
	int changed = 0;
	if (old_key[0]) {
		bl_entry_t *oe = entry_find_by_key(bl, old_key);
		if (oe && oe->div) {
			uint32_t p = lam_ui_get_pseudo(oe->div) & ~bit;
			lam_ui_set_pseudo(oe->div, p);
			changed = 1;
		}
	}
	if (new_key[0]) {
		bl_entry_t *ne = entry_find_by_key(bl, new_key);
		if (ne && ne->div) {
			uint32_t p = lam_ui_get_pseudo(ne->div) | bit;
			lam_ui_set_pseudo(ne->div, p);
			changed = 1;
		}
	}
	if (changed && restyle && bl->ui_root) {
		lam_ui_apply_styles(bl->ui_root, bl->ss);
		lam_ui_layout(bl->ui_root);
	}
}

/* Propagate CSS :hover or :active along the full ancestor chain (leaf -> body). */
static int set_pseudo_chain(lam_ui_div_t *old_leaf, lam_ui_div_t *new_leaf, uint32_t bit)
{
	if (old_leaf == new_leaf)
		return 0;
	for (lam_ui_div_t *d = old_leaf; d; d = lam_ui_parent(d)) {
		uint32_t p = lam_ui_get_pseudo(d);
		if (p & bit)
			lam_ui_set_pseudo(d, p & ~bit);
	}
	for (lam_ui_div_t *d = new_leaf; d; d = lam_ui_parent(d)) {
		uint32_t p = lam_ui_get_pseudo(d);
		if (!(p & bit))
			lam_ui_set_pseudo(d, p | bit);
	}
	return 1;
}

static lam_cursor_t resolve_cursor_for_div(bud_lamina_t *bl, const lam_ui_div_t *div)
{
	if (bl && bl->drag_slider_key[0])
		return LAM_CURSOR_GRABBING;
	if (!div)
		return LAM_CURSOR_DEFAULT;

	const lam_ui_style_t *st = lam_ui_get_style(div);
	if (st && st->cursor != LAM_CURSOR_AUTO)
		return (lam_cursor_t)st->cursor;

	for (const lam_ui_div_t *cur = div; cur; cur = lam_ui_parent(cur)) {
		bl_entry_t *e = entry_find_by_div(bl, cur);
		if (e) {
			if (e->kind == W_INPUT_TEXT || e->kind == W_TERM)
				return LAM_CURSOR_TEXT;
			if (e->kind == W_INPUT_RANGE)
				return LAM_CURSOR_POINTER;
		}
		const char *tag = lam_ui_get_tag(cur);
		if (tag && (ci_eq(tag, "button") || ci_eq(tag, "a")))
			return LAM_CURSOR_POINTER;
		const char *iref = lam_ui_get_attr(cur, "data-item-ref");
		if (iref && iref[0])
			return LAM_CURSOR_POINTER;
	}
	return LAM_CURSOR_DEFAULT;
}

static lam_ui_div_t *update_hover_state(bud_lamina_t *bl, int x, int y)
{
	if (!bl || !bl->ui_root)
		return NULL;

	lam_ui_div_t *hit = lam_ui_hit_test(bl->ui_root, x, y);
	if (hit != bl->hover_div) {
		set_pseudo_chain(bl->hover_div, hit, LAM_PSEUDO_HOVER);
		bl->hover_div = hit;
		bl_entry_t *he = entry_find_by_div(bl, hit);
		snprintf(bl->hover_key, sizeof(bl->hover_key), "%s", he ? he->key : "");
		lam_ui_apply_styles(bl->ui_root, bl->ss);
		lam_ui_layout(bl->ui_root);

		/* One bounded re-hit pass in case :hover revealed pointer-events:auto
		 * children such as .hud-win-toolbar or .chat-resize-handle. */
		lam_ui_div_t *hit2 = lam_ui_hit_test(bl->ui_root, x, y);
		if (hit2 && hit2 != bl->hover_div) {
			set_pseudo_chain(bl->hover_div, hit2, LAM_PSEUDO_HOVER);
			bl->hover_div = hit2;
			bl_entry_t *he2 = entry_find_by_div(bl, hit2);
			snprintf(bl->hover_key, sizeof(bl->hover_key), "%s", he2 ? he2->key : "");
			lam_ui_apply_styles(bl->ui_root, bl->ss);
			lam_ui_layout(bl->ui_root);
			hit = hit2;
		}
	}

	lam_cursor_t cur = resolve_cursor_for_div(bl, hit);
	if (cur != bl->last_cursor) {
		bl->last_cursor = cur;
		lam_set_cursor(cur);
	}
	return hit;
}

static int glfw_btn_to_w3c(int btn)
{
	if (btn == 1) return 2; /* GLFW right  -> W3C right  (2) */
	if (btn == 2) return 1; /* GLFW middle -> W3C middle (1) */
	return btn;
}

static lam_ui_div_t *find_common_ancestor(lam_ui_div_t *a, lam_ui_div_t *b)
{
	if (!a || !b) return NULL;
	for (lam_ui_div_t *pa = a; pa; pa = lam_ui_parent(pa)) {
		for (lam_ui_div_t *pb = b; pb; pb = lam_ui_parent(pb)) {
			if (pa == pb)
				return pa;
		}
	}
	return NULL;
}

static int div_in_win_drag_handle(const lam_ui_div_t *div)
{
	for (const lam_ui_div_t *cur = div; cur; cur = lam_ui_parent(cur)) {
		const char *tag = lam_ui_get_tag(cur);
		if (tag && ci_eq(tag, "button"))
			return 0;
		const char *d = lam_ui_get_attr(cur, "data-win-drag");
		if (d && d[0])
			return 1;
	}
	return 0;
}

/* ── Main Input Event Handler ───────────────────────────────────────────── */

void bud_lamina_handle_event(bud_lamina_t *bl, const lam_event_t *ev)
{
	if (!bl || !ev) return;

	switch (ev->type) {
	case LAM_EV_RESIZE:
		bud_lamina_resize(bl, ev->x, ev->y);
		break;

	case LAM_EV_MOUSE_MOVE: {
		bl->mouse_x = ev->x;
		bl->mouse_y = ev->y;
		bl->has_mouse = 1;

		if (bl->mouse_buttons != 0 &&
		    (abs(ev->x - bl->press_x) > 6 || abs(ev->y - bl->press_y) > 6)) {
			bl->press_dragged = 1;
		}

		if (bl->drag_slider_key[0]) {
			bl_entry_t *se = entry_find_by_key(bl, bl->drag_slider_key);
			if (se) update_slider_from_mouse(bl, se, ev->x, "input");
		}

		update_hover_state(bl, ev->x, ev->y);

		int hx = 0, hy = 0, hw = bl->vw, hh = bl->vh;
		find_holder_box(bl, &hx, &hy, &hw, &hh);
		char payload[128];
		snprintf(payload, sizeof(payload), "%d,%d,%d,%d,-1,-1,0,0,0,%u",
		         ev->x - hx, ev->y - hy, hw, hh, bl->mouse_buttons);
		dispatch_window_event(bl, "mousemove@window", payload);
		break;
	}

	case LAM_EV_MOUSE_BTN: {
		int raw_btn = (int)ev->code;
		int w3c_btn = glfw_btn_to_w3c(raw_btn);
		int down = (ev->action == 1);
		bl->mouse_x = ev->x;
		bl->mouse_y = ev->y;
		bl->has_mouse = 1;

		/* GLFW 0=left(1), 1=right(2), 2=middle(4) already matches W3C buttons mask. */
		if (down) bl->mouse_buttons |= (1u << (raw_btn & 7));
		else      bl->mouse_buttons &= ~(1u << (raw_btn & 7));

		/* Ensure hover chain is live before hit-testing (e.g. click without move). */
		if (down)
			update_hover_state(bl, ev->x, ev->y);

		lam_ui_div_t *hit = bl->ui_root ? lam_ui_hit_test(bl->ui_root, ev->x, ev->y) : NULL;
		bl_entry_t *he = entry_find_by_div(bl, hit);
		bud_node *target_bud = he ? he->bud : NULL;
		if (!target_bud && hit) {
			/* Walk up to nearest div backed by a bud_node (e.g. inline text span) */
			lam_ui_div_t *p = hit;
			while (p && !target_bud) {
				target_bud = (bud_node *)lam_ui_get_user_data(p);
				if (!target_bud) p = lam_ui_parent(p);
			}
			if (p) {
				hit = p;
				he = entry_find_by_div(bl, p);
			}
		}

		int rx = -1, ry = -1, rw = 0, rh = 0;
		int hx = 0, hy = 0, hw = bl->vw, hh = bl->vh;
		resolve_target_rect(bl, hit, &rx, &ry, &rw, &rh, &hx, &hy, &hw, &hh);

		char payload[160];
		snprintf(payload, sizeof(payload), "%d,%d,%d,%d,%d,%d,%d,%d,%d,%u",
		         ev->x - hx, ev->y - hy, hw, hh, rx, ry, rw, rh, w3c_btn, bl->mouse_buttons);

		if (down) {
			/* Update active ancestor chain & focus */
			char prev_foc[BL_KEY_MAX];
			snprintf(prev_foc, sizeof(prev_foc), "%s", bl->focus_key);

			const char *hk = he ? he->key : "";
			snprintf(bl->active_key, sizeof(bl->active_key), "%s", hk);
			if (set_pseudo_chain(bl->active_div, hit, LAM_PSEUDO_ACTIVE) && bl->ui_root) {
				bl->active_div = hit;
				lam_ui_apply_styles(bl->ui_root, bl->ss);
				lam_ui_layout(bl->ui_root);
			}

			bl->press_btn = w3c_btn;
			bl->press_x = ev->x;
			bl->press_y = ev->y;
			bl->press_dragged = 0;
			snprintf(bl->press_key, sizeof(bl->press_key), "%s", hk);

			if (he && (he->kind == W_INPUT_TEXT || he->kind == W_TERM)) {
				snprintf(bl->focus_key, sizeof(bl->focus_key), "%s", hk);
				bl->term_focused = (he->kind == W_TERM);
			} else {
				bl->focus_key[0] = '\0';
				bl->term_focused = 0;
			}
			set_pseudo_by_key(bl, prev_foc, bl->focus_key, LAM_PSEUDO_FOCUS, 1);

			if (w3c_btn == 0 && he && he->kind == W_INPUT_RANGE) {
				snprintf(bl->drag_slider_key, sizeof(bl->drag_slider_key), "%s", hk);
				update_slider_from_mouse(bl, he, ev->x, "input");
				update_hover_state(bl, ev->x, ev->y);
			}

			if (bl->rt && target_bud)
				bl_dispatch_bud(bl, target_bud, "mousedown", payload);
		} else {
			int was_slider_drag = (bl->drag_slider_key[0] != '\0');
			if (was_slider_drag) {
				bl_entry_t *se = entry_find_by_key(bl, bl->drag_slider_key);
				if (se) update_slider_from_mouse(bl, se, ev->x, "change");
				bl->drag_slider_key[0] = '\0';
			}

			bl->active_key[0] = '\0';
			if (set_pseudo_chain(bl->active_div, NULL, LAM_PSEUDO_ACTIVE) && bl->ui_root) {
				bl->active_div = NULL;
				lam_ui_apply_styles(bl->ui_root, bl->ss);
				lam_ui_layout(bl->ui_root);
			}

			dispatch_window_event(bl, "mouseup@window", payload);

			if (bl->rt && hit && w3c_btn == 0 && bl->press_btn == 0 && !was_slider_drag) {
				bl_entry_t *pe = bl->press_key[0] ? entry_find_by_key(bl, bl->press_key) : NULL;
				lam_ui_div_t *press_div = pe ? pe->div : NULL;
				lam_ui_div_t *click_div = press_div ? find_common_ancestor(press_div, hit) : NULL;

				int suppress_click = 0;
				if (bl->press_dragged) {
					if ((pe && (pe->kind == W_WEBGL_CANVAS || pe->kind == W_SW_CANVAS)) ||
					    (press_div && div_in_win_drag_handle(press_div))) {
						suppress_click = 1;
					}
				}

				bud_node *click_bud = NULL;
				lam_ui_div_t *cd = click_div;
				while (cd && !click_bud) {
					click_bud = (bud_node *)lam_ui_get_user_data(cd);
					if (!click_bud) cd = lam_ui_parent(cd);
				}

				if (!suppress_click && click_bud) {
					/* Item rows expect popper Y = (row_bottom - win_top + 4) per bud-cap.js */
					const char *click_payload = payload;
					char ybuf[32];
					lam_ui_div_t *idiv = cd;
					while (idiv) {
						const char *iref = lam_ui_get_attr(idiv, "data-item-ref");
						if (iref && iref[0]) {
							int row_bot = lam_ui_get_y(idiv) + lam_ui_get_height(idiv) - hy;
							int pop_y = (ry >= 0) ? (row_bot - ry + 4) : (ev->y - hy);
							snprintf(ybuf, sizeof(ybuf), "%d", pop_y);
							click_payload = ybuf;
							break;
						}
						idiv = lam_ui_parent(idiv);
					}
					if (pe && pe == he && pe->kind == W_INPUT_CHECKBOX) {
						pe->is_checked = !pe->is_checked;
						bl_dispatch_bud(bl, click_bud, "change",
						                (void *)(pe->is_checked ? "1" : "0"));
					} else {
						/* Clicking a <label> (or child like .switch <i>) toggles its nested checkbox */
						lam_ui_div_t *ldiv = cd;
						while (ldiv) {
							const char *ltag = lam_ui_get_tag(ldiv);
							if (ltag && ci_eq(ltag, "label")) {
								for (lam_ui_div_t *lc = lam_ui_first_child(ldiv);
								     lc; lc = lam_ui_next_sibling(lc)) {
									bl_entry_t *cbe = entry_find_by_div(bl, lc);
									if (cbe && cbe->kind == W_INPUT_CHECKBOX && cbe->bud) {
										cbe->is_checked = !cbe->is_checked;
										bl_dispatch_bud(bl, cbe->bud, "change",
										                (void *)(cbe->is_checked ? "1" : "0"));
										break;
									}
								}
								break;
							}
							ldiv = lam_ui_parent(ldiv);
						}
					}
					bl_dispatch_bud(bl, click_bud, "click", (void *)click_payload);

					uint64_t tnow = now_ms();
					if (tnow - bl->last_click_ms <= 350 &&
					    abs(ev->x - bl->last_click_x) <= 6 &&
					    abs(ev->y - bl->last_click_y) <= 6) {
						if (bl->rt && click_bud)
							bl_dispatch_bud(bl, click_bud, "dblclick", payload);
						bl->last_click_ms = 0;
					} else {
						bl->last_click_ms = tnow;
						bl->last_click_x = ev->x;
						bl->last_click_y = ev->y;
					}
				}
			}

			bl->press_key[0] = '\0';
			bl->press_btn = -1;
			bl->press_dragged = 0;
			update_hover_state(bl, ev->x, ev->y);
		}
		break;
	}

	case LAM_EV_SCROLL: {
		bl->mouse_x = ev->x;
		bl->mouse_y = ev->y;
		bl->has_mouse = 1;
		lam_ui_div_t *hit = bl->ui_root ? lam_ui_hit_test(bl->ui_root, ev->x, ev->y) : NULL;

		/* 1. Check if hit or ancestor listens to "wheel" (e.g. 3D stage zoom) */
		lam_ui_div_t *cur = hit;
		while (cur) {
			bud_node *bn = (bud_node *)lam_ui_get_user_data(cur);
			if (bn && node_has_listener(bn, "wheel") && bl->rt) {
				char wbuf[64];
				int dx = (int)(-ev->dx * 100.0f);
				int dy = (int)(-ev->dy * 100.0f);
				snprintf(wbuf, sizeof(wbuf), "%d,%d,0", dx, dy);
				bl_dispatch_bud(bl, bn, "wheel", wbuf);
				return;
			}
			cur = lam_ui_parent(cur);
		}

		/* 2. Check if scrolling inside #term */
		cur = hit;
		while (cur) {
			bl_entry_t *e = entry_find_by_div(bl, cur);
			if (e && e->kind == W_TERM) {
				term_ansi_scroll(bl->term, (int)(ev->dy * 3.0f));
				return;
			}
			cur = lam_ui_parent(cur);
		}

		/* 3. Scroll nearest scrollable ancestor */
		cur = hit;
		while (cur) {
			const lam_ui_style_t *st = lam_ui_get_style(cur);
			if (st && (st->overflow_y == LAM_UI_OVERFLOW_AUTO ||
			           st->overflow_y == LAM_UI_OVERFLOW_SCROLL)) {
				int32_t sx = 0, sy = 0;
				lam_ui_get_scroll(cur, &sx, &sy);
				sy -= (int32_t)(ev->dy * 32.0f);
				int32_t max_sy = (int32_t)lam_ui_get_content_height(cur) - lam_ui_get_height(cur);
				if (max_sy < 0) max_sy = 0;
				if (sy < 0) sy = 0;
				if (sy > max_sy) sy = max_sy;
				lam_ui_set_scroll(cur, sx, sy);

				bl_entry_t *e = entry_find_by_div(bl, cur);
				if (e) {
					bl_state_t *ps = state_find(bl, e->key, 1);
					if (ps) { ps->scroll_x = sx; ps->scroll_y = sy; }
				}
				return;
			}
			cur = lam_ui_parent(cur);
		}
		break;
	}

	case LAM_EV_CHAR: {
		if (bl->term_focused) {
			term_ansi_input_char(bl->term, ev->code);
			return;
		}
		if (bl->focus_key[0]) {
			bl_entry_t *fe = entry_find_by_key(bl, bl->focus_key);
			if (fe && fe->kind == W_INPUT_TEXT) {
				bl_state_t *st = state_find(bl, fe->key, 1);
				uint32_t cp = ev->code;
				char utf8[4];
				int ulen = 0;
				if (cp >= 0x20 && cp < 0x80) {
					utf8[0] = (char)cp; ulen = 1;
				} else if (cp >= 0x80 && cp < 0x800) {
					utf8[0] = (char)(0xC0 | (cp >> 6));
					utf8[1] = (char)(0x80 | (cp & 0x3F));
					ulen = 2;
				} else if (cp >= 0x800 && cp < 0x10000) {
					utf8[0] = (char)(0xE0 | (cp >> 12));
					utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
					utf8[2] = (char)(0x80 | (cp & 0x3F));
					ulen = 3;
				} else if (cp >= 0x10000 && cp <= 0x10FFFF) {
					utf8[0] = (char)(0xF0 | (cp >> 18));
					utf8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
					utf8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
					utf8[3] = (char)(0x80 | (cp & 0x3F));
					ulen = 4;
				}
				if (st && ulen > 0 && (int)strlen(st->text) + ulen + 1 < BL_INPUT_MAX) {
					int len = (int)strlen(st->text);
					if (st->cursor > len) st->cursor = len;
					memmove(st->text + st->cursor + ulen,
					        st->text + st->cursor,
					        (size_t)(len - st->cursor + 1));
					memcpy(st->text + st->cursor, utf8, (size_t)ulen);
					st->cursor += ulen;
					st->has_text = 1;
					if (bl->rt && fe->bud)
						bl_dispatch_bud(bl, fe->bud, "input", st->text);
				}
			}
		}
		break;
	}

	case LAM_EV_KEY: {
		int down = (ev->action == 1 || ev->action == 2);
		int up   = (ev->action == 0);

		/* Escape blurs #term or focused <input> AND dispatches to window (closes modals) */
		if (down && ev->code == K_ESCAPE) {
			if (bl->term_focused || bl->focus_key[0]) {
				char prev[BL_KEY_MAX];
				snprintf(prev, sizeof(prev), "%s", bl->focus_key);
				bl->focus_key[0] = '\0';
				bl->term_focused = 0;
				set_pseudo_by_key(bl, prev, "", LAM_PSEUDO_FOCUS, 1);
			}
			dispatch_window_event(bl, "keydown@window", (void *)"Escape");
			return;
		}

		/* Terminal input editing when focused */
		if (bl->term_focused) {
			if (down) {
				char cmd[520];
				if (term_ansi_input_key(bl->term, (int)ev->code, ev->mods,
				                        cmd, sizeof(cmd)) && cmd[0]) {
					if (bl->term_input_fn)
						bl->term_input_fn(bl->term_input_ctx, cmd, strlen(cmd));
				}
			}
			return;
		}

		/* Text input editing when focused */
		if (bl->focus_key[0]) {
			bl_entry_t *fe = entry_find_by_key(bl, bl->focus_key);
			if (fe && fe->kind == W_INPUT_TEXT) {
				if (down) {
					bl_state_t *st = state_find(bl, fe->key, 1);
					if (st) {
						int len = (int)strlen(st->text);
						if ((ev->mods & MOD_CTRL) && ev->code == 'V') {
							const char *clip = lam_clipboard_get_text();
							if (clip && clip[0]) {
								int clen = (int)strlen(clip);
								int avail = BL_INPUT_MAX - 1 - len;
								if (clen > avail) clen = avail;
								if (clen > 0) {
									int inserted = 0;
									char tmp[BL_INPUT_MAX];
									for (int ci = 0; ci < clen; ci++) {
										unsigned char ch = (unsigned char)clip[ci];
										if (!fe->is_multiline && (ch == '\r' || ch == '\n'))
											continue;
										if (ch == '\r') continue;
										tmp[inserted++] = (char)ch;
									}
									if (inserted > 0) {
										memmove(st->text + st->cursor + inserted,
										        st->text + st->cursor,
										        (size_t)(len - st->cursor + 1));
										memcpy(st->text + st->cursor, tmp, (size_t)inserted);
										st->cursor += inserted;
										st->has_text = 1;
										if (bl->rt && fe->bud)
											bl_dispatch_bud(bl, fe->bud, "input", st->text);
									}
								}
							}
						} else if ((ev->mods & MOD_CTRL) && ev->code == 'C') {
							if (st->text[0])
								lam_clipboard_set_text(st->text);
						} else if ((ev->mods & MOD_CTRL) && ev->code == 'U') {
							st->text[0] = '\0';
							st->cursor = 0;
							st->has_text = 1;
							if (bl->rt && fe->bud)
								bl_dispatch_bud(bl, fe->bud, "input", st->text);
						} else if (ev->code == K_BACKSPACE && st->cursor > 0) {
							int del = 1;
							while (st->cursor - del > 0 &&
							       (((unsigned char)st->text[st->cursor - del]) & 0xC0) == 0x80)
								del++;
							memmove(st->text + st->cursor - del,
							        st->text + st->cursor,
							        (size_t)(len - st->cursor + 1));
							st->cursor -= del;
							st->has_text = 1;
							if (bl->rt && fe->bud)
								bl_dispatch_bud(bl, fe->bud, "input", st->text);
						} else if (ev->code == K_LEFT && st->cursor > 0) {
							st->cursor--;
						} else if (ev->code == K_RIGHT && st->cursor < len) {
							st->cursor++;
						} else if (ev->code == K_HOME) {
							st->cursor = 0;
						} else if (ev->code == K_END) {
							st->cursor = len;
						} else if (ev->code == K_ENTER || ev->code == K_KP_ENTER) {
							if (fe->is_multiline && (ev->mods & MOD_SHIFT)) {
								if (len + 2 < BL_INPUT_MAX) {
									memmove(st->text + st->cursor + 1,
									        st->text + st->cursor,
									        (size_t)(len - st->cursor + 1));
									st->text[st->cursor++] = '\n';
									st->has_text = 1;
									if (bl->rt && fe->bud)
										bl_dispatch_bud(bl, fe->bud, "input", st->text);
								}
							} else if (bl->rt && fe->bud) {
								bl_dispatch_bud(bl, fe->bud, "change", st->text);
								bl_dispatch_bud(bl, fe->bud, "submit", st->text);
							}
						}
					}
				}
				return;
			}
		}

		/* Global window keyboard dispatch (WASD, arrows, Q/E/R, +/-/0, etc.) */
		char kbuf[4];
		const char *w3c = glfw_key_to_w3c(ev->code, ev->mods, kbuf);
		if (w3c) {
			if (down)
				dispatch_window_event(bl, "keydown@window", (void *)w3c);
			else if (up)
				dispatch_window_event(bl, "keyup@window", (void *)w3c);
		}
		break;
	}

	case LAM_EV_FILE_DROP: {
		if (ev->text && ev->text[0])
			dispatch_window_event(bl, "drop@window", (void *)ev->text);
		break;
	}
	}
}

static void on_lam_event_cb(const lam_event_t *ev, void *user)
{
	bud_lamina_handle_event((bud_lamina_t *)user, ev);
}

void bud_lamina_attach_input(bud_lamina_t *bl)
{
	if (!bl) return;
	lam_event_reg(on_lam_event_cb, bl);
}

int bud_lamina_dispatch_root(bud_lamina_t *bl, const char *event_name, void *user)
{
	if (!bl || !bl->rt || !event_name) return -1;
	bud_node *root = bud_runtime_root(bl->rt);
	if (!root) return -1;
	return bl_dispatch_bud(bl, root, event_name, user);
}

/* ── Introspection ──────────────────────────────────────────────────────── */

lam_ui_div_t *bud_lamina_ui_root(const bud_lamina_t *bl)
{
	return bl ? bl->ui_root : NULL;
}

bud_node *bud_lamina_bud_root(const bud_lamina_t *bl)
{
	return (bl && bl->rt) ? bud_runtime_root(bl->rt) : NULL;
}

lam_ui_div_t *bud_lamina_find_by_id(const bud_lamina_t *bl, const char *id)
{
	if (!bl || !id || !id[0]) return NULL;
	for (size_t i = 0; i < bl->n_entries; i++) {
		const char *did = lam_ui_get_id(bl->entries[i].div);
		if (did && strcmp(did, id) == 0)
			return bl->entries[i].div;
	}
	return NULL;
}

int bud_lamina_is_term_focused(const bud_lamina_t *bl)
{
	return bl ? bl->term_focused : 0;
}
