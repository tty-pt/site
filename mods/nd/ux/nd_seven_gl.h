#ifndef ND_SEVEN_GL_H
#define ND_SEVEN_GL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "nd_seven.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cloud-shadow response (shared by terrain, water and sprite shaders; stringified into GLSL):
 * direct sun lost under full cloud shadow, and neutral ambient lost. Under thick overcast the lit
 * ground keeps about 64% of its sunlit brightness instead of reading as night. */
#define ND_CLOUD_SHADOW_DIRECT_LOSS  0.58
#define ND_CLOUD_SHADOW_AMBIENT_LOSS 0.20

/* CPU mirror of GLSL nd_cloud_shadow_map after the map fetch: density on the cloud slice -> shadow strength. */
float nd_seven_gl_cloud_shadow_from_density(float density, float frag_z);

/* CPU mirror of the shader lighting: ambient 0.45 + direct 0.55 * diffuse, attenuated by cloud shadow s. */
float nd_seven_gl_scene_light(float cloud_shadow, float diffuse);

/* Cloud-shadow map window: the R8 map (ND_CSM_RES^2) covers a power-of-two half-extent around the camera target on the
 * cloud slice and is snapped to whole texels so panning never shimmers. Returns false (no map) above 180 km, where the
 * analytic shadow evaluation is used. */
#define ND_CSM_RES 128
typedef struct {
	float cx, cy;   /* window centre on the slice (world XY, texel-snapped) */
	float half;     /* half-extent in world units (power of two) */
} nd_seven_gl_csm_window_t;
bool nd_seven_gl_csm_window(float dist, float tx, float ty, nd_seven_gl_csm_window_t *out);

/* Billboard shading / material style hints */
#define ND_BB_STYLE_STANDARD  0u  /* General sprite / entity / lamb */
#define ND_BB_STYLE_LEAF_TINT 1u  /* Flora with leaf-selective plant_tint in tint_rgba.rgb */
#define ND_BB_STYLE_FLORA     2u  /* Flora with full-sprite tint */
#define ND_BB_STYLE_MINERAL   3u  /* Rock / stone / boulder mineral scenery */
#define ND_BB_STYLE_GLOW      4u  /* Subtle emissive universe-owner lamb glow */
#define ND_BB_STYLE_GRASS     5u  /* Ground-pinned reactive grass / reed / flower tuft */
#define ND_BB_STYLE_PARTICLE  6u  /* Volumetric sunlit water droplet / sea-spray mist particle */

typedef struct lam_billboard_gpu {
	float x, y, z;
	float width, height;
	float anchor_y;     /* downward screen-Y shift from bottom of quad (0 = bottom at world pos) */
	float water_z_off;  /* offset from bottom of quad to physical ground/water contact line */
	float u0, v0, u1, v1;
	uint32_t tint_rgba;
	uint8_t style;      /* ND_BB_STYLE_* material/shading hint (0 = standard) */
	uint8_t shadow_rx;  /* 3D ground contact shadow X radius in world units (0 = auto from width) */
	uint8_t shadow_ry;  /* 3D ground contact shadow Y radius in world units (0 = auto from width) */
	uint8_t pad0;
	uint8_t mesh_id;    /* 3D procedural mesh template ID (0 = 2D billboard, 1..64 = plant, 65..115 = stone) */
} lam_billboard_gpu_t;

/* 20 floats (80 bytes) per billboard vertex:
 * world_pos[3], uv[2], tint[4], local_pos[3], quad_uv[4], sprite_bounds[4] */
typedef struct {
	float world_pos[3];
	float uv[2];
	float tint[4];
	float local_pos[3];
	float quad_uv[4];       /* x: [-1..1], y: [0..1] (or [-1..1] for shadow), z: ground_z, w: style (-1 = shadow) */
	float sprite_bounds[4]; /* u_min, v_min, u_max, v_max in atlas UV space */
} nd_seven_gl_bb_vert_t;

