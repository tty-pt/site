#ifndef ND_SEVEN_H
#define ND_SEVEN_H

#include <stdint.h>
#include "nd/bcp.h"
#include <nd/seven-world.h>
#include <ttypt/areole.h>
#include "nd_seven_motion.h"
#include "nd_seven_objects.h"

/*
 * The 3D "seven" stage: the NeverDark map as one continuous, scrolling 3D world
 * with the seven-ways lamb as the player. Pure C, dual-compiled.
 */

/* Feed a key event. Returns 1 if the key drives the lamb (and was consumed). */
int nd_seven_key(const char *key, int down);
void nd_seven_reset_input(void);
void nd_spatial_resolver_invalidate(void);

/*
 * Advance the lamb by dt seconds. Returns the NeverDark move key ('h','j','k',
 * 'l') when the lamb walked into a neighbouring tile, else 0.
 */
int nd_seven_tick(const nd_client_state_t *st, float dt);

/* Render the 3D WebGL world around the lamb for a w x h viewport. */
void nd_seven_render(uint32_t w, uint32_t h, const nd_client_state_t *st);
int nd_seven_shaders_ready(void);

/* Forget remembered tile glyphs (new session or world). */
void nd_seven_forget(void);

/*
 * The objects Seven places in map tile (col, row) of md, in tile-local units
 * (NDW_TILE_W x NDW_TILE_H), back to front. Same layout the stage draws, so
 * other views (the lamina minimap) can mirror it. Returns the count.
 */
int nd_seven_tile_objects(const nd_map_data_t *md, int col, int row, ndw_obj_t out[NDW_MAX_OBJ]);

/*
 * The border warp grid of the tile at absolute pos (x, y, z, world), memoised
 * (it is a pure function of pos): Seven's ground and the lamina minimap share
 * it, so the biome borders have one shape in both. Never NULL; valid until a
 * later call may evict it.
 */
const ndw_warp_grid_t *nd_seven_warp_grid(const int16_t pos[4]);

void nd_seven_lamb(int *fx, int *fy, int *dir, int *walking);
void nd_seven_set_lamb(int fx, int fy);
void nd_seven_spot(uint32_t ref, int *sx, int *sy);

/* Interaction: detects if the lamb is near an interactable object or entity,
 * returns 1 and formats the command string (e.g. "get stick\n") into out. */
int nd_seven_action(const nd_client_state_t *st, char *out, size_t sz);
int nd_seven_action_execute(const nd_client_state_t *st, char *out, size_t sz);

/* Lamb feet relative to the map's player tile, after adopting the state's
 * current origin (so a map update and the lamb never disagree by a tile). */
void nd_seven_lamb_world(const nd_client_state_t *st, int *fx, int *fy);
void nd_seven_debug(int *pending, int *pos_x, int *pos_y, int *have_room);
int16_t nd_seven_lamb_elevation(const nd_client_state_t *st, int32_t wx, int32_t wy);
int nd_seven_is_universe_owner(const nd_client_state_t *st);

/* Unified multi-scale terrain chunk system */
ar_system_t *nd_seven_get_chunk_sys(void);

/* Camera zoom controls */
void nd_seven_set_zoom(float zoom);
float nd_seven_get_zoom(void);
void nd_seven_zoom_in(void);
void nd_seven_zoom_out(void);

/* Diorama 2.5D & camera rendering constants (zero magic numbers) */
#define ND_SEVEN_SCALE              2
#ifndef ND_SEVEN_ZOOM_MIN
#define ND_SEVEN_ZOOM_MIN           0.00001f /* 100,000x deep space sector overview */
#endif
#ifndef ND_SEVEN_ZOOM_MAX
#define ND_SEVEN_ZOOM_MAX           8.0f     /* 8x magnification (inspection scale) */
#endif
#ifndef ND_SEVEN_ZOOM_STEP
#define ND_SEVEN_ZOOM_STEP          1.15f    /* Exponential step factor per key press */
#endif
#define ND_SEVEN_DIORAMA_PITCH      55.0f
#define ND_SEVEN_CAM_DIST           280.0f
#define ND_SEVEN_CAM_YAW            0.0f
#define ND_SEVEN_CAM_FOV            45.0f
#define ND_SEVEN_ANIM_TIME_FACTOR   0.05f
#define ND_SEVEN_SEA_LEVEL_NONE     (-999.0f)
#define ND_SEVEN_WATER_MAX_DEPTH    32

