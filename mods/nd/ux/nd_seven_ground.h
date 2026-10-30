#ifndef ND_SEVEN_GROUND_H
#define ND_SEVEN_GROUND_H

#include <stdint.h>
#include <stddef.h>
#include <nd/seven-world.h>
#include "nd/bcp.h"

void ground_cell(uint32_t *fb, int w, int h, const ndw_ground_t *g,
                 const int16_t pos[4], int cx, int cy, int px, int py, uint32_t anim);

void ground_blend(uint32_t *fb, int w, int h, const nd_client_state_t *st,
                  int ti, int tj, const int16_t pos[4], const ndw_tile_t *own,
                  int tx, int ty);

const ndw_warp_grid_t *nd_seven_warp_grid(const int16_t pos[4]);

#endif /* ND_SEVEN_GROUND_H */
