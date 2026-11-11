#include "nd_seven_atlas_draw.h"
#include "nd_seven_motion.h"
#include "nd_seven_ground.h"
#include <ttypt/lam.h>
#include <nd/plant.h>
#include <stdio.h>
#include <string.h>

static uint8_t s_atlas[ND_SEVEN_ATLAS_W * ND_SEVEN_ATLAS_H_PX];
static uint32_t s_atlas_rgba[ND_SEVEN_ATLAS_W * ND_SEVEN_RUNTIME_ATLAS_H];
static int s_atlas_ready;
static uint32_t s_atlas_ver = 1;
static uint16_t s_shelf_x = 0;
static uint16_t s_shelf_y = ND_SEVEN_ATLAS_H_PX;
static uint16_t s_shelf_h = 0;

typedef struct {
	uint32_t img_id;
	uint8_t  loaded;
	nd_seven_sprite_rect_t rect;
} flora_dyn_slot_t;

static flora_dyn_slot_t s_flora_slots[ND_PLANT_MAX][2][2];
static uint32_t s_decode_scratch[160 * 160];

void atlas_load(void)
{
	size_t o = 0;
	if (s_atlas_ready)
		return;
	for (size_t i = 0; i + 1 < ND_SEVEN_RLE_LEN && o < sizeof(s_atlas); i += 2) {
		size_t run = (size_t)nd_seven_rle[i] + 1;
		uint8_t v = nd_seven_rle[i + 1];
		while (run-- && o < sizeof(s_atlas))
			s_atlas[o++] = v;
	}
	for (size_t p = 0; p < sizeof(s_atlas); p++) {
		uint8_t idx = s_atlas[p];
		s_atlas_rgba[p] = (idx < ND_SEVEN_PALETTE_N) ? nd_seven_palette[idx] : 0;
	}
	s_atlas_ready = 1;
}

const uint8_t *atlas_px(void)
{
	atlas_load();
	return s_atlas;
}

const uint32_t *atlas_rgba(void)
{
	atlas_load();
	return s_atlas_rgba;
}

uint32_t atlas_rgba_version(void)
{
	atlas_load();
	return s_atlas_ver;
}

int nd_seven_plant_rect(uint8_t plant_id, nd_seven_sprite_rect_t *out_rect)
{
	if (!ND_PLANT_UNPACK_PRESENT(plant_id))
		return 0;
	unsigned sp_idx = ND_PLANT_UNPACK_IDX(plant_id);
	unsigned is_big = ND_PLANT_UNPACK_BIG(plant_id);
	unsigned var_idx = (ND_PLANT_UNPACK_VAR(plant_id) - 1u) & 1u;

	atlas_load();
	flora_dyn_slot_t *slot = &s_flora_slots[sp_idx][is_big][var_idx];
	if (slot->loaded) {
		if (out_rect) *out_rect = slot->rect;
		return 1;
	}
	if (!slot->img_id) {
		char url[96];
		snprintf(url, sizeof(url), "/nd/art/plant/%s/%s/%u.png",
		         s_nd_plant_specs[sp_idx].slug,
		         is_big ? "big" : "small",
		         var_idx + 1u);
		slot->img_id = lam_image_request(url);
	}
	if (slot->img_id) {
		uint32_t iw = 0, ih = 0;
		if (lam_image_read(slot->img_id, s_decode_scratch, sizeof(s_decode_scratch), &iw, &ih) &&
		    iw > 0 && ih > 0 && iw <= 160 && ih <= 160) {
			if ((uint32_t)s_shelf_x + iw > ND_SEVEN_ATLAS_W) {
				s_shelf_x = 0;
				s_shelf_y = (uint16_t)(s_shelf_y + s_shelf_h);
				s_shelf_h = 0;
			}
			if ((uint32_t)s_shelf_y + ih <= ND_SEVEN_RUNTIME_ATLAS_H) {
				for (uint32_t ry = 0; ry < ih; ry++) {
					memcpy(&s_atlas_rgba[(s_shelf_y + ry) * ND_SEVEN_ATLAS_W + s_shelf_x],
					       &s_decode_scratch[ry * iw],
					       iw * sizeof(uint32_t));
				}
				slot->rect.x = s_shelf_x;
				slot->rect.y = s_shelf_y;
				slot->rect.w = (uint16_t)iw;
				slot->rect.h = (uint16_t)ih;
				s_shelf_x = (uint16_t)(s_shelf_x + iw);
				if (ih > s_shelf_h)
					s_shelf_h = (uint16_t)ih;
				slot->loaded = 1;
				s_atlas_ver++;
				if (out_rect) *out_rect = slot->rect;
				return 1;
			}
		}
	}
	return 0;
}

