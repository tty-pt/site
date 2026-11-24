/*
 * nd_seven_gl_internal.h — Shared internal types, constants, and helpers
 * across the split nd_seven_gl_*.c compilation units.
 */

#ifndef ND_SEVEN_GL_INTERNAL_H
#define ND_SEVEN_GL_INTERNAL_H

#include "nd_seven_gl.h"
#include "nd_seven_shader.h"
#include "nd_seven_atlas.h"
#include "nd_seven_atlas_draw.h"
#include "nd_seven_ground.h"
#include <nd/plant_foliage_atlas.h>
#include <ttypt/webgl.h>
#include <ttypt/areole.h>
#include <ttypt/chaff_glsl.h>
#include <nd/field.h>
#include <nd/field_glsl.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#endif
#ifndef GL_REPEAT
#define GL_REPEAT 0x2901
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_INVERT
#define GL_INVERT 0x150A
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_SAMPLE_ALPHA_TO_COVERAGE
#define GL_SAMPLE_ALPHA_TO_COVERAGE 0x809E
#endif
#define ND_TREE_SHADOW_RES 1024

#define PI_F 3.14159265358979323846f
#define TWO_PI_F 6.28318530717958647692f

/* Macro low-zoom grid: 16x16 quads = 17x17 vertices = 289 vertices (512 triangles, 4x lighter than 32x32) */
#define GL_GRID_QUADS_MACRO 16
#define GL_GRID_VERTS_MACRO (GL_GRID_QUADS_MACRO + 1)
#define GL_TOTAL_VERTS_MACRO (GL_GRID_VERTS_MACRO * GL_GRID_VERTS_MACRO)
#define GL_TOTAL_INDICES_MACRO (GL_GRID_QUADS_MACRO * GL_GRID_QUADS_MACRO * 6)

/* Mid-altitude station-aligned grid: 24x24 quads = 25x25 vertices = 625 vertices (1,152 triangles, 4x lighter than 48x48, exact 1:2 border alignment) */
#define GL_GRID_QUADS_LO 24
#define GL_GRID_QUADS GL_GRID_QUADS_LO
#define GL_GRID_VERTS (GL_GRID_QUADS + 1)
#define GL_TOTAL_VERTS (GL_GRID_VERTS * GL_GRID_VERTS)
#define GL_TOTAL_INDICES (GL_GRID_QUADS * GL_GRID_QUADS * 6)

/* Level 0 normal-zoom high-detail grid: 48x48 quads = 49x49 vertices = 2,401 vertices (4,608 triangles, 2.25x density) */
#define GL_GRID_QUADS_HI 48
#define GL_GRID_VERTS_HI (GL_GRID_QUADS_HI + 1)
#define GL_TOTAL_VERTS_HI (GL_GRID_VERTS_HI * GL_GRID_VERTS_HI)
#define GL_TOTAL_INDICES_HI (GL_GRID_QUADS_HI * GL_GRID_QUADS_HI * 6)

#define MAX_CHUNK_TEXTURES 384
#define MAX_WATER_QUEUE_BASE 192
#define MAX_BB_VERTS 65536
#define MAX_BB_OTHER_VERTS 32768
#define MAX_BB_INSTANCES 16384

#define SEVEN_HORIZON_BINS 128
#define ND_OCC2D_MAX_LEVELS 5
#define ND_OCC2D_TILE_SIZE 32
#define ND_HIZ_MAX_LEVELS 6

/* Dynamic queue sizes based on detail scale (Low preset = smaller queues) */
static inline int nd_seven_gl_max_water_queue(void) {
	float scale = nd_seven_get_detail_scale();
	if (scale < 0.75f) return 64;   /* Low preset */
	if (scale < 0.9f) return 128;   /* Medium preset */
	return MAX_WATER_QUEUE_BASE;    /* High/Ultra preset */
}

typedef struct { float x, y; } grass_lamb_pos_t;

typedef struct {
	float base_pos[3];
	float E[3];
	float S[3];
	float N[3];
	float grass_sx;
	float grass_sy;
	float grass_dz;
	float grass_rustle;
	float flora_sway_scale;
	float u_min;
	float u_max;
	float v_min;
	float v_max;
} billboard_basis_t;

