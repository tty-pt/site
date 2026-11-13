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
static uint16_t s_shelf_y = ND_SEVEN_ATLAS_H_PX + 24;
static uint16_t s_shelf_h = 0;

static const nd_seven_sprite_rect_t s_botanical_rects[NDB_COUNT] = {
	[NDB_GRASS_TUFT_1] = { 0,   ND_SEVEN_ATLAS_H_PX, 20, 18 },
	[NDB_GRASS_TUFT_2] = { 20,  ND_SEVEN_ATLAS_H_PX, 20, 18 },
	[NDB_GRASS_TUFT_3] = { 40,  ND_SEVEN_ATLAS_H_PX, 20, 18 },
	[NDB_TALL_GRASS_1] = { 60,  ND_SEVEN_ATLAS_H_PX, 18, 22 },
	[NDB_TALL_GRASS_2] = { 78,  ND_SEVEN_ATLAS_H_PX, 18, 22 },
	[NDB_REED_1]       = { 96,  ND_SEVEN_ATLAS_H_PX, 16, 24 },
	[NDB_REED_2]       = { 112, ND_SEVEN_ATLAS_H_PX, 16, 24 }
};

static inline void atlas_put_px_alpha(uint16_t x, uint16_t y, uint32_t rgb, int alpha)
{
	if (alpha <= 0 || x >= ND_SEVEN_ATLAS_W || y >= ND_SEVEN_RUNTIME_ATLAS_H)
		return;
	if (alpha > 255)
		alpha = 255;
	uint32_t *dst = &s_atlas_rgba[(size_t)y * ND_SEVEN_ATLAS_W + x];
	uint32_t old = *dst;
	int old_a = (int)((old >> 24) & 0xFFu);
	if (old_a == 0 || alpha >= 252) {
		*dst = (rgb & 0x00FFFFFFu) | ((uint32_t)alpha << 24);
		return;
	}
	int out_a = alpha + ((old_a * (255 - alpha)) >> 8);
	if (out_a > 255)
		out_a = 255;
	int nr = (int)(rgb & 0xFFu);
	int ng = (int)((rgb >> 8) & 0xFFu);
	int nb = (int)((rgb >> 16) & 0xFFu);
	int or_c = (int)(old & 0xFFu);
	int og_c = (int)((old >> 8) & 0xFFu);
	int ob_c = (int)((old >> 16) & 0xFFu);
	int r = ((nr * alpha) + (or_c * (255 - alpha))) >> 8;
	int g = ((ng * alpha) + (og_c * (255 - alpha))) >> 8;
	int b = ((nb * alpha) + (ob_c * (255 - alpha))) >> 8;
	*dst = ((uint32_t)out_a << 24) | ((uint32_t)(b & 0xFF) << 16) |
	       ((uint32_t)(g & 0xFF) << 8) | (uint32_t)(r & 0xFF);
}

static inline uint32_t lerp_rgb(uint32_t c0, uint32_t c1, int t_q8)
{
	if (t_q8 <= 0) return c0;
	if (t_q8 >= 256) return c1;
	int r0 = (int)(c0 & 0xFFu), g0 = (int)((c0 >> 8) & 0xFFu), b0 = (int)((c0 >> 16) & 0xFFu);
	int r1 = (int)(c1 & 0xFFu), g1 = (int)((c1 >> 8) & 0xFFu), b1 = (int)((c1 >> 16) & 0xFFu);
	int r = r0 + (((r1 - r0) * t_q8) >> 8);
	int g = g0 + (((g1 - g0) * t_q8) >> 8);
	int b = b0 + (((b1 - b0) * t_q8) >> 8);
	return 0xFF000000u | ((uint32_t)(b & 0xFF) << 16) |
	       ((uint32_t)(g & 0xFF) << 8) | (uint32_t)(r & 0xFF);
}