int nd_seven_obj_rect(const ndw_obj_t *o, nd_seven_sprite_rect_t *out_rect)
{
	if (!o || !out_rect)
		return 0;
	if (o->plant_id && nd_seven_plant_rect(o->plant_id, out_rect))
		return 1;
	int id = nd_seven_obj_sprite(o);
	if (id > 0 && id < NDS_COUNT) {
		*out_rect = nd_seven_sprite_rects[id];
		return 1;
	}
	return 0;
}

static const uint32_t s_ansi[16] = {
	0xFF2C2C2C, 0xFF6A4ABD, 0xFF465E3A, 0xFF5A97A3,
	0xFFBD8973, 0xFFAC778A, 0xFF5F5C41, 0xFFA5858B,
	0xFFC1AAB4, 0xFF874ABD, 0xFF8AD353, 0xFF2ECDB8,
	0xFFBD9C73, 0xFFC58995, 0xFFBAD016, 0xFFF5F5F5
};

uint32_t tint_col(uint32_t c, int r, int g, int b)
{
	uint32_t cr = (uint32_t)((int)(c & 0xFF) * r / 128);
	uint32_t cg = (uint32_t)((int)((c >> 8) & 0xFF) * g / 128);
	uint32_t cb = (uint32_t)((int)((c >> 16) & 0xFF) * b / 128);
	if (cr > 255) cr = 255;
	if (cg > 255) cg = 255;
	if (cb > 255) cb = 255;
	return (c & 0xFF000000u) | (cb << 16) | (cg << 8) | cr;
}

uint32_t shade(uint32_t c, int num)
{
	return tint_col(c, num * 128 / 100, num * 128 / 100, num * 128 / 100);
}

void put_px(uint32_t *fb, int w, int h, int x, int y, uint32_t c)
{
	uint32_t a = c >> 24;
	uint32_t *d;
	if (x < 0 || y < 0 || x >= w || y >= h || !a)
		return;
	d = &fb[y * w + x];
	if (a == 255) {
		*d = c;
	} else {
		uint32_t dr = *d & 0xFF, dg = (*d >> 8) & 0xFF, db = (*d >> 16) & 0xFF;
		uint32_t sr = c & 0xFF, sg = (c >> 8) & 0xFF, sb = (c >> 16) & 0xFF;
		dr = (sr * a + dr * (255 - a)) / 255;
		dg = (sg * a + dg * (255 - a)) / 255;
		db = (sb * a + db * (255 - a)) / 255;
		*d = 0xFF000000u | (db << 16) | (dg << 8) | dr;
	}
}

void fill_px(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, uint32_t c)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > w) x1 = w;
	if (y1 > h) y1 = h;
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			put_px(fb, w, h, x, y, c);
}

void blit_sub(uint32_t *fb, int w, int h, int id,
              int sx, int sy, int sw, int sh,
              int ax, int ay, int dx, int dy, int scale, const tint_t *tint,
              int flip, int sub_units)
{
	const nd_seven_sprite_rect_t *r = &nd_seven_sprite_rects[id];
	int ox = dx - ax * scale, oy = dy - ay * scale;

	if (ox >= w || oy >= h || ox + sw * scale <= 0 || oy + sh * scale <= 0)
		return;

	int y_water = (sub_units > 0) ? (ay - sub_units) : 9999;

	for (int y = 0; y < sh; y++) {
		const uint8_t *row = &s_atlas[(r->y + sy + y) * ND_SEVEN_ATLAS_W + r->x + sx];
		int yy0 = oy + y * scale;
		if (yy0 >= h || yy0 + scale <= 0)
			continue;
		for (int x = 0; x < sw; x++) {
			uint8_t srcx = (uint8_t)(flip ? (sw - 1 - x) : x);
			uint32_t c = nd_seven_palette[row[srcx]];
			int xx0 = ox + x * scale;
			if (!(c >> 24) || xx0 >= w || xx0 + scale <= 0)
				continue;
			if (tint) {
				int pr = (int)(c & 0xFF), pg = (int)((c >> 8) & 0xFF), pb = (int)((c >> 16) & 0xFF);
				if (!tint->leaf || (pg > pr + 8 && pg > pb + 8))
					c = tint_col(c, tint->r, tint->g, tint->b);
			}
			if (y >= y_water) {
				int d_row = y - y_water;
				if (d_row == 0) {
					/* Crisp waterline meniscus highlight */
					c = alpha_blend_rgb(c, RGB(240, 250, 255), 180);
				} else {
					uint32_t wcol = water_color_at_depth(d_row, 128, 128, 128);
					int alpha = water_alpha_at_depth(d_row);
					/* Non-linear shallow alpha ramp with immediate Caribbean turquoise baseline */
					int shallow_alpha = 48 + ((d_row < 6) ? (alpha * d_row / 12) : (alpha * 2 / 5));
					if (shallow_alpha > 255) shallow_alpha = 255;
					c = alpha_blend_rgb(c, wcol, shallow_alpha);
				}
			}
			for (int py = 0; py < scale; py++)
				for (int px = 0; px < scale; px++)
					put_px(fb, w, h, xx0 + px, yy0 + py, c);
		}
	}
}

