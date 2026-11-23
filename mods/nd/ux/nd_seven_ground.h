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

#ifndef ND_SEVEN_CAM_PITCH_MIN
#define ND_SEVEN_CAM_PITCH_MIN      10.0f
#endif
#ifndef ND_SEVEN_CAM_PITCH_MAX
#define ND_SEVEN_CAM_PITCH_MAX      85.0f
#endif

/* Free 3D orbit camera controls (yaw in [-180..180), pitch in [10..85] deg) */
void  nd_seven_set_cam_angles(float yaw_deg, float pitch_deg);
void  nd_seven_orbit_cam(float dyaw_deg, float dpitch_deg);
void  nd_seven_reset_cam(void);
float nd_seven_get_yaw_deg(void);
float nd_seven_get_pitch_deg(void);
void  nd_seven_step_cam(float dt);
float nd_seven_clamp_cam_pitch(uint16_t world, float tx, float ty, float tz,
                               float dist, float yaw_deg, float pitch_deg);

/* User-adjustable billboard detail / draw distance scale (0.20f .. 2.50f, default 1.0f) */
void nd_seven_set_detail_scale(float scale);
float nd_seven_get_detail_scale(void);
void nd_seven_detail_in(void);
void nd_seven_detail_out(void);

#ifndef ND_SEVEN_DETAIL_MIN
#define ND_SEVEN_DETAIL_MIN         0.20f
#endif
#ifndef ND_SEVEN_DETAIL_MAX
#define ND_SEVEN_DETAIL_MAX         2.50f
#endif
#ifndef ND_SEVEN_DETAIL_STEP
#define ND_SEVEN_DETAIL_STEP        1.25f
#endif

#ifndef ND_SEVEN_ZOOM_MIN
#define ND_SEVEN_ZOOM_MIN           0.00001f
#endif
#ifndef ND_SEVEN_ZOOM_MAX
#define ND_SEVEN_ZOOM_MAX           8.0f
#endif

/* Returns the adaptive Living Meadow lattice stride (in world units, multiple of 26)
 * for a given camera distance and detail scale, or 0 when outside the meadow cull range. */
int32_t nd_seven_meadow_lattice_step(float cam_dist, float max_meadow_cull);

#ifndef ND_SEVEN_RES_MIN
#define ND_SEVEN_RES_MIN            0.25f
#endif
#ifndef ND_SEVEN_RES_MAX
#define ND_SEVEN_RES_MAX            1.00f
#endif
#ifndef ND_SEVEN_RES_STEP
#define ND_SEVEN_RES_STEP           0.05f
#endif

#ifndef ND_CLOUD_QUALITY_OFF
#define ND_CLOUD_QUALITY_OFF        0
#define ND_CLOUD_QUALITY_LOW        1
#define ND_CLOUD_QUALITY_MEDIUM     2
#define ND_CLOUD_QUALITY_HIGH       3
#endif

#ifndef ND_QUALITY_LOW
#define ND_QUALITY_LOW              0
#define ND_QUALITY_MEDIUM           1
#define ND_QUALITY_HIGH             2
#define ND_QUALITY_CUSTOM           3
#endif

void nd_seven_set_res_scale(float scale);
float nd_seven_get_res_scale(void);
void nd_seven_res_in(void);
void nd_seven_res_out(void);
void nd_seven_scaled_dims(uint32_t w, uint32_t h, uint32_t *out_w, uint32_t *out_h);

void nd_seven_set_cloud_quality(int quality);
int  nd_seven_get_cloud_quality(void);
void nd_seven_cycle_cloud_quality(void);

void nd_seven_set_msaa(int enabled);
int  nd_seven_get_msaa(void);
void nd_seven_toggle_msaa(void);

void nd_seven_set_fps_cap(int fps);
int  nd_seven_get_fps_cap(void);
void nd_seven_cycle_fps_cap(void);

void nd_seven_apply_quality_preset(int preset);
int  nd_seven_get_quality_preset(void);
void nd_seven_cycle_quality_preset(void);

#endif /* ND_SEVEN_GROUND_H */