static void draw_tapered_blade(uint16_t bx, uint16_t by, uint16_t rect_w, uint16_t rect_h,
                               int x0, int dy_start, int bh, int lean,
                               uint32_t c_root, uint32_t c_deep, uint32_t c_mid,
                               uint32_t c_lush, uint32_t c_tip, int is_plume)
{
	int span = (bh - dy_start > 1) ? (bh - dy_start - 1) : 1;
	for (int dy = dy_start; dy < bh; dy++) {
		int py = (int)(rect_h - 1) - dy;
		if (py < 0 || py >= (int)rect_h)
			continue;
		int rel = dy - dy_start;
		int t_q8 = (rel * 256) / span; /* 0..256 from root to tip */

		/* Quadratic curved trajectory in 1/256th pixel fixed point */
		int curve_q8 = (t_q8 * ((t_q8 * 180 + 76 * 256) >> 8)) >> 8;
		int fx_q8 = (x0 << 8) + ((lean * curve_q8));
		int ix = fx_q8 >> 8;
		int frac = fx_q8 & 0xFF;

		/* Smooth color gradient from terrain-matched root to sunlit tip */
		uint32_t col;
		if (t_q8 < 64) {
			col = lerp_rgb(c_root, c_deep, t_q8 * 4);
		} else if (t_q8 < 144) {
			col = lerp_rgb(c_deep, c_mid, ((t_q8 - 64) * 256) / 80);
		} else if (t_q8 < 216) {
			col = lerp_rgb(c_mid, c_lush, ((t_q8 - 144) * 256) / 72);
		} else {
			col = lerp_rgb(c_lush, c_tip, ((t_q8 - 216) * 256) / 40);
		}

		/* Vertical alpha envelope: feathered roots (no dark bottom line) & pointed tip (no square top) */
		int root_alpha = (dy == 0) ? 95 : (dy == 1 ? 165 : (dy == 2 ? 225 : 255));
		if (rel == 0 && dy > 0)
			root_alpha = (root_alpha * 165) >> 8;
		int tip_rem = bh - 1 - dy;
		int tip_alpha = (tip_rem == 0) ? 105 : (tip_rem == 1 ? 185 : (tip_rem == 2 ? 235 : 255));
		int base_alpha = (root_alpha * tip_alpha) >> 8;

		/* Taper width from ~1.55px at base down to 0.75px at tip with Wu sub-pixel edge anti-aliasing */
		int width_q8 = 396 - ((t_q8 * 210) >> 8); /* 396 (~1.55px) -> 186 (~0.73px) */
		if (is_plume && tip_rem >= 1 && tip_rem <= 3)
			width_q8 += 72;

		int a_main = (base_alpha * (256 - ((frac * (256 - (width_q8 >> 1))) >> 8))) >> 8;
		int a_right = (base_alpha * ((frac * width_q8) >> 8)) >> 8;
		int a_left = (width_q8 > 280 && frac < 96) ? ((base_alpha * (96 - frac)) >> 8) : 0;

		if (ix >= 1 && ix < (int)rect_w - 1)
			atlas_put_px_alpha((uint16_t)(bx + ix), (uint16_t)(by + py), col, a_main);
		if (a_right > 16 && ix + 1 >= 1 && ix + 1 < (int)rect_w - 1)
			atlas_put_px_alpha((uint16_t)(bx + ix + 1), (uint16_t)(by + py), col, a_right);
		if (a_left > 16 && ix - 1 >= 1 && ix - 1 < (int)rect_w - 1)
			atlas_put_px_alpha((uint16_t)(bx + ix - 1), (uint16_t)(by + py), col, a_left);
	}
}