void blit(uint32_t *fb, int w, int h, int id,
          int sx, int sy, int sw, int sh,
          int ax, int ay, int dx, int dy, int scale, const tint_t *tint,
          int flip)
{
	blit_sub(fb, w, h, id, sx, sy, sw, sh, ax, ay, dx, dy, scale, tint, flip, 0);
}

void blit_all(uint32_t *fb, int w, int h, int id, int ax, int ay, int dx, int dy, int scale,
              const tint_t *tint)
{
	const nd_seven_sprite_rect_t *r = &nd_seven_sprite_rects[id];
	blit(fb, w, h, id, 0, 0, r->w, r->h, ax, ay, dx, dy, scale, tint, 0);
}

void blit_all_f(uint32_t *fb, int w, int h, int id, int ax, int ay, int dx, int dy,
                int scale, const tint_t *tint, int flip)
{
	const nd_seven_sprite_rect_t *r = &nd_seven_sprite_rects[id];
	blit(fb, w, h, id, 0, 0, r->w, r->h, ax, ay, dx, dy, scale, tint, flip);
}

void urect(const ctx_t *c, int ux, int uy, int uw, int uh, uint32_t col)
{
	int sc = (c && c->scale > 0) ? c->scale : SCALE;
	fill_px(c->fb, c->w, c->h,
	        c->ox + ux * sc, c->oy + uy * sc,
	        c->ox + (ux + uw) * sc, c->oy + (uy + uh) * sc, col);
}

void uellipse(const ctx_t *c, int ux, int uy, int rx, int ry, uint32_t col)
{
	for (int dy = -ry; dy <= ry; dy++) {
		int hw = rx;
		while (hw > 0 && hw * hw * ry * ry + dy * dy * rx * rx > rx * rx * ry * ry)
			hw--;
		urect(c, ux - hw, uy + dy, 2 * hw + 1, 1, col);
	}
}

void shadow(const ctx_t *c, int rx)
{
	uellipse(c, 0, -1, rx, 3, 0x55000000u);
}