typedef struct {
	GLuint prog;
	GLint uMVP;
	GLint uHeightTex;
	GLint uSubstrateTex;
	GLint uChunkOrigin;
	GLint uChunkRelOrigin;
	GLint uChunkSpan;
	GLint uRefTrig;
	GLint uHeightScale;
	GLint uSeaLevel;
	GLint uCameraPos;
	GLint uPlanetCenter;
	GLint uBodyRot;
	GLint uTorusRadii;
	GLint uWorldDim;
	GLint uFogStart;
	GLint uFogEnd;
	GLint uAnimTime;
	GLint uSkyColor;
	GLint uAtmoIntensity;
	GLint uLambLight;
	GLint uCloudShadowMap;
	GLint uCsm;
	GLint uBaseSeaLevel;
	GLint uSunDir;
	GLint uMoonDir;
	GLint uLambWakeHead;
	GLint uLambWakeXYT0;
	GLint uLambWakeXYT1;
	GLint uLambWakeVel0;
	GLint uLambWakeVel1;
	GLint uWorldId;
	GLint uSeaOffsetQ12_4;
	GLint uTreeShadowTex;
	GLint uTreeShadowOrigin;
	GLint uTreeShadowVP;
	GLint uTreeShadowActive;
	GLint uTerrDetailTex;
	GLint uTerrNormTex;
	GLint aGridUV;
} gl_terrain_pipe_t;

typedef struct {
	GLuint prog;
	GLint uMVP;
	GLint uHeightTex;
	GLint uSubstrateTex;
	GLint uChunkOrigin;
	GLint uChunkRelOrigin;
	GLint uChunkSpan;
	GLint uRefTrig;
	GLint uHeightScale;
	GLint uSeaLevel;
	GLint uCameraPos;
	GLint uPlanetCenter;
	GLint uBodyRot;
	GLint uTorusRadii;
	GLint uWorldDim;
	GLint uFogStart;
	GLint uFogEnd;
	GLint uAnimTime;
	GLint uSkyColor;
	GLint uAtmoIntensity;
	GLint uLambLight;
	GLint uCloudShadowMap;
	GLint uCsm;
	GLint uBaseSeaLevel;
	GLint uSunDir;
	GLint uMoonDir;
	GLint uLambWakeHead;
	GLint uLambWakeXYT0;
	GLint uLambWakeXYT1;
	GLint uLambWakeVel0;
	GLint uLambWakeVel1;
	GLint aGridUV;
	GLint uSceneColor;
	GLint uSceneDepth;
	GLint uViewport;
	GLint uNearFar;
	GLint uProjScale;
} gl_water_pipe_t;

typedef struct {
	GLuint prog;
	GLint uMVP;
	GLint uAtlasTex;
	GLint uAtlasNormTex;
	GLint uSeaLevel;
	GLint uAnimTime;
	GLint uCameraPos;
	GLint uFogStart;
	GLint uFogEnd;
	GLint uSkyColor;
	GLint uAtmoIntensity;
	GLint uLambLight;
	GLint uSunDir;
	GLint uCloudShadowMap;
	GLint uCsm;
	GLint uPass;
	GLint uCamAngles;
	GLint uHiZTex;
	GLint uHiZActive;
	GLint uTreeShadowTex;
	GLint uTreeShadowOrigin;
	GLint uTreeShadowVP;
	GLint uTreeShadowActive;
	GLint aPos;
	GLint aUV;
	GLint aTint;
	GLint aLocalPos;
	GLint aQuadUV;
	GLint aSpriteBounds;
} gl_billboard_pipe_t;

typedef struct {
	GLuint prog;
	GLint uHeightTex;
	GLint uChunkOrigin;
	GLint uChunkSpan;
	GLint uHeightScale;
	GLint uTreeShadowWriteOrigin;
	GLint uTreeShadowWriteVP;
	GLint aGridUV;
} gl_terrain_shadow_pipe_t;

typedef struct {
	GLuint height_tex;
	float ox;
	float oy;
	float span_w;
	float span_h;
} gl_terr_shadow_chunk_t;

#define MAX_TERR_SHADOW_QUEUE 64

typedef struct {
	GLuint prog;
	GLint uCamFwd;
	GLint uCamRight;
	GLint uCamUp;
	GLint uTanHalfAspect;
	GLint uEast;
	GLint uSouth;
	GLint uZenith;
	GLint uSunDir;
	GLint uMoonDir;
	GLint uSkyColor;
	GLint uCamDist;
	GLint uAnimTime;
	GLint uTorusEye;
	GLint uTorusRadii;
	GLint uAtmoHaloVis;
} gl_sky_pipe_t;

