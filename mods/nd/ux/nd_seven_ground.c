#include "nd_seven_ground.h"
#include "nd_seven_atlas_draw.h"
#include "nd_seven_motion.h"
#include <stdlib.h>
#include <string.h>
#include <nd/terrain.h>

static inline uint32_t unit_noise(uint32_t h, int ux, int uy)
{
	return ndw_mix(h ^ (uint32_t)(uy * 16 + ux + 1) * 0x9E3779B1u);
}

static inline int bnoise(uint32_t seed, int x, int y, int freq_x, int freq_y)
{
	int gx = x / freq_x;
	int gy = y / freq_y;
	int fx = x % freq_x; if (fx < 0) { fx += freq_x; gx--; }
	int fy = y % freq_y; if (fy < 0) { fy += freq_y; gy--; }

	int u = (fx * fx * (3 * freq_x - 2 * fx)) / (freq_x * freq_x);
	int v = (fy * fy * (3 * freq_y - 2 * fy)) / (freq_y * freq_y);

	int n00 = (int)(unit_noise(seed, gx, gy) & 255);
	int n10 = (int)(unit_noise(seed, gx + 1, gy) & 255);
	int n01 = (int)(unit_noise(seed, gx, gy + 1) & 255);
	int n11 = (int)(unit_noise(seed, gx + 1, gy + 1) & 255);

	int a = n00 + ((n10 - n00) * u) / freq_x;
	int b = n01 + ((n11 - n01) * u) / freq_x;
	return a + ((b - a) * v) / freq_y;
}

static uint8_t *s_active_chunk_depth = NULL;
static uint8_t s_active_chunk_land_mask = 0;
static uint8_t s_active_chunk_grp[9];

static inline uint32_t alpha_blend_rgb(uint32_t bg, uint32_t fg, int alpha)
{
	if (alpha <= 0) return bg;
	if (alpha >= 255) return fg;
	int r_bg = (int)(bg & 0xFF), g_bg = (int)((bg >> 8) & 0xFF), b_bg = (int)((bg >> 16) & 0xFF);
	int r_fg = (int)(fg & 0xFF), g_fg = (int)((fg >> 8) & 0xFF), b_fg = (int)((fg >> 16) & 0xFF);
	int r = r_bg + ((r_fg - r_bg) * alpha) / 255;
	int g = g_bg + ((g_fg - g_bg) * alpha) / 255;
	int b = b_bg + ((b_fg - b_bg) * alpha) / 255;
	return RGB(r, g, b);
}

static inline uint32_t lerp_rgb(uint32_t c1, uint32_t c2, int num, int den)
{
	if (num <= 0) return c1;
	if (num >= den) return c2;
	int r1 = (int)(c1 & 0xFF), g1 = (int)((c1 >> 8) & 0xFF), b1 = (int)((c1 >> 16) & 0xFF);
	int r2 = (int)(c2 & 0xFF), g2 = (int)((c2 >> 8) & 0xFF), b2 = (int)((c2 >> 16) & 0xFF);
	int r = r1 + ((r2 - r1) * num) / den;
	int g = g1 + ((g2 - g1) * num) / den;
	int b = b1 + ((b2 - b1) * num) / den;
	return RGB(r, g, b);
}

static inline uint32_t water_color_at_depth(int depth, int r, int gg, int b)
{
	static const uint32_t C_SHORE = RGB(64, 195, 230);  /* vibrant translucent cerulean shallows (0-12 units) */
	static const uint32_t C_INNER = RGB(48, 160, 215);  /* luminous inner shelf aquamarine (12-36 units) */
	static const uint32_t C_OUTER = RGB(32, 115, 190);  /* coastal outer shelf azure (36-72 units) */
	static const uint32_t C_DEEP  = RGB(18, 52, 130);   /* deep sapphire ocean basin (72+ units) */

	uint32_t water_col;
	if (depth <= 6) {
		water_col = lerp_rgb(C_SHORE, C_INNER, depth - 1, 5);
	} else if (depth <= 18) {
		water_col = lerp_rgb(C_INNER, C_OUTER, depth - 6, 12);
	} else {
		int t = depth - 18;
		if (t > 10) t = 10;
		water_col = lerp_rgb(C_OUTER, C_DEEP, t, 10);
	}
	return tint_col(water_col, r, gg, b);
}

static inline int water_alpha_at_depth(int depth)
{
	if (depth <= 6) {
		return 75 + (depth * 45) / 6;
	} else if (depth <= 18) {
		return 120 + ((depth - 6) * 55) / 12;
	} else {
		int t = depth - 18;
		if (t > 10) t = 10;
		return 175 + (t * 65) / 10;
	}
}

typedef struct {
	int32_t xc, yc;          /* Center in world units */
	int32_t x0, y0, x1, y1;  /* Bounding box */
	uint8_t round_corners;   /* Bitmask: bit 0=NW, 1=NE, 2=SW, 3=SE round into water */
} nd_land_tile_t;