const uint8_t *font_rows(int ch)
{
	static const uint8_t f[128][5] = {
		['0'] = {7,5,5,5,7}, ['1'] = {2,6,2,2,7}, ['2'] = {7,1,7,4,7}, ['3'] = {7,1,7,1,7},
		['4'] = {5,5,7,1,1}, ['5'] = {7,4,7,1,7}, ['6'] = {7,4,7,5,7}, ['7'] = {7,1,1,1,1},
		['8'] = {7,5,7,5,7}, ['9'] = {7,5,7,1,7},
		['A'] = {2,5,7,5,5}, ['B'] = {6,5,6,5,6}, ['C'] = {3,4,4,4,3}, ['D'] = {6,5,5,5,6},
		['E'] = {7,4,6,4,7}, ['F'] = {7,4,6,4,4}, ['G'] = {3,4,5,5,3}, ['H'] = {5,5,7,5,5},
		['I'] = {7,2,2,2,7}, ['J'] = {1,1,1,5,2}, ['K'] = {5,5,6,5,5}, ['L'] = {4,4,4,4,7},
		['M'] = {5,7,7,5,5}, ['N'] = {6,5,5,5,5}, ['O'] = {2,5,5,5,2}, ['P'] = {6,5,6,4,4},
		['Q'] = {2,5,5,6,3}, ['R'] = {6,5,6,5,5}, ['S'] = {3,4,2,1,6}, ['T'] = {7,2,2,2,2},
		['U'] = {5,5,5,5,7}, ['V'] = {5,5,5,5,2}, ['W'] = {5,5,7,7,5}, ['X'] = {5,5,2,5,5},
		['Y'] = {5,5,2,2,2}, ['Z'] = {7,1,2,4,7},
		['!'] = {2,2,2,0,2}, ['$'] = {3,6,2,3,6}, ['~'] = {0,3,6,0,0}, ['*'] = {5,2,7,2,5},
		['>'] = {4,2,1,2,4}, ['<'] = {1,2,4,2,1}, ['.'] = {0,0,0,0,2}, ['#'] = {5,7,5,7,5},
		['+'] = {0,2,7,2,0}, ['@'] = {7,5,7,4,7}, ['?'] = {7,1,2,0,2},
	};
	if (ch >= 'a' && ch <= 'z')
		ch -= 32;
	if (ch <= 0 || ch >= 128)
		return NULL;
	for (int i = 0; i < 5; i++)
		if (f[ch][i])
			return f[ch];
	return NULL;
}

void draw_glyph(const ctx_t *c, int ux, int uy, int ch, uint32_t col)
{
	const uint8_t *rows = font_rows(ch);
	if (!rows)
		return;
	for (int r = 0; r < 5; r++)
		for (int k = 0; k < 3; k++)
			if (rows[r] & (4 >> k))
				urect(c, ux + k, uy + r, 1, 1, col);
}

void draw_pawn(const ctx_t *c, int glyph, int fg)
{
	uint32_t col = s_ansi[fg & 15];
	uint32_t dark = shade(col, 50), light = shade(col, 135);
	shadow(c, 7);
	urect(c, -5, -11, 10, 11, dark);
	urect(c, -4, -10, 8, 9, col);
	urect(c, -4, -10, 2, 9, light);
	urect(c, -3, -17, 6, 6, RGB(40, 30, 30));
	urect(c, -2, -16, 4, 4, RGB(232, 196, 160));
	urect(c, -1, -15, 1, 1, RGB(30, 30, 40));
	urect(c, 1, -15, 1, 1, RGB(30, 30, 40));
	if (font_rows(glyph)) {
		urect(c, -3, -25, 7, 7, 0xAA101018u);
		draw_glyph(c, -1, -24, glyph, col);
	}
}

static void draw_pebble(const ctx_t *c, int fg, int variant)
{
	uint32_t col = (fg == 7 || fg == 15) ? RGB(180, 185, 195) :
	               (fg == 4 || fg == 12) ? RGB(190, 160, 130) : RGB(140, 145, 155);
	uint32_t dark = shade(col, 50), light = shade(col, 135);
	int rx = (variant & 1) ? 3 : 4;
	shadow(c, rx + 1);
	uellipse(c, 0, -2, rx, 2, dark);
	uellipse(c, 0, -3, rx - 1, 1, col);
	urect(c, -1, -3, 2, 1, light);
}

static void draw_rock(const ctx_t *c, int big, int fg, int variant)
{
	uint32_t col = (fg == 7 || fg == 15) ? RGB(200, 205, 215) :
	               (fg == 4 || fg == 12) ? RGB(205, 175, 140) : RGB(155, 160, 170);
	uint32_t dark = shade(col, 55), light = shade(col, 135);
	int rw = big ? 14 : 9, rh = big ? 10 : 7;
	shadow(c, rw / 2 + 3);
	urect(c, -rw / 2, -rh, rw, rh, dark);
	urect(c, -rw / 2 + 1, -rh + 1, rw - 2, rh - 2, col);
	urect(c, -rw / 2 + 1, -rh + 1, rw / 3, rh - 2, light);
	urect(c, 0, -rh + 1, 1, rh - 2, dark);
	if (variant & 1)
		urect(c, -rw / 4, -rh / 2, 2, 1, light);
	/* Moss / lichen patch on upper rock surface */
	if (variant & 8)
		urect(c, -rw / 3, -rh + 1, rw / 2, 1, RGB(72, 128, 54));
	/* Geological strata line */
	if (variant & 16)
		urect(c, -rw / 2 + 1, -rh / 2, rw - 3, 1, shade(dark, 85));
}

