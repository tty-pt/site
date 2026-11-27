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
#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D 0x806F
#endif
#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif
#ifndef GL_RGB
#define GL_RGB 0x1907
#endif
#ifndef GL_R11F_G11F_B10F
#define GL_R11F_G11F_B10F 0x8C3A
#endif
#ifndef GL_UNSIGNED_INT_10F_11F_11F_REV
#define GL_UNSIGNED_INT_10F_11F_11F_REV 0x8C3B
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_UNSIGNED_INT_24_8
#define GL_UNSIGNED_INT_24_8 0x84FA
#endif
#define ND_TREE_SHADOW_RES 1024

/* GPU pass IDs for EXT_disjoint_timer_query_webgl2 (non-nested, 1-based) */
typedef enum {
	ND_GPU_PASS_CSM           = 1u,
	ND_GPU_PASS_TERRAIN       = 2u,
	ND_GPU_PASS_SKY           = 3u,
	ND_GPU_PASS_HIZ           = 4u,
	ND_GPU_PASS_TREE_SHADOW   = 5u,
	ND_GPU_PASS_MESH3D_OPAQUE = 6u,
	ND_GPU_PASS_MESH3D_ALPHA  = 7u,
	ND_GPU_PASS_BILLBOARDS    = 8u,
	ND_GPU_PASS_GRASS         = 9u,
	ND_GPU_PASS_WATER         = 10u,
	ND_GPU_PASS_CLOUDS        = 11u,
	ND_GPU_PASS_POST          = 12u
} nd_gpu_pass_id_t;

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

#define MAX_CHUNK_TEXTURES 256
#define MAX_TERR_DRAW_QUEUE MAX_CHUNK_TEXTURES
#define MAX_TERR_BODIES 8
#define CHUNK_CACHE_HASH_SIZE 1024
#define MAX_WATER_QUEUE_BASE 192
#define MAX_CLOUD_QUEUE MAX_CHUNK_TEXTURES
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

/* Internal 128-byte (2-cache-line) mesh3d GPU instance layout: wraps the 112-byte
 * nd_seven_gl_mesh3d_inst_t ABI with a CPU-evaluated linear RGB ground substrate vec4
 * (attrib location 10), eliminating per-vertex procedural noise across ~2M instanced vertices. */
typedef struct {
	nd_seven_gl_mesh3d_inst_t base; /* 112 bytes: locations 3..9 */
	float sub_col[4];               /*  16 bytes: location 10 (rgb: linear substrate, a: unused) */
} gl_mesh3d_inst_t;
_Static_assert(sizeof(gl_mesh3d_inst_t) == 128, "gl_mesh3d_inst_t must be 128 bytes");

/* WebGL2 std140 Frame-Global Uniform Buffer (binding 0, shared by 8 scene programs).
 * Exact 16-byte vec4 alignment: 18 * 16 = 288 bytes, zero wasted padding. */
typedef struct {
	float uMVP[16];              /*   0 (64 B): mat4 uMVP */
	float uTreeShadowVP[16];     /*  64 (64 B): mat4 uTreeShadowVP */
	float uRefTrig[4];           /* 128 (16 B): vec4 uRefTrig */
	float uLambLight[4];         /* 144 (16 B): vec4 uLambLight */
	float uCameraPos[3];         /* 160 (12 B): vec3 uCameraPos */
	float uAnimTime;             /* 172 ( 4 B): float uAnimTime */
	float uSkyColor[3];          /* 176 (12 B): vec3 uSkyColor */
	float uAtmoIntensity;        /* 188 ( 4 B): float uAtmoIntensity */
	float uSunDir[3];            /* 192 (12 B): vec3 uSunDir */
	float uFogStart;             /* 204 ( 4 B): float uFogStart */
	float uMoonDir[3];           /* 208 (12 B): vec3 uMoonDir */
	float uFogEnd;               /* 220 ( 4 B): float uFogEnd */
	float uCsm[3];               /* 224 (12 B): vec3 uCsm */
	float uBaseSeaLevel;         /* 236 ( 4 B): float uBaseSeaLevel */
	float uTreeShadowOrigin[3];  /* 240 (12 B): vec3 uTreeShadowOrigin */
	float uTreeShadowActive;     /* 252 ( 4 B): float uTreeShadowActive */
	float uWorldDim[2];          /* 256 ( 8 B): vec2 uWorldDim */
	float uCamDist;              /* 264 ( 4 B): float uCamDist */
	float uTerrFastDist;         /* 268 ( 4 B): float uTerrFastDist */
	float uPlanetMetric[4];      /* 272 (16 B): vec4 uPlanetMetric (wu_per_m, max_elev_wu, snowline_wu, cloud_lcl_wu) */
} nd_frame_ubo_t;