/* 28 floats (112 bytes) per instanced billboard quad (streamed via glDrawArraysInstanced):
 * 4 corners in world space, uv bounds, tint, and local entity params */
typedef struct {
	float pos0[4];  /* Corner 0 (BL): world_x, world_y, world_z, local_dx */
	float pos1[4];  /* Corner 1 (BR): world_x, world_y, world_z, local_dx */
	float pos2[4];  /* Corner 2 (TL): world_x, world_y, world_z, local_dx */
	float pos3[4];  /* Corner 3 (TR): world_x, world_y, world_z, local_dx */
	float uv[4];    /* u0, v0, u1, v1 in atlas */
	float tint[4];  /* r, g, b, a */
	float param[4]; /* local_base_x, local_base_y, local_base_z, style */
} nd_seven_gl_bb_inst_t;

/* Pure-C Torus Orthonormal Frame & Position Evaluation:
 * Evaluates 3D torus world position and unit basis vectors:
 *   out_E = East tangent (+wx)
 *   out_S = South tangent (+wy)
 *   out_N = Zenith normal (+wz) */
void nd_seven_gl_torus_frame(float wx, float wy, float wz,
                             const nd_planet_def_t *pdef,
                             float out_pos[3],
                             float out_E[3],
                             float out_S[3],
                             float out_N[3]);

/* Pure-C 3D Torus Camera MVP Matrix:
 * Matches the left-handed camera on the curved torus at arbitrary (yaw_deg, pitch_deg). */
void nd_seven_gl_compute_mvp_ex(uint32_t vp_w, uint32_t vp_h,
                                float tx, float ty, float tz,
                                float dist, float yaw_deg, float pitch_deg, float fov_deg,
                                const nd_planet_def_t *pdef,
                                float out_mvp[16],
                                float out_eye[3],
                                float out_target[3],
                                float out_up[3]);

void nd_seven_gl_compute_mvp(uint32_t vp_w, uint32_t vp_h,
                             float tx, float ty, float tz,
                             float dist, float pitch_deg, float fov_deg,
                             const nd_planet_def_t *pdef,
                             float out_mvp[16],
                             float out_eye[3],
                             float out_target[3],
                             float out_up[3]);

/* Pure-C Flat Tangent-Space Camera MVP Matrix for libareole quadtree culling:
 * Uses identical left-handed convention (ar_mat4_lookat_lh) and
 * dynamic near/far depth planes. */
void nd_seven_gl_compute_flat_mvp_ex(uint32_t vp_w, uint32_t vp_h,
                                     float tx, float ty, float tz,
                                     float dist, float yaw_deg, float pitch_deg, float fov_deg,
                                     float out_mvp[16]);

void nd_seven_gl_compute_flat_mvp(uint32_t vp_w, uint32_t vp_h,
                                  float tx, float ty, float tz,
                                  float dist, float pitch_deg, float fov_deg,
                                  float out_mvp[16]);

/* Camera-Facing Billboard Basis & 6-Vertex Quad Construction:
 * Rotates the billboard quad around Zenith by yaw_deg and tilts by pitch_deg
 * so the quad faces the orbiting camera with 1:1 projected aspect ratio. */
void nd_seven_gl_billboard_basis_ex(float wx, float wy, float wz,
                                    float yaw_deg, float pitch_deg,
                                    const nd_planet_def_t *pdef,
                                    float out_base_pos[3],
                                    float out_right[3],
                                    float out_up[3],
                                    float out_view_dir[3]);

void nd_seven_gl_billboard_basis(float wx, float wy, float wz,
                                 float pitch_deg,
                                 const nd_planet_def_t *pdef,
                                 float out_base_pos[3],
                                 float out_right[3],
                                 float out_up[3],
                                 float out_view_dir[3]);

void nd_seven_gl_eval_billboard_quad_ex(const lam_billboard_gpu_t *b,
                                        float yaw_deg, float pitch_deg,
                                        const nd_planet_def_t *pdef,
                                        nd_seven_gl_bb_vert_t out_verts[6]);