static void draw_cactus(const ctx_t *c, int big, int fg, int variant)
{
	uint32_t body = shade(s_ansi[fg & 15], 85);
	uint32_t dark = shade(body, 52), light = shade(body, 130);
	int bw = big ? 8 : 6, bh = big ? 36 : 25, aw = big ? 5 : 4, ah = big ? 10 : 8;
	int a1 = bh * 55 / 100, a2 = bh * 38 / 100;
	int flip = variant & 1;
	int lx = -bw / 2 - aw, rx = bw / 2 + (bw & 1);
	int lh = a1, rh = a2;

	if (flip) { lh = a2; rh = a1; }
	shadow(c, bw + 4);

	for (int pass = 0; pass < 2; pass++) {
		int g = pass == 0 ? 1 : 0;
		uint32_t col = pass == 0 ? dark : body;
		urect(c, -bw / 2 - g, -bh - g, bw + 2 * g, bh + g, col);
		urect(c, lx - g, -lh - g, aw + bw / 2 + 2 * g, 3 + 2 * g, col);
		urect(c, lx - g, -lh - ah - g, 3 + 2 * g, ah + 3 + 2 * g, col);
		urect(c, -bw / 2 - g + bw, -rh - g, aw + 2 * g, 3 + 2 * g, col);
		urect(c, rx + aw - 3 - g, -rh - ah - g, 3 + 2 * g, ah + 3 + 2 * g, col);
	}
	urect(c, -bw / 2, -bh, 2, bh, light);
	urect(c, lx, -lh - ah, 1, ah + 3, light);
	urect(c, rx + aw - 3, -rh - ah, 1, ah + 3, light);
	urect(c, 0, -bh + 2, 1, bh - 3, dark);
	for (int y = 4; y < bh - 2; y += 5) {
		urect(c, -bw / 2 - 1, -y, 1, 1, RGB(236, 226, 160));
		urect(c, bw / 2 + (bw & 1), -y - 2, 1, 1, RGB(236, 226, 160));
	}
	urect(c, -1, -bh - 1, 2, 1, RGB(230, 120, 150));
}

static void draw_fir(const ctx_t *c, int big, int fg, int variant)
{
	uint32_t base = shade(s_ansi[fg & 15], 80);
	int extra = (variant & 1);
	int tiers = (big ? 4 : 3) + extra, tierh = big ? 10 : 9, maxhw = big ? 15 : 11;
	int top = -(tiers * (tierh - 3) + 9);

	shadow(c, maxhw);
	urect(c, -2, -7, 4, 7, RGB(60, 40, 24));
	urect(c, -1, -7, 2, 7, RGB(110, 76, 44));
	for (int t = 0; t < tiers; t++) {
		int y0 = top + t * (tierh - 3);
		int span = (maxhw - 2) / tiers;
		for (int r = 0; r < tierh; r++) {
			int hw = 2 + t * span + (r + 1) * span / tierh + (r > 0 ? 1 : 0);
			uint32_t col = shade(base, 128 - t * 8);
			urect(c, -hw - 1, y0 + r, 2 * hw + 2, 1, shade(base, 48));
			urect(c, -hw, y0 + r, hw, 1, shade(col, 118));
			urect(c, 0, y0 + r, hw, 1, shade(col, 84));
		}
		urect(c, -(2 + t * span + span + 1), y0 + tierh - 1, 2 * (2 + t * span + span + 1), 1, shade(base, 52));
	}
}

static void draw_pond(const ctx_t *c, int anim)
{
	uellipse(c, 0, -7, 24, 12, RGB(86, 98, 80));
	uellipse(c, 0, -7, 22, 10, RGB(44, 96, 170));
	uellipse(c, 0, -7, 15, 6, RGB(60, 120, 196));
	for (int i = 0; i < 4; i++) {
		int ux = -12 + i * 7 + ((anim / 14 + i) & 3);
		urect(c, ux, -11 + (i & 1) * 6, 4, 1, RGB(170, 210, 240));
	}
}