_Static_assert(sizeof(nd_frame_ubo_t) == 288, "nd_frame_ubo_t must match std140 layout (288 bytes)");

/* 64-byte (4 x vec4, exact 1 cache line) per-chunk terrain instance descriptor (attribs 1..4, divisor 1) */
typedef struct {
	float orig_span[4];  /* loc 1: ox, oy, span_w, span_h */
	float rel_layers[4]; /* loc 2: rel_ox, rel_oy, slot_layer, river_layer */
	float meta[4];       /* loc 3: sea_level, morph_start_dist, morph_inv_range, grid_quads + reveal_morph */
	float height_z[4];   /* loc 4: base_z_wu, min_z_wu, max_z_wu, 0.0f */
} gl_chunk_inst_t;
_Static_assert(sizeof(gl_chunk_inst_t) == 64, "gl_chunk_inst_t must be 64 bytes");

typedef struct {
	uint16_t world;
	int32_t sea_offset_q12_4;
	nd_planet_def_t pdef;
	float rel_center[3];
	float rot_mat4[16];
} gl_terr_body_t;

typedef struct {
	gl_chunk_inst_t inst;
	uint16_t sort_key;  /* (body_idx << 8) | (variant << 3) | (seabed_st << 2) | grid_lod */
	uint8_t body_idx;
	uint8_t variant;
	uint8_t grid_lod;   /* 0 = macro (16x16), 1 = lo (24x24), 2 = hi (48x48) */
	uint8_t seabed_st;  /* 0 or 1 */
} gl_terr_draw_item_t;

typedef struct {
	GLuint prog;
	GLuint pending_prog;
	GLint uHeightTex;
	GLint uSubstrateTex;
	GLint uRiverTex;
	GLint uHeightScale;
	GLint uPlanetCenter;
	GLint uBodyRot;
	GLint uTorusRadii;
	GLint uCloudShadowMap;
	GLint uLambWakeHead;
	GLint uLambWakeXYT0;
	GLint uLambWakeXYT1;
	GLint uLambWakeVel0;
	GLint uLambWakeVel1;
	GLint uWorldId;
	GLint uSeaOffsetQ12_4;
	GLint uTreeShadowTex;
	GLint uTerrDetailTex;
	GLint uTerrNormTex;
	GLint uNoise2DTex;
	GLint uNoise3DTex;
	GLint aGridUV;
} gl_terrain_pipe_t;

typedef struct {
	GLuint prog;
	GLuint pending_prog;
	GLint uHeightTex;
	GLint uSubstrateTex;
	GLint uRiverTex;
	GLint uChunkOrigin;
	GLint uChunkRelOrigin;
	GLint uChunkSpan;
	GLint uChunkLayers;
	GLint uHeightScale;
	GLint uSeaLevel;
	GLint uPlanetCenter;
	GLint uBodyRot;
	GLint uTorusRadii;
	GLint uCloudShadowMap;
	GLint uLambWakeHead;
	GLint uLambWakeXYT0;
	GLint uLambWakeXYT1;
	GLint uLambWakeVel0;
	GLint uLambWakeVel1;
	GLint aGridUV;
	GLint uSceneColor;
	GLint uSceneDepth;
	GLint uViewport;
	GLint uSceneUvScale;
	GLint uNearFar;
	GLint uProjScale;
	GLint uNoise2DTex;
	GLint uNoise3DTex;
	GLint uWaterMode;
} gl_water_pipe_t;