void nd_seven_gl_eval_billboard_quad(const lam_billboard_gpu_t *b,
                                     float pitch_deg,
                                     const nd_planet_def_t *pdef,
                                     nd_seven_gl_bb_vert_t out_verts[6]);

/* 3D Ground-Contact Shadow Quad Construction:
 * Evaluates a 6-vertex tangent-plane shadow quad beneath the billboard, shifted
 * slightly Southeast away from the Northwest directional sun. */
void nd_seven_gl_eval_billboard_shadow_quad(const lam_billboard_gpu_t *b,
                                            const nd_planet_def_t *pdef,
                                            nd_seven_gl_bb_vert_t out_verts[6]);

/* Secondary 3D Point-Light Silhouette Shadow Quad Cast by the Glowing Lamb:
 * Evaluates a 6-vertex tangent-plane shadow quad projecting radially away from uLambLight
 * for billboards within the Lamb's glow radius (220 wu). Returns true if active & within range. */
bool nd_seven_gl_eval_billboard_lamb_shadow_quad(const lam_billboard_gpu_t *b,
                                                 const nd_planet_def_t *pdef,
                                                 nd_seven_gl_bb_vert_t out_verts[6]);

/* Returns true when a billboard carries a 3D mesh without a valid 2D atlas tile
 * (e.g. procedural understory, geobreaks, or v0==v1 / full-atlas-sentinel 3D flora)
 * and therefore must never fall back to a 2D atlas quad or impostor crossfade. */
bool nd_seven_gl_billboard_is_mesh3d_only(const lam_billboard_gpu_t *b);

/* Evaluates the canonical world-grid anchor (bx, by, bz) packed into a_iparam.xyz
 * for a 3D mesh instance, preserving ground z = b->z (never subtracting water_z_off). */
void nd_seven_gl_eval_mesh3d_anchor(const lam_billboard_gpu_t *b,
                                    float out_grid_pos[3]);

/* Pure-C reference mirror of the 3D volumetric billboard normal, directional sun
 * illumination, micro-relief, and ground-contact ambient occlusion in s_fs_billboard:
 * Evaluates the effective 3D lighting multiplier and writes the perturbed unit normal
 * (out_normal) and ground-contact ambient occlusion factor (out_contact_ao). */
float nd_seven_gl_eval_billboard_lighting(float qx, float qy,
                                          float wx, float wz, float ground_z,
                                          float cam_dist,
                                          float out_normal[3],
                                          float *out_contact_ao);

/* Smooth 4-Tap Hermite Height Reconstruction (CPU mirror of sampleHeightSmooth):
 * Returns unpacked elevation wz = raw_h * 0.0625f and writes analytical gradients. */
float nd_seven_gl_sample_height_smooth(const int16_t *height_i16,
                                       uint32_t tex_w, uint32_t tex_h,
                                       float u, float v,
                                       float span_x, float span_y,
                                       float *out_grad_x, float *out_grad_y);

/* Evaluate water vertex torus minor radius with dry-land depression:
 * Depresses water mesh vertices below terrain when the vertex and its 1-ring
 * height texel neighborhood are on dry land. */
float nd_seven_gl_eval_water_radius(const int16_t *height_i16,
                                    uint32_t tex_w, uint32_t tex_h,
                                    float u, float v,
                                    float span_x, float span_y,
                                    float r0, float sea_level,
                                    float *out_wz, float *out_water_depth);

/* 3D WebGL2 Frame & Pass Dispatchers */
bool nd_seven_gl_pipelines_ready(void);
void nd_seven_gl_set_lamb_light(float wx, float wy, float wz, float intensity);
float nd_seven_gl_eval_lamb_light(float frag_wx, float frag_wy, float frag_wz,
                                  float lamb_wx, float lamb_wy, float lamb_wz,
                                  float intensity);

/* Samples of the multisampled scene target (resolved before water and at end of frame). */
#define ND_SCENE_MSAA_SAMPLES 4