typedef struct {
	int32_t tile_xc, tile_yc;   /* Water tile center */
	int8_t sx, sy;              /* Direction towards the corner (+-1) */
} nd_land_jut_t;

static nd_land_tile_t s_active_land_tiles[64];
static int s_active_n_land_tiles = 0;

static nd_land_jut_t s_active_land_juts[64];
static int s_active_n_land_juts = 0;

static uint32_t s_last_coast_map_room = 0xFFFFFFFF;
static int16_t s_last_coast_map_pos[4] = { -32768, -32768, -32768, -32768 };

static void nd_coast_map_build(const nd_map_data_t *map)
{
	s_active_n_land_tiles = 0;
	s_active_n_land_juts = 0;
	if (!map || map->width == 0 || map->height == 0) return;

	int w = (int)map->width;
	int h = (int)map->height;
	int px = (int)map->player_x;
	int py = (int)map->player_y;

	for (int r = 0; r < h; r++) {
		for (int c = 0; c < w; c++) {
			int idx = r * w + c;
			uint8_t bio = map->tiles[idx].bio_idx;
			int is_w = ndw_is_water_ground(ndw_biome_style(bio).kind);

			int dcol = c - px;
			int drow = r - py;
			int32_t bx = (int32_t)(map->pos[0] + dcol) * NDW_TILE_W;
			int32_t by = -(int32_t)(map->pos[1] - drow) * NDW_TILE_H;
			int32_t xc = bx + NDW_TILE_W / 2;
			int32_t yc = by + NDW_TILE_H / 2;

			/* Check orthogonal neighbors: North (r-1), South (r+1), West (c-1), East (c+1) */
			int w_west = (c > 0) ? ndw_is_water_ground(ndw_biome_style(map->tiles[r * w + (c - 1)].bio_idx).kind) : is_w;
			int w_east = (c + 1 < w) ? ndw_is_water_ground(ndw_biome_style(map->tiles[r * w + (c + 1)].bio_idx).kind) : is_w;
			int w_north = (r > 0) ? ndw_is_water_ground(ndw_biome_style(map->tiles[(r - 1) * w + c].bio_idx).kind) : is_w;
			int w_south = (r + 1 < h) ? ndw_is_water_ground(ndw_biome_style(map->tiles[(r + 1) * w + c].bio_idx).kind) : is_w;

			if (!is_w) {
				/* Land tile: check which corners round into water */
				if (s_active_n_land_tiles < 64) {
					nd_land_tile_t *lt = &s_active_land_tiles[s_active_n_land_tiles++];
					lt->xc = xc; lt->yc = yc;
					lt->x0 = bx; lt->y0 = by;
					lt->x1 = bx + NDW_TILE_W; lt->y1 = by + NDW_TILE_H;
					uint8_t rc = 0;
					if (w_west && w_north) rc |= (1 << 0); /* NW */
					if (w_east && w_north) rc |= (1 << 1); /* NE */
					if (w_west && w_south) rc |= (1 << 2); /* SW */
					if (w_east && w_south) rc |= (1 << 3); /* SE */
					lt->round_corners = rc;
				}
			} else {
				/* Water tile: check if land juts into concave corners */
				if (!w_west && !w_north && s_active_n_land_juts < 64) {
					nd_land_jut_t *lj = &s_active_land_juts[s_active_n_land_juts++];
					lj->tile_xc = xc; lj->tile_yc = yc;
					lj->sx = -1; lj->sy = -1;
				}
				if (!w_east && !w_north && s_active_n_land_juts < 64) {
					nd_land_jut_t *lj = &s_active_land_juts[s_active_n_land_juts++];
					lj->tile_xc = xc; lj->tile_yc = yc;
					lj->sx = 1; lj->sy = -1;
				}
				if (!w_west && !w_south && s_active_n_land_juts < 64) {
					nd_land_jut_t *lj = &s_active_land_juts[s_active_n_land_juts++];
					lj->tile_xc = xc; lj->tile_yc = yc;
					lj->sx = -1; lj->sy = 1;
				}
				if (!w_east && !w_south && s_active_n_land_juts < 64) {
					nd_land_jut_t *lj = &s_active_land_juts[s_active_n_land_juts++];
					lj->tile_xc = xc; lj->tile_yc = yc;
					lj->sx = 1; lj->sy = 1;
				}
			}
		}
	}
}

static inline void nd_coast_map_build_if_needed(const nd_map_data_t *map)
{
	if (!map || map->width == 0 || map->height == 0) {
		s_active_n_land_tiles = 0;
		s_active_n_land_juts = 0;
		s_last_coast_map_room = 0xFFFFFFFF;
		return;
	}
	if (map->room_ref == s_last_coast_map_room &&
	    memcmp(map->pos, s_last_coast_map_pos, sizeof(s_last_coast_map_pos)) == 0) {
		return;
	}
	s_last_coast_map_room = map->room_ref;
	memcpy(s_last_coast_map_pos, map->pos, sizeof(s_last_coast_map_pos));
	nd_coast_map_build(map);
}

