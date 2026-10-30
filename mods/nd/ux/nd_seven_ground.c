#include "nd_seven_ground.h"
#include "nd_seven_atlas_draw.h"
#include "nd_seven_motion.h"
#include <string.h>

static uint32_t unit_noise(uint32_t h, int ux, int uy)
{
	return ndw_mix(h ^ (uint32_t)(uy * 16 + ux + 1) * 0x9E3779B1u);
}

void ground_cell(uint32_t *fb, int w, int h, const ndw_ground_t *g,
                 const int16_t pos[4], int cx, int cy, int px, int py, uint32_t anim)
{
	ctx_t c = { fb, w, h, px, py };
	uint32_t hash = ndw_cell_hash(pos, cx, cy);
	uint32_t base, alt1, alt2, alt3 = 0;
	int r = g->r, gg = g->g, b = g->b;

	if (px >= w || py >= h || px + NDW_CELL_PX * SCALE <= 0 || py + NDW_CELL_PX * SCALE <= 0)
		return;

	if (g->kind == NDW_G_GRASS) {
		tint_t t = { g->r, g->g, g->b, 0 };
		blit_all(fb, w, h, NDS_GRASS, 0, 0, px, py, SCALE, &t);
		if ((hash % 100) < 6) {
			int id = NDS_DECOR_YEL + (int)((hash >> 8) & 1);
			const nd_seven_sprite_rect_t *sr = &nd_seven_sprite_rects[id];
			blit_all(fb, w, h, id, sr->w / 2, sr->h - 1,
			         px + (4 + (int)((hash >> 12) & 7)) * SCALE, py + 14 * SCALE, SCALE, NULL);
		}
		return;
	}

	switch (g->kind) {
	case NDW_G_SAND:
		base = RGB(226, 200, 140); alt1 = RGB(210, 182, 122); alt2 = RGB(238, 214, 160);
		break;
	case NDW_G_SNOW:
		base = RGB(234, 240, 250); alt1 = RGB(210, 222, 240); alt2 = RGB(252, 252, 255);
		break;
	case NDW_G_TUNDRA:
		base = RGB(150, 166, 148); alt1 = RGB(128, 148, 126); alt2 = RGB(180, 190, 176);
		break;
	case NDW_G_WATER:
		base = RGB(36, 78, 148);   /* deep sapphire */
		alt1 = RGB(48, 112, 186);  /* mid azure body */
		alt2 = RGB(88, 170, 232);  /* luminous wave crest */
		alt3 = RGB(220, 245, 255); /* specular foam / glint */
		break;
	default: /* ash */
		base = RGB(54, 48, 52); alt1 = RGB(40, 36, 40); alt2 = RGB(76, 66, 68);
		break;
	}
	base = tint_col(base, r, gg, b);
	alt1 = tint_col(alt1, r, gg, b);
	alt2 = tint_col(alt2, r, gg, b);
	if (alt3) alt3 = tint_col(alt3, r, gg, b);

	urect(&c, 0, 0, NDW_CELL_PX, NDW_CELL_PX, base);
	for (int uy = 0; uy < NDW_CELL_PX; uy++)
		for (int ux = 0; ux < NDW_CELL_PX; ux++) {
			uint32_t n = unit_noise(hash, ux, uy) & 31;
			if (g->kind == NDW_G_WATER) {
				int gx = (int)pos[0] * NDW_TILE_W + cx * NDW_CELL_PX + ux;
				int gy = -(int)pos[1] * NDW_TILE_H + cy * NDW_CELL_PX + uy;
				int drift = (int)(anim / 6);

				/* Wave 1: primary gentle undulating wave crest */
				int wave_y = gy + ((((gx >> 2) ^ (gx >> 4)) * 3) & 3);
				int w1 = (wave_y + (gx >> 3) - drift) % 12;
				if (w1 < 0) w1 += 12;

				int chop = ((gx * 3) / 4 + (gy >> 1) + (drift >> 1)) % 14;
				if (chop < 0) chop += 14;

				/* Secondary swell for natural surface depth */
				int w2 = (gy * 2 - gx + (drift >> 1)) % 19;
				if (w2 < 0) w2 += 19;
				int chop2 = (gx + gy * 2) % 15;
				if (chop2 < 0) chop2 += 15;

				if (w1 == 0 && chop < 8) {
					if (chop < 3)
						urect(&c, ux, uy, 1, 1, alt3);
					else
						urect(&c, ux, uy, 1, 1, alt2);
				} else if (w1 == 1 && chop < 7) {
					urect(&c, ux, uy, 1, 1, alt1);
				} else if (w2 == 0 && chop2 < 5) {
					urect(&c, ux, uy, 1, 1, alt1);
				} else if (n == 0) {
					urect(&c, ux, uy, 1, 1, alt1);
				}
			} else if (n < 2) {
				urect(&c, ux, uy, 1, 1, alt1);
			} else if (n == 2) {
				urect(&c, ux, uy, 1, 1, alt2);
			}
		}

	if (g->kind == NDW_G_SAND && (((hash >> 8) & 3) == 0)) {
		static const int8_t arc[8] = { 1, 0, 0, -1, -1, 0, 0, 1 };
		int ux0 = 2 + (int)((hash >> 12) & 3), uy0 = 5 + (int)((hash >> 15) & 7);
		for (int k = 0; k < 8; k++) {
			urect(&c, ux0 + k, uy0 + arc[k], 1, 1, shade(alt1, 92));
			urect(&c, ux0 + k, uy0 + arc[k] - 1, 1, 1, alt2);
		}
	}
	if (g->kind == NDW_G_ASH && (hash % 23) == 0) {
		int ux0 = 3 + (int)((hash >> 8) & 3), uy0 = 4 + (int)((hash >> 12) & 7);
		for (int k = 0; k < 8; k++)
			urect(&c, ux0 + k, uy0 + ((k >> 1) & 1), 1, 1, RGB(226, 96, 28));
	}
	if (g->kind == NDW_G_SNOW && (hash % 17) == 0)
		urect(&c, 3 + (int)((hash >> 8) & 7), 3 + (int)((hash >> 12) & 7), 2, 1, RGB(255, 255, 255));
}

