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