static inline int nd_global_land_dist(int32_t wx, int32_t wy)
{
	if (s_active_n_land_tiles == 0 && s_active_n_land_juts == 0) return 99999;
	int min_dist = 99999;

	/* 1. Distance to land tiles */
	for (int i = 0; i < s_active_n_land_tiles; i++) {
		const nd_land_tile_t *lt = &s_active_land_tiles[i];

		/* Quick bounding box cull with current min_dist */
		if (wx < lt->x0 - min_dist || wx > lt->x1 + min_dist ||
		    wy < lt->y0 - min_dist || wy > lt->y1 + min_dist)
			continue;

		int sx = (wx >= lt->xc) ? 1 : -1;
		int sy = (wy >= lt->yc) ? 1 : -1;
		int q_idx = (sy > 0 ? 2 : 0) | (sx > 0 ? 1 : 0);
		int rounds = (lt->round_corners >> q_idx) & 1;

		int px = (wx - lt->xc) * sx; /* >= 0 */
		int py = (wy - lt->yc) * sy; /* >= 0 */

		if (!rounds) {
			int dx = (px > 80) ? (px - 80) : 0;
			int dy = (py > 64) ? (py - 64) : 0;
			int d = ndw_isqrt(dx * dx + dy * dy);
			if (d < min_dist) {
				min_dist = d;
				if (min_dist == 0) return 0;
			}
		} else {
			/* Ellipse centered at (xc, yc) with rx=80, ry=64 */
			int py_scaled = (py * 5) / 4;
			int r = ndw_isqrt(px * px + py_scaled * py_scaled);
			if (r <= 80) {
				return 0; /* Inside land! */
			}
			int d = r - 80;
			if (d < min_dist) {
				min_dist = d;
			}
		}
	}

	/* 2. Distance to land juts (concave water corners where land juts in) */
	for (int i = 0; i < s_active_n_land_juts; i++) {
		const nd_land_jut_t *lj = &s_active_land_juts[i];
		int px = (wx - lj->tile_xc) * lj->sx;
		int py = (wy - lj->tile_yc) * lj->sy;
		if (px > 0 && py > 0) {
			int py_scaled = (py * 5) / 4;
			int r = ndw_isqrt(px * px + py_scaled * py_scaled);
			if (r >= 80) {
				return 0; /* Inside jutting land! */
			}
			int d = 80 - r;
			if (d < min_dist) {
				min_dist = d;
			}
		}
	}

	return min_dist;
}

/* Sub-linear bathymetric shelf distance (R3): extends water shelf darkening
 * across a 72-unit coastal shelf using sub-linear power attenuation
 * to preserve wide, luminous shallow water near the shore before gently
 * transitioning into the deep ocean basin. */
static inline int nd_water_depth_at(int dist_units, int terrain_depth)
{
	int depth;
	if (dist_units < 0) dist_units = 0;
	if (dist_units >= 72) {
		depth = 28;
	} else {
		int t_frac = (dist_units * 256) / 72;
		int t_sqrt = ndw_isqrt(t_frac * 256);
		int t_sub = (t_frac + t_sqrt) / 2;
		depth = 1 + (27 * t_sub) / 256;
	}
	if (terrain_depth > depth && terrain_depth <= 28) {
		depth = terrain_depth;
	}
	if (depth < 1) depth = 1;
	return depth;
}