typedef struct {
	GLuint prog;
	GLint uMVP;
	GLint uSeaLevel;
	GLint uAnimTime;
	GLint uCameraPos;
	GLint uFogStart;
	GLint uFogEnd;
	GLint uSkyColor;
	GLint uAtmoIntensity;
	GLint uLambLight;
	GLint uSunDir;
	GLint uCloudShadowMap;
	GLint uCsm;
	GLint uShadowPass;
	GLint uBranchletTex;
	GLint uBranchletNorm;
	GLint uGrassTex;
	GLint uGrassNorm;
	GLint uBarkTex;
	GLint uBarkNorm;
	GLint uTreeShadowTex;
	GLint uTreeShadowOrigin;
	GLint uTreeShadowVP;
	GLint uTreeShadowActive;
	GLint uTreeShadowWriteOrigin;
	GLint uTreeShadowWriteVP;
} gl_mesh3d_pipe_t;

#define MAX_GRASS_CHUNKS 32

typedef struct {
	GLuint prog;
	GLint uMVP;
	GLint uHeightTex;
	GLint uSubstrateTex;
	GLint uChunkOrigin;
	GLint uChunkRelOrigin;
	GLint uChunkSpan;
	GLint uRefTrig;
	GLint uHeightScale;
	GLint uSeaLevel;
	GLint uCameraPos;
	GLint uPlanetCenter;
	GLint uBodyRot;
	GLint uTorusRadii;
	GLint uWorldDim;
	GLint uGridN;
	GLint uFogStart;
	GLint uFogEnd;
	GLint uAnimTime;
	GLint uSkyColor;
	GLint uAtmoIntensity;
	GLint uLambLight;
	GLint uSunDir;
	GLint uCloudShadowMap;
	GLint uCsm;
	GLint uTreeShadowTex;
	GLint uTreeShadowOrigin;
	GLint uTreeShadowVP;
	GLint uTreeShadowActive;
} gl_grass_pipe_t;

typedef struct {
	GLuint height_tex;
	GLuint substrate_tex;
	int32_t ox, oy;
	int32_t span_w, span_h;
	uint8_t level;
	float sea_level;
	float rel_center[3];
	float rot_mat4[16];
	float major_r;
	float minor_r;
} gl_grass_chunk_t;

typedef struct {
	GLuint prog;
	GLint uMVP;
	GLint uHeightTex;
	GLint uSubstrateTex;
	GLint uChunkOrigin;
	GLint uChunkRelOrigin;
	GLint uChunkSpan;
	GLint uRefTrig;
	GLint uSeaLevel;
	GLint uCameraPos;
	GLint uPlanetCenter;
	GLint uBodyRot;
	GLint uTorusRadii;
	GLint uWorldDim;
	GLint uFogStart;
	GLint uFogEnd;
	GLint uAnimTime;
	GLint uSkyColor;
	GLint uAtmoIntensity;
	GLint uCamDist;
	GLint uCloudDeck;
	GLint uSunDir;
	GLint uCloudShadowMap;
	GLint uCloudShadowMapHistory;
	GLint uCsm;
	GLint uPrevCsm;
	GLint uPrevSunDir;
	GLint uCsmConfidence;
	GLint uCsmJitterDelta;
	GLint aGridUV;
	GLint uMoonDir;
	GLint uViewport;
	GLint uClusterGridDim;
	GLint uClusterDepthParams;
	GLint uClusterLightCount;
	GLint uClusterLightData;
	GLint uClusterLightListsTex;
	GLint uCloudResScale;
} gl_cloud_pipe_t;

typedef struct {
	uint8_t count;
	uint8_t light_indices[ND_MAX_LIGHTS_PER_CLUSTER];
} nd_cluster_light_list_t;

typedef struct {
	GLuint prog;
	GLint uCsm;
	GLint uAnimTime;
	GLint uSeaWet;
} gl_csm_pipe_t;

typedef struct {
	GLuint prog;
	GLint uCloudTex;
	GLint uCloudUvScale;
	GLint uSceneUvScale;
	GLint uCloudDepthTex;
	GLint uSceneDepthTex;
	GLint uBilateralActive;
} gl_cloud_comp_pipe_t;

typedef struct {
	GLuint prog;
	GLint uDepthTex;
	GLint uNearFar;
	GLint uProjScale;
	GLint uViewport;
	GLint uAnimTime;
	GLint uSceneUvScale;
} gl_ssao_pipe_t;

typedef struct {
	GLuint prog;
	GLint uSrcTex;
	GLint uTexelSize;
	GLint uPassDir;
	GLint uUvScale;
} gl_bloom_pipe_t;

typedef struct {
	GLuint prog;
	GLint uSceneTex;
	GLint uBloomTex;
	GLint uSsaoTex;
	GLint uSsaoActive;
	GLint uBloomActive;
	GLint uExposure;
	GLint uCamDist;
	GLint uSceneUvScale;
	GLint uPostUvScale;
	GLint uSceneTexel;
	GLint uFxaaActive;
} gl_post_pipe_t;

