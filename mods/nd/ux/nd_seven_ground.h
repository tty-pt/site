#ifndef ND_SEVEN_GROUND_H
#define ND_SEVEN_GROUND_H

#include <stdint.h>
#include <stddef.h>
#include <nd/seven-world.h>
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
	uint32_t last_frame;
	uint32_t *pixels;
	uint8_t *depth;
	int16_t *height;
	struct nd_room_chunk *next;
} nd_room_chunk_t;

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
#define ND_CHUNK_BUILDS_PER_TICK 2
void nd_seven_ground_build_budget(int n);
void nd_seven_ground_frame_begin(void);
void nd_seven_ground_frame_end(const nd_map_data_t *map);
int nd_seven_chunk_count(void);
int nd_seven_chunk_alloc_count(void);
int nd_seven_chunk_free_count(void);
int nd_seven_pos_in_map(const int16_t pos[4], const nd_map_data_t *map);
const nd_room_chunk_t *nd_seven_chunk_find(const int16_t pos[4]);
const nd_room_chunk_t *nd_seven_chunk_ensure(const nd_map_data_t *map, int col, int row);

uint32_t water_color_at_depth(int depth, int r, int gg, int b);
uint32_t ndw_biome_base_rgb(unsigned biome);
uint32_t alpha_blend_rgb(uint32_t bg, uint32_t fg, int alpha);
uint32_t shade_rgb(uint32_t col, int shade256);

const ndw_warp_grid_t *nd_seven_warp_grid(const int16_t pos[4]);

#endif /* ND_SEVEN_GROUND_H */