static void draw_shop(const ctx_t *c, int fg)
{
	uint32_t roof = shade(s_ansi[fg & 15] ? s_ansi[fg & 15] : RGB(170, 60, 50), 100);
	shadow(c, 20);
	urect(c, -17, -23, 34, 23, RGB(70, 50, 36));
	urect(c, -16, -22, 32, 22, RGB(206, 176, 126));
	urect(c, -16, -22, 32, 2, RGB(226, 198, 150));
	for (int k = 0; k < 9; k++) {
		int hw = 11 + k * 2;
		urect(c, -hw - 1, -32 + k, 2 * hw + 2, 1, shade(roof, 55));
		urect(c, -hw, -32 + k, 2 * hw, 1, shade(roof, 90 + (k & 1) * 14));
	}
	urect(c, -4, -14, 8, 14, RGB(86, 52, 30));
	urect(c, -3, -13, 6, 13, RGB(120, 76, 42));
	urect(c, 8, -16, 6, 6, RGB(150, 200, 230));
	urect(c, -14, -16, 6, 6, RGB(150, 200, 230));
	draw_glyph(c, -1, -30, '$', RGB(250, 230, 120));
}

static void draw_stairs(const ctx_t *c, int up)
{
	if (up) {
		for (int k = 0; k < 4; k++) {
			urect(c, -12 + k * 2, -3 * (k + 1), 24 - k * 4, 3, RGB(110, 110, 124));
			urect(c, -12 + k * 2, -3 * (k + 1), 24 - k * 4, 1, RGB(170, 170, 186));
		}
	} else {
		urect(c, -13, -11, 26, 11, RGB(20, 18, 24));
		for (int k = 0; k < 4; k++)
			urect(c, -11 + k * 2, -9 + k * 2, 22 - k * 4, 1, RGB(90, 90, 104));
	}
}

void plant_tint(int fg, tint_t *t)
{
	t->leaf = 1;
	switch (fg & 15) {
	case 3: case 11: t->r = 230; t->g = 118; t->b = 60; break; /* autumn gold / yellow */
	case 1: case 9:  t->r = 255; t->g = 70;  t->b = 64; break; /* flowering red / apple */
	case 7: case 15: t->r = 200; t->g = 150; t->b = 190; break; /* birch / blossom */
	case 2: case 10: t->r = 115; t->g = 195; t->b = 95; break; /* lush emerald / spring conifer */
	case 6: case 14: t->r = 85;  t->g = 180; t->b = 160; break; /* alpine spruce / boreal teal */
	case 4: case 12: t->r = 160; t->g = 145; t->b = 90; break; /* savanna acacia / olive */
	default:         t->r = t->g = t->b = 128; break;
	}
}

int nd_seven_obj_sprite(const ndw_obj_t *o)
{
	if (!o) return 0;
	switch (o->kind) {
	case NDW_K_DECID:
		if (o->big) {
			int pick = o->variant & 3;
			if (pick == 0) return NDS_TREE_OAK;
			if (pick == 1) return NDS_TREE_APPLE;
			if (pick == 2) return NDS_TREE_BIG;
			return NDS_TREE_APPLE2;
		}
		return (o->variant & 1) ? NDS_TREE_ROUND : NDS_BUSH_DARK;

	case NDW_K_TALL:
		if (o->big) {
			return (o->variant & 1) ? NDS_TREE_BIG : NDS_TREE_OAK;
		}
		return (o->variant & 1) ? NDS_TREE_APPLE2 : NDS_TREE_ROUND;

	case NDW_K_FIR:
		return o->big ? NDS_TREE_BIG : ((o->variant & 1) ? NDS_BUSH_TEAL : NDS_BUSH_DARK);

	case NDW_K_CACTUS:
		return (o->variant & 1) ? NDS_BUSH_DARK : NDS_BUSH_TEAL;

	case NDW_K_STONE: {
		static const int stones[4] = { NDS_STONE_FLAT, NDS_STONE_TINY, NDS_STONE_GREY, NDS_STONE_BLUE_SM };
		return stones[o->variant & 3];
	}

	case NDW_K_ROCK: {
		static const int rocks[6] = {
			NDS_ROCK_GREY, NDS_ROCK_TAN, NDS_ROCK_WHITE,
			NDS_ROCK_TAN_SM, NDS_ROCK_TAN_TINY, NDS_STONE_BLUE
		};
		return rocks[(o->variant >> 1) % 6];
	}

	case NDW_K_BOULDER:
		return (o->variant & 1) ? NDS_ROCK_WHITE_BIG : NDS_ROCK_TAN_BIG;

	default:
		return 0;
	}
}