typedef struct {
	uint16_t world;
	uint8_t level;
	uint8_t active;
	uint32_t rev;
	int32_t ox, oy;
	int32_t span_w, span_h;
	uint32_t tex_w, tex_h;
	uint32_t last_touch;
	GLuint height_tex;
	GLuint substrate_tex;
} gl_chunk_tex_t;

typedef struct {
	int32_t ox, oy;
	int32_t span_w, span_h;
	uint8_t level;
	float sea_level;
	float rel_center[3];
	float rot_mat4[16];
	float major_r;
	float minor_r;
	GLuint height_tex;
	GLuint substrate_tex;
} gl_water_chunk_t;

typedef struct {
	bool initialized;
	bool pipelines_ready;
	gl_terrain_pipe_t terrain_pipe;
	gl_water_pipe_t water_pipe;
	gl_billboard_pipe_t bb_pipe;
	gl_mesh3d_pipe_t mesh3d_pipe;
	gl_grass_pipe_t grass_pipe;
	gl_sky_pipe_t sky_pipe;
	gl_cloud_pipe_t cloud_pipe;
	gl_cloud_comp_pipe_t cloud_comp_pipe;
	gl_csm_pipe_t csm_pipe;
	gl_ssao_pipe_t ssao_pipe;
	gl_bloom_pipe_t bloom_pipe;
	gl_post_pipe_t post_pipe;
	GLuint csm_tex;
	GLuint csm_fbo;
	bool csm_failed;
	bool csm_valid;
	bool csm_clear_sky;
	bool csm_has_cache;
	nd_seven_gl_csm_window_t csm_win;
	nd_seven_gl_csm_window_t csm_rendered_win;
	float csm_rendered_time;
	float csm_rendered_wet;

	/* Temporal Cloud Shadow Map Reprojection */
	GLuint csm_history_tex;
	GLuint csm_history_fbo;
	GLuint csm_reproj_prog;

	/* Clustered Lighting Buffers & Textures */
	GLuint cluster_light_tex;
	GLuint cluster_list_tex;
	int cluster_ssbo_valid;
	bool cluster_dirs_only_cached;
	nd_cluster_light_t local_lights[ND_MAX_TOTAL_LIGHTS];
	uint32_t local_light_count;
	nd_cluster_light_t active_lights[ND_MAX_TOTAL_LIGHTS];
	uint32_t active_light_count;
	uint32_t active_cluster_count;
	uint8_t cluster_light_tex_data[ND_MAX_TOTAL_LIGHTS * 4 * 4];
	uint8_t cluster_list_tex_data[ND_CLUSTER_TOTAL * (ND_MAX_LIGHTS_PER_CLUSTER + 1)];
	float csm_prev_mvp[16];
	nd_seven_gl_csm_window_t csm_prev_win;
	float csm_prev_anim_time;
	float csm_prev_sun_dir[3];
	bool csm_history_valid;
	int csm_reproj_mode;
	/* Temporal AA jitter compensation */
	float csm_jitter_x;
	float csm_jitter_y;
	float csm_prev_jitter_x;
	float csm_prev_jitter_y;
	int csm_prev_quality;
	int csm_prev_msaa;
	float csm_prev_res_scale;
	uint32_t stage_w;
	uint32_t stage_h;

	/* Scene render target */
	GLuint scene_fbo;
	GLuint scene_rb_color;
	GLuint scene_rb_depth;
	GLuint res_fbo;
	GLuint res_color_tex;
	GLuint res_depth_tex;
	GLuint cloud_fbo;
	GLuint cloud_tex;
	GLuint cloud_depth_tex;
	GLuint ssao_fbo;
	GLuint ssao_tex;
	GLuint bloom_fbo[2];
	GLuint bloom_tex[2];
	uint32_t post_half_w;
	uint32_t post_half_h;
	uint32_t post_rw;
	uint32_t post_rh;
	bool scene_hdr;
	bool post_targets_ok;
	uint32_t cloud_w;
	uint32_t cloud_h;
	uint32_t cloud_rw;
	uint32_t cloud_rh;
	uint32_t scene_w;
	uint32_t scene_h;
	uint32_t render_w;
	uint32_t render_h;
	float cur_res_scale;
	float cur_cloud_scale;
	bool render_scales_set;
	int scene_samples;
	bool scene_ok;
	bool scene_failed;
	bool frame_open;
	bool sprites_in_water;

	GLuint grid_vbo_macro;
	GLuint grid_ibo_macro;
	GLuint grid_vbo;
	GLuint grid_ibo;
	GLuint grid_vbo_hi;
	GLuint grid_ibo_hi;

	GLuint bb_vbo;
	GLuint mesh3d_vbo;
	GLuint mesh3d_ibo;
	GLuint mesh3d_inst_vbo;
	GLuint fol_atlas_tex;
	GLuint fol_atlas_norm_tex;
	GLuint fol_grass_tex;
	GLuint fol_grass_norm_tex;
	GLuint fol_bark_tex;
	GLuint fol_bark_norm_tex;
	GLuint terr_detail_tex;
	GLuint terr_norm_tex;
	GLuint tree_shadow_fbo[2];
	GLuint tree_shadow_tex[2];
	GLuint tree_shadow_dummy_tex;
	int    tree_shadow_read_idx;
	bool   tree_shadow_ready;
	bool   tree_shadow_failed;
	bool   tree_shadow_read_valid;
	bool   tree_shadow_write_draws;
	float  tree_shadow_read_origin[3];
	float  tree_shadow_read_vp[16];
	float  tree_shadow_write_origin[3];
	float  tree_shadow_write_vp[16];
	float  tree_shadow_write_half_xy;
	float  tree_shadow_active;
	gl_terrain_shadow_pipe_t terrain_shadow_pipe;
	gl_terr_shadow_chunk_t   terr_shadow_q[MAX_TERR_SHADOW_QUEUE];
	uint32_t                 terr_shadow_q_count;
	GLuint                   grass_vbo;
	GLuint                   grass_ibo;
	GLsizei                  grass_idx_count;
	gl_grass_chunk_t         grass_q[MAX_GRASS_CHUNKS];
	uint32_t                 grass_q_count;
	GLuint atlas_tex;
	GLuint atlas_norm_tex;
	uint32_t atlas_ver;

	gl_chunk_tex_t chunk_cache[MAX_CHUNK_TEXTURES];
	uint32_t frame_count;

	gl_water_chunk_t water_queue[MAX_WATER_QUEUE_BASE];
	uint32_t water_count;
	gl_water_chunk_t cloud_queue[MAX_WATER_QUEUE_BASE];
	uint32_t cloud_count;

	/* Occlusion stats for HUD/debugging */
	uint32_t water_culled_occ2d;
	uint32_t cloud_culled_hiz;
	uint32_t billboard_culled_hiz;

	float cur_mvp[16];
	ar_frustum_t cur_frustum;
	float cur_eye[3];
	float cur_target[3];
	float cur_up[3];
	double cur_origin[3];
	float cur_tx;
	float cur_ty;
	float cur_tz;
	float cur_ref_trig[4];
	float cur_dist;
	float cur_near;
	float cur_far;
	float cur_tan_half;
	float cur_aspect;
	float cur_yaw_deg;
	float cur_pitch_deg;
	float cur_cos_y;
	float cur_sin_y;
	float cur_cos_p;
	float cur_sin_p;
	float cur_E[3];
	float cur_S[3];
	float cur_N[3];
	float cur_cam_right[3];
	float cur_cam_up[3];
	float cur_cam_fwd[3];
	float cur_anim_time;
	float cur_sea_level;
	float cur_base_sea_level;
	float focal_sea_level;
	float cur_sun_dir[3];
	float cur_moon_dir[3];
	float cur_lamb_light[4];
	float cur_wake_head[4];
	float cur_wake_xyt[ND_LAMB_WAKE_NODES][4];
	float cur_wake_vel[ND_LAMB_WAKE_NODES][4];
	uint32_t cur_wake_cursor;
	float cur_wake_last_x;
	float cur_wake_last_y;
	float cur_wake_last_t;
	nd_planet_def_t cur_pdef;

	/* Hi-Z Buffer (hierarchical depth pyramid for GPU-driven occlusion culling) */
	GLuint hiz_fbo;
	GLuint hiz_tex;
	GLuint hiz_depth_tex;
	GLuint hiz_mip_tex[ND_HIZ_MAX_LEVELS];
	GLuint hiz_mip_fbo[ND_HIZ_MAX_LEVELS];
	uint32_t hiz_w;
	uint32_t hiz_h;
	int hiz_levels;
	bool hiz_valid;
	bool hiz_failed;
	bool gpu_hiz_built;
	GLuint hiz_downsample_prog;

	/* GPU Indirect Draw Command Buffer & Stats */
	nd_gpu_draw_elements_indirect_cmd_t indirect_cmds[ND_GPU_INDIRECT_MAX];
	uint32_t indirect_cmd_count;
} nd_seven_gl_state_t;