typedef struct {
	GLuint prog;
	GLuint pending_prog;
	GLint uAtlasTex;
	GLint uAtlasNormTex;
	GLint uSeaLevel;
	GLint uCloudShadowMap;
	GLint uPass;
	GLint uCamAngles;
	GLint uHiZTex;
	GLint uHiZActive;
	GLint uTreeShadowTex;
	GLint uNoise2DTex;
	GLint uNoise3DTex;
	GLint aPos;
	GLint aUV;
	GLint aTint;
	GLint aLocalPos;
	GLint aQuadUV;
	GLint aSpriteBounds;
} gl_billboard_pipe_t;

typedef struct {
	GLuint prog;
	GLuint pending_prog;
	GLint uHeightTex;
	GLint uChunkOrigin;
	GLint uChunkSpan;
	GLint uChunkLayer;
	GLint uHeightScale;
	GLint uTreeShadowWriteOrigin;
	GLint uTreeShadowWriteVP;
	GLint aGridUV;
} gl_terrain_shadow_pipe_t;

typedef struct {
	uint16_t slot_layer;
	float ox;
	float oy;
	float span_w;
	float span_h;
	float base_z_wu;
} gl_terr_shadow_chunk_t;

#define MAX_TERR_SHADOW_QUEUE 64

typedef struct {
	GLuint prog;
	GLuint pending_prog;
	GLint uCamFwd;
	GLint uCamRight;
	GLint uCamUp;
	GLint uTanHalfAspect;
	GLint uEast;
	GLint uSouth;
	GLint uZenith;
	GLint uTorusEye;
	GLint uTorusRadii;
	GLint uAtmoHaloVis;
} gl_sky_pipe_t;

typedef struct {
	GLuint prog;
	GLuint pending_prog;
	GLint uSeaLevel;
	GLint uCloudShadowMap;
	GLint uShadowPass;
	GLint uBranchletTex;
	GLint uBranchletNorm;
	GLint uGrassTex;
	GLint uGrassNorm;
	GLint uBarkTex;
	GLint uBarkNorm;
	GLint uTreeShadowTex;
	GLint uNoise2DTex;
} gl_mesh3d_pipe_t;

typedef struct {
	GLuint prog;
	GLuint pending_prog;
	GLint uSeaLevel;
	GLint uTreeShadowWriteOrigin;
	GLint uTreeShadowWriteVP;
	GLint uBranchletTex;
	GLint uGrassTex;
	GLint uNoise2DTex;
} gl_mesh3d_shadow_pipe_t;

#define MAX_GRASS_CHUNKS 32
#define MAX_GRASS_VBO_CACHE 32
#define MAX_GRASS_TUFTS_PER_CHUNK (68 * 68)

/* 40-byte compact per-instance grass tuft descriptor (pre-culled on CPU per chunk) */
typedef struct {
	float tuft0[4];         /* loc 2: uv.x, uv.y, wz, rYaw */
	float tuft1[4];         /* loc 3: tuftW_base, tuftH_base, staticEnv, rHue */
	uint8_t sub_rgba[4];    /* loc 4 (GL_UNSIGNED_BYTE norm): substrate.rgb, 255 */
	uint8_t nterr_rgba[4];  /* loc 5 (GL_UNSIGNED_BYTE norm): nTerr.xyz * 0.5 + 0.5, 255 */
} gl_grass_tuft_t;

typedef struct {
	uint16_t world;
	uint8_t level;
	uint8_t active;
	uint32_t rev;
	int32_t ox, oy;
	int32_t span_w, span_h;
	int32_t grid_n;
	int32_t sea_level_q;    /* (int32_t)floorf(sea_level * 16.0f + 0.5f) */
	uint32_t last_touch;
	uint32_t tuft_count;
	GLuint vbo;
	bool vbo_allocated;
} gl_grass_vbo_slot_t;

typedef struct {
	GLuint prog;
	GLuint pending_prog;
	GLint uChunkOrigin;
	GLint uChunkRelOrigin;
	GLint uChunkSpan;
	GLint uPlanetCenter;
	GLint uBodyRot;
	GLint uTorusRadii;
	GLint uSeaLevel;
	GLint uCloudShadowMap;
	GLint uTreeShadowTex;
} gl_grass_pipe_t;

