#ifndef ND_SEVEN_GROUND_H
#define ND_SEVEN_GROUND_H

#include <stdint.h>
#include <stddef.h>
#include <nd/seven-world.h>
#include <nd/field.h>
#include "nd/bcp.h"

#ifndef SCALE
#define SCALE 2
#endif

#define TILE_PX_W (NDW_TILE_W * SCALE)
#define TILE_PX_H (NDW_TILE_H * SCALE)

typedef struct nd_room_chunk {
	int16_t pos[4];
	uint8_t nbiomes[9];
	uint8_t has_water;
	uint8_t lod; /* 1 = 16 units, 2 = 4 units, 3 = 1 unit */
	uint16_t span_tiles; /* physical extent in room tiles (1, 2, 4, 8) */
	uint32_t rev;
	uint32_t last_frame;
	uint32_t last_anim;
	uint32_t *pixels;
	uint32_t *substrate;
	uint8_t *depth;
	int16_t *height;
	struct nd_room_chunk *next;
} nd_room_chunk_t;

static inline uint16_t nd_seven_lod_span_tiles(uint8_t lod)
{
	switch (lod) {
	case 3:  return 1;
	case 2:  return 2;
	case 1:  return 4;
	default: return 8;
	}
}

void ground_cell(uint32_t *fb, int w, int h, const ndw_ground_t *g,
                 const int16_t pos[4], int cx, int cy, int px, int py, uint32_t anim);

void ground_blend(uint32_t *fb, int w, int h, const nd_client_state_t *st,
                  int ti, int tj, const int16_t pos[4], const ndw_tile_t *own,
                  int tx, int ty);

void nd_seven_ground_tile_render(uint32_t *fb, int w, int h, const nd_client_state_t *st,
                                 int ti, int tj, const int16_t pos[4], const ndw_tile_t *own,
                                 int tx, int ty);
void nd_seven_ground_cache_forget(void);

/* Chunk build budget. Building a chunk is expensive, so the client tick
 * allows at most ND_CHUNK_BUILDS_PER_TICK new chunks per tick; tiles whose
 * chunk is not built yet draw a flat placeholder. n < 0 = unlimited (the
 * default, used by native renders and tests). */
#define ND_CHUNK_BUILDS_PER_TICK 32
void nd_seven_ground_build_budget(int n);
void nd_seven_ground_frame_begin(void);
void nd_seven_ground_frame_end(const nd_map_data_t *map);
int nd_seven_chunk_count(void);
int nd_seven_chunk_alloc_count(void);
int nd_seven_chunk_free_count(void);
int nd_seven_pos_in_map(const int16_t pos[4], const nd_map_data_t *map);
const nd_room_chunk_t *nd_seven_chunk_find(const int16_t pos[4]);
const nd_room_chunk_t *nd_seven_chunk_ensure(const nd_map_data_t *map, int col, int row);
const nd_room_chunk_t *nd_seven_chunk_get_or_create(const int16_t pos[4], uint16_t span, uint8_t lod,
                                                    const nd_client_state_t *st, int ti, int tj);
uint8_t nd_seven_calc_target_lod(int ti, int tj, float zoom, int is_3d);

const nd_field_chunk_t *nd_seven_field_chunk_get(uint16_t world, int32_t ox, int32_t oy);

uint32_t water_color_at_depth(int depth, int r, int gg, int b);
int water_alpha_at_depth(int depth);
uint32_t ndw_biome_base_rgb(unsigned biome);
uint32_t alpha_blend_rgb(uint32_t bg, uint32_t fg, int alpha);
uint32_t shade_rgb(uint32_t col, int shade256);

const ndw_warp_grid_t *nd_seven_warp_grid(const int16_t pos[4]);

int16_t nd_seven_ground_elevation(int32_t wx, int32_t wy);
int16_t nd_seven_ground_elevation_q12_4(int32_t wx, int32_t wy);
float nd_seven_ground_elevation_f(int32_t wx, int32_t wy);
const int16_t *nd_seven_chunk_height(const nd_room_chunk_t *chunk);

uint32_t water_color_at_depth(int depth, int r, int gg, int b);

/* 3D mode and zoom controls */
void nd_seven_set_3d_mode(int enable);
int nd_seven_get_3d_mode(void);
void nd_seven_set_zoom(float zoom);
float nd_seven_get_zoom(void);
void nd_seven_zoom_in(void);
void nd_seven_zoom_out(void);

#ifndef ND_SEVEN_ZOOM_MIN
#define ND_SEVEN_ZOOM_MIN           0.00001f
#endif
#ifndef ND_SEVEN_ZOOM_MAX
#define ND_SEVEN_ZOOM_MAX           8.0f
#endif

#endif /* ND_SEVEN_GROUND_H */
