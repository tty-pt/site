#ifndef ND_SEVEN_GL_H
#define ND_SEVEN_GL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "nd_seven.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	float x, y, z;
	float width, height;
	float u0, v0, u1, v1;
	uint32_t tint_rgba;
} lam_billboard_gpu_t;

/* 12 floats (48 bytes) per billboard vertex:
 * world_pos[3], uv[2], tint[4], local_pos[3] */
typedef struct {
	float world_pos[3];
	float uv[2];
	float tint[4];
	float local_pos[3];
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
 * Level 0 at normal zoom (cam_dist > 230) returns 48 (48x48 quads = 4,608 triangles);
 * Level 0 at close zoom (125 < cam_dist <= 230) returns 96 (96x96 quads = 18,432 triangles);
 * Level 0 at ultra-close zoom (cam_dist <= 125) returns 144 (144x144 quads = 41,472 triangles);
 * Level 1+ returns 32 (32x32 quads = 2,048 triangles). */
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

#ifdef __cplusplus
}
#endif

#endif /* ND_SEVEN_GL_H */