static void draw_drop_shadow(uint32_t *fb, int w, int h, int sx, int sy, int rx, int ry)
{
	int x0 = sx - rx, x1 = sx + rx;
	int y0 = sy - ry, y1 = sy + ry;
	if (x0 < 0)
		x0 = 0;
	if (x1 >= w)
		x1 = w - 1;
	if (y0 < 0)
		y0 = 0;
	if (y1 >= h)
		y1 = h - 1;

	for (int y = y0; y <= y1; y++) {
		int dy = y - sy;
		for (int x = x0; x <= x1; x++) {
			int dx = x - sx;
			if ((dx * dx * ry * ry + dy * dy * rx * rx) <= (rx * rx * ry * ry)) {
				uint32_t col = fb[y * w + x];
				uint32_t r = (col & 0xFF) * 68 / 100;
				uint32_t g = ((col >> 8) & 0xFF) * 68 / 100;
				uint32_t b = ((col >> 16) & 0xFF) * 68 / 100;
				fb[y * w + x] = (col & 0xFF000000) | (b << 16) | (g << 8) | r;
			}
		}
	}
}

static void blit_rgba_rect(uint32_t *fb, int w, int h, const nd_seven_sprite_rect_t *r,
                           int ax, int ay, int dx, int dy, int scale, int flip, int sub_units)
{
	int sw = r->w, sh = r->h;
	int ox = dx - ax * scale, oy = dy - ay * scale;
	if (ox >= w || oy >= h || ox + sw * scale <= 0 || oy + sh * scale <= 0)
		return;
	int y_water = (sub_units > 0) ? (ay - sub_units) : 9999;

	for (int y = 0; y < sh; y++) {
		const uint32_t *row = &s_atlas_rgba[(r->y + y) * ND_SEVEN_ATLAS_W + r->x];
		int yy0 = oy + y * scale;
		if (yy0 >= h || yy0 + scale <= 0)
			continue;
		for (int x = 0; x < sw; x++) {
			int srcx = flip ? (sw - 1 - x) : x;
			uint32_t c = row[srcx];
			int xx0 = ox + x * scale;
			if (!(c >> 24) || xx0 >= w || xx0 + scale <= 0)
				continue;
			if (y >= y_water) {
				int d_row = y - y_water;
				if (d_row == 0) {
					c = alpha_blend_rgb(c, RGB(240, 250, 255), 180);
				} else {
					uint32_t wcol = water_color_at_depth(d_row, 128, 128, 128);
					int alpha = water_alpha_at_depth(d_row);
					int shallow_alpha = 48 + ((d_row < 6) ? (alpha * d_row / 12) : (alpha * 2 / 5));
					if (shallow_alpha > 255) shallow_alpha = 255;
					c = alpha_blend_rgb(c, wcol, shallow_alpha);
				}
			}
			for (int py = 0; py < scale; py++)
				for (int px = 0; px < scale; px++)
					put_px(fb, w, h, xx0 + px, yy0 + py, c);
		}
	}
}