#define BORDER_CACHE 64
typedef struct {
	int16_t pos[4];
	uint8_t used;
	uint8_t act_valid;
	uint64_t act_mask;
	uint8_t act[NDW_ROWS * NDW_COLS];
	ndw_warp_grid_t grid;
} border_ent_t;
static border_ent_t s_border[BORDER_CACHE];
static unsigned s_border_next;

static border_ent_t *border_find(const int16_t pos[4])
{
	border_ent_t *e;

	for (int i = 0; i < BORDER_CACHE; i++)
		if (s_border[i].used && !memcmp(s_border[i].pos, pos, sizeof(s_border[i].pos)))
			return &s_border[i];
	e = &s_border[s_border_next++ % BORDER_CACHE];
	memcpy(e->pos, pos, sizeof(e->pos));
	e->used = 1;
	e->act_valid = 0;
	ndw_warp_grid_fill(&e->grid, pos);
	return e;
}

const ndw_warp_grid_t *nd_seven_warp_grid(const int16_t pos[4])
{
	return &border_find(pos)->grid;
}

static int border_q(int cell, int s)
{
	return cell * NDW_CELL_PX * NDW_SUB + (s * 2 + 1) * NDW_SUB / (2 * SCALE);
}

static const uint8_t *border_active(border_ent_t *e, const uint8_t grp[9], unsigned no_blend)
{
	uint64_t key = (uint64_t)no_blend << 40;

	for (int k = 0; k < 9; k++)
		key |= (uint64_t)(grp[k] & 15) << (4 * k);
	if (e->act_valid && e->act_mask == key)
		return e->act;
	for (int cy = 0; cy < NDW_ROWS; cy++)
		for (int cx = 0; cx < NDW_COLS; cx++) {
			int any = 0;

			for (int sy = 0; sy < NDW_CELL_PX * SCALE && !any; sy++)
				for (int sx = 0; sx < NDW_CELL_PX * SCALE && !any; sx++) {
					uint16_t w[9];
					any = ndw_ground_weights(&e->grid, border_q(cx, sx), border_q(cy, sy), grp, no_blend, w);
				}
			e->act[cy * NDW_COLS + cx] = (uint8_t)any;
		}
	e->act_mask = key;
	e->act_valid = 1;
	return e->act;
}

static int same_ground(const ndw_ground_t *a, const ndw_ground_t *b)
{
	return a->kind == b->kind && a->r == b->r && a->g == b->g && a->b == b->b;
}

void ground_blend(uint32_t *fb, int w, int h, const nd_client_state_t *st,
                  int ti, int tj, const int16_t pos[4], const ndw_tile_t *own,
                  int tx, int ty)
{
	static uint32_t scratch[9][NDW_CELL_PX * SCALE * NDW_CELL_PX * SCALE];
	const int cpx = NDW_CELL_PX * SCALE;
	ndw_ground_t style[9];
	unsigned no_blend = 0;
	uint8_t grp[9];
	const uint8_t *act;
	border_ent_t *ent;
	int differs = 0;

	style[4] = ndw_biome_style(own->biome);
	for (int dj = -1; dj <= 1; dj++)
		for (int di = -1; di <= 1; di++) {
			ndw_tile_t nt;
			int16_t np[4];
			int k = (dj + 1) * 3 + (di + 1);

			if (!di && !dj) {
				nt = *own;
			} else {
				nd_seven_tile_at(st, ti + di, tj + dj, &nt, np);
			}
			style[k] = ndw_biome_style(nt.biome);
			differs |= !same_ground(&style[k], &style[4]);
			if (style[k].kind == NDW_G_WATER)
				no_blend |= NDW_WATER_BIT(di, dj);
		}
	for (int k = 0; k < 9; k++) {
		grp[k] = (uint8_t)k;
		for (int j = 0; j < k; j++)
			if (same_ground(&style[j], &style[k])) {
				grp[k] = grp[j];
				break;
			}
	}
	if (!differs)
		return;
	ent = border_find(pos);
	act = border_active(ent, grp, no_blend);

	for (int cy = 0; cy < NDW_ROWS; cy++)
		for (int cx = 0; cx < NDW_COLS; cx++) {
			int px = tx + cx * cpx, py = ty + cy * cpx;
			int have[9] = { 0 };

			if (!act[cy * NDW_COLS + cx])
				continue;
			if (px >= w || py >= h || px + cpx <= 0 || py + cpx <= 0)
				continue;
			for (int sy = 0; sy < cpx; sy++) {
				int y = py + sy;
				if (y < 0 || y >= h)
					continue;
				for (int sx = 0; sx < cpx; sx++) {
					int x = px + sx;
					uint16_t wt[9];
					int own_k = 4;

					if (x < 0 || x >= w)
						continue;
					ndw_ground_weights(&ent->grid, border_q(cx, sx), border_q(cy, sy), grp, no_blend, wt);
					for (int k = 0; k < 9; k++)
						if (wt[k])
							own_k = k;
					if (!have[own_k]) {
						ground_cell(scratch[own_k], cpx, cpx, &style[own_k], pos, cx, cy, 0, 0, nd_seven_get_anim());
						have[own_k] = 1;
					}
					fb[y * w + x] = scratch[own_k][sy * cpx + sx];
				}
			}
		}
}
