#ifndef ND_SEVEN_GL_H
#define ND_SEVEN_GL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "nd_seven.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Billboard shading / material style hints */
#define ND_BB_STYLE_STANDARD  0u  /* General sprite / entity / lamb */
#define ND_BB_STYLE_LEAF_TINT 1u  /* Flora with leaf-selective plant_tint in tint_rgba.rgb */
#define ND_BB_STYLE_FLORA     2u  /* Flora with full-sprite tint */
#define ND_BB_STYLE_MINERAL   3u  /* Rock / stone / boulder mineral scenery */
#define ND_BB_STYLE_GLOW      4u  /* Subtle emissive universe-owner lamb glow */
#define ND_BB_STYLE_GRASS     5u  /* Ground-pinned reactive grass / reed / flower tuft */
#define ND_BB_STYLE_PARTICLE  6u  /* Volumetric sunlit water droplet / sea-spray mist particle */

typedef struct {
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
 * Matches the left-handed East-right diorama camera on the curved torus. */
void nd_seven_gl_compute_mvp(uint32_t vp_w, uint32_t vp_h,
                             float tx, float ty, float tz,
                             float dist, float pitch_deg, float fov_deg,
                             const nd_planet_def_t *pdef,
                             float out_mvp[16],
                             float out_eye[3],
                             float out_target[3],
                             float out_up[3]);

/* Pure-C Flat Tangent-Space Camera MVP Matrix for libareole quadtree culling:
 * Uses identical left-handed East-right convention (ar_mat4_lookat_lh) and
 * dynamic near/far depth planes. */
void nd_seven_gl_compute_flat_mvp(uint32_t vp_w, uint32_t vp_h,
                                  float tx, float ty, float tz,
                                  float dist, float pitch_deg, float fov_deg,
                                  float out_mvp[16]);

/* Pitch-Rotated Billboard Basis & 6-Vertex Quad Construction:
 * Rotates the billboard quad around the horizontal camera axis (out_E) by
 * pitch_deg so the quad faces the pitched camera with 1:1 projected aspect ratio. */
void nd_seven_gl_billboard_basis(float wx, float wy, float wz,
                                 float pitch_deg,
                                 const nd_planet_def_t *pdef,
                                 float out_base_pos[3],
                                 float out_right[3],
                                 float out_up[3],
                                 float out_view_dir[3]);

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
void nd_seven_gl_set_lamb_light(float wx, float wy, float wz, float intensity);
float nd_seven_gl_eval_lamb_light(float frag_wx, float frag_wy, float frag_wz,
                                  float lamb_wx, float lamb_wy, float lamb_wz,
                                  float intensity);

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

void nd_seven_gl_flush_billboards(const lam_billboard_gpu_t *batch,
                                  uint32_t count,
                                  const uint32_t *atlas_rgba);

/* Shader source accessors for verification */
const char *nd_seven_gl_vs_terrain(void);
const char *nd_seven_gl_fs_terrain(void);
const char *nd_seven_gl_vs_water(void);
const char *nd_seven_gl_fs_water(void);
const char *nd_seven_gl_vs_billboard(void);
const char *nd_seven_gl_fs_billboard(void);

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

/* 4x Real-Life Astronomical Solar & Lunar Cycle Constants:
 * - Solar Day (1/4 real day = 4x real speed): 86,400 / 4 = 21,600 seconds (6.0 real hours)
 * - Sidereal Lunar Month (4x real speed): 27.321661 * 21,600 = 590,147.88 seconds (~6.83 real days)
 * - Synodic Lunar Month (4x real speed):  29.530589 * 21,600 = 637,860.72 seconds (~7.38 real days)
 * - Lunar Tidal Day (meridian transit to transit): 24h50m / 4 = 22,356.86 seconds (~6.21 real hours) */
#define ND_ASTRO_SOLAR_DAY_SEC      21600.0f
#define ND_ASTRO_SIDEREAL_MONTH_SEC 590147.88f
#define ND_ASTRO_SYNODIC_MONTH_SEC  637860.72f
#define ND_ASTRO_LUNAR_DAY_SEC      22356.86f

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

#ifdef __cplusplus
}
#endif

#endif /* ND_SEVEN_GL_H */
