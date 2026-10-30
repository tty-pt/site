#include "nd_seven_atlas_draw.h"
#include "nd_seven_motion.h"
#include <string.h>

static uint8_t s_atlas[ND_SEVEN_ATLAS_W * ND_SEVEN_ATLAS_H_PX];
static int s_atlas_ready;

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
	s_atlas_ready = 1;
}

const uint8_t *atlas_px(void)
{
	atlas_load();
	return s_atlas;
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

void blit(uint32_t *fb, int w, int h, int id,
          int sx, int sy, int sw, int sh,
          int ax, int ay, int dx, int dy, int scale, const tint_t *tint,
          int flip)
{
	const nd_seven_sprite_rect_t *r = &nd_seven_sprite_rects[id];
	int ox = dx - ax * scale, oy = dy - ay * scale;

	if (ox >= w || oy >= h || ox + sw * scale <= 0 || oy + sh * scale <= 0)
		return;
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
			for (int py = 0; py < scale; py++)
				for (int px = 0; px < scale; px++)
					put_px(fb, w, h, xx0 + px, yy0 + py, c);
		}
	}
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
	fill_px(c->fb, c->w, c->h,
	        c->ox + ux * SCALE, c->oy + uy * SCALE,
	        c->ox + (ux + uw) * SCALE, c->oy + (uy + uh) * SCALE, col);
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

static void draw_fir(const ctx_t *c, int big, int fg)
{
	uint32_t base = shade(s_ansi[fg & 15], 80);
	int tiers = big ? 4 : 3, tierh = big ? 10 : 9, maxhw = big ? 15 : 11;
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
	case 3: case 11: t->r = 230; t->g = 118; t->b = 60; break;
	case 1: case 9:  t->r = 255; t->g = 70;  t->b = 64; break;
	case 7: case 15: t->r = 200; t->g = 150; t->b = 190; break;
	default:         t->r = t->g = t->b = 128; break;
	}
}

void draw_world_obj(uint32_t *fb, int w, int h, const ndw_obj_t *o, int sx, int sy, uint32_t anim)
{
	ctx_t c = { fb, w, h, sx, sy };
	const nd_seven_sprite_rect_t *r;
	tint_t t;
	int id;

	switch (o->kind) {
	case NDW_K_DECID:
		id = o->big ? ((o->variant & 1) ? NDS_TREE_OAK : NDS_TREE_APPLE) : NDS_TREE_ROUND;
		goto tree;
	case NDW_K_TALL:
		id = o->big ? NDS_TREE_BIG : NDS_TREE_APPLE2;
	tree:
		plant_tint(o->fg, &t);
		r = &nd_seven_sprite_rects[id];
		blit_all(fb, w, h, id, r->w / 2, r->h - 1, sx, sy, SCALE, &t);
		break;
	case NDW_K_FIR:      draw_fir(&c, o->big, o->fg); break;
	case NDW_K_CACTUS:   draw_cactus(&c, o->big, o->fg, o->variant); break;
	case NDW_K_POND:     draw_pond(&c, (int)anim); break;
	case NDW_K_SHOP:     draw_shop(&c, o->fg); break;
	case NDW_K_STAIRS_DOWN: draw_stairs(&c, 0); break;
	case NDW_K_STAIRS_UP:   draw_stairs(&c, 1); break;
	case NDW_K_STONE:    draw_pebble(&c, o->fg, o->variant); break;
	case NDW_K_ROCK:
		if ((o->variant & 3) < 2)
			draw_rock(&c, 0, o->fg, o->variant);
		else {
			id = (o->variant & 1) ? NDS_ROCK_GREY : NDS_ROCK_TAN;
			r = &nd_seven_sprite_rects[id];
			blit_all(fb, w, h, id, r->w / 2, r->h - 1, sx, sy, SCALE, NULL);
		}
		break;
	case NDW_K_BOULDER:
		id = (o->variant & 1) ? NDS_ROCK_WHITE_BIG : NDS_ROCK_TAN_BIG;
		r = &nd_seven_sprite_rects[id];
		blit_all(fb, w, h, id, r->w / 2, r->h - 1, sx, sy, SCALE, NULL);
		break;
	default:             draw_pawn(&c, o->glyph, o->fg); break;
	}
}

void draw_lamb(uint32_t *fb, int w, int h, int sx, int sy)
{
	int row, col;
	if (nd_seven_get_walking()) {
		row = nd_seven_get_dir();
		col = (int)((nd_seven_get_anim() / 4) % 6);
	} else {
		row = 4 + nd_seven_get_dir();
		col = (int)((nd_seven_get_anim() / 24) % 4);
	}
	blit(fb, w, h, NDS_LAMB, col * LAMB_SIZE, row * LAMB_SIZE, LAMB_SIZE, LAMB_SIZE,
	     LAMB_AX, LAMB_AY, sx, sy, SCALE, NULL, 0);
}