/* Resolve the scene and present it to the canvas. Must follow the last draw of a begin_frame()d frame. */
void nd_seven_gl_end_frame(void);

/* Query active WebGL2 render target dimensions, MSAA sample count, cloud FBO dimensions, and CSM state */
void nd_seven_gl_get_target_info(uint32_t *out_stage_w, uint32_t *out_stage_h,
                                 uint32_t *out_scene_w, uint32_t *out_scene_h,
                                 int *out_samples,
                                 uint32_t *out_cloud_w, uint32_t *out_cloud_h,
                                 bool *out_csm_active);
uint32_t nd_seven_gl_get_frame_count(void);

void nd_seven_gl_set_focal_water(float water_level);
void nd_seven_gl_begin_frame_ex(uint32_t stage_w, uint32_t stage_h,
                                float tx, float ty, float tz,
                                float dist, float yaw_deg, float pitch_deg, float fov_deg,
                                float anim_time,
                                const nd_planet_def_t *pdef);
void nd_seven_gl_begin_frame(uint32_t stage_w, uint32_t stage_h,
                             float tx, float ty, float tz,
                             float dist, float pitch_deg, float fov_deg,
                             float anim_time,
                             const nd_planet_def_t *pdef);

void nd_seven_gl_flush_terrain_chunk(uint16_t world, uint8_t level, uint32_t rev,
                                     int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                     const uint32_t *color_rgba, const int16_t *height_i16,
                                     uint32_t chunk_w, uint32_t chunk_h,
                                     uint32_t stage_w, uint32_t stage_h,
                                     float sea_level,
                                     const nd_planet_def_t *pdef);

void nd_seven_gl_flush_water(void);

void nd_seven_gl_flush_clouds(void);
bool nd_seven_gl_should_queue_cloud_chunk(int32_t ox, int32_t oy,
                                          int32_t span_w, int32_t span_h,
                                          const nd_planet_def_t *eff_pdef);

void nd_seven_gl_flush_billboards(const lam_billboard_gpu_t *batch,
                                  uint32_t count,
                                  const uint32_t *atlas_rgba);

/* Shader source accessors for verification */
const char *nd_seven_gl_vs_sky(void);
const char *nd_seven_gl_fs_sky(void);
const char *nd_seven_gl_vs_terrain(void);
const char *nd_seven_gl_fs_terrain(void);
const char *nd_seven_gl_vs_water(void);
const char *nd_seven_gl_fs_water(void);
const char *nd_seven_gl_vs_billboard(void);
const char *nd_seven_gl_fs_billboard(void);
const char *nd_seven_gl_vs_mesh3d(void);
const char *nd_seven_gl_fs_mesh3d(void);
const char *nd_seven_gl_vs_csm(void);
const char *nd_seven_gl_fs_csm(void);
const char *nd_seven_gl_vs_cloud(void);
const char *nd_seven_gl_fs_cloud(void);
const char *nd_seven_gl_fs_ssao(void);
const char *nd_seven_gl_fs_bloom(void);
const char *nd_seven_gl_fs_post(void);

/* Query active GPU mesh quad resolution for a given LOD level and camera distance:
 * Level 0 at mid-near zoom (340 < cam_dist < 5000) returns 48 (48x48 quads = 4,608 triangles);
 * Level 0 at normal/close zoom (220 < cam_dist <= 340) returns 96 (96x96 quads = 18,432 triangles);
 * Level 0 at close/ultra-close zoom (cam_dist <= 220) returns 144 (144x144 quads = 41,472 triangles);
 * Level 1..2 at mid zoom (cam_dist < 5000) returns 32 (32x32 quads = 2,048 triangles);
 * Level 3+ or macro low zoom (cam_dist >= 5000) returns 16 (16x16 quads = 512 triangles). */
uint32_t nd_seven_gl_grid_quads_for_zoom(uint8_t level, float cam_dist);
uint32_t nd_seven_gl_grid_quads_for_level(uint8_t level);