static void render_water_cell(ctx_t *c, const ndw_ground_t *g, const int16_t pos[4],
                              int cx, int cy, uint32_t hash, uint32_t anim,
                              int r, int gg, int b)
{
	(void)anim;
	(void)hash;
	uint16_t world = (uint16_t)pos[3];
	int16_t w_level = nd_terrain_water_level(world);
	int land_di[9], land_dj[9], n_land = 0;
	const ndw_warp_grid_t *wg = NULL;

	if (s_active_chunk_land_mask) {
		wg = nd_seven_warp_grid(pos);
		for (int dj = -1; dj <= 1; dj++) {
			for (int di = -1; di <= 1; di++) {
				int k = (dj + 1) * 3 + (di + 1);
				if (s_active_chunk_land_mask & (1 << k)) {
					land_di[n_land] = di;
					land_dj[n_land] = dj;
					n_land++;
				}
			}
		}
	}

	for (int uy = 0; uy < NDW_CELL_PX; uy++) {
		for (int ux = 0; ux < NDW_CELL_PX; ux++) {
			int px_in_tile = cx * NDW_CELL_PX + ux;
			int py_in_tile = cy * NDW_CELL_PX + uy;
			int32_t wx = (int32_t)pos[0] * NDW_TILE_W + px_in_tile;
			int32_t wy = -(int32_t)pos[1] * NDW_TILE_H + py_in_tile;
			int16_t elev = nd_terrain_height_hd(wx, wy, world);
			int terrain_depth = (int)(w_level - elev);
			if (terrain_depth < 1) terrain_depth = 1;

			int depth;
			if (g->kind == NDW_G_SHALLOW_WATER) {
				depth = 4;
			} else if (s_active_n_land_tiles > 0 || s_active_n_land_juts > 0) {
				int qx = px_in_tile * NDW_SUB;
				int qy = py_in_tile * NDW_SUB;
				int dx = 0, dy = 0;
				if (wg) ndw_warp_at(wg, qx, qy, &dx, &dy);

				int dist_units = nd_global_land_dist(wx + dx / NDW_FINE, wy + dy / NDW_FINE);
				depth = nd_water_depth_at(dist_units, terrain_depth);
			} else if (n_land > 0 && wg) {
				int qx = px_in_tile * NDW_SUB;
				int qy = py_in_tile * NDW_SUB;
				int dx = 0, dy = 0;
				ndw_warp_at(wg, qx, qy, &dx, &dy);

				int px_w = px_in_tile * NDW_FINE + dx;
				int py_w = py_in_tile * NDW_FINE + dy;

				int max_land_d = -999999;
				for (int l = 0; l < n_land; l++) {
					int d = ndw_tile_depth(s_active_chunk_grp, 0, land_di[l], land_dj[l], px_w, py_w);
					if (d > max_land_d) max_land_d = d;
				}

				int dist_units = (-max_land_d) / NDW_FINE;
				depth = nd_water_depth_at(dist_units, terrain_depth);
			} else {
				depth = terrain_depth;
			}
			if (depth < 1) depth = 1;

			/* Underlying sandy seabed with subtle micro-grain texture */
			int sx0 = c->ox + ux * SCALE;
			int sy0 = c->oy + uy * SCALE;
			uint32_t n = unit_noise(hash, ux, uy) & 31;
			uint32_t sand_base  = RGB(226, 200, 140);
			uint32_t sand_dark  = RGB(214, 188, 126);
			uint32_t sand_light = RGB(236, 212, 155);
			uint32_t seabed = (n < 2) ? sand_dark : (n == 2 ? sand_light : sand_base);
			seabed = tint_col(seabed, r, gg, b);

			uint32_t water_col = water_color_at_depth(depth, r, gg, b);
			int alpha = water_alpha_at_depth(depth);

			uint32_t final_pixel = alpha_blend_rgb(seabed, water_col, alpha);
			urect(c, ux, uy, 1, 1, final_pixel);

			if (s_active_chunk_depth) {
				uint8_t d_byte = (depth > 255) ? 255 : (uint8_t)depth;
				for (int dy = 0; dy < SCALE; dy++) {
					for (int dx = 0; dx < SCALE; dx++) {
						s_active_chunk_depth[(sy0 + dy) * TILE_PX_W + (sx0 + dx)] = d_byte;
					}
				}
			}
		}
	}
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

	if (ndw_is_water_ground(g->kind)) {
		render_water_cell(&c, g, pos, cx, cy, hash, anim, r, gg, b);
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
			if (n < 2) {
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

void ground_blend_nbiomes(uint32_t *fb, int w, int h, const uint8_t nbiomes[9],
                          const int16_t pos[4], const ndw_tile_t *own,
                          int tx, int ty, uint8_t *depth_grid)
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
	for (int k = 0; k < 9; k++) {
		style[k] = ndw_biome_style(nbiomes ? nbiomes[k] : own->biome);
		differs |= !same_ground(&style[k], &style[4]);
		int di = (k % 3) - 1, dj = (k / 3) - 1;
		if (ndw_is_water_ground(style[k].kind))
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
					if (!ndw_ground_weights(&ent->grid, border_q(cx, sx), border_q(cy, sy), grp, no_blend, wt))
						continue;
					for (int k = 0; k < 9; k++)
						if (wt[k])
							own_k = k;
					if (own_k == 4)
						continue;

					if (!have[own_k]) {
						ground_cell(scratch[own_k], cpx, cpx, &style[own_k], pos, cx, cy, 0, 0, nd_seven_get_anim());
						have[own_k] = 1;
					}
					fb[y * w + x] = scratch[own_k][sy * cpx + sx];
					if (depth_grid) {
						if (!ndw_is_water_ground(style[own_k].kind)) {
							depth_grid[y * w + x] = 0; /* Dry land encroaching */
						} else {
							int32_t wx = (int32_t)pos[0] * NDW_TILE_W + (cx * NDW_CELL_PX + sx / SCALE);
							int32_t wy = -(int32_t)pos[1] * NDW_TILE_H + (cy * NDW_CELL_PX + sy / SCALE);
							uint16_t world = (uint16_t)pos[3];
							int16_t elev = nd_terrain_height_hd(wx, wy, world);
							int16_t w_level = nd_terrain_water_level(world);
							int terrain_depth = (int)(w_level - elev);
							if (terrain_depth < 1) terrain_depth = 1;
							int qx = border_q(cx, sx);
							int qy = border_q(cy, sy);
							int dx = 0, dy = 0;
							ndw_warp_at(&ent->grid, qx, qy, &dx, &dy);
							int dist_units = nd_global_land_dist(wx + dx / NDW_FINE, wy + dy / NDW_FINE);
							depth_grid[y * w + x] = (uint8_t)nd_water_depth_at(dist_units, terrain_depth);
						}
					}
				}
			}
		}
}

