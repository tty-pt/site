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
void nd_seven_ground_frame_begin(void);
void nd_seven_ground_frame_end(const nd_map_data_t *map);
int nd_seven_chunk_count(void);
int nd_seven_chunk_alloc_count(void);
int nd_seven_chunk_free_count(void);
int nd_seven_pos_in_map(const int16_t pos[4], const nd_map_data_t *map);
const nd_room_chunk_t *nd_seven_chunk_find(const int16_t pos[4]);
const nd_room_chunk_t *nd_seven_chunk_ensure(const nd_map_data_t *map, int col, int row);

const ndw_warp_grid_t *nd_seven_warp_grid(const int16_t pos[4]);

#endif /* ND_SEVEN_GROUND_H */