/* Pure-C reference mirror of the 7-octave zoom-progressive & Nyquist-filtered
 * terrain surface detail cascade in s_fs_terrain:
 * Evaluates signed surface micro-relief height and writes analytical world-space
 * partial derivatives (out_grad_x, out_grad_y) and micro-cavity AO factor (out_cavity_ao). */
float nd_seven_gl_eval_terrain_detail(float wx, float wy,
                                      float cam_dist, float pixel_footprint,
                                      float *out_grad_x, float *out_grad_y,
                                      float *out_cavity_ao);

/* Pure-C reference mirror of the 6-octave zoom-progressive & Nyquist-filtered
 * C2 quintic gradient-noise water wave cascade in s_fs_water:
 * Evaluates signed wave elevation and writes analytical world-space partial
 * derivatives (out_grad_x, out_grad_y) and wave crest factor (out_crest). */
float nd_seven_gl_eval_water_waves(float wx, float wy,
                                   float anim_time,
                                   float cam_dist, float pixel_footprint,
                                   float *out_grad_x, float *out_grad_y,
                                   float *out_crest);

/* Pure-C reactive grass & botanical displacement physics:
 * Combines 3-octave recursive wind (gl_eval_recursive_wind), radial ankle parting
 * around (lamb_wx, lamb_wy) within R=18 world units, and elastic footstep wake trail
 * depression + underdamped harmonic rustle recovery over 1.4s. */
void nd_seven_gl_eval_grass_displacement(float wx, float wy,
                                         float anim_time,
                                         float lamb_wx, float lamb_wy,
                                         float *out_shear_x, float *out_shear_y,
                                         float *out_depress_z, float *out_rustle);

/* Pure-C reference mirror of the 10-octave observer-gated ("quantum") 4D wind-coupled
 * fractal cloud cascade in s_fs_cloud / s_fs_terrain / s_fs_water / s_fs_billboard:
 * Gates fine fractal octaves by camera distance (cam_dist) and screen-space Nyquist
 * footprint (pixel_footprint), early-outing in clear-sky synoptic regions while
 * preserving large-scale weather systems and resolving close-up billow detail
 * where observed. */
float nd_seven_gl_eval_zoom_cloud_density(float wx, float wy, float wz,
                                          float anim_time,
                                          float climate_humidity,
                                          float cam_dist, float pixel_footprint,
                                          float *out_grad_x, float *out_grad_y,
                                          float *out_grad_z, float *out_grad_t);

float nd_seven_gl_eval_cloud_density(float wx, float wy, float wz,
                                     float anim_time,
                                     float climate_humidity,
                                     float *out_grad_x, float *out_grad_y,
                                     float *out_grad_z, float *out_grad_t);

/* Astronomical Solar & Lunar Cycle Constants:
 * Option B: 1 real hour per 24-hour diurnal cycle (24x real speed: 86,400 / 24 = 3,600 seconds).
 *   - Solar Day: 3,600.0 seconds (30m day, 30m night)
 *   - Sidereal Lunar Month: 27.321661 * 3,600 = 98,357.98 seconds (~27.3 real hours)
 *   - Synodic Lunar Month:  29.530589 * 3,600 = 106,310.12 seconds (~29.5 real hours)
 *   - Lunar Tidal Day: (24.8333h / 24) * 3,600 = 3,725.0 seconds (~62.1 real minutes) */
#define ND_ASTRO_SOLAR_DAY_SEC      3600.0f
#define ND_ASTRO_SOLAR_YEAR_SEC     (64.0f * ND_ASTRO_SOLAR_DAY_SEC)
#define ND_ASTRO_SIDEREAL_MONTH_SEC 98357.98f
#define ND_ASTRO_SYNODIC_MONTH_SEC  106310.12f
#define ND_ASTRO_LUNAR_DAY_SEC      3725.0f

/* =========================================================================
 * Vegetation & Surface Wind Tuning Parameters
 * Single source of truth for grass, flora, tree, and meadow wind dynamics.
 * ========================================================================= */

