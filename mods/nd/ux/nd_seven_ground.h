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

void nd_seven_ground_cache_forget(void);
int nd_seven_pos_in_map(const int16_t pos[4], const nd_map_data_t *map);

uint32_t water_color_at_depth(int depth, int r, int gg, int b);
int water_alpha_at_depth(int depth);
uint32_t ndw_biome_base_rgb(unsigned biome);
uint32_t alpha_blend_rgb(uint32_t bg, uint32_t fg, int alpha);
uint32_t shade_rgb(uint32_t col, int shade256);

const ndw_warp_grid_t *nd_seven_warp_grid(const int16_t pos[4]);

int16_t nd_seven_ground_elevation(int32_t wx, int32_t wy);
int16_t nd_seven_ground_elevation_q12_4(int32_t wx, int32_t wy);
float nd_seven_ground_elevation_f(int32_t wx, int32_t wy);

/* Camera zoom controls */
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