void ground_blend(uint32_t *fb, int w, int h, const nd_client_state_t *st,
                  int ti, int tj, const int16_t pos[4], const ndw_tile_t *own,
                  int tx, int ty)
{
	uint8_t nbiomes[9];
	nbiomes[4] = own->biome;
	for (int dj = -1; dj <= 1; dj++)
		for (int di = -1; di <= 1; di++) {
			int k = (dj + 1) * 3 + (di + 1);
			if (!di && !dj) continue;
			ndw_tile_t nt;
			int16_t np[4];
			if (st) {
				nd_seven_tile_at(st, ti + di, tj + dj, &nt, np);
				nbiomes[k] = nt.biome;
			} else {
				nbiomes[k] = own->biome;
			}
		}
	ground_blend_nbiomes(fb, w, h, nbiomes, pos, own, tx, ty, NULL);
}

#define TILE_PX_W (NDW_TILE_W * SCALE)
#define TILE_PX_H (NDW_TILE_H * SCALE)
#define CHUNK_HASH_BITS 6
#define CHUNK_HASH_BUCKETS (1 << CHUNK_HASH_BITS)

static nd_room_chunk_t *s_chunk_buckets[CHUNK_HASH_BUCKETS];
static uint32_t s_current_frame = 1;
static int s_chunk_count = 0;
static int s_chunk_alloc_count = 0;
static int s_chunk_free_count = 0;

static inline unsigned chunk_hash(const int16_t pos[4])
{
	uint32_t h = (uint32_t)pos[0] * 73856093u ^
	             (uint32_t)pos[1] * 19349663u ^
	             (uint32_t)pos[2] * 83492791u ^
	             (uint32_t)pos[3] * 39916801u;
	return (unsigned)(h & (CHUNK_HASH_BUCKETS - 1));
}

static nd_room_chunk_t *chunk_find(const int16_t pos[4], const uint8_t nbiomes[9])
{
	unsigned bucket = chunk_hash(pos);
	for (nd_room_chunk_t *c = s_chunk_buckets[bucket]; c; c = c->next) {
		if (memcmp(c->pos, pos, sizeof(c->pos)) == 0) {
			if (nbiomes && memcmp(c->nbiomes, nbiomes, sizeof(c->nbiomes)) != 0) {
				return NULL; /* biomes changed, needs regeneration */
			}
			return c;
		}
	}
	return NULL;
}

const nd_room_chunk_t *nd_seven_chunk_find(const int16_t pos[4])
{
	return chunk_find(pos, NULL);
}

int nd_seven_pos_in_map(const int16_t pos[4], const nd_map_data_t *map)
{
	if (!map || map->width == 0 || map->height == 0)
		return 0;
	if (pos[2] != map->pos[2] || pos[3] != map->pos[3])
		return 0;

	int ti = (int)pos[0] - (int)map->pos[0];
	int tj = (int)map->pos[1] - (int)pos[1];
	int col = (int)map->player_x + ti;
	int row = (int)map->player_y + tj;

	return (col >= 0 && col < (int)map->width && row >= 0 && row < (int)map->height);
}

void nd_seven_ground_frame_begin(void)
{
	s_current_frame++;
	if (s_current_frame == 0) s_current_frame = 1;
}

void nd_seven_ground_frame_end(const nd_map_data_t *map)
{
	/* Active view window chunk retention & eager eviction:
	 * If map is provided, retain any chunk within the active 7x7 view window (st->map_data).
	 * Eagerly free any chunk whose coordinates have exited the 7x7 view window.
	 * If map is NULL, fallback to checking c->last_frame >= s_current_frame. */
	for (int b = 0; b < CHUNK_HASH_BUCKETS; b++) {
		nd_room_chunk_t **pp = &s_chunk_buckets[b];
		while (*pp) {
			nd_room_chunk_t *c = *pp;
			int keep = 0;
			if (map && map->width > 0 && map->height > 0) {
				keep = nd_seven_pos_in_map(c->pos, map);
			} else {
				keep = (c->last_frame >= s_current_frame);
			}

			if (!keep) {
				*pp = c->next;
				if (c->pixels) free(c->pixels);
				if (c->depth) free(c->depth);
				free(c);
				s_chunk_count--;
				s_chunk_free_count++;
			} else {
				pp = &c->next;
			}
		}
	}
}

