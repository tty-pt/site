#include "nd_seven_ground.h"
#include "nd_seven_atlas_draw.h"
#include "nd_seven_motion.h"
#include <ttypt/areole.h>
#include <nd/field.h>
#include <string.h>

__attribute__((weak)) ar_system_t *nd_seven_get_chunk_sys(void) { return NULL; }
__attribute__((weak)) void ar_system_clear(ar_system_t *cs) { (void)cs; }
__attribute__((weak)) bool ar_system_probe_view(ar_system_t *cs, uint8_t view_id, uint16_t world, int32_t wx, int32_t wy, ar_sample_t *out)
{
	(void)cs; (void)view_id; (void)world; (void)wx; (void)wy; (void)out;
	return false;
}

uint32_t alpha_blend_rgb(uint32_t bg, uint32_t fg, int alpha)
{
	if (alpha <= 0) return bg;
	if (alpha >= 256) return fg;
	uint32_t inv = (uint32_t)(256 - alpha);
	uint32_t a   = (uint32_t)alpha;
	uint32_t rb  = (((bg & 0x00FF00FFu) * inv + (fg & 0x00FF00FFu) * a) >> 8) & 0x00FF00FFu;
	uint32_t g   = (((bg & 0x0000FF00u) * inv + (fg & 0x0000FF00u) * a) >> 8) & 0x0000FF00u;
	return 0xFF000000u | rb | g;
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

uint32_t water_color_at_depth(int depth, int r, int gg, int b)
{
	static const uint32_t C_SHALLOW = RGB(64, 218, 235);
	static const uint32_t C_SHELF   = RGB(52, 178, 228);
	static const uint32_t C_OUTER   = RGB(36, 122, 202);
	static const uint32_t C_ABYSS   = RGB(14, 50, 135);
	const int shallow_max = 4;
	const int shelf_max   = 11;
	const int max_depth   = 28;

	uint32_t water_col;
	if (depth <= shallow_max) {
		water_col = lerp_rgb(C_SHALLOW, C_SHELF, depth - 1, shallow_max - 1);
	} else if (depth <= shelf_max) {
		water_col = lerp_rgb(C_SHELF, C_OUTER, depth - shallow_max, shelf_max - shallow_max);
	} else {
		int t = depth - shelf_max;
		int span = max_depth - shelf_max;
		if (t > span) t = span;
		water_col = lerp_rgb(C_OUTER, C_ABYSS, t, span);
	}
	return tint_col(water_col, r, gg, b);
}

uint32_t shade_rgb(uint32_t col, int shade256)
{
	if (shade256 == 256) return col;
	int r = (int)(col & 0xFF) * shade256 / 256;
	int g = (int)((col >> 8) & 0xFF) * shade256 / 256;
	int b = (int)((col >> 16) & 0xFF) * shade256 / 256;
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	return RGB(r, g, b);
}

int water_alpha_at_depth(int depth)
{
	static const uint8_t s_beer_lambert_alpha[33] = {
		0,
		32,  48,  64,  82, 102, 124, 146, 168, 188, 206,
		220, 232, 240, 246, 250, 252, 253, 254, 254, 255,
		255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		255, 255
	};
	if (depth <= 0) return 0;
	if (depth >= 28) return 255;
	return (int)s_beer_lambert_alpha[depth];
}

static uint32_t ndw_ground_base_rgb(const ndw_ground_t *g)
{
	int r, gg, b;
	switch (g->kind) {
	case NDW_G_WATER:
	case NDW_G_SHALLOW_WATER:
	case NDW_G_SAND:   r = 226; gg = 200; b = 140; break;
	case NDW_G_SNOW:   r = 248; gg = 252; b = 255; break;
	case NDW_G_TUNDRA: r = 216; gg = 224; b = 236; break;
	case NDW_G_ASH:    r = 54;  gg = 48;  b = 52;  break;
	default:           r = 78;  gg = 128; b = 60;  break;
	}
	r = r * g->r / 128; gg = gg * g->g / 128; b = b * g->b / 128;
	if (r > 255) r = 255;
	if (gg > 255) gg = 255;
	if (b > 255) b = 255;
	return RGB(r, gg, b);
}

uint32_t ndw_biome_base_rgb(unsigned biome)
{
	ndw_ground_t g = ndw_biome_style(biome);
	return ndw_ground_base_rgb(&g);
}

#define BORDER_CACHE 64
typedef struct {
	int16_t pos[4];
	uint8_t used;
	ndw_warp_grid_t grid;
} border_ent_t;
static border_ent_t s_border[BORDER_CACHE];
static unsigned s_border_next;

const ndw_warp_grid_t *nd_seven_warp_grid(const int16_t pos[4])
{
	for (int i = 0; i < BORDER_CACHE; i++) {
		if (s_border[i].used && !memcmp(s_border[i].pos, pos, sizeof(s_border[i].pos)))
			return &s_border[i].grid;
	}
	border_ent_t *e = &s_border[s_border_next++ % BORDER_CACHE];
	memcpy(e->pos, pos, sizeof(e->pos));
	e->used = 1;
	ndw_warp_grid_fill(&e->grid, pos);
	return &e->grid;
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

static float s_camera_zoom = 1.0f;

void nd_seven_set_zoom(float zoom)
{
	if (zoom < ND_SEVEN_ZOOM_MIN) zoom = ND_SEVEN_ZOOM_MIN;
	if (zoom > ND_SEVEN_ZOOM_MAX) zoom = ND_SEVEN_ZOOM_MAX;
	if (s_camera_zoom != zoom) {
		s_camera_zoom = zoom;
	}
}

float nd_seven_get_zoom(void)
{
	return s_camera_zoom;
}

void nd_seven_zoom_in(void)
{
	nd_seven_set_zoom(s_camera_zoom * 1.15f);
}

void nd_seven_zoom_out(void)
{
	nd_seven_set_zoom(s_camera_zoom / 1.15f);
}

void nd_seven_ground_cache_forget(void)
{
	memset(s_border, 0, sizeof(s_border));
	ar_system_t *cs = nd_seven_get_chunk_sys();
	if (cs) {
		ar_system_clear(cs);
	}
}

int16_t nd_seven_ground_elevation_q12_4(int32_t wx, int32_t wy)
{
	uint16_t world = (uint16_t)nd_seven_get_world();
	ar_system_t *cs = nd_seven_get_chunk_sys();
	if (cs) {
		ar_sample_t s;
		if (ar_system_probe_view(cs, 0, world, wx, wy, &s))
			return s.val;
	}

	int32_t cx = morton_fdiv(wx, NDW_TILE_W);
	int32_t cy = morton_fdiv(wy, NDW_TILE_H);
	int32_t ox = cx * NDW_TILE_W;
	int32_t oy = cy * NDW_TILE_H;
	const int gdim = 48;

	float fx = ((float)(wx - ox) / (float)NDW_TILE_W) * (float)gdim;
	float fy = ((float)(wy - oy) / (float)NDW_TILE_H) * (float)gdim;
	int col0 = (int)fx;
	int row0 = (int)fy;
	if (col0 < 0) col0 = 0;
	if (col0 >= gdim) col0 = gdim - 1;
	if (row0 < 0) row0 = 0;
	if (row0 >= gdim) row0 = gdim - 1;
	int col1 = col0 + 1;
	int row1 = row0 + 1;

	float u = fx - (float)col0;
	float v = fy - (float)row0;
	float wu = u * u * (3.0f - 2.0f * u);
	float wv = v * v * (3.0f - 2.0f * v);

	int32_t x0 = ox + (int32_t)(((int64_t)col0 * NDW_TILE_W) / gdim);
	int32_t x1 = ox + (int32_t)(((int64_t)col1 * NDW_TILE_W) / gdim);
	int32_t y0 = oy + (int32_t)(((int64_t)row0 * NDW_TILE_H) / gdim);
	int32_t y1 = oy + (int32_t)(((int64_t)row1 * NDW_TILE_H) / gdim);
	float v00 = (float)nd_field_sample(world, 3, x0, y0).elevation_q12_4;
	float v10 = (float)nd_field_sample(world, 3, x1, y0).elevation_q12_4;
	float v01 = (float)nd_field_sample(world, 3, x0, y1).elevation_q12_4;
	float v11 = (float)nd_field_sample(world, 3, x1, y1).elevation_q12_4;

	float h0 = v00 * (1.0f - wu) + v10 * wu;
	float h1 = v01 * (1.0f - wu) + v11 * wu;
	float h  = h0 * (1.0f - wv) + h1 * wv;
	return (int16_t)(h >= 0.0f ? (h + 0.5f) : (h - 0.5f));
}

int16_t nd_seven_ground_elevation(int32_t wx, int32_t wy)
{
	return nd_field_elevation_to_block(nd_seven_ground_elevation_q12_4(wx, wy));
}

float nd_seven_ground_elevation_f(int32_t wx, int32_t wy)
{
	int16_t q = nd_seven_ground_elevation_q12_4(wx, wy);
	return (float)q / 16.0f;
}