/* Shared module state */
extern nd_seven_gl_state_t s_gl;
extern grass_lamb_pos_t s_grass_lamb_pos;
extern float s_grass_anim_time;
extern float s_view_wind_sx;
extern float s_view_wind_sy;
extern float s_view_wind_dz;
extern float s_view_wind_rustle;
extern float s_view_tree_sx;
extern float s_view_tree_sy;
extern float s_view_tree_rustle;
extern int   s_view_wind_ready;

extern const double *s_bb_ref_origin;
extern const nd_planet_def_t *s_bb_ref_pdef;
extern float s_bb_tx;
extern float s_bb_ty;
extern float s_bb_dist;
extern float s_bb_ref_trig[4];
extern float s_shadow_sun_dir[3];
extern float s_lamb_shadow_light[4];

/* Shader string declarations (defined in nd_seven_gl_shaders.c) */
extern const char *s_vs_sky;
extern const char *s_fs_sky;
extern const char *s_vs_terrain;
extern const char *s_fs_terrain;
extern const char *s_vs_water;
extern const char *s_fs_water;
extern const char *s_vs_billboard;
extern const char *s_fs_billboard;
extern const char *s_vs_mesh3d;
extern const char *s_fs_mesh3d;
extern const char *s_vs_grass;
extern const char *s_fs_grass;
extern const char *s_vs_csm;
extern const char *s_fs_csm;
extern const char *s_vs_csm_reproj;
extern const char *s_fs_csm_reproj;
extern const char *s_vs_cloud;
extern const char *s_fs_cloud;
extern const char *s_fs_cloud_comp;
extern const char *s_fs_hiz_downsample;
extern const char *s_fs_ssao;
extern const char *s_fs_bloom;
extern const char *s_fs_post;
extern const char *s_vs_terrain_shadow;
extern const char *s_fs_terrain_shadow;