void nd_seven_ground_cache_forget(void)
{
	s_active_n_land_tiles = 0;
	s_active_n_land_juts = 0;
	s_last_coast_map_room = 0xFFFFFFFF;
	for (int b = 0; b < CHUNK_HASH_BUCKETS; b++) {
		nd_room_chunk_t *c = s_chunk_buckets[b];
		while (c) {
			nd_room_chunk_t *next = c->next;
			if (c->pixels) free(c->pixels);
			if (c->depth) free(c->depth);
			free(c);
			c = next;
		}
		s_chunk_buckets[b] = NULL;
	}
	s_chunk_count = 0;
	s_chunk_alloc_count = 0;
	s_chunk_free_count = 0;
}

int nd_seven_chunk_count(void)
{
	return s_chunk_count;
}

int nd_seven_chunk_alloc_count(void)
{
	return s_chunk_alloc_count;
}

int nd_seven_chunk_free_count(void)
{
	return s_chunk_free_count;
}

static nd_room_chunk_t *chunk_get_or_create(const int16_t pos[4],
                                            const ndw_tile_t *own,
                                            const uint8_t nbiomes[9],
                                            const nd_map_data_t *map,
                                            int ti, int tj)
{
	(void)ti;
	(void)tj;
	atlas_load();
	nd_room_chunk_t *chunk = chunk_find(pos, nbiomes);
	if (chunk) {
		chunk->last_frame = s_current_frame;
		return chunk;
	}

	/* Evict any old chunk at this pos (e.g. stale biomes) */
	unsigned bucket = chunk_hash(pos);
	nd_room_chunk_t **pp = &s_chunk_buckets[bucket];
	while (*pp) {
		nd_room_chunk_t *c = *pp;
		if (memcmp(c->pos, pos, sizeof(c->pos)) == 0) {
			*pp = c->next;
			if (c->pixels) free(c->pixels);
			free(c);
			s_chunk_count--;
			s_chunk_free_count++;
			break;
		}
		pp = &c->next;
	}

	/* Allocate new room chunk */
	chunk = (nd_room_chunk_t *)calloc(1, sizeof(nd_room_chunk_t));
	if (!chunk) return NULL;
	chunk->pixels = (uint32_t *)malloc(TILE_PX_W * TILE_PX_H * sizeof(uint32_t));
	if (!chunk->pixels) {
		free(chunk);
		return NULL;
	}
	memcpy(chunk->pos, pos, sizeof(chunk->pos));
	if (nbiomes) {
		memcpy(chunk->nbiomes, nbiomes, sizeof(chunk->nbiomes));
	} else if (own) {
		memset(chunk->nbiomes, own->biome, sizeof(chunk->nbiomes));
	}
	chunk->last_frame = s_current_frame;

	uint8_t own_biome = own ? own->biome : (nbiomes ? nbiomes[4] : 0);
	int has_water = ndw_is_water_ground(ndw_biome_style(own_biome).kind);
	if (nbiomes) {
		for (int k = 0; k < 9; k++) {
			if (ndw_is_water_ground(ndw_biome_style(nbiomes[k]).kind))
				has_water = 1;
		}
	}
	chunk->has_water = (uint8_t)has_water;
	if (has_water) {
		chunk->depth = (uint8_t *)calloc(TILE_PX_W * TILE_PX_H, sizeof(uint8_t));
	}
	s_active_chunk_depth = chunk->depth;

	nd_coast_map_build_if_needed(map);
	uint8_t land_mask = 0;
	uint8_t cgrp[9];
	for (int k = 0; k < 9; k++) {
		uint8_t b = nbiomes ? nbiomes[k] : own_biome;
		ndw_ground_t st_b = ndw_biome_style(b);
		int is_water = ndw_is_water_ground(st_b.kind);
		cgrp[k] = is_water ? 0 : 1;
		if (!is_water)
			land_mask |= (1 << k);
	}
	s_active_chunk_land_mask = land_mask;
	memcpy(s_active_chunk_grp, cgrp, sizeof(s_active_chunk_grp));

	ndw_ground_t g = ndw_biome_style(own_biome);
	int is_water_tile = ndw_is_water_ground(g.kind);

	for (int cy = 0; cy < NDW_ROWS; cy++) {
		for (int cx = 0; cx < NDW_COLS; cx++) {
			int cell_px = cx * NDW_CELL_PX * SCALE;
			int cell_py = cy * NDW_CELL_PX * SCALE;

			if (is_water_tile) {
				/* 1. Render authentic ground substrate first */
				ndw_ground_t substrate_style = { NDW_G_SAND, 128, 128, 128 };
				if (land_mask && nbiomes) {
					/* Find nearest land neighbor for authentic local substrate */
					int px_w = (cx * NDW_CELL_PX + NDW_CELL_PX / 2) * NDW_FINE;
					int py_w = (cy * NDW_CELL_PX + NDW_CELL_PX / 2) * NDW_FINE;
					int max_d = -999999;
					int best_k = 4;
					for (int dj = -1; dj <= 1; dj++) {
						for (int di = -1; di <= 1; di++) {
							int k = (dj + 1) * 3 + (di + 1);
							if (land_mask & (1 << k)) {
								int d = ndw_tile_depth(cgrp, 0, di, dj, px_w, py_w);
								if (d > max_d) { max_d = d; best_k = k; }
							}
						}
					}
					int dist_u = (-max_d) / NDW_FINE;
					if (dist_u < 24)
						substrate_style = ndw_biome_style(nbiomes[best_k]);
				}
				ground_cell(chunk->pixels, TILE_PX_W, TILE_PX_H, &substrate_style, pos, cx, cy,
				            cell_px, cell_py, nd_seven_get_anim());

				/* 2. Composite volumetric water layer over authentic substrate */
				ground_cell(chunk->pixels, TILE_PX_W, TILE_PX_H, &g, pos, cx, cy,
				            cell_px, cell_py, nd_seven_get_anim());
			} else {
				ground_cell(chunk->pixels, TILE_PX_W, TILE_PX_H, &g, pos, cx, cy,
				            cell_px, cell_py, nd_seven_get_anim());
			}
		}
	}
	s_active_chunk_depth = NULL;
	if (own) {
		ground_blend_nbiomes(chunk->pixels, TILE_PX_W, TILE_PX_H, nbiomes, pos, own, 0, 0, chunk->depth);
	}
	s_active_chunk_land_mask = 0;

	chunk->next = s_chunk_buckets[bucket];
	s_chunk_buckets[bucket] = chunk;
	s_chunk_count++;
	s_chunk_alloc_count++;
	return chunk;
}

