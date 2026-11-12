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

static inline void atlas_put_px(uint16_t x, uint16_t y, uint32_t rgba)
{
	if (x < ND_SEVEN_ATLAS_W && y < ND_SEVEN_RUNTIME_ATLAS_H)
		s_atlas_rgba[(size_t)y * ND_SEVEN_ATLAS_W + x] = rgba;
}

static void synthesize_botanical_shelf(void)
{
	const uint32_t c_root  = RGB(28, 52, 28);
	const uint32_t c_deep  = RGB(40, 82, 38);
	const uint32_t c_mid   = RGB(62, 126, 54);
	const uint32_t c_lush  = RGB(86, 162, 66);
	const uint32_t c_tip   = RGB(130, 196, 84);
	const uint32_t c_plume = RGB(178, 210, 98);
	const uint32_t c_stalk = RGB(72, 120, 60);
	const uint32_t c_head1 = RGB(116, 74, 40);
	const uint32_t c_head2 = RGB(84, 52, 26);

	/* Clear reserved botanical shelf row (y: ND_SEVEN_ATLAS_H_PX .. +23) */
	memset(&s_atlas_rgba[(size_t)ND_SEVEN_ATLAS_H_PX * ND_SEVEN_ATLAS_W], 0,
	       24u * ND_SEVEN_ATLAS_W * sizeof(uint32_t));

	/* Synthesize 3 dense multi-blade meadow turf swatches (20x18 each, 11 overlapping blades) */
	for (int v = 0; v < 3; v++) {
		uint16_t bx = s_botanical_rects[NDB_GRASS_TUFT_1 + v].x;
		uint16_t by = s_botanical_rects[NDB_GRASS_TUFT_1 + v].y;
		static const int8_t blade_x[3][11] = {
			{ 2, 3, 5, 7, 8, 10, 11, 13, 14, 16, 17 },
			{ 2, 4, 5, 7, 9, 10, 12, 13, 15, 16, 17 },
			{ 1, 3, 5, 6, 8,  9, 11, 12, 14, 15, 17 }
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
			int x0 = blade_x[v][b];
			int bh = blade_h[v][b];
			int lean = blade_lean[v][b];
			for (int dy = 0; dy < bh; dy++) {
				int py = 17 - dy;
				int px = x0 + (lean * dy) / (bh > 1 ? bh - 1 : 1);
				if (px < 1) px = 1;
				if (px > 18) px = 18;
				uint32_t col = (dy < 3) ? c_root :
				               (dy < 6) ? c_deep :
				               (dy < bh - 4) ? c_mid :
				               (dy < bh - 1) ? c_lush : c_tip;
				atlas_put_px((uint16_t)(bx + px), (uint16_t)(by + py), col);
				if (dy < bh - 2 && px + 1 < 19) {
					uint32_t side_col = (dy < 4) ? c_root : (dy < 8 ? c_deep : c_mid);
					atlas_put_px((uint16_t)(bx + px + 1), (uint16_t)(by + py), side_col);
				}
				if (dy < 5 && px > 1) {
					atlas_put_px((uint16_t)(bx + px - 1), (uint16_t)(by + py), c_root);
				}
			}
		}
	}

	/* Synthesize 2 tall meadow grass clusters with arching seed plumes (18x22 each, 9 blades) */
	for (int v = 0; v < 2; v++) {
		uint16_t bx = s_botanical_rects[NDB_TALL_GRASS_1 + v].x;
		uint16_t by = s_botanical_rects[NDB_TALL_GRASS_1 + v].y;
		static const int8_t tall_x[2][9] = {
			{ 2, 4, 6, 7, 9, 10, 12, 14, 15 },
			{ 2, 3, 5, 7, 9, 11, 12, 14, 15 }
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
			int x0 = tall_x[v][b];
			int bh = tall_h[v][b];
			int lean = tall_lean[v][b];
			int has_plume = (bh >= 19);
			for (int dy = 0; dy < bh; dy++) {
				int py = 21 - dy;
				int px = x0 + (lean * dy * dy) / ((bh > 1 ? bh - 1 : 1) * (bh > 1 ? bh - 1 : 1));
				if (px < 1) px = 1;
				if (px > 16) px = 16;
				uint32_t col = (dy < 3) ? c_root :
				               (dy < 7) ? c_deep :
				               (dy < bh - 5) ? c_mid :
				               (dy < bh - 2) ? c_lush :
				               (has_plume ? c_plume : c_tip);
				atlas_put_px((uint16_t)(bx + px), (uint16_t)(by + py), col);
				if (dy < bh - 2 && px + 1 < 17) {
					uint32_t side_col = ( has_plume && dy >= bh - 4 ) ? c_tip :
					                    (dy < 4 ? c_root : (dy < 9 ? c_deep : c_mid));
					atlas_put_px((uint16_t)(bx + px + 1), (uint16_t)(by + py), side_col);
				}
			}
		}
	}

	/* Synthesize 2 dense littoral shoreline reed / cattail clusters (16x24 each, 6 stalks) */
	for (int r = 0; r < 2; r++) {
		uint16_t bx = s_botanical_rects[NDB_REED_1 + r].x;
		uint16_t by = s_botanical_rects[NDB_REED_1 + r].y;
		static const int8_t stalk_x[2][6] = {
			{ 2, 4, 7, 9, 11, 13 },
			{ 2, 5, 7, 10, 12, 14 }
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
			int sh = stalk_h[r][s];
			int lean = (s < 2) ? -1 : (s > 3 ? 1 : 0);
			for (int dy = 0; dy < sh; dy++) {
				int py = 23 - dy;
				int px = sx + (lean * dy) / 14;
				if (px < 1) px = 1;
				if (px > 14) px = 14;
				uint32_t col = (dy < 4) ? c_root :
				               (dy < 9) ? c_deep :
				               (dy < sh - 2) ? c_stalk : c_tip;
				atlas_put_px((uint16_t)(bx + px), (uint16_t)(by + py), col);
				if (dy < 6 && px + 1 < 15)
					atlas_put_px((uint16_t)(bx + px + 1), (uint16_t)(by + py), c_deep);
				/* Brown cylindrical cattail seed-head near upper third */
				if (has_cattail[r][s] && dy >= sh - 9 && dy <= sh - 4) {
					atlas_put_px((uint16_t)(bx + px), (uint16_t)(by + py), c_head1);
					if (px > 0)
						atlas_put_px((uint16_t)(bx + px - 1), (uint16_t)(by + py), c_head2);
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