/* Shared internal functions across translation units */
GLuint create_program_async(const char *vs_src, const char *fs_src);
void gl_update_view_wind(float wx, float wy, float anim_time);
void torus_frame_double(double wx, double wy, double wz,
                        const nd_planet_def_t *pdef,
                        double out_pos[3],
                        float out_E[3],
                        float out_S[3],
                        float out_N[3]);
void gl_ensure_scene_targets(uint32_t w, uint32_t h);
void gl_bind_scene_target(void);
void gl_resolve_scene(GLbitfield mask);
bool gl_build_hiz_pyramid(void);
void gl_prepare_tree_shadow_frame(float tx, float ty, float tz,
                                  const float E[3], const float S[3], const float N[3]);
void gl_flush_terrain_shadows_to_fbo(void);
GLuint gl_ensure_tree_shadow_dummy(void);
void gl_bind_tree_shadow_read(GLint uTex, GLint uOrigin, GLint uVP, GLint uActive);
void gl_render_cloud_shadow_map(void);
void gl_bind_csm(GLint loc_tex, GLint loc_csm);
bool gl_eval_synoptic_is_clear_sky_cpu(float wx, float wy, float anim_time,
                                       float sea_wet, float fw,
                                       bool want_cirrus, float margin);

int nd_seven_hiz_test_sphere(float cx, float cy, float cz, float radius);
int nd_seven_is_water_ridge_occluded(int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                     const float flat_mvp[16], float lwx, float lwy, float r_planet);
int nd_seven_hiz_test_cloud_chunk(int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                  const float mvp[16], float cloud_z);
int nd_seven_hiz_test_billboard_batch(const lam_billboard_gpu_t *batch, uint32_t count,
                                      const float mvp[16], float max_radius);
int nd_seven_occ2d_test_rect(float ndc_x0, float ndc_y0, float ndc_x1, float ndc_y1, float near_z);
int nd_seven_occ2d_test_water_chunk(int32_t ox, int32_t oy, int32_t span_w, int32_t span_h,
                                    const float mvp[16], float water_z);

/* Inline math/noise helpers used across multiple units */
static inline uint32_t gl_chaff_rotl32(uint32_t x, int r)
{
	return (x << r) | (x >> (32 - r));
}