static void synthesize_botanical_shelf(void)
{
	/* Terrain-harmonized palette matching s_fs_terrain deepMoss / lushTurf / sunlit tips */
	const uint32_t c_root  = RGB(74, 124, 58);
	const uint32_t c_deep  = RGB(82, 138, 62);
	const uint32_t c_mid   = RGB(94, 156, 68);
	const uint32_t c_lush  = RGB(112, 178, 76);
	const uint32_t c_tip   = RGB(148, 206, 90);
	const uint32_t c_plume = RGB(182, 214, 102);
	const uint32_t c_stalk = RGB(84, 136, 64);
	const uint32_t c_head1 = RGB(122, 80, 44);
	const uint32_t c_head2 = RGB(92, 58, 30);

	/* Clear reserved botanical shelf row (y: ND_SEVEN_ATLAS_H_PX .. +23) */
	memset(&s_atlas_rgba[(size_t)ND_SEVEN_ATLAS_H_PX * ND_SEVEN_ATLAS_W], 0,
	       24u * ND_SEVEN_ATLAS_W * sizeof(uint32_t));

	/* Synthesize 3 fine, tapered multi-blade meadow turf swatches (20x18 each, 11 overlapping curved blades) */
	for (int v = 0; v < 3; v++) {
		uint16_t bx = s_botanical_rects[NDB_GRASS_TUFT_1 + v].x;
		uint16_t by = s_botanical_rects[NDB_GRASS_TUFT_1 + v].y;
		static const int8_t blade_x[3][11] = {
			{ 3, 4, 6, 7, 8, 10, 11, 12, 14, 15, 16 },
			{ 3, 5, 6, 8, 9, 10, 12, 13, 14, 15, 16 },
			{ 2, 4, 6, 7, 9, 10, 11, 13, 14, 15, 17 }
		};
		static const int8_t blade_y0[3][11] = {
			{ 2, 1, 0, 1, 0, 0, 1, 0, 1, 2, 2 },
			{ 2, 0, 1, 0, 1, 0, 0, 1, 0, 1, 2 },
			{ 2, 1, 0, 0, 1, 0, 1, 0, 1, 2, 2 }
		};
		static const int8_t blade_h[3][11] = {
			{ 9, 12, 14, 11, 16, 13, 17, 12, 15, 11, 9 },
			{ 10, 13, 11, 15, 13, 17, 14, 16, 12, 13, 8 },
			{ 8, 11, 15, 13, 16, 12, 17, 14, 13, 10, 9 }
		};
		static const int8_t blade_lean[3][11] = {
			{ -3, -2, -2, -1, -1, 0, 1, 1, 2, 2, 3 },
			{ -2, -2, -1,  1, -1, 0, 1, 2, 1, 2, 3 },
			{ -3, -1, -2,  0,  1, -1, 1, 2, 1, 3, 2 }
		};
		for (int b = 0; b < 11; b++) {
			draw_tapered_blade(bx, by, 20, 18,
			                   blade_x[v][b], blade_y0[v][b], blade_h[v][b], blade_lean[v][b],
			                   c_root, c_deep, c_mid, c_lush, c_tip, 0);
		}
	}

	/* Synthesize 2 tall meadow grass clusters with arching tapered seed plumes (18x22 each, 9 blades) */
	for (int v = 0; v < 2; v++) {
		uint16_t bx = s_botanical_rects[NDB_TALL_GRASS_1 + v].x;
		uint16_t by = s_botanical_rects[NDB_TALL_GRASS_1 + v].y;
		static const int8_t tall_x[2][9] = {
			{ 3, 5, 6, 8, 9, 10, 12, 13, 14 },
			{ 3, 4, 6, 7, 9, 11, 12, 13, 15 }
		};
		static const int8_t tall_y0[2][9] = {
			{ 2, 1, 0, 1, 0, 0, 1, 1, 2 },
			{ 2, 1, 1, 0, 0, 1, 0, 1, 2 }
		};
		static const int8_t tall_h[2][9] = {
			{ 13, 16, 19, 15, 21, 17, 20, 16, 12 },
			{ 12, 17, 15, 20, 18, 21, 16, 18, 13 }
		};
		static const int8_t tall_lean[2][9] = {
			{ -3, -2, -2, -1, 0, 1, 2, 2, 3 },
			{ -2, -3, -1, -1, 1, 1, 2, 3, 2 }
		};
		for (int b = 0; b < 9; b++) {
			int bh = tall_h[v][b];
			int has_plume = (bh >= 19);
			draw_tapered_blade(bx, by, 18, 22,
			                   tall_x[v][b], tall_y0[v][b], bh, tall_lean[v][b],
			                   c_root, c_deep, c_mid, c_lush,
			                   has_plume ? c_plume : c_tip, has_plume);
		}
	}

	/* Synthesize 2 dense littoral shoreline reed / cattail clusters (16x24 each, 6 tapered stalks) */
	for (int r = 0; r < 2; r++) {
		uint16_t bx = s_botanical_rects[NDB_REED_1 + r].x;
		uint16_t by = s_botanical_rects[NDB_REED_1 + r].y;
		static const int8_t stalk_x[2][6] = {
			{ 3, 5, 7, 9, 11, 13 },
			{ 3, 5, 7, 10, 11, 13 }
		};
		static const int8_t stalk_y0[2][6] = {
			{ 2, 0, 0, 1, 0, 2 },
			{ 2, 0, 1, 0, 1, 2 }
		};
		static const int8_t stalk_h[2][6] = {
			{ 16, 20, 23, 18, 22, 15 },
			{ 15, 22, 19, 23, 17, 20 }
		};
		static const int8_t has_cattail[2][6] = {
			{ 0, 1, 1, 0, 1, 0 },
			{ 0, 1, 0, 1, 0, 1 }
		};
		for (int s = 0; s < 6; s++) {
			int sx = stalk_x[r][s];
			int sy0 = stalk_y0[r][s];
			int sh = stalk_h[r][s];
			int lean = (s < 2) ? -1 : (s > 3 ? 1 : 0);
			draw_tapered_blade(bx, by, 16, 24,
			                   sx, sy0, sh, lean,
			                   c_root, c_deep, c_stalk, c_lush, c_tip, 0);
			if (has_cattail[r][s]) {
				for (int dy = sh - 8; dy <= sh - 4; dy++) {
					int py = 23 - dy;
					int px = sx + (lean * dy) / 14;
					if (px >= 1 && px < 15) {
						atlas_put_px_alpha((uint16_t)(bx + px), (uint16_t)(by + py), c_head1, 245);
						if (px > 1)
							atlas_put_px_alpha((uint16_t)(bx + px - 1), (uint16_t)(by + py), c_head2, 185);
					}
				}
			}
		}
	}
}

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
	synthesize_botanical_shelf();
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
			/* Pad left/right by 4px and top by 2px of transparent texels (bottom pad = 0 so
			 * ground root anchor_y is unchanged) so waving canopy leaves never clip at rect edges */
			const uint32_t pad_x = 4u;
			const uint32_t pad_top = 2u;
			uint32_t slot_w = iw + pad_x * 2u;
			uint32_t slot_h = ih + pad_top;
			if ((uint32_t)s_shelf_x + slot_w > ND_SEVEN_ATLAS_W) {
				s_shelf_x = 0;
				s_shelf_y = (uint16_t)(s_shelf_y + s_shelf_h);
				s_shelf_h = 0;
			}
			if ((uint32_t)s_shelf_y + slot_h <= ND_SEVEN_RUNTIME_ATLAS_H) {
				for (uint32_t ry = 0; ry < slot_h; ry++) {
					memset(&s_atlas_rgba[(s_shelf_y + ry) * ND_SEVEN_ATLAS_W + s_shelf_x],
					       0, slot_w * sizeof(uint32_t));
				}
				for (uint32_t ry = 0; ry < ih; ry++) {
					memcpy(&s_atlas_rgba[(s_shelf_y + pad_top + ry) * ND_SEVEN_ATLAS_W + s_shelf_x + pad_x],
					       &s_decode_scratch[ry * iw],
					       iw * sizeof(uint32_t));
				}
				slot->rect.x = s_shelf_x;
				slot->rect.y = s_shelf_y;
				slot->rect.w = (uint16_t)slot_w;
				slot->rect.h = (uint16_t)slot_h;
				s_shelf_x = (uint16_t)(s_shelf_x + slot_w);
				if (slot_h > s_shelf_h)
					s_shelf_h = (uint16_t)slot_h;
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

int nd_seven_botanical_rect(int kind, nd_seven_sprite_rect_t *out_rect)
{
	if (kind < 0 || kind >= NDB_COUNT || !out_rect)
		return 0;
	atlas_load();
	*out_rect = s_botanical_rects[kind];
	return 1;
}

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
