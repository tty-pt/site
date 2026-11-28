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
uint32_t nd_seven_ground_forget_seq(void);
int nd_seven_pos_in_map(const int16_t pos[4], const nd_map_data_t *map);

uint32_t water_color_at_depth(int depth, int r, int gg, int b);
int water_alpha_at_depth(int depth);
uint32_t ndw_biome_base_rgb(unsigned biome);
uint32_t alpha_blend_rgb(uint32_t bg, uint32_t fg, int alpha);
uint32_t shade_rgb(uint32_t col, int shade256);

const ndw_warp_grid_t *nd_seven_warp_grid(const int16_t pos[4]);

int16_t nd_seven_ground_elevation(int32_t wx, int32_t wy);
int32_t nd_seven_ground_elevation_q12_4(int32_t wx, int32_t wy);
float nd_seven_ground_elevation_f(int32_t wx, int32_t wy);

/* Camera zoom controls */
void nd_seven_set_zoom(float zoom);
void nd_seven_zoom_to(float zoom);
float nd_seven_get_zoom(void);
float nd_seven_get_zoom_target(void);
void nd_seven_zoom_in(void);
void nd_seven_zoom_out(void);

#ifndef ND_SEVEN_CAM_PITCH_MIN
#define ND_SEVEN_CAM_PITCH_MIN      10.0f
#endif
#ifndef ND_SEVEN_CAM_PITCH_MAX
#define ND_SEVEN_CAM_PITCH_MAX      85.0f
#endif
#ifndef ND_SEVEN_DIORAMA_PITCH
#define ND_SEVEN_DIORAMA_PITCH      55.0f
#endif
#ifndef ND_SEVEN_ZELDA_PITCH
#define ND_SEVEN_ZELDA_PITCH        35.0f
#endif
#ifndef ND_SEVEN_ZELDA_ZOOM
#define ND_SEVEN_ZELDA_ZOOM         1.65f
#endif

/* Free 3D orbit camera controls (yaw in [-180..180), pitch in [10..85] deg) */
void  nd_seven_set_cam_angles(float yaw_deg, float pitch_deg);
void  nd_seven_orbit_cam(float dyaw_deg, float dpitch_deg);
void  nd_seven_reset_cam(void);
void  nd_seven_cam_recenter_behind(float lamb_yaw_rad);
void  nd_seven_cam_auto_follow(float lamb_yaw_rad, float strength, float dt);
float nd_seven_cam_target_z_lift(float eff_cam_dist);
float nd_seven_get_yaw_deg(void);
float nd_seven_get_pitch_deg(void);
void  nd_seven_step_cam(float dt);
float nd_seven_clamp_cam_pitch(uint16_t world, float tx, float ty, float tz,
                               float dist, float yaw_deg, float pitch_deg);
float nd_seven_eval_effective_pitch(float base_pitch_deg, float eff_cam_dist);
int   nd_seven_has_pitch_override(void);

/* Local player avatar form cycle (0 = lamb default, 1..N-1 = character presets) */
int         nd_seven_player_form(void);
int         nd_seven_player_form_count(void);
const char *nd_seven_player_form_name(int form);
void        nd_seven_set_player_form(int form);
int         nd_seven_cycle_player_form(int delta);
void        nd_seven_player_form_metrics(int form, float *cam_lift_wu,
                                         float *light_lift_wu, float *swim_depth_wu);
int         nd_seven_player_is_human(void);
void        nd_seven_set_player_human(int human);
int         nd_seven_toggle_player_form(void);

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
void nd_seven_set_msaa_samples(int samples);
int  nd_seven_get_msaa_samples(void);
void nd_seven_toggle_msaa(void);
void nd_seven_cycle_msaa(void);

void nd_seven_set_fps_cap(int fps);
int  nd_seven_get_fps_cap(void);
void nd_seven_cycle_fps_cap(void);

void nd_seven_apply_quality_preset(int preset);
int  nd_seven_get_quality_preset(void);
void nd_seven_cycle_quality_preset(void);

/* Unified continuous altitude profile evaluated in log2(cam_dist) space.
 * Eliminates disjoint step-function cliffs and aligns CPU/GPU cutoffs. */
typedef struct {
	float    target_quad_px;   /* U-shaped: crisp close -> lean mid-zoom -> crisp orbit */
	float    eff_res_scale;    /* Smoothly relaxes base_res_scale -> 1.0 in orbit */
	float    eff_cloud_scale;  /* Relative to NATIVE viewport (never compounded with res_scale) */
	float    grass_max_dist;   /* Shared CPU/VS grass cutoff (1450 wu) */
	float    ssao_max_dist;    /* Shared CPU/FS SSAO cutoff (2200 wu) */
	float    eco_detail_fade;  /* Smooth 1 -> 0 across 2500..6500 wu */
	uint8_t  min_level;        /* 0: continuous target_quad_px + hysteresis drives splits */
	uint16_t chunk_budget;     /* Incremental libareole populate budget per frame */
} nd_seven_alt_profile_t;

void nd_seven_eval_alt_profile(float cam_dist, float detail_scale,
                               float base_res_scale, int cloud_quality,
                               nd_seven_alt_profile_t *out);

void nd_seven_ground_perf_set(int enabled);
uint32_t nd_seven_ground_perf_take_probes(void);

/* Phase 6C Piece 3: Closed-loop Dynamic Resolution Scaling (DRS) controller.
 * Scales render_w/h sub-rect (zero FBO realloc via uSceneUvScale) between [min_scale, max_scale]
 * using frame-time EMA hysteresis + immediate fast-motion downscale + 250 ms settle recovery.
 * Opt-in (enabled = 0 by default) so fixed quality presets and deterministic tests stay exact. */
typedef struct {
	int     enabled;        /* 0 = off (1.00x multiplier), 1 = active */
	float   target_ms;      /* nominal budget (default 16.67 ms) */
	float   hi_ms;          /* downscale threshold (default 18.0 ms) */
	float   lo_ms;          /* upscale threshold (default 13.0 ms) */
	float   min_scale;      /* floor multiplier (default 0.75) */
	float   max_scale;      /* ceiling multiplier (default 1.00) */
	float   step;           /* hysteresis step (default 0.05) */
	float   cur_scale;      /* active output scale in [min_scale, max_scale] */
	float   ema_ms;         /* frame time EMA (ms) */
	float   cooldown_s;     /* holdoff between steady-state steps (s) */
	float   settle_s;       /* continuous stationary time since camera motion (s) */
	float   last_cam_x;
	float   last_cam_y;
	float   last_cam_dist;
	float   last_cam_yaw;
	float   last_cam_pitch;
	uint8_t has_pose;
	uint8_t in_motion;
} nd_seven_drs_t;

void  nd_seven_drs_init(nd_seven_drs_t *drs);
float nd_seven_drs_update(nd_seven_drs_t *drs, float dt_s,
                          float cam_x, float cam_y, float cam_dist,
                          float cam_yaw, float cam_pitch);
void  nd_seven_set_drs(int enabled);
int   nd_seven_get_drs(void);
void  nd_seven_toggle_drs(void);
float nd_seven_get_drs_scale(void);
void  nd_seven_drs_feed_frame(float dt_s, float cam_x, float cam_y,
                              float cam_dist, float cam_yaw, float cam_pitch);

#endif /* ND_SEVEN_GROUND_H */