/* Planetary Torus Geometry & Celestial Sector Definitions: 2^16 x 2^16 rooms */
#define ND_SEVEN_WORLD_ROOMS_W      65536
#define ND_SEVEN_WORLD_ROOMS_H      65536
#define ND_SEVEN_WORLD_DIM_W        ((float)(ND_SEVEN_WORLD_ROOMS_W * NDW_TILE_W)) /* 10485760.0f */
#define ND_SEVEN_WORLD_DIM_H        ((float)(ND_SEVEN_WORLD_ROOMS_H * NDW_TILE_H)) /* 8388608.0f */

typedef struct {
	uint16_t world_id;
	float center[3];
	float major_r;
	float minor_r;
} nd_planet_def_t;

const nd_planet_def_t *nd_seven_get_planet(uint16_t world);

/* Multi-scale scenery billboard batch diagnostics (for unit verification) */
uint32_t nd_seven_last_mid_billboard_count(void);
int nd_seven_last_mid_billboard(uint32_t idx, float *x, float *y, float *z, float *w, float *h);

/* Botanical Micro-Ecology & Living Meadow Classifier:
 * Evaluates deterministic multi-octave noise + land-coupled botanical placement for (wx, wy).
 * Returns 1 if a meadow swatch/reed/flower is placed and populates out_zone, out_scale, and out_flip.
 * Returns 0 when submerged (elev_z <= water_level), on barren substrates, steep cliffs, or inside tree trunks. */
enum {
	ND_MEADOW_NONE       = 0,
	ND_MEADOW_REED       = 1,
	ND_MEADOW_WILDFLOWER = 2,
	ND_MEADOW_GRASS      = 3,
	ND_MEADOW_UNDERSTORY = 4,
	ND_MEADOW_TALL_GRASS = 5
};

int nd_seven_eval_meadow_area(int32_t wx, int32_t wy,
                              float elev_z, float water_level, float slope_mag,
                              unsigned biome, float near_tree_dist_sq,
                              int *out_zone, float *out_scale, int *out_flip);

int nd_seven_eval_meadow_tuft(int32_t wx, int32_t wy,
                              float elev_z, float water_level,
                              unsigned biome, float near_tree_dist_sq,
                              int *out_zone, int *out_flip);

int nd_seven_eval_meadow_lamb_occlusion(float wx_f, float wy_f,
                                        float lwx_f, float lwy_f,
                                        float *io_sort_y, float *io_scale);

/* Viewport LOD & Macro Chunk Thresholds (zero magic numbers) */
#define ND_SEVEN_STRATOSPHERE_ZOOM_THRESHOLD 0.25f  /* Camera zoom threshold below which macro quadtree LOD activates */
#define ND_SEVEN_FOCAL_WINDOW_RADIUS_ROOMS   3      /* 7x7 focal room grid radius around the player (-3..+3) */
#define ND_SEVEN_MACRO_LOD_MAX_DETAIL        3      /* Highest detail continuous LOD tier */
#define ND_SEVEN_MACRO_LOD_MID_LEVEL_CUTOFF  3      /* Quadtree level where mid-frequency LOD engages */
#define ND_SEVEN_MACRO_LOD_COARSE_LEVEL_CUTOFF 6    /* Quadtree level where coarse macro noise engages */
#define ND_SEVEN_BLEND_MAX_WEIGHT            128    /* Fixed-point blend weight denominator (0..128) */
#define ND_SEVEN_MAX_COLOR_CHANNEL           255    /* 8-bit color channel maximum value */

#endif /* ND_SEVEN_H */