const nd_room_chunk_t *nd_seven_chunk_ensure(const nd_map_data_t *map, int col, int row)
{
	if (!map || col < 0 || col >= (int)map->width || row < 0 || row >= (int)map->height)
		return NULL;

	nd_coast_map_build_if_needed(map);

	int16_t pos[4];
	pos[0] = (int16_t)(map->pos[0] + (col - (int)map->player_x));
	pos[1] = (int16_t)(map->pos[1] - (row - (int)map->player_y));
	pos[2] = map->pos[2];
	pos[3] = map->pos[3];

	const nd_room_chunk_t *existing = nd_seven_chunk_find(pos);
	if (existing) {
		((nd_room_chunk_t *)existing)->last_frame = s_current_frame;
		return existing;
	}

	int idx = row * (int)map->width + col;
	ndw_tile_t own = nd_seven_convert_tile(&map->tiles[idx]);

	uint8_t nbiomes[9];
	nbiomes[4] = own.biome;
	for (int dj = -1; dj <= 1; dj++) {
		for (int di = -1; di <= 1; di++) {
			int k = (dj + 1) * 3 + (di + 1);
			if (di == 0 && dj == 0) continue;
			int nc = col + di;
			int nr = row + dj;
			if (nc >= 0 && nc < (int)map->width && nr >= 0 && nr < (int)map->height) {
				nbiomes[k] = map->tiles[nr * (int)map->width + nc].bio_idx;
			} else {
				nbiomes[k] = own.biome;
			}
		}
	}

	int ti = col - (int)map->player_x;
	int tj = row - (int)map->player_y;
	return chunk_get_or_create(pos, &own, nbiomes, map, ti, tj);
}