typedef struct {
	GLuint tuft_vbo;
	uint32_t tuft_count;
	int32_t ox, oy;
	int32_t span_w, span_h;
	uint8_t level;
	float sea_level;
	float center_x;
	float center_y;
	float rel_center[3];
	float rot_mat4[16];
	float major_r;
	float minor_r;
} gl_grass_chunk_t;

typedef struct {
	GLuint prog;
	GLuint pending_prog;
	GLint uHeightTex;
	GLint uSubstrateTex;
	GLint uChunkOrigin;
	GLint uChunkRelOrigin;
	GLint uChunkSpan;
	GLint uChunkLayer;
	GLint uSeaLevel;
	GLint uPlanetCenter;
	GLint uBodyRot;
	GLint uTorusRadii;
	GLint uCloudDeck;
	GLint uCloudShadowMap;
	GLint aGridUV;
	GLint uViewport;
	GLint uClusterGridDim;
	GLint uClusterDepthParams;
	GLint uClusterLightCount;
	GLint uClusterLightData;
	GLint uClusterLightListsTex;
	GLint uCloudResScale;
	GLint uNoise2DTex;
	GLint uNoise3DTex;
	GLint uCloudJitter;
} gl_cloud_pipe_t;

typedef struct {
	uint8_t count;
	uint8_t light_indices[ND_MAX_LIGHTS_PER_CLUSTER];
} nd_cluster_light_list_t;

typedef struct {
	GLuint prog;
	GLuint pending_prog;
	GLint uCsm;
	GLint uAnimTime;
	GLint uSeaWet;
	GLint uCloudShape3D;
} gl_csm_pipe_t;

typedef struct {
	GLuint prog;
	GLuint pending_prog;
	GLint uCloudTex;
	GLint uCloudUvScale;
	GLint uSceneUvScale;
	GLint uCloudDepthTex;
	GLint uSceneDepthTex;
	GLint uBilateralActive;
} gl_cloud_comp_pipe_t;

typedef struct {
	GLuint prog;
	GLuint pending_prog;
	GLint uCloudQuarterTex;
	GLint uCloudHistTex;
	GLint uCloudDepthTex;
	GLint uReprojMat;
	GLint uQuarterUvScale;
	GLint uPrevUvScale;
	GLint uCloudUvScale;
	GLint uCloudSubpixel;
	GLint uHistoryValid;
} gl_cloud_reproj_pipe_t;

#define ND_CLOUD_WTH_RES    256
#define ND_CLOUD_WTH_RINGS  2
#define ND_CLOUD_WTH_HALF_0 32768.0f
#define ND_CLOUD_WTH_HALF_1 262144.0f

typedef struct {
	GLuint prog;
	GLuint pending_prog;
	GLint uWthWin;
	GLint uAnimTime;
	GLint uSeaWet;
	GLint uWthFw;
} gl_cloud_wth_pipe_t;

typedef struct {
	GLuint tex;
	GLuint fbo;
	float cx, cy;
	float half;
	float rendered_time;
	float rendered_wet;
	bool valid;
} gl_cloud_wth_ring_t;

typedef struct {
	GLuint prog;
	GLuint pending_prog;
	GLint uCamFwd;
	GLint uCamRight;
	GLint uCamUp;
	GLint uTanHalfAspect;
	GLint uNearFar;
	GLint uCloudJitter;
	GLint uEast;
	GLint uSouth;
	GLint uZenith;
	GLint uEyeLocal;
	GLint uRefWorldXY;
	GLint uInvTorusScale;
	GLint uCurvature;
	GLint uTorusEye;
	GLint uTorusRadii;
	GLint uBodyRot;
	GLint uCloudParams;
	GLint uWthNearWin;
	GLint uWthFarWin;
	GLint uCloudWindDir;
	GLint uSceneUvScale;
	GLint uSeaLevel;
	GLint uSceneDepthTex;
	GLint uCloudWthNear;
	GLint uCloudWthFar;
	GLint uNoise2DTex;
	GLint uCloudShape3D;
} gl_cloud_vol_pipe_t;

typedef struct {
	GLuint prog;
	GLuint pending_prog;
	GLint uDepthTex;
	GLint uNearFar;
	GLint uProjScale;
	GLint uViewport;
	GLint uAnimTime;
	GLint uSceneUvScale;
} gl_ssao_pipe_t;