/* Grass tuft sway & lean dynamics (natural rolling meadow waves driven by 3-octave wind) */
#define ND_GRASS_WIND_BASE_LEAN       0.32f  /* Prevailing breeze baseline lean */
#define ND_GRASS_WIND_GUST_SCALE      0.85f  /* Rolling sway deflection added by passing gusts */
#define ND_GRASS_WIND_CATSPAW_SCALE   0.38f  /* Secondary catspaw ripple shear response */
#define ND_GRASS_WAVE_FREQ_RAD        1.45f  /* Rolling meadow wave frequency in rad/s */
#define ND_GRASS_FLUTTER_FREQ_RAD     2.85f  /* Natural blade tip flutter frequency in rad/s */
#define ND_GRASS_WAVE_AMP             0.72f  /* Primary traveling wave deflection amplitude */
#define ND_GRASS_FLUTTER_AMP          0.20f  /* Secondary blade flutter deflection amplitude */

/* Woody tree canopy & trunk dynamics (stately, heavy pendulum inertia) */
#define ND_TREE_WIND_BASE_LEAN        0.18f  /* Static upright trunk rest lean */
#define ND_TREE_WIND_GUST_SCALE       0.24f  /* Trunk deflection added by gusts */
#define ND_TREE_WIND_CATSPAW_SCALE    0.08f  /* Canopy shear added by catspaw */
#define ND_TREE_TRUNK_SWAY_FREQ_RAD   0.18f  /* Slow trunk pendulum frequency in rad/s (was 0.42f) */
#define ND_TREE_BOUGH_NOD_FREQ_RAD    0.32f  /* Canopy bough nod frequency in rad/s (was 0.72f) */

/* Billboard shader travelling grass sheen frequency in rad/s */
#define ND_GRASS_SHEEN_FREQ_RAD       1.45f

/* Evaluate 3D unit vectors toward the Sun and Moon in the planet's frame at time `t_sec` (4x real-life clock):
 *   out_sun_dir[3]  = (cos(lambda_sun), sin(lambda_sun), sin(obliquity) * sin(lambda_sun)) normalized
 *   out_moon_dir[3] = (cos(lambda_moon)*cos(inc), sin(lambda_moon)*cos(inc), sin(lambda_moon)*sin(inc)) normalized */
void nd_seven_gl_eval_celestial_dirs(float t_sec,
                                     float out_sun_dir[3],
                                     float out_moon_dir[3]);

/* Laplace / Equilibrium Astronomical Ocean Tide on the Torus:
 * Evaluates second-order Legendre tidal potential P_2(cos gamma) = 0.5 * (3 * (N . D)^2 - 1)
 * from the Moon and Sun (with realistic Earth-Moon-Sun 1 : 0.46 amplitude ratio) against
 * the local torus surface normal N(wx, wy). Returns 0 on perched mountain lakes
 * (sea_level > base_sea_level + 0.5). */
float nd_seven_gl_eval_astronomical_tide(float wx, float wy,
                                         float sea_level, float base_sea_level,
                                         float t_sec,
                                         const nd_planet_def_t *pdef);

/* Asymmetric Shoreline Wave Run-Up & Backwash (Swash) + Intertidal Wet-Sand Hysteresis:
 * Evaluates instantaneous vertical water surface excursion (tide + wave-group run-up/backwash),
 * recent high-water mark (out_high_water_z) for wet-sand memory, and rocky/surf splash foam. */
float nd_seven_gl_eval_shore_swash(float wx, float wy,
                                   float sea_level, float base_sea_level,
                                   float terrain_z,
                                   float slope_gx, float slope_gy,
                                   float t_sec,
                                   const nd_planet_def_t *pdef,
                                   float *out_high_water_z,
                                   float *out_splash_foam);

/* Real-Life Path-Ribbon Lamb Wake Trail in Water (Lakes & Oceans):
 * Records a breadcrumb ring buffer of recent water-contact path nodes (x_k, y_k, vx_k, vy_k, t_k)
 * so turns and curves leave a lingering dispersive Kelvin/viscous wake along the actual path walked. */