void nd_seven_ground_tile_render(uint32_t *fb, int w, int h, const nd_client_state_t *st,
                                 int ti, int tj, const int16_t pos[4], const ndw_tile_t *own,
                                 int tx, int ty)
{
	if (tx + TILE_PX_W <= 0 || tx >= w || ty + TILE_PX_H <= 0 || ty >= h)
		return;

	if (st) {
		nd_coast_map_build_if_needed(&st->map_data);
	}

	uint8_t nbiomes[9];
	nbiomes[4] = own->biome;

	for (int dj = -1; dj <= 1; dj++) {
		for (int di = -1; di <= 1; di++) {
			int k = (dj + 1) * 3 + (di + 1);
			if (di == 0 && dj == 0) continue;
			ndw_tile_t nt;
			int16_t np[4];
			nd_seven_tile_at(st, ti + di, tj + dj, &nt, np);
			nbiomes[k] = nt.biome;
		}
	}

	nd_room_chunk_t *chunk = chunk_get_or_create(pos, own, nbiomes, st ? &st->map_data : NULL, ti, tj);
	if (!chunk || !chunk->pixels)
		return;

	int src_x0 = 0, src_y0 = 0;
	int dst_x0 = tx, dst_y0 = ty;
	int bw = TILE_PX_W;
	int bh = TILE_PX_H;

	if (dst_x0 < 0) {
		src_x0 -= dst_x0;
		bw += dst_x0;
		dst_x0 = 0;
	}
	if (dst_y0 < 0) {
		src_y0 -= dst_y0;
		bh += dst_y0;
		dst_y0 = 0;
	}
	if (dst_x0 + bw > w)
		bw = w - dst_x0;
	if (dst_y0 + bh > h)
		bh = h - dst_y0;

	if (bw <= 0 || bh <= 0)
		return;

	if (chunk->depth) {
		uint32_t anim = nd_seven_get_anim();
		int base_gx = (int)pos[0] * NDW_TILE_W;
		int base_gy = -(int)pos[1] * NDW_TILE_H;

		for (int r = 0; r < bh; r++) {
			int y_in_tile = (src_y0 + r);
			int gy = base_gy + y_in_tile / SCALE;
			const uint32_t *src_row = &chunk->pixels[y_in_tile * TILE_PX_W + src_x0];
			const uint8_t *depth_row = &chunk->depth[y_in_tile * TILE_PX_W + src_x0];
			uint32_t *dst_row = &fb[(dst_y0 + r) * w + dst_x0];

			for (int c = 0; c < bw; c++) {
				uint8_t d = depth_row[c];
				if (d == 0) {
					dst_row[c] = src_row[c];
					continue;
				}

				int x_in_tile = (src_x0 + c);
				int gx = base_gx + x_in_tile / SCALE;
				uint32_t col = src_row[c];

				int drift = (int)(anim / 3);

				if (d <= 3) {
					/* Dynamic animated caustic shoreline shimmer & breathing surf surge */
					uint32_t c1 = unit_noise(0x43415553u, (gx + drift) / 2, (gy + drift / 2) / 2) & 31;
					uint32_t c2 = unit_noise(0x54494353u, (gx - drift / 2) / 2, (gy + drift) / 2) & 31;
					int caustic = (int)(c1 + c2); /* 0..62 */

					/* Intertidal surf breathing cycle (period = 144 ticks ~ 2.4s) */
					int32_t surf_phase = (int32_t)anim + (int32_t)(gx * 3 + gy * 2) / 8;
					uint32_t surf_cycle = (uint32_t)(((surf_phase % 144) + 144) % 144);
					int surge = (surf_cycle < 60) ? (int)(surf_cycle / 2) : (surf_cycle < 84 ? 30 : (int)((144 - surf_cycle) * 30 / 60));

					if (d == 1) {
						int foam_alpha = 110 + surge * 2 + (caustic > 38 ? (caustic - 38) * 3 : 0);
						if (foam_alpha > 195) foam_alpha = 195;
						col = alpha_blend_rgb(col, RGB(250, 255, 254), foam_alpha);
					} else if (d == 2) {
						int c_alpha = (caustic > 36 ? (caustic - 36) * 3 : 0) + (surge > 12 ? (surge - 12) * 2 : 0);
						if (c_alpha > 85) c_alpha = 85;
						if (c_alpha > 0) {
							col = alpha_blend_rgb(col, RGB(215, 248, 255), c_alpha);
						}
					} else if (d == 3 && caustic > 42) {
						int c_alpha = (caustic - 42) * 2;
						col = alpha_blend_rgb(col, RGB(200, 245, 255), c_alpha);
					}
				}
				if (d >= 8) {
					/* 2D organic ocean swells in deep water (shallows remain crystal clear) */
					int w1 = bnoise(0x4F434541u, gx + drift, gy, 22, 16);
					int w2 = bnoise(0x5357454Cu, gx + drift * 2, gy - drift / 2, 16, 12);
					int h = (w1 * 6 + w2 * 4) / 10;

					/* Vertical wave slope for directional sunlight */
					int w1_up = bnoise(0x4F434541u, gx + drift, gy - 1, 22, 16);
					int w2_up = bnoise(0x5357454Cu, gx + drift * 2, gy - 1 - drift / 2, 16, 12);
					int h_up = (w1_up * 6 + w2_up * 4) / 10;
					int slope_y = h - h_up; /* -18..+18 */

					int swell_scale = (d <= 16) ? (d - 8) * 255 / 8 : 255;

					/* Wave crest highlights */
					if (slope_y > 4) {
						int spec = ((slope_y - 4) * 8 * swell_scale) / 255;
						if (spec > 130) spec = 130;
						uint32_t highlight = (d <= 16) ? RGB(115, 195, 250) : RGB(85, 165, 240);
						col = alpha_blend_rgb(col, highlight, spec);
					}
					/* Wave trough shadows */
					else if (slope_y < -4) {
						int shadow = ((-slope_y - 4) * 6 * swell_scale) / 255;
						if (shadow > 90) shadow = 90;
						col = alpha_blend_rgb(col, RGB(8, 22, 65), shadow);
					}
				}
				dst_row[c] = col;
			}
		}
	} else {
		for (int r = 0; r < bh; r++) {
			const uint32_t *src_row = &chunk->pixels[(src_y0 + r) * TILE_PX_W + src_x0];
			uint32_t *dst_row = &fb[(dst_y0 + r) * w + dst_x0];
			memcpy(dst_row, src_row, (size_t)bw * sizeof(uint32_t));
		}
	}
}