void draw_world_obj_scaled_sub(uint32_t *fb, int w, int h, const ndw_obj_t *o, int sx, int sy, uint32_t anim, int scale, int sub_units)
{
	if (scale < 1) scale = 1;
	ctx_t c = { fb, w, h, sx, sy, scale };
	const nd_seven_sprite_rect_t *r;
	nd_seven_sprite_rect_t dyn_r;
	tint_t t;
	int id;
	int flip = (o->variant >> 2) & 1;

	if (o->plant_id &&
	    (o->kind == NDW_K_DECID || o->kind == NDW_K_TALL ||
	     o->kind == NDW_K_FIR || o->kind == NDW_K_CACTUS) &&
	    nd_seven_plant_rect(o->plant_id, &dyn_r)) {
		draw_drop_shadow(fb, w, h, sx, sy, (o->big ? 12 : 8) * scale, (o->big ? 4 : 3) * scale);
		blit_rgba_rect(fb, w, h, &dyn_r, dyn_r.w / 2, dyn_r.h - 1, sx, sy, scale, flip, sub_units);
		return;
	}

	switch (o->kind) {
	case NDW_K_DECID:
	case NDW_K_TALL:
		id = nd_seven_obj_sprite(o);
		draw_drop_shadow(fb, w, h, sx, sy, (o->big ? 14 : 10) * scale, (o->big ? 5 : 3) * scale);
		plant_tint(o->fg, &t);
		r = &nd_seven_sprite_rects[id];
		blit_sub(fb, w, h, id, 0, 0, r->w, r->h, r->w / 2, r->h - 1, sx, sy, scale, &t, flip, sub_units);
		break;
	case NDW_K_FIR:
		draw_drop_shadow(fb, w, h, sx, sy, (o->big ? 10 : 7) * scale, 3 * scale);
		draw_fir(&c, o->big, o->fg, o->variant);
		break;
	case NDW_K_CACTUS:
		draw_drop_shadow(fb, w, h, sx, sy, (o->big ? 9 : 6) * scale, 3 * scale);
		draw_cactus(&c, o->big, o->fg, o->variant);
		break;
	case NDW_K_POND:     draw_pond(&c, (int)anim); break;
	case NDW_K_SHOP:     draw_shop(&c, o->fg); break;
	case NDW_K_STAIRS_DOWN: draw_stairs(&c, 0); break;
	case NDW_K_STAIRS_UP:   draw_stairs(&c, 1); break;
	case NDW_K_STONE:
		if ((o->variant & 7) < 3)
			draw_pebble(&c, o->fg, o->variant);
		else {
			id = nd_seven_obj_sprite(o);
			r = &nd_seven_sprite_rects[id];
			draw_drop_shadow(fb, w, h, sx, sy, 4 * scale, 2 * scale);
			blit_sub(fb, w, h, id, 0, 0, r->w, r->h, r->w / 2, r->h - 1, sx, sy, scale, NULL, flip, sub_units);
		}
		break;
	case NDW_K_ROCK:
		draw_drop_shadow(fb, w, h, sx, sy, 7 * scale, 3 * scale);
		if ((o->variant & 7) == 0)
			draw_rock(&c, 0, o->fg, o->variant);
		else {
			id = nd_seven_obj_sprite(o);
			r = &nd_seven_sprite_rects[id];
			blit_sub(fb, w, h, id, 0, 0, r->w, r->h, r->w / 2, r->h - 1, sx, sy, scale, NULL, flip, sub_units);
		}
		break;
	case NDW_K_BOULDER:
		draw_drop_shadow(fb, w, h, sx, sy, 11 * scale, 4 * scale);
		id = nd_seven_obj_sprite(o);
		r = &nd_seven_sprite_rects[id];
		blit_sub(fb, w, h, id, 0, 0, r->w, r->h, r->w / 2, r->h - 1, sx, sy, scale, NULL, flip, sub_units);
		break;
	default:             draw_pawn(&c, o->glyph, o->fg); break;
	}
}

void draw_world_obj_scaled(uint32_t *fb, int w, int h, const ndw_obj_t *o, int sx, int sy, uint32_t anim, int scale)
{
	draw_world_obj_scaled_sub(fb, w, h, o, sx, sy, anim, scale, 0);
}

void draw_world_obj(uint32_t *fb, int w, int h, const ndw_obj_t *o, int sx, int sy, uint32_t anim)
{
	draw_world_obj_scaled(fb, w, h, o, sx, sy, anim, SCALE);
}

void draw_lamb_scaled_sub(uint32_t *fb, int w, int h, int sx, int sy, int scale, int sub_units)
{
	if (scale < 1) scale = 1;
	int row, col;
	if (nd_seven_get_walking()) {
		row = nd_seven_get_dir();
		col = (int)((nd_seven_get_anim() / 4) % 6);
	} else {
		row = 4 + nd_seven_get_dir();
		col = (int)((nd_seven_get_anim() / 24) % 4);
	}
	blit_sub(fb, w, h, NDS_LAMB, col * LAMB_SIZE, row * LAMB_SIZE, LAMB_SIZE, LAMB_SIZE,
	         LAMB_AX, LAMB_AY, sx, sy, scale, NULL, 0, sub_units);
}

void draw_lamb_scaled(uint32_t *fb, int w, int h, int sx, int sy, int scale)
{
	draw_lamb_scaled_sub(fb, w, h, sx, sy, scale, 0);
}

void draw_lamb(uint32_t *fb, int w, int h, int sx, int sy)
{
	draw_lamb_scaled(fb, w, h, sx, sy, SCALE);
}