#define ND_LAMB_WAKE_NODES 8

void nd_seven_gl_set_lamb_wake(float wx, float wy, float ground_z,
                               int dir, int walking, float anim_time);

void nd_seven_gl_get_lamb_wake(float out_head[4],
                               float out_nodes_xy_t[ND_LAMB_WAKE_NODES][4],
                               float out_nodes_vel[ND_LAMB_WAKE_NODES][4]);

float nd_seven_gl_eval_lamb_wake(float frag_wx, float frag_wy,
                                 float anim_time,
                                 float *out_grad_x, float *out_grad_y,
                                 float *out_foam);

/* Analytical Ray-Cloud-Shell Intersection via <ttypt/corolla.h> (corolla_ray_hit):
 * Evaluates exact quartic ray-torus intersection against the concentric atmospheric cloud shell
 * at altitude `shell_z` above `pdef->minor_r`, returning world coordinates (out_wx, out_wy)
 * and hit distance `out_t` when the ray intersects the front hull of the cloud shell. */
bool nd_seven_gl_cloud_shell_ray_hit(const float ray_orig[3],
                                     const float ray_dir[3],
                                     float shell_z,
                                     const nd_planet_def_t *pdef,
                                     float *out_wx,
                                     float *out_wy,
                                     float *out_t);

/* =========================================================================
 * Clustered Cloud Lighting (16x16x24 Frustum Cluster Grid + Multi-Light Shading)
 * ========================================================================= */
#define ND_CLUSTER_GRID_X 16
#define ND_CLUSTER_GRID_Y 16
#define ND_CLUSTER_GRID_Z 24
#define ND_CLUSTER_TOTAL (ND_CLUSTER_GRID_X * ND_CLUSTER_GRID_Y * ND_CLUSTER_GRID_Z)
#define ND_MAX_LIGHTS_PER_CLUSTER 32
#define ND_MAX_TOTAL_LIGHTS 256

#define ND_LIGHT_TYPE_DIRECTIONAL 0
#define ND_LIGHT_TYPE_POINT       1
#define ND_LIGHT_TYPE_SPOT        2

typedef struct {
	float pos_x, pos_y, pos_z, pos_w;
	float dir_x, dir_y, dir_z, dir_w;
	float color_r, color_g, color_b, intensity;
	float range, spot_cos_cutoff, spot_exponent, type;
} nd_cluster_light_t;

void nd_seven_gl_clear_local_lights(void);
int  nd_seven_gl_add_point_light(float wx, float wy, float wz,
                                 float r, float g, float b,
                                 float intensity, float range);
int  nd_seven_gl_add_spot_light(float wx, float wy, float wz,
                                float dx, float dy, float dz,
                                float r, float g, float b,
                                float intensity, float range,
                                float cutoff_deg, float exponent);
uint32_t nd_seven_gl_get_active_light_count(void);
uint32_t nd_seven_gl_get_active_cluster_count(void);

/* =========================================================================
 * GPU-Driven Hi-Z Culling & Multi-Draw Indirect Batching
 * ========================================================================= */
#define ND_HIZ_MIP_LEVELS     6
#define ND_GPU_INDIRECT_MAX   512

/* Indirect indexed draw command descriptor (matches DrawElementsIndirectCommand) */
typedef struct {
	uint32_t count;
	uint32_t instance_count;
	uint32_t first_index;
	int32_t  base_vertex;
	uint32_t base_instance;
} nd_gpu_draw_elements_indirect_cmd_t;

/* Query GPU-driven culling and indirect batching state */
bool     nd_seven_gl_is_gpu_hiz_active(void);
int      nd_seven_gl_get_hiz_mip_levels(void);
uint32_t nd_seven_gl_get_indirect_draw_count(void);
const char *nd_seven_gl_vs_billboard(void);

#ifdef __cplusplus
}
#endif

#endif /* ND_SEVEN_GL_H */