static inline uint32_t gl_chaff_hash3(int32_t px, int32_t py, int32_t pz, uint32_t seed)
{
	uint32_t v1 = ((uint32_t)py & 0xFFFFu) | (((uint32_t)px & 0xFFFFu) << 16u);
	uint32_t v2 = ((uint32_t)pz & 0xFFFFu);
	uint32_t h = seed + 0x165667B1u + 6u;
	h += v1 * 0xC2B2AE3Du;
	h = gl_chaff_rotl32(h, 17) * 0x27D4EB2Fu;
	h += v2 * 0x85EBCA77u;
	h = gl_chaff_rotl32(h, 13) * 0xC2B2AE3Du;
	h ^= h >> 15u;
	h *= 0x85EBCA77u;
	h ^= h >> 16u;
	return h;
}

static inline void gl_chaff_grad_dir(int32_t cx, int32_t cy, uint32_t seed,
                                     float *out_gx, float *out_gy)
{
	uint32_t h = gl_chaff_hash3(cx, cy, 0, seed);
	float gx = (float)((int32_t)(h & 0xFFFFu) - 32768) * (1.0f / 32768.0f);
	float gy = (float)((int32_t)(h >> 16u) - 32768) * (1.0f / 32768.0f);
	float inv_len = 1.0f / sqrtf(gx * gx + gy * gy + 1e-6f);
	*out_gx = gx * inv_len;
	*out_gy = gy * inv_len;
}

static inline void gl_chaff_grad_noise2d(float px, float py, uint32_t seed,
                                         float *out_val, float *out_gx, float *out_gy)
{
	float ipx = floorf(px);
	float ipy = floorf(py);
	float fpx = px - ipx;
	float fpy = py - ipy;
	float ux = fpx * fpx * fpx * (fpx * (fpx * 6.0f - 15.0f) + 10.0f);
	float uy = fpy * fpy * fpy * (fpy * (fpy * 6.0f - 15.0f) + 10.0f);
	float dux = 30.0f * fpx * fpx * (fpx * (fpx - 2.0f) + 1.0f);
	float duy = 30.0f * fpy * fpy * (fpy * (fpy - 2.0f) + 1.0f);
	int32_t cx = (int32_t)ipx;
	int32_t cy = (int32_t)ipy;

	float g00x, g00y, g10x, g10y, g01x, g01y, g11x, g11y;
	gl_chaff_grad_dir(cx,     cy,     seed, &g00x, &g00y);
	gl_chaff_grad_dir(cx + 1, cy,     seed, &g10x, &g10y);
	gl_chaff_grad_dir(cx,     cy + 1, seed, &g01x, &g01y);
	gl_chaff_grad_dir(cx + 1, cy + 1, seed, &g11x, &g11y);

	float v00 = g00x * fpx + g00y * fpy;
	float v10 = g10x * (fpx - 1.0f) + g10y * fpy;
	float v01 = g01x * fpx + g01y * (fpy - 1.0f);
	float v11 = g11x * (fpx - 1.0f) + g11y * (fpy - 1.0f);

	float k0 = v10 - v00;
	float k1 = v01 - v00;
	float k2 = v11 - v01 - k0;
	float val = 0.5f + 0.70710678f * (v00 + k0 * ux + k1 * uy + k2 * ux * uy);
	if (val < 0.0f) val = 0.0f;
	if (val > 1.0f) val = 1.0f;

	float gix = g00x + (g10x - g00x) * ux + (g01x - g00x) * uy + (g11x - g01x - g10x + g00x) * (ux * uy);
	float giy = g00y + (g10y - g00y) * ux + (g01y - g00y) * uy + (g11y - g01y - g10y + g00y) * (ux * uy);

	*out_val = val;
	*out_gx  = 0.70710678f * (gix + dux * (k0 + k2 * uy));
	*out_gy  = 0.70710678f * (giy + duy * (k1 + k2 * ux));
}

static inline float gl_smoothstep(float edge0, float edge1, float x)
{
	float t = (x - edge0) / (edge1 - edge0);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	return t * t * (3.0f - 2.0f * t);
}

static inline float gl_smoothstep01(float e0, float e1, float x)
{
	float t = (x - e0) / (e1 - e0);
	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	return t * t * (3.0f - 2.0f * t);
}