typedef struct {
	GLuint prog;
	GLuint pending_prog;
	GLint uSrcTex;
	GLint uTexelSize;
	GLint uPassDir;
	GLint uUvScale;
} gl_bloom_pipe_t;

typedef struct {
	GLuint prog;
	GLuint pending_prog;
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

typedef enum {
	ND_TERR_VAR_FULL        = 0,
	ND_TERR_VAR_DRY_NEAR    = 1,
	ND_TERR_VAR_DRY_CSM     = 2,
	ND_TERR_VAR_SEABED_NEAR = 3,
	ND_TERR_VAR_SEABED_MID  = 4,
	ND_TERR_VAR_MACRO       = 5,
	ND_TERR_VAR_MACRO_FAR   = 6,
	ND_TERR_VAR_COUNT       = 7
} nd_terr_var_t;

typedef enum {
	ND_CLOUD_VAR_FULL     = 0,
	ND_CLOUD_VAR_WIDE     = 1,
	ND_CLOUD_VAR_WIDE_CSM = 2,
	ND_CLOUD_VAR_COUNT    = 3
} nd_cloud_var_t;

typedef struct {
	uint16_t world;
	uint8_t level;
	uint8_t active;
	uint32_t rev;
	int32_t ox, oy;
	int32_t span_w, span_h;
	uint32_t tex_w, tex_h;
	uint32_t last_touch;
	int32_t min_h_q12_4;
	int32_t max_h_q12_4;
	float base_z_wu;     /* per-chunk midpoint height subtracted from RGBA16F R channel */
	uint8_t has_river;
	uint8_t has_cpos;
	uint8_t river_baked; /* 1 = river_array layer slot_idx has valid baked data */
	uint8_t last_grid_lod; /* 0..2 or 0xFF unset (hysteresis band for grid_lod selection) */
	float river_sea_level;
	float reveal_start_time; /* anim_time when chunk uploaded/upgraded for smooth CDLOD reveal */
	double cpos[3];
	float cpos_center[3];
	float cpos_rot[9];
} gl_chunk_tex_t;

typedef struct {
	int32_t ox, oy;
	int32_t span_w, span_h;
	uint8_t level;
	float sea_level;
	float base_z_wu;     /* per-chunk midpoint height for RGBA16F R channel reconstruction */
	float rel_center[3];
	float rot_mat4[16];
	float major_r;
	float minor_r;
	uint16_t slot_layer;
	int16_t river_layer; /* -1 = no river; 0..MAX_CHUNK_TEXTURES-1 = layer */
} gl_water_chunk_t;

typedef struct {
	bool initialized;
	bool pipelines_ready;
	bool pipelines_prewarmed;
	bool tier_b_queued;
	bool tier_b_done;
	bool prewarm_frame0_seen;
	uint8_t shader_queue_stage;
	uint8_t noise_lut_stage;
	uint32_t prewarmed_mask;
	GLuint prewarm_dummy_rgba;
	GLuint noise2d_tex;
	GLuint noise3d_tex;
	GLuint cloud_shape3d_tex;
	gl_terrain_pipe_t terrain_pipes[ND_TERR_VAR_COUNT];
	gl_water_pipe_t water_pipe;
	gl_billboard_pipe_t bb_pipe;
	gl_mesh3d_pipe_t mesh3d_pipe;
	gl_mesh3d_pipe_t mesh3d_opaque_pipe;
	gl_mesh3d_shadow_pipe_t mesh3d_shadow_alpha_pipe;
	gl_mesh3d_shadow_pipe_t mesh3d_shadow_opaque_pipe;
	gl_grass_pipe_t grass_pipe;
	gl_sky_pipe_t sky_pipe;
	gl_cloud_pipe_t cloud_pipes[ND_CLOUD_VAR_COUNT];
	gl_cloud_vol_pipe_t cloud_vol_pipe;
	gl_cloud_wth_pipe_t cloud_wth_pipe;
	gl_cloud_comp_pipe_t cloud_comp_pipe;
	gl_cloud_reproj_pipe_t cloud_reproj_pipe;
	gl_csm_pipe_t csm_pipe;
	gl_ssao_pipe_t ssao_pipe;
	gl_bloom_pipe_t bloom_pipe;
	gl_post_pipe_t post_pipe;
	gl_cloud_wth_ring_t cloud_wth_rings[ND_CLOUD_WTH_RINGS];
	bool cloud_wth_failed;
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
	/* Phase 6C Piece 2: 2x2 interleaved temporal checkerboard cloud targets & history */
	GLuint cloud_q_fbo;
	GLuint cloud_q_tex;
	GLuint cloud_q_depth_tex;
	GLuint cloud_hist_fbo[2];
	GLuint cloud_hist_tex[2];
	uint32_t cloud_qw;
	uint32_t cloud_qh;
	bool cloud_temporal_ok;
	float cloud_prev_mvp[16];
	double cloud_prev_origin[3];
	float cloud_prev_uv_scale[2];
	uint32_t cloud_prev_trw;
	uint32_t cloud_prev_trh;
	uint32_t cloud_phase;
	int cloud_hist_idx;
	bool cloud_hist_valid;
	GLuint ssao_fbo;
	GLuint ssao_tex;
	GLuint bloom_fbo[2];
	GLuint bloom_tex[2];
	uint32_t post_half_w;
	uint32_t post_half_h;
	uint32_t post_rw;
	uint32_t post_rh;
	bool scene_hdr;
	bool scene_hdr_r11f;
	bool scene_depth24;
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
	int scene_want_samples;
	bool scene_ok;
	bool scene_failed;
	bool frame_open;
	bool sky_rendered;
	float cur_space_t;
	bool res_depth_valid;
	bool res_color_valid;
	bool sprites_in_water;
	bool terrain_split_water_pending;

	GLuint grid_vbo_macro;
	GLuint grid_ibo_macro;
	GLuint grid_vbo;
	GLuint grid_ibo;
	GLuint grid_vbo_hi;
	GLuint grid_ibo_hi;
	GLuint grid_vao_arr[3]; /* 0 = macro (16x16), 1 = lo (24x24), 2 = hi (48x48) */
	GLuint terr_inst_vbo;

	GLuint bb_vbo;
	GLuint mesh3d_vbo;
	GLuint mesh3d_ibo;
	GLuint mesh3d_inst_vbo;
	GLuint mesh3d_vao;
	GLuint fol_atlas_tex;
	GLuint fol_atlas_norm_tex;
	GLuint fol_grass_tex;
	GLuint fol_grass_norm_tex;
	GLuint fol_bark_tex;
	GLuint fol_bark_norm_tex;
	GLuint terr_detail_tex;
	GLuint terr_norm_tex;
	uint8_t atlas_bake_stage;
	GLuint tree_shadow_fbo[2];
	GLuint tree_shadow_tex[2];
	GLuint tree_shadow_dummy_tex;
	int    tree_shadow_read_idx;
	bool   tree_shadow_ready;
	bool   tree_shadow_failed;
	bool   tree_shadow_read_valid;
	bool   tree_shadow_write_draws;
	bool   tree_shadow_skip_write;
	float  tree_shadow_rendered_time;
	int    tree_shadow_rendered_preset;
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
	GLuint                   grass_vbo_lod;
	GLuint                   grass_ibo_lod;
	GLsizei                  grass_idx_count_lod;
	gl_grass_vbo_slot_t      grass_cache[MAX_GRASS_VBO_CACHE];
	gl_grass_chunk_t         grass_q[MAX_GRASS_CHUNKS];
	uint32_t                 grass_q_count;
	GLuint atlas_tex;
	GLuint atlas_norm_tex;
	uint32_t atlas_ver;

	/* Shared GL_TEXTURE_2D_ARRAY chunk texture pools (Sub-Phase 6D) */
	GLuint height_array;
	GLuint substrate_array;
	GLuint river_array;
	uint32_t chunk_array_w;
	uint32_t chunk_array_h;
	int chunk_array_mips;
	bool substrate_array_mip_dirty;

	gl_chunk_tex_t chunk_cache[MAX_CHUNK_TEXTURES];
	uint16_t chunk_hash[CHUNK_CACHE_HASH_SIZE]; /* 0 = empty, slot_idx + 1 = occupied */
	uint32_t chunk_cache_used;
	uint32_t frame_count;
	float lod_px_scale;
	float lod_torus_sx;
	float lod_torus_sy;

	/* Deferred instanced terrain draw queue (flushed once in gl_end_terrain_pass) */
	gl_terr_draw_item_t terr_draw_q[MAX_TERR_DRAW_QUEUE];
	uint32_t terr_draw_count;
	gl_terr_body_t terr_bodies[MAX_TERR_BODIES];
	uint32_t terr_body_count;

	gl_water_chunk_t water_queue[MAX_WATER_QUEUE_BASE];
	uint32_t water_count;
	gl_water_chunk_t cloud_queue[MAX_CLOUD_QUEUE];
	uint32_t cloud_count;

	/* Occlusion stats for HUD/debugging */
	uint32_t water_culled_occ2d;
	uint32_t cloud_culled_hiz;
	uint32_t billboard_culled_hiz;

	/* Per-frame render counters for P0 profiler */
	uint32_t perf_terrain_chunks;
	uint32_t perf_bb_flushes;
	uint32_t perf_mesh3d_count;
	uint32_t perf_bb_count;

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
	bool hiz_prog_ready;
	GLint hiz_uDepthTex;
	GLint hiz_uTexelSize;
	GLint hiz_uUvScale;

	/* GPU Indirect Draw Command Buffer & Stats */
	nd_gpu_draw_elements_indirect_cmd_t indirect_cmds[ND_GPU_INDIRECT_MAX];
	uint32_t indirect_cmd_count;

	/* WebGL2 Frame-Global UBO (binding 0) & Flat uint32 Command Ring Buffer */
	GLuint frame_ubo;
	lam_gl_cmd_buf_t cmd_buf;
	uint32_t cmd_words[4096];
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

/* Internal shader accessors (public ones are in nd_seven_gl.h; all defined in nd_seven_gl_shaders.c) */
const char *nd_seven_gl_fs_cloud_comp(void);
const char *nd_seven_gl_fs_hiz_downsample(void);

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
void gl_bind_tree_shadow_read(GLint uTex);
void gl_render_cloud_shadow_map(void);
void gl_render_cloud_weather_rings(void);
void gl_bind_csm(GLint loc_tex);
void gl_reset_billboard_frame_state(void);
bool gl_eval_synoptic_is_clear_sky_cpu(float wx, float wy, float anim_time,
                                       float sea_wet, float fw,
                                       bool want_cirrus, float margin);

void gl_flush_terrain_queue(void);

static inline void gl_flush_cmd_ring(void)
{
	lam_gl_cmd_flush(&s_gl.cmd_buf);
}

static inline void gl_end_terrain_pass(void)
{
	if (s_gl.terr_draw_count > 0)
		gl_flush_terrain_queue();
	gl_flush_cmd_ring();
}

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

static inline float gl_eval_trail_weight(float px, float py, float slope_mag)
{
	if (slope_mag >= 0.40f)
		return 0.0f;
	float u1 = px * 0.0042f + py * 0.0029f
	         + 0.85f * sinf(py * 0.0061f - px * 0.0033f)
	         + 0.40f * cosf(px * 0.0117f + py * 0.0089f);
	float u2 = px * -0.0031f + py * 0.0046f
	         + 0.80f * sinf(px * 0.0057f + py * 0.0037f)
	         + 0.38f * cosf(py * 0.0109f - px * 0.0083f);
	float d1 = fabsf(sinf(u1));
	float d2 = fabsf(sinf(u2));
	float g1 = gl_smoothstep01(0.15f, 0.55f, sinf(px * 0.0011f - py * 0.0014f + 0.6f));
	float g2 = gl_smoothstep01(0.20f, 0.60f, cosf(px * 0.0013f + py * 0.0009f - 0.4f));
	float t1 = (1.0f - gl_smoothstep01(0.038f, 0.095f, d1)) * g1;
	float t2 = (1.0f - gl_smoothstep01(0.035f, 0.088f, d2)) * g2;
	float t = t1 > t2 ? t1 : t2;
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	return t * (1.0f - gl_smoothstep01(0.20f, 0.40f, slope_mag));
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