static inline void gl_eval_recursive_wind(float wx, float wy, float anim_time,
                                          float *out_dir_x, float *out_dir_y,
                                          float *out_gust, float *out_catspaw)
{
	float raw_t0 = anim_time * 0.05f;
	float t0 = raw_t0 - 1024.0f * floorf(raw_t0 / 1024.0f);
	float p0x = wx * 0.0055f - 0.741f * t0;
	float p0y = wy * 0.0055f + 0.671f * t0;
	float w0v = 0.0f, w0gx = 0.0f, w0gy = 0.0f;
	gl_chaff_grad_noise2d(p0x, p0y, 0x57494E44u, &w0v, &w0gx, &w0gy);

	float bwx = 0.741f - w0gy * 0.28f;
	float bwy = -0.671f + w0gx * 0.28f;
	float bw_inv = 1.0f / sqrtf(bwx * bwx + bwy * bwy + 1e-6f);
	bwx *= bw_inv;
	bwy *= bw_inv;

	float raw_t1 = anim_time * 0.12f;
	float t1 = raw_t1 - 1024.0f * floorf(raw_t1 / 1024.0f);
	float u1x = wx * 0.028f + w0gx * 0.65f;
	float u1y = wy * 0.028f + w0gy * 0.65f;
	float p1x = ( 0.8f * u1x + 0.6f * u1y) - 0.822f * t1;
	float p1y = (-0.6f * u1x + 0.8f * u1y) + 0.569f * t1;
	float w1v = 0.0f, w1gx = 0.0f, w1gy = 0.0f;
	gl_chaff_grad_noise2d(p1x, p1y, 0x47555354u, &w1v, &w1gx, &w1gy);
	float mgx =  0.8f * w1gx - 0.6f * w1gy;
	float mgy =  0.6f * w1gx + 0.8f * w1gy;

	float lwx = bwx - mgy * 0.22f;
	float lwy = bwy + mgx * 0.22f;
	float lw_inv = 1.0f / sqrtf(lwx * lwx + lwy * lwy + 1e-6f);
	lwx *= lw_inv;
	lwy *= lw_inv;

	float raw_t2 = anim_time * 0.22f;
	float t2 = raw_t2 - 1024.0f * floorf(raw_t2 / 1024.0f);
	float r1x =  0.8f * (wx * 0.120f) + 0.6f * (wy * 0.120f) + w0gx * 0.45f + w1gx * 0.55f;
	float r1y = -0.6f * (wx * 0.120f) + 0.8f * (wy * 0.120f) + w0gy * 0.45f + w1gy * 0.55f;
	float p2x = ( 0.8f * r1x + 0.6f * r1y) - 0.640f * t2;
	float p2y = (-0.6f * r1x + 0.8f * r1y) + 0.768f * t2;
	float w2v = 0.0f, w2gx = 0.0f, w2gy = 0.0f;
	gl_chaff_grad_noise2d(p2x, p2y, 0x45444459u, &w2v, &w2gx, &w2gy);
	/* irot * irot = mat2(0.28, 0.96, -0.96, 0.28) */
	float u2gx =  0.28f * w2gx - 0.96f * w2gy;
	float u2gy =  0.96f * w2gx + 0.28f * w2gy;

	float fwx = lwx - u2gy * 0.16f;
	float fwy = lwy + u2gx * 0.16f;
	float fw_inv = 1.0f / sqrtf(fwx * fwx + fwy * fwy + 1e-6f);
	fwx *= fw_inv;
	fwy *= fw_inv;

	float gust = 0.42f + 0.45f * w0v + 0.55f * (w1v - 0.38f) + 0.30f * (w2v - 0.42f);
	if (gust < 0.25f) gust = 0.25f;
	if (gust > 1.35f) gust = 1.35f;

	float cp = gl_smoothstep(0.38f, 0.76f, w1v * 0.62f + w2v * 0.38f);
	if (out_dir_x) *out_dir_x = fwx;
	if (out_dir_y) *out_dir_y = fwy;
	if (out_gust) *out_gust = gust;
	if (out_catspaw) *out_catspaw = cp;
}

static inline float shadow_corner_terrain_dz(float wx, float wy, float base_z, float dx, float dy)
{
	float d2 = dx * dx + dy * dy;
	if (d2 < 9.0f || !nd_seven_get_chunk_sys())
		return 0.0f;
	float gz = nd_seven_ground_elevation_f((int32_t)floorf(wx + 0.5f), (int32_t)floorf(wy + 0.5f));
	float dz = gz - base_z;
	if (d2 > 100.0f) {
		float gz_mid = nd_seven_ground_elevation_f((int32_t)floorf(wx - 0.5f * dx + 0.5f),
		                                           (int32_t)floorf(wy - 0.5f * dy + 0.5f));
		float dz_mid = gz_mid - base_z;
		float crest_lift = dz_mid - 0.5f * dz;
		if (crest_lift > 0.0f)
			dz += crest_lift * 1.15f;
	}
	if (dz < -48.0f) dz = -48.0f;
	if (dz >  48.0f) dz =  48.0f;
	return dz;
}

#endif /* ND_SEVEN_GL_INTERNAL_H */
