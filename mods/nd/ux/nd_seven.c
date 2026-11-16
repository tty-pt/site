#include <string.h>
#include <math.h>
#include <ttypt/lam.h>
#include <ttypt/areole.h>
#include <nd/seven-world.h>
#include <nd/field.h>
#include <nd/attr-bcp.h>
#include "nd_seven.h"
#include "nd_seven_atlas_draw.h"
#include "nd_seven_ground.h"
#include "nd_seven_motion.h"
#include "nd_seven_objects.h"
#include "nd_seven_gl.h"
#include <ttypt/webgl.h>

/* --- macro chunk hierarchy integration ---------------------------------- */

static ar_system_t *s_seven_chunk_sys = NULL;
static lam_particle_sys_t *s_water_particles = NULL;
static float s_prev_particle_time = 0.0f;
static float s_last_splash_emit_time = 0.0f;
static float s_last_spray_emit_time = 0.0f;
static float s_prev_lamb_wx = 0.0f;
static float s_prev_lamb_wy = 0.0f;
static float s_prev_lamb_vx = 0.0f;
static float s_prev_lamb_vy = 0.0f;
static int   s_prev_lamb_dir = -1;
static int   s_prev_lamb_walking = 0;
static int   s_prev_lamb_in_water = 0;

static int seven_has_water_at(uint16_t world, int32_t wx, int32_t wy, int16_t *out_wl);

static float seven_particle_surface_z(void *user_ctx, float wx, float wy)
{
	uint16_t world = user_ctx ? *(const uint16_t *)user_ctx : 0;
	int16_t w_lvl = 0;
	float gz = nd_seven_ground_elevation_f((int32_t)wx, (int32_t)wy);
	if (seven_has_water_at(world, (int32_t)wx, (int32_t)wy, &w_lvl) && (float)w_lvl > gz)
		return (float)w_lvl;
	return gz;
}

static inline uint32_t macro_blend_rgb(uint32_t c1, uint32_t c2, int alpha)
{
	if (alpha <= 0) return c1;
	if (alpha >= 255) return c2;
	uint32_t rb1 = c1 & 0x00FF00FF;
	uint32_t g1  = c1 & 0x0000FF00;
	uint32_t rb2 = c2 & 0x00FF00FF;
	uint32_t g2  = c2 & 0x0000FF00;
	uint32_t rb = (rb1 + (((rb2 - rb1) * alpha) >> 8)) & 0x00FF00FF;
	uint32_t g  = (g1  + (((g2  - g1 ) * alpha) >> 8)) & 0x0000FF00;
	return rb | g;
}

static void seven_macro_chunk_sampler(void *user_ctx, uint16_t world, uint8_t level,
                                     int32_t wx, int32_t wy, ar_sample_t *out)
{
	(void)user_ctx;
	(void)level;
	/* Always evaluate canonical full-precision field (lod=3) so that parent and child
	 * quadtree levels agree 100% on world elevation, coastlines, and water depth
	 * (zero elevation/shoreline popping when chunks split or merge). */
	nd_field_sample_t fs = nd_field_sample(world, 3, wx, wy);
	uint8_t water_depth = fs.is_water ? (uint8_t)(fs.depth > 255 ? 255 : fs.depth) : 0;

	out->val = fs.elevation_q12_4;
	out->attr[0] = fs.bio;
	out->attr[1] = fs.bio2;
	out->attr[2] = fs.blend;
	out->attr[3] = water_depth;
	out->attr[4] = 0;
	out->attr[5] = 0;
	out->attr[6] = 0;
	out->attr[7] = 0;
	/* Include shoreline micro-relief envelope (wz <= water_level + 4.0 units) so any chunk
	 * where sub-texel shoreline relief or capillary wet sand can reach sea level sets flag bit 0.
	 * On coarse chunks (level >= 2), ignore perched mountain-lake water overrides (fs.water_level > base_wl)
	 * so a single isolated mountain lake sample does not raise the sea level of an entire continental chunk. */
	int16_t base_wl = nd_field_water_level(world);
	uint8_t river_carve_q4 = (level <= 8) ? fs.river_carve_q4 : 0;
	int16_t eff_wl = (level <= 4 || river_carve_q4 > 0) ? fs.water_level : base_wl;
	bool eff_is_water = (level <= 4 || river_carve_q4 > 0) ? fs.is_water : ((fs.elevation_q12_4 >> 4) < base_wl);
	int32_t eff_depth = eff_is_water ? ((int32_t)eff_wl - (fs.elevation_q12_4 >> 4)) : 0;
	if (eff_depth < 0) eff_depth = 0;
	out->attr[3] = (uint8_t)(eff_depth > 255 ? 255 : eff_depth);
	out->attr[6] = river_carve_q4;
	int32_t shore_ceil_q12_4 = ((int32_t)eff_wl + 4) << 4;
	uint8_t flags = (eff_depth > 0 || (int32_t)fs.elevation_q12_4 <= shore_ceil_q12_4 || river_carve_q4 > 0) ? 1u : 0u;
	if (level <= 4 && fs.water_level > base_wl && fs.river_carve_q4 == 0)
		flags |= 2u;
	out->flags = flags;
	if (flags) {
		uint16_t wl_u16 = (uint16_t)eff_wl;
		out->attr[4] = (uint8_t)(wl_u16 & 0xFFu);
		out->attr[5] = (uint8_t)(wl_u16 >> 8);
	}

	uint32_t c1 = ndw_biome_base_rgb(fs.bio);
	uint32_t c2 = ndw_biome_base_rgb(fs.bio2);
	uint8_t blend = fs.blend;
	if (blend > 0 && fs.bio != fs.bio2) {
		/* Natural ecological ecotone mosaic: modulate smooth Whittaker blend (0..128)
		 * with multi-scale spatial patches and micro-relief (hollows vs rises) so
		 * adjacent biomes interdigitate organically without hard seams or flat washes. */
		int16_t p_eco[2] = { (int16_t)wy, (int16_t)wx };
		uint32_t n_meso = noise_wpoint_n(p_eco, 5, 8, 0x45434F31u ^ world, 2, NOISE_FADE5) >> 16;
		uint32_t n_loc  = noise_wpoint_n(p_eco, 3, 8, 0x45434F32u ^ world, 2, NOISE_FADE5) >> 16;
		int32_t eco_jitter = ((int32_t)n_meso - 128) + (((int32_t)n_loc - 128) >> 1) - ((int32_t)fs.fine_elev * 2);
		int32_t span = (blend <= 128) ? 128 : 252;
		int32_t env_w = (int32_t)blend * (span - (int32_t)blend);
		if (env_w > 4096) env_w = 4096;
		int32_t mod_blend = (int32_t)blend + ((eco_jitter * env_w) >> 11);
		if (mod_blend < 2) mod_blend = 2;
		if (mod_blend > span - 2) mod_blend = span - 2;
		blend = (uint8_t)mod_blend;
	}
	uint32_t col = (blend > 0 && fs.bio != fs.bio2)
		? macro_blend_rgb(c1, c2, (int)blend) : c1;

	/* Preserve clean biome chromaticity ratios (R:G:B) in uSubstrateTex so s_fs_terrain can
	 * classify all 8 ecological surface regimes accurately, while encoding a subtle neutral
	 * scalar micro-relief variation for 2D minimap and vertex variety. */
	{
		int fine_rel = (int)fs.fine_elev;
		int elev_rel = ((int)fs.elevation_q12_4 - ((int)fs.water_level << 4)) >> 4;
		if (elev_rel < -2000) elev_rel = -2000;
		if (elev_rel > 1854) elev_rel = 1854;

		/* Natural alpine snowline transition on mountain ranges (> 320..620 wu):
		 * Lowlands, plains, and foothills (< 320 wu) preserve their authentic biome color in
		 * uSubstrateTex, while mountain cordilleras and summits (> 320..620+ wu) are capped in white snow. */
		int snow_w = 0;
		if (elev_rel > 320 && !fs.is_water && fs.bio != BIOME_PERMANENT_ICE) {
			uint32_t c_snow = 0xFFFFFAF6u; /* Pure alpine snow RGB(246, 250, 255) */
			snow_w = (elev_rel <= 620) ? ((elev_rel - 320) * 255 / 300) : 255;
			col = macro_blend_rgb(col, c_snow, snow_w);
		}

		int r = (int)(col & 0xFF);
		int g = (int)((col >> 8) & 0xFF);
		int b = (int)((col >> 16) & 0xFF);
		if (fs.bio == BIOME_PERMANENT_ICE || snow_w >= 192) {
			/* Keep polar & summit snow crisp cool-white (B >= G >= R) so wPolar in s_fs_terrain is exact */
			int ice_shade = 252 + (fine_rel >> 3);
			if (ice_shade < 242) ice_shade = 242;
			if (ice_shade > 256) ice_shade = 256;
			r = (244 * ice_shade) >> 8;
			g = (249 * ice_shade) >> 8;
			b = (255 * ice_shade) >> 8;
		} else {
			/* Strictly neutral scalar shade_q8 in [242..262] so R:G:B chromaticity ratios never drift */
			int shade_q8 = 252 + (fine_rel >> 2);
			if (shade_q8 < 242) shade_q8 = 242;
			if (shade_q8 > 262) shade_q8 = 262;
			r = (r * shade_q8) >> 8;
			g = (g * shade_q8) >> 8;
			b = (b * shade_q8) >> 8;
		}
		if (r < 0) r = 0; else if (r > 255) r = 255;
		if (g < 0) g = 0; else if (g > 255) g = 255;
		if (b < 0) b = 0; else if (b > 255) b = 255;
		uint32_t alpha_u8 = 255u - (uint32_t)out->attr[6];
		col = (alpha_u8 << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
	}
	out->rgba = col;
}

ar_system_t *nd_seven_get_chunk_sys(void)
{
	if (!s_seven_chunk_sys) {
		ar_config_t cfg;
		memset(&cfg, 0, sizeof(cfg));
		cfg.base_w = NDW_TILE_W;
		cfg.base_h = NDW_TILE_H;
		cfg.grid_dim = 48;
		cfg.sampler = seven_macro_chunk_sampler;
		cfg.max_active = 160;
		cfg.pool_capacity = 384;
		cfg.max_levels = 17;
		cfg.default_rgba = 0xFF3C804Eu;
		cfg.period_x = 6.2831853f * 2000000.0f;
		cfg.period_y = 6.2831853f * 1335000.0f;
		s_seven_chunk_sys = ar_system_create_ex(&cfg);
	}
	return s_seven_chunk_sys;
}

/* --- tile objects -------------------------------------------------------- */

int nd_seven_tile_objects(const nd_map_data_t *md, int col, int row, ndw_obj_t out[NDW_MAX_OBJ])
{
	ndw_tile_t t;
	int16_t pos[4];

	if (!md || md->width == 0 || md->height == 0 ||
	    col < 0 || row < 0 || col >= md->width || row >= md->height ||
	    col >= ND_MAP_GRID_WIDTH || row >= ND_MAP_GRID_HEIGHT)
		return 0;
	pos[0] = (int16_t)(md->pos[0] + col - md->player_x);
	pos[1] = (int16_t)(md->pos[1] - (row - md->player_y));
	pos[2] = md->pos[2];
	pos[3] = md->pos[3];
	t = nd_seven_convert_tile(&md->tiles[row * md->width + col]);
	return ndw_tile_objects(&t, pos, 0, out);
}

static const int8_t s_spots[16][2] = {
	{ -16, 26 }, { 16, 26 }, { -28, 12 }, { 28, 12 }, { -38, -6 }, { 38, -6 }, { -22, -18 }, { 22, -18 },
	{ -52, 0 }, { 52, 0 }, { -46, 24 }, { 46, 24 }, { -30, 34 }, { 30, 34 }, { -46, -22 }, { 46, -22 }
};

void nd_seven_spot(uint32_t ref, int *sx, int *sy)
{
	uint32_t spot = ndw_mix(ref) % 16;
	if (sx) *sx = NDW_CENTER_X + s_spots[spot][0];
	if (sy) *sy = NDW_CENTER_Y + s_spots[spot][1];
}

static uint8_t entity_atlas_sprite(uint8_t sprite_id)
{
	if (sprite_id > 0 && sprite_id < NDS_COUNT)
		return sprite_id;
	return 0;
}

static int seven_has_water_at(uint16_t world, int32_t wx, int32_t wy, int16_t *out_wl)
{
	int16_t base_wl = nd_field_water_level(world);
	int16_t wl = base_wl;
	int has_w = 0;

	ar_system_t *cs = nd_seven_get_chunk_sys();
	if (cs) {
		ar_sample_t s;
		if (ar_probe_view(cs, 0, world, wx, wy, &s)) {
			if (s.flags & 1) {
				has_w = 1;
				int16_t s_wl = (int16_t)((uint16_t)s.attr[4] | ((uint16_t)s.attr[5] << 8));
				if (s_wl > wl)
					wl = s_wl;
			}
		}
	} else {
		nd_field_sample_t fs = nd_field_sample(world, 3, wx, wy);
		wl = fs.water_level;
		has_w = fs.is_water || (wl > base_wl);
	}

	int16_t gz = nd_seven_ground_elevation(wx, wy);
	if (gz < base_wl)
		has_w = 1;

	int ti = ndw_fdiv(wx, NDW_TILE_W);
	int pos_y = -ndw_fdiv(wy, NDW_TILE_H);
	if (ti == -3 && pos_y == 0)
		has_w = 1;

	if (out_wl)
		*out_wl = wl;
	return has_w;
}

int16_t nd_seven_lamb_elevation(const nd_client_state_t *st, int32_t wx, int32_t wy)
{
	(void)st;
	return nd_seven_ground_elevation(wx, wy);
}

float nd_seven_lamb_elevation_f(const nd_client_state_t *st, int32_t wx, int32_t wy)
{
	(void)st;
	return nd_seven_ground_elevation_f(wx, wy);
}

int nd_seven_is_universe_owner(const nd_client_state_t *st)
{
	if (!st)
		return 0;
	if ((st->pres_flags & ND_REGION_UNIVERSE_OWNER) != 0)
		return 1;
	for (size_t i = 0; i < (size_t)st->entity_count && i < sizeof(st->entities) / sizeof(st->entities[0]); i++) {
		if (st->entities[i].ref == st->me && (st->entities[i].flags & ND_EF_UNIVERSE_OWNER) != 0)
			return 1;
	}
	const nd_stats_state_t *ss = (const nd_stats_state_t *)nd_client_ext_get(st, ND_ATTR_EXT_KEY);
	if (ss && ss->attr[ND_ATTR_WIZ] > 0)
		return 1;
	if (st->me == 1 && (st->pres_flags & ND_REGION_LOCKED) == 0)
		return 1;
	return 0;
}

/* Celestial Body Definitions */
static const nd_planet_def_t s_planets[] = {
	{
		.world_id = 0,
		.center = { 0.0f, 0.0f, 0.0f },
		.major_r = 2000000.0f,
		.minor_r = 1335000.0f,
		.rot = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f },
		.axial_tilt_rad = (float)ND_FIELD_WORLD0_AXIAL_TILT,
		.spin_period = ND_ASTRO_SOLAR_DAY_SEC
	},
	{
		.world_id = 1,
		.center = { 0.0f, 0.0f, 0.0f },
		.major_r = 2000000.0f,
		.minor_r = 1335000.0f,
		.rot = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f },
		.axial_tilt_rad = (float)ND_FIELD_WORLD0_AXIAL_TILT,
		.spin_period = ND_ASTRO_SOLAR_DAY_SEC
	},
	{
		.world_id = 2,
		.center = { 4500000.0f, -14000000.0f, -8500000.0f },
		.major_r =  800000.0f,
		.minor_r =  500000.0f,
		.rot = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f },
		.axial_tilt_rad = (float)ND_FIELD_MOON_AXIAL_TILT,
		.spin_period = (float)ND_FIELD_MOON_ORBIT_PERIOD
	},
};

const nd_planet_def_t *nd_seven_get_planet(uint16_t world)
{
	for (size_t i = 0; i < sizeof(s_planets) / sizeof(s_planets[0]); i++) {
		if (s_planets[i].world_id == world)
			return &s_planets[i];
	}
	return &s_planets[0];
}

void nd_seven_eval_planet_at_time(uint16_t world, uint16_t observer_world,
                                  float t_sec, nd_planet_def_t *out_pdef)
{
	if (!out_pdef)
		return;
	memset(out_pdef, 0, sizeof(*out_pdef));
	corolla_body_t body, obs_body;
	nd_field_world_body(world, &body);
	nd_field_world_body(observer_world, &obs_body);

	out_pdef->world_id = world;
	out_pdef->major_r = (float)body.surf.r_major;
	out_pdef->minor_r = (float)body.surf.r_minor;
	out_pdef->axial_tilt_rad = (float)body.axial_tilt_rad;
	out_pdef->spin_period = (float)body.spin_period;

	if (world == observer_world) {
		out_pdef->center[0] = 0.0f;
		out_pdef->center[1] = 0.0f;
		out_pdef->center[2] = 0.0f;
		out_pdef->rot[0] = 1.0f; out_pdef->rot[1] = 0.0f; out_pdef->rot[2] = 0.0f;
		out_pdef->rot[3] = 0.0f; out_pdef->rot[4] = 1.0f; out_pdef->rot[5] = 0.0f;
		out_pdef->rot[6] = 0.0f; out_pdef->rot[7] = 0.0f; out_pdef->rot[8] = 1.0f;
		return;
	}

	corolla_pose_t target_pose, obs_pose, rel_pose;
	corolla_eval_body_pose(&body, NULL, (double)t_sec, &target_pose);
	corolla_eval_body_pose(&obs_body, NULL, (double)t_sec, &obs_pose);
	corolla_pose_relative(&obs_pose, NULL, &target_pose, &rel_pose);

	out_pdef->center[0] = (float)rel_pose.center[0];
	out_pdef->center[1] = (float)rel_pose.center[1];
	out_pdef->center[2] = (float)rel_pose.center[2];
	for (int i = 0; i < 9; i++)
		out_pdef->rot[i] = (float)rel_pose.rot[i];
}

typedef struct {
	corolla_surface_t surf;
	corolla_pose_t pose;
} seven_bounds_3d_ctx_t;

static void seven_torus_bounds_3d_cb(void *bounds_ctx, float ox, float oy, float w, float h,
                                     float z_min, float z_max,
                                     float out_min[3], float out_max[3],
                                     float out_center[3],
                                     float *out_span_w, float *out_span_h)
{
	const seven_bounds_3d_ctx_t *ctx = (const seven_bounds_3d_ctx_t *)bounds_ctx;
	double u0 = (double)ox / (double)ND_SEVEN_WORLD_DIM_W;
	double du = (double)w / (double)ND_SEVEN_WORLD_DIM_W;
	double v0 = ((double)oy / (double)ND_SEVEN_WORLD_DIM_H) + 0.5;
	double dv = (double)h / (double)ND_SEVEN_WORLD_DIM_H;

	double bmin[3], bmax[3];
	corolla_patch_aabb(&ctx->surf, &ctx->pose, NULL, u0, v0, du, dv,
	                   (double)z_min, (double)z_max, bmin, bmax);
	if (out_min) {
		out_min[0] = (float)bmin[0];
		out_min[1] = (float)bmin[1];
		out_min[2] = (float)bmin[2];
	}
	if (out_max) {
		out_max[0] = (float)bmax[0];
		out_max[1] = (float)bmax[1];
		out_max[2] = (float)bmax[2];
	}
	if (out_center) {
		double u_mid = u0 + 0.5 * du;
		double v_mid = v0 + 0.5 * dv;
		double h_mid = 0.5 * ((double)z_min + (double)z_max);
		corolla_frame_t fr = {0};
		corolla_eval_frame(&ctx->surf, u_mid, v_mid, h_mid, &fr);
		double p_world[3];
		corolla_pose_point_to_inertial(&ctx->pose, fr.pos, p_world);
		out_center[0] = (float)p_world[0];
		out_center[1] = (float)p_world[1];
		out_center[2] = (float)p_world[2];
	}
	double R_maj = ctx->surf.r_major;
	double r_min = ctx->surf.r_minor;
	if (out_span_w) *out_span_w = (float)(COROLLA_TWO_PI * R_maj * fabs(du));
	if (out_span_h) *out_span_h = (float)(COROLLA_TWO_PI * r_min * fabs(dv));
}

#define SEVEN_MAX_VISIBLE_LEAVES 160
static const ar_node_t *s_visible_leaves[SEVEN_MAX_VISIBLE_LEAVES];
static size_t s_visible_leaf_count = 0;
static ar_frustum_t s_frame_frustum;
static float s_frame_sag_radius = 16000.0f;
static int s_frame_frustum_valid = 0;

static inline bool seven_test_frustum_xy(float min_x, float min_y, float max_x, float max_y,
                                         float fx, float fy, float base_z_min, float base_z_max)
{
	if (!s_frame_frustum_valid)
		return true;
	float dx0 = min_x - fx, dx1 = max_x - fx;
	float dy0 = min_y - fy, dy1 = max_y - fy;
	float mx = (dx0 * dx0 > dx1 * dx1) ? (dx0 * dx0) : (dx1 * dx1);
	float my = (dy0 * dy0 > dy1 * dy1) ? (dy0 * dy0) : (dy1 * dy1);
	float r2_max = mx + my;
	float sag = r2_max * (1.0f / 2000000.0f);
	return ar_frustum_test_box(&s_frame_frustum,
	                           min_x, min_y, base_z_min - sag - 256.0f,
	                           max_x, max_y, base_z_max + 256.0f);
}

/* 3-state frustum test for 8x8 room blocks:
 *   0 = completely outside at least one plane
 *   1 = partially intersecting frustum (requires per-room frustum test)
 *   2 = all 8 corners inside all 6 frustum planes (all contained rooms guaranteed inside) */
static inline int seven_test_frustum_xy_state(float min_x, float min_y, float max_x, float max_y,
                                              float fx, float fy, float base_z_min, float base_z_max)
{
	if (!s_frame_frustum_valid)
		return 2;
	float dx0 = min_x - fx, dx1 = max_x - fx;
	float dy0 = min_y - fy, dy1 = max_y - fy;
	float mx = (dx0 * dx0 > dx1 * dx1) ? (dx0 * dx0) : (dx1 * dx1);
	float my = (dy0 * dy0 > dy1 * dy1) ? (dy0 * dy0) : (dy1 * dy1);
	float r2_max = mx + my;
	float sag = r2_max * (1.0f / 2000000.0f);
	float min_z = base_z_min - sag - 256.0f;
	float max_z = base_z_max + 256.0f;
	int all_inside = 1;
	for (int i = 0; i < 6; i++) {
		float a = s_frame_frustum.planes[i][0];
		float b = s_frame_frustum.planes[i][1];
		float c = s_frame_frustum.planes[i][2];
		float d = s_frame_frustum.planes[i][3];
		float px = (a > 0.0f) ? max_x : min_x;
		float py = (b > 0.0f) ? max_y : min_y;
		float pz = (c > 0.0f) ? max_z : min_z;
		if (a * px + b * py + c * pz + d < 0.0f)
			return 0;
		if (all_inside) {
			float nx = (a > 0.0f) ? min_x : max_x;
			float ny = (b > 0.0f) ? min_y : max_y;
			float nz = (c > 0.0f) ? min_z : max_z;
			if (a * nx + b * ny + c * nz + d < 0.0f)
				all_inside = 0;
		}
	}
	return all_inside ? 2 : 1;
}

/* Uniform altitude fade: all scenery fades out slowly and steadily across the
 * zoom-out progression starting around -15x (8,000), remaining visible at -22x
 * (~37,947), and fully dissolving before -23x (44,000) with zero radial rings. */
#define SCENERY_ALT_FADE_START 8000.0f
#define SCENERY_ALT_FADE_END   44000.0f
#define SCENERY_RAD_MAX_ROOMS   250

static inline float seven_smooth_fade(float val, float start, float end)
{
	if (val <= start)
		return 1.0f;
	if (val >= end)
		return 0.0f;
	float t = (val - start) / (end - start);
	return 1.0f - t;
}

static inline uint32_t seven_pack_white_alpha(float alpha)
{
	int a = (int)(alpha * 255.0f + 0.5f);
	if (a <= 0)
		return 0;
	if (a >= 255)
		return 0xFFFFFFFFu;
	return 0xFFFFFF00u | (uint32_t)a;
}

static void seven_dispatch_macro_backdrop(int w, int h, const nd_client_state_t *st,
                                         float zoom, float eff_cam_dist)
{
	(void)zoom;
	ar_system_t *cs = nd_seven_get_chunk_sys();
	if (!cs) {
		s_visible_leaf_count = 0;
		s_frame_frustum_valid = 0;
		return;
	}

	int64_t lwx = 0, lwy = 0;
	nd_seven_world_pos(st, &lwx, &lwy);
	uint16_t world = (uint16_t)(st ? st->map_data.pos[3] : 0);
	float anim_t = nd_seven_get_anim_time();

	nd_planet_def_t cur_pdef;
	nd_seven_eval_planet_at_time(world, world, anim_t, &cur_pdef);
	const nd_planet_def_t *pdef = &cur_pdef;

	float dist = (eff_cam_dist > 1.0f) ? eff_cam_dist : ND_SEVEN_CAM_DIST;
	float lwz_est = nd_seven_ground_elevation_f((int32_t)lwx, (int32_t)lwy);

	/* Compute canonical left-handed flat tangent-space MVP for near-field scenery culling */
	float flat_mvp[16];
	nd_seven_gl_compute_flat_mvp((uint32_t)w, (uint32_t)h, (float)lwx, (float)lwy, lwz_est,
	                             dist, ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_FOV, flat_mvp);
	ar_frustum_from_matrix(&s_frame_frustum, flat_mvp);
	s_frame_frustum_valid = 1;

	/* Compute true 3D curved-torus camera MVP for 3D manifold frustum culling */
	float mvp_3d[16], eye_3d[3];
	nd_seven_gl_compute_mvp((uint32_t)w, (uint32_t)h, (float)lwx, (float)lwy, lwz_est,
	                        dist, ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_FOV,
	                        pdef, mvp_3d, eye_3d, NULL, NULL);

	float r_planet = pdef->minor_r;
	float z_min_cull = lwz_est - 256.0f;
	float z_max_cull = lwz_est + 320.0f;
	float cull_margin = 96.0f;
	if (dist >= 1200.0f) {
		z_min_cull = -2600.0f;
		z_max_cull = 3200.0f;
		cull_margin = 180.0f;
	}
	s_frame_sag_radius = r_planet;

	float target_quad_px = 2.0f * (32.0f / 48.0f);
	size_t max_leaves = SEVEN_MAX_VISIBLE_LEAVES;
	uint8_t min_lvl = 0;

	/* Continuous logarithmic LOD stepping function from atmosphere through orbital space:
	 * Preserves higher chunk and biome resolution during zoom-out so biomes remain crisp
	 * multi-octave provinces rather than collapsing prematurely into coarse LODs. */
	if (dist >= 2500.0f) {
		float atmo_t = fminf(1.0f, fmaxf(0.0f, (dist - 2500.0f) / 42000.0f));
		target_quad_px = 1.33f + 3.80f * atmo_t;
		int lvl_step = (int)floorf(log2f(dist * (1.0f / 5000.0f)));
		if (lvl_step < 1) lvl_step = 1;
		if (lvl_step > 4) lvl_step = 4;
		min_lvl = (uint8_t)lvl_step;
	}

	seven_bounds_3d_ctx_t primary_bctx;
	primary_bctx.surf = corolla_torus((double)pdef->major_r, (double)pdef->minor_r);
	memset(&primary_bctx.pose, 0, sizeof(primary_bctx.pose));
	for (int i = 0; i < 9; i++)
		primary_bctx.pose.rot[i] = (double)pdef->rot[i];
	primary_bctx.pose.center[0] = (double)pdef->center[0];
	primary_bctx.pose.center[1] = (double)pdef->center[1];
	primary_bctx.pose.center[2] = (double)pdef->center[2];

	bool use_3d_primary = (dist >= 250000.0f);

	ar_cull_opts_t opts = {
		.z_min = z_min_cull,
		.z_max = z_max_cull,
		.sag_radius = r_planet,
		.focal_x = (float)lwx,
		.focal_y = (float)lwy,
		.bounds_3d_fn = use_3d_primary ? seven_torus_bounds_3d_cb : NULL,
		.bounds_ctx = use_3d_primary ? &primary_bctx : NULL,
		.margin = cull_margin,
		.target_quad_px = target_quad_px,
		.viewport_w = (float)w,
		.viewport_h = (float)h,
		.min_level = min_lvl,
		.max_level = 14,
		.view_id = 0,
		.max_populates_per_query = 48,
		.split_hysteresis = 0.14f
	};

	s_visible_leaf_count = ar_system_query_matrix(cs, world, use_3d_primary ? mvp_3d : flat_mvp,
	                                              &opts, s_visible_leaves, max_leaves);
	size_t macro_count = s_visible_leaf_count;

	/* Re-evaluate focal elevation on the newly active View 0 leaves */
	float lwz = nd_seven_ground_elevation_f((int32_t)lwx, (int32_t)lwy);
	float lamb_light_z = nd_seven_lamb_elevation_f(st, (int32_t)lwx, (int32_t)lwy);

	/* Begin 3D WebGL frame in pure C */
	nd_seven_gl_set_lamb_light((float)lwx, (float)lwy, lamb_light_z,
	                           nd_seven_is_universe_owner(st) ? 1.0f : 0.0f);
	nd_seven_gl_begin_frame((uint32_t)w, (uint32_t)h, (float)lwx, (float)lwy, lwz,
	                        dist, ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_FOV,
	                        anim_t, pdef);

	/* Update lamb wake ribbon & step 3D splash / surf-spray particles */
	{
		int16_t lamb_wl = 0;
		int16_t base_wl = nd_field_water_level(world);
		bool has_loc_water = seven_has_water_at(world, (int32_t)lwx, (int32_t)lwy, &lamb_wl);
		float live_wl = has_loc_water
		                ? nd_seven_gl_eval_shore_swash((float)lwx, (float)lwy, (float)lamb_wl, (float)base_wl,
		                                               lwz, 0.0f, 0.0f, anim_t, pdef, NULL, NULL)
		                : (float)lamb_wl;
		float eff_wl = fmaxf((float)lamb_wl, live_wl);
		bool lamb_in_water = has_loc_water && (lwz < eff_wl + 0.65f);
		if (lamb_in_water) {
			nd_seven_gl_set_focal_water(eff_wl);
		}
		float wake_ground_z = lamb_in_water ? (lwz - ((float)lamb_wl - (float)base_wl)) : (float)base_wl + 10.0f;

		/* Compute the exact 3D waterline intersection on the 55-deg tilted lamb billboard:
		 * In s_fs_billboard, the water plane Z = eff_wl intersects the sprite at vLocalPos.z = eff_wl,
		 * which projects onto the horizontal water plane at Y = lwy + ((lamb_render_z + 6.0) - eff_wl) / sin(55 deg).
		 * Anchoring both the wake trail and splash droplets here makes the trail and submerged level agree 100%. */
		float lamb_render_z = lamb_light_z;
		float waterline_dy = lamb_in_water
		                     ? fmaxf(0.0f, fminf(6.5f, (lamb_render_z + 6.0f) - eff_wl))
		                     : 4.5f;
		float wake_wx = (float)lwx;
		float wake_wy = (float)lwy + (waterline_dy / 0.819152f);

		int walking = nd_seven_get_walking();
		int dir = nd_seven_get_dir();
		nd_seven_gl_set_lamb_wake(wake_wx, wake_wy, wake_ground_z,
		                          dir, walking, anim_t);

		if (!s_water_particles)
			s_water_particles = lam_particle_sys_create(512, 42.0f);
		if (s_water_particles) {
			float dt = (s_prev_particle_time > 0.0f && anim_t >= s_prev_particle_time)
			           ? (anim_t - s_prev_particle_time) : 0.033f;
			if (dt > 0.12f || dt <= 0.0005f) dt = 0.016f;
			s_prev_particle_time = anim_t;

			/* 1. Sudden-movement impulse detection for lamb water droplets:
			 * Track velocity and acceleration so droplets burst when there is sudden movement
			 * (water entry, sudden start from standstill, sharp turn/reversal, or acceleration spike). */
			float dx = (s_prev_lamb_dir >= 0) ? ((float)lwx - s_prev_lamb_wx) : 0.0f;
			float dy = (s_prev_lamb_dir >= 0) ? ((float)lwy - s_prev_lamb_wy) : 0.0f;
			float dist_step = sqrtf(dx * dx + dy * dy);
			float cur_vx = (dist_step < 48.0f && dt > 0.001f) ? (dx / dt) : 0.0f;
			float cur_vy = (dist_step < 48.0f && dt > 0.001f) ? (dy / dt) : 0.0f;
			float dvx = cur_vx - s_prev_lamb_vx;
			float dvy = cur_vy - s_prev_lamb_vy;
			float accel = sqrtf(dvx * dvx + dvy * dvy) / fmaxf(dt, 0.012f);

			bool sudden_start = (walking && !s_prev_lamb_walking);
			bool sudden_turn  = (walking && s_prev_lamb_dir >= 0 && dir != s_prev_lamb_dir);
			bool water_entry  = (lamb_in_water && !s_prev_lamb_in_water && walking);
			bool accel_spike  = (walking && accel > 260.0f && dist_step > 0.25f);
			bool sudden_move  = sudden_start || sudden_turn || water_entry || accel_spike;

			s_prev_lamb_wx = (float)lwx;
			s_prev_lamb_wy = (float)lwy;
			s_prev_lamb_vx = cur_vx;
			s_prev_lamb_vy = cur_vy;
			s_prev_lamb_dir = dir;
			s_prev_lamb_walking = walking;
			s_prev_lamb_in_water = lamb_in_water ? 1 : 0;

			if (lamb_in_water && (eff_wl - lwz < 14.0f) && walking && dist < 1800.0f) {
				float min_interval = sudden_move ? 0.018f : 0.140f;
				if (sudden_move || (anim_t - s_last_splash_emit_time >= min_interval || anim_t < s_last_splash_emit_time)) {
					s_last_splash_emit_time = anim_t;
					float dir_vx = (dir == 3) ? 1.0f : (dir == 2 ? -1.0f : 0.0f);
					float dir_vy = (dir == 0) ? 1.0f : (dir == 1 ? -1.0f : 0.0f);
					float impulse_boost = sudden_move ? 1.35f : 0.78f;
					lam_particle_emitter_t em;
					memset(&em, 0, sizeof(em));
					em.x = wake_wx - dir_vx * 1.4f;
					em.y = wake_wy - dir_vy * 1.4f;
					em.z = eff_wl + 0.30f;
					em.spread_x = sudden_move ? 3.4f : 2.2f;
					em.spread_y = sudden_move ? 2.8f : 1.8f;
					em.spread_z = 0.22f;
					em.dir_x = -dir_vx * 0.68f;
					em.dir_y = -dir_vy * 0.68f;
					em.dir_z = sudden_move ? 0.88f : 0.74f;
					em.speed_min = 11.0f * impulse_boost;
					em.speed_max = 22.0f * impulse_boost;
					em.cone_spread = sudden_move ? 0.68f : 0.42f;
					em.size0_min = 0.54f;
					em.size0_max = sudden_move ? 0.88f : 0.74f;
					em.size1_min = 0.34f;
					em.size1_max = 0.56f;
					em.life_min = 0.22f;
					em.life_max = sudden_move ? 0.48f : 0.34f;
					em.drag = 0.95f;
					em.gravity_scale = 1.05f;
					em.restitution = -1.0f;
					em.rgba0 = sudden_move ? 0xFFFFFFC4u : 0xFFFFFF90u;
					em.rgba1 = 0xFFFFFF00u;
					em.count = water_entry ? 22u : (sudden_move ? 16u : 3u);
					em.kind = 1;
					lam_particle_emit(s_water_particles, &em);
				}
			}

			/* 2. Water-body wave-surge spindrift & shoreline wave-impact mist emitter:
			 * Triggered only where there is sudden wave movement (steep breaking crests or surging shore impact). */
			if (dist < 1800.0f && (anim_t - s_last_spray_emit_time >= 0.055f || anim_t < s_last_spray_emit_time)) {
				s_last_spray_emit_time = anim_t;
				uint32_t p_hash = (uint32_t)(anim_t * 53.0f) * 0x9E3779B9u;
				int spawned_batches = 0;
				for (int p_try = 0; p_try < 14 && spawned_batches < 2; p_try++) {
					p_hash ^= p_hash << 13;
					p_hash ^= p_hash >> 17;
					p_hash ^= p_hash << 5;
					int32_t ox = (int32_t)(p_hash & 0x1FFu) - 256;
					int32_t oy = (int32_t)((p_hash >> 9) & 0x1FFu) - 256;
					int32_t sx = (int32_t)lwx + ox;
					int32_t sy = (int32_t)lwy + oy;
					int16_t loc_wl = 0;
					if (!seven_has_water_at(world, sx, sy, &loc_wl))
						continue;
					float gz = nd_seven_ground_elevation_f(sx, sy);
					float depth = (float)loc_wl - gz;
					if (depth < -1.5f)
						continue;
					float w_gx = 0.0f, w_gy = 0.0f, w_crest = 0.0f;
					float w_h = nd_seven_gl_eval_water_waves((float)sx, (float)sy, anim_t, dist, 0.35f, &w_gx, &w_gy, &w_crest);
					float wave_slope = sqrtf(w_gx * w_gx + w_gy * w_gy);
					bool near_shore = (fabsf(depth) <= 2.4f);
					float foam = 0.0f;
					float sw_z = (float)loc_wl + w_h * 0.35f;
					float gx = 0.0f, gy = 0.0f, slope_mag = 0.0f;
					if (near_shore) {
						gx = (nd_seven_ground_elevation_f(sx + 4, sy) - nd_seven_ground_elevation_f(sx - 4, sy)) * 0.125f;
						gy = (nd_seven_ground_elevation_f(sx, sy + 4) - nd_seven_ground_elevation_f(sx, sy - 4)) * 0.125f;
						slope_mag = sqrtf(gx * gx + gy * gy);
						sw_z = nd_seven_gl_eval_shore_swash((float)sx, (float)sy, (float)loc_wl, (float)base_wl,
						                                    gz, gx, gy, anim_t, pdef, NULL, &foam);
					}
					/* Trigger spindrift only on sudden steep wave peaks or strong shore run-up surges */
					bool active_open_wave = (depth > 0.50f) && (w_crest > 0.30f && wave_slope > 0.32f);
					bool active_shore_wave = near_shore && (foam > 0.14f || (slope_mag > 0.08f && w_crest > 0.24f && w_h > 0.24f));
					if (active_open_wave || active_shore_wave) {
						lam_particle_emitter_t em;
						memset(&em, 0, sizeof(em));
						em.x = (float)sx;
						em.y = (float)sy;
						em.z = fmaxf((float)loc_wl, sw_z) + 0.35f;
						em.spread_x = 4.0f;
						em.spread_y = 4.0f;
						em.spread_z = 0.25f;
						if (active_shore_wave && slope_mag > 1e-3f) {
							em.dir_x = (gx / slope_mag) * 0.52f;
							em.dir_y = (gy / slope_mag) * 0.52f;
						} else {
							em.dir_x = 0.52f;
							em.dir_y = -0.45f;
						}
						em.dir_z = 0.85f;
						em.speed_min = 9.0f;
						em.speed_max = 19.0f;
						em.cone_spread = 0.62f;
						em.size0_min = 0.52f;
						em.size0_max = 0.82f;
						em.size1_min = 0.32f;
						em.size1_max = 0.55f;
						em.life_min = 0.22f;
						em.life_max = 0.46f;
						em.drag = 1.15f;
						em.gravity_scale = 0.95f;
						em.restitution = -1.0f;
						em.rgba0 = 0xFFFFFF98u;
						em.rgba1 = 0xFFFFFF00u;
						em.count = active_shore_wave ? 6u : 4u;
						em.kind = 2;
						lam_particle_emit(s_water_particles, &em);
						spawned_batches++;
					}
				}
			}

			lam_particle_step(s_water_particles, dt, 6.5f, -5.5f, 0.0f,
			                  seven_particle_surface_z, &world);
		}
	}

	for (size_t m = 0; m < macro_count; m++) {
		const ar_node_t *node = s_visible_leaves[m];
		if (!node) continue;

		int16_t w_lvl_i = nd_field_water_level(world);
		bool has_water = ((node->flags & 1) != 0 || node->min_val <= ((int32_t)(w_lvl_i + 8) << 4));
		if (node->level <= 4 && (node->flags & 2) != 0 && node->samples &&
		    node->min_val > ((int32_t)(w_lvl_i + 8) << 4)) {
			uint32_t vcnt = node->vert_count;
			for (uint32_t vi = 0; vi < vcnt; vi++) {
				if (node->samples[vi].flags & 2) {
					int16_t s_wl = (int16_t)((uint16_t)node->samples[vi].attr[4] |
					                         ((uint16_t)node->samples[vi].attr[5] << 8));
					if (s_wl > w_lvl_i) {
						w_lvl_i = s_wl;
						break;
					}
				}
			}
		}
		float sea_lvl = has_water ? (float)w_lvl_i : ND_SEVEN_SEA_LEVEL_NONE;
		uint32_t vdim = node->vert_dim;

		nd_seven_gl_flush_terrain_chunk(world, node->level, node->rev,
		                                node->ox, node->oy, node->w, node->h,
		                                node->rgba, node->values,
		                                vdim, vdim,
		                                (uint32_t)w, (uint32_t)h,
		                                sea_lvl, pdef);
	}

	/* Multi-Planet 3D Manifold Rendering:
	 * Evaluate the companion body's dynamic 3D Keplerian orbit & tilted spin pose at anim_t,
	 * and query libareole with true 3D curved-torus bounding boxes (seven_torus_bounds_3d_cb)
	 * against the 3D camera frustum across all zoom levels. */
	{
		uint16_t alt_world = (world == 2) ? 1 : 2;
		nd_planet_def_t alt_pdef;
		nd_seven_eval_planet_at_time(alt_world, world, anim_t, &alt_pdef);

		ar_frustum_t frustum_3d;
		ar_frustum_from_matrix(&frustum_3d, mvp_3d);
		float body_rad = alt_pdef.major_r + alt_pdef.minor_r + 8000.0f;
		if (ar_frustum_test_box(&frustum_3d,
		                        alt_pdef.center[0] - body_rad,
		                        alt_pdef.center[1] - body_rad,
		                        alt_pdef.center[2] - body_rad,
		                        alt_pdef.center[0] + body_rad,
		                        alt_pdef.center[1] + body_rad,
		                        alt_pdef.center[2] + body_rad)) {
			seven_bounds_3d_ctx_t alt_bctx;
			alt_bctx.surf = corolla_torus((double)alt_pdef.major_r, (double)alt_pdef.minor_r);
			memset(&alt_bctx.pose, 0, sizeof(alt_bctx.pose));
			for (int i = 0; i < 9; i++)
				alt_bctx.pose.rot[i] = (double)alt_pdef.rot[i];
			alt_bctx.pose.center[0] = (double)alt_pdef.center[0];
			alt_bctx.pose.center[1] = (double)alt_pdef.center[1];
			alt_bctx.pose.center[2] = (double)alt_pdef.center[2];

			ar_cull_opts_t alt_opts = opts;
			alt_opts.view_id = 2;
			alt_opts.min_level = 10;
			alt_opts.max_level = 14;
			alt_opts.bounds_3d_fn = seven_torus_bounds_3d_cb;
			alt_opts.bounds_ctx = &alt_bctx;

			const ar_node_t *alt_visible[32];
			size_t alt_count = ar_system_query_matrix(cs, alt_world, mvp_3d, &alt_opts, alt_visible, 32);
			int16_t alt_wl_i = nd_field_water_level(alt_world);
			for (size_t m = 0; m < alt_count; m++) {
				const ar_node_t *node = alt_visible[m];
				if (!node) continue;
				uint32_t vdim = node->vert_dim;

				nd_seven_gl_flush_terrain_chunk(alt_world, node->level, node->rev,
				                                node->ox, node->oy, node->w, node->h,
				                                node->rgba, node->values,
				                                vdim, vdim,
				                                (uint32_t)w, (uint32_t)h,
				                                (float)alt_wl_i, &alt_pdef);
			}
		}
	}

	/* The water queue is flushed after the sprites (nd_seven_render): the water pass composites what stands below the surface */
}

#define MID_MAX_DRAW 262144
#define MID_STREAM_CHUNK 2048
static lam_billboard_gpu_t s_mid_gpu_batch[MID_MAX_DRAW];
static lam_billboard_gpu_t s_mid_stream_batch[MID_STREAM_CHUNK];
static uint32_t s_mid_batch_len = 0;
static uint32_t s_last_mid_gpu_count = 0;

static void seven_apply_obj_billboard_style(const ndw_obj_t *o,
                                            const nd_seven_sprite_rect_t *sr,
                                            uint32_t base_tint,
                                            lam_billboard_gpu_t *bb)
{
	bb->tint_rgba = base_tint;
	bb->style = ND_BB_STYLE_STANDARD;
	bb->shadow_rx = 0;
	bb->shadow_ry = 0;
	bb->pad0 = 0;
	if (!o)
		return;

	bool is_non_trunk_plant =
		(ND_PLANT_UNPACK_PRESENT(o->plant_id) && o->col_mask == ND_PLANT_COL_NONE) ||
		(o->glyph == 'f' || o->glyph == 'F' || o->glyph == 'r' || o->glyph == 'R') ||
		(sr && sr->h <= 30 && o->kind != NDW_K_CACTUS);

	switch (o->kind) {
	case NDW_K_DECID:
	case NDW_K_TALL:
		if (o->plant_id && nd_seven_plant_has_normal(o->plant_id) && sr && sr->w > 80) {
			uint32_t rx = (uint32_t)(sr->w * 0.16f);
			if (rx > 48u) rx = 48u;
			bb->shadow_rx = (uint8_t)rx;
			bb->shadow_ry = (uint8_t)(rx * 0.45f > 6.0f ? rx * 0.45f : 6.0f);
		} else {
			bb->shadow_rx = (uint8_t)(is_non_trunk_plant ? (o->big ? 8 : 5) : (o->big ? 14 : 10));
			bb->shadow_ry = (uint8_t)(is_non_trunk_plant ? 3 : (o->big ? 6 : 4));
		}
		bb->pad0 = is_non_trunk_plant ? 1u : 2u;
		bb->style = ND_BB_STYLE_FLORA;
		break;
	case NDW_K_FIR:
		bb->shadow_rx = (uint8_t)(o->big ? 10 : 7);
		bb->shadow_ry = 4;
		bb->style = ND_BB_STYLE_FLORA;
		bb->pad0 = is_non_trunk_plant ? 1u : 2u;
		break;
	case NDW_K_CACTUS:
		bb->shadow_rx = (uint8_t)(o->big ? 9 : 6);
		bb->shadow_ry = 4;
		bb->style = ND_BB_STYLE_FLORA;
		bb->pad0 = 0;
		break;
	case NDW_K_STONE:
		bb->shadow_rx = 4;
		bb->shadow_ry = 2;
		bb->style = ND_BB_STYLE_MINERAL;
		break;
	case NDW_K_ROCK:
		bb->shadow_rx = 7;
		bb->shadow_ry = 3;
		bb->style = ND_BB_STYLE_MINERAL;
		break;
	case NDW_K_BOULDER:
		bb->shadow_rx = 11;
		bb->shadow_ry = 5;
		bb->style = ND_BB_STYLE_MINERAL;
		break;
	default:
		break;
	}
}

static void seven_calc_obj_billboard_dims(const ndw_obj_t *o,
                                          const nd_seven_sprite_rect_t *sr,
                                          float *out_w, float *out_h, float *out_ay)
{
	float w = (float)sr->w;
	float h = (float)sr->h;
	float ay = (float)ndw_obj_anchor_y(o, sr->h);
	if (o && ND_PLANT_UNPACK_PRESENT(o->plant_id) && h > 1.0f &&
	    !nd_seven_plant_has_normal(o->plant_id)) {
		unsigned sp_idx = ND_PLANT_UNPACK_IDX(o->plant_id);
		unsigned is_big = ND_PLANT_UNPACK_BIG(o->plant_id);
		float max_h = is_big ? 62.0f : 38.0f;
		float max_w = is_big ? 64.0f : 40.0f;
		if (sp_idx == 7u || sp_idx == 13u) {
			/* Acacia & Olive: wide spreading crowns, moderate height */
			max_h = is_big ? 54.0f : 34.0f;
			max_w = is_big ? 68.0f : 44.0f;
		} else if (sp_idx == 6u) {
			/* Cactus: slender columnar profile */
			max_h = is_big ? 46.0f : 28.0f;
			max_w = is_big ? 28.0f : 20.0f;
		} else if (sp_idx == 15u) {
			/* Typha latifolia (bulrush): compact wetland reed clump */
			max_h = is_big ? 34.0f : 24.0f;
			max_w = is_big ? 28.0f : 20.0f;
		} else if (o->col_mask == ND_PLANT_COL_NONE) {
			/* Herbs, flax, wild carrot, tomato, daisy */
			max_h = is_big ? 28.0f : 20.0f;
			max_w = is_big ? 30.0f : 22.0f;
		}
		float scale = 1.0f;
		if (h > max_h)
			scale = max_h / h;
		if (w * scale > max_w)
			scale = max_w / w;
		if (scale < 1.0f) {
			w *= scale;
			h *= scale;
			ay *= scale;
		}
	}
	*out_w = w;
	*out_h = h;
	*out_ay = ay;
}

static inline void seven_sample_node_smooth(const ar_node_t *node, int32_t wx, int32_t wy,
                                            int16_t default_wl, int16_t *out_elev_q, int16_t *out_wl)
{
	int gdim = (int)node->grid_dim;
	int vdim = (int)node->vert_dim;

	float fx = ((float)(wx - node->ox) / (float)node->w) * (float)gdim;
	float fy = ((float)(wy - node->oy) / (float)node->h) * (float)gdim;
	int col0 = (int)fx;
	int row0 = (int)fy;
	if (col0 < 0) col0 = 0;
	if (col0 >= gdim) col0 = gdim - 1;
	if (row0 < 0) row0 = 0;
	if (row0 >= gdim) row0 = gdim - 1;
	int col1 = col0 + 1;
	int row1 = row0 + 1;

	float u = fx - (float)col0;
	float v = fy - (float)row0;
	if (u < 0.0f) u = 0.0f;
	if (u > 1.0f) u = 1.0f;
	if (v < 0.0f) v = 0.0f;
	if (v > 1.0f) v = 1.0f;
	float wu = u * u * (3.0f - 2.0f * u);
	float wv = v * v * (3.0f - 2.0f * v);

	float v00 = (float)node->values[row0 * vdim + col0];
	float v10 = (float)node->values[row0 * vdim + col1];
	float v01 = (float)node->values[row1 * vdim + col0];
	float v11 = (float)node->values[row1 * vdim + col1];

	float h0 = v00 * (1.0f - wu) + v10 * wu;
	float h1 = v01 * (1.0f - wu) + v11 * wu;
	float h  = h0 * (1.0f - wv) + h1 * wv;
	*out_elev_q = (int16_t)(h >= 0.0f ? (h + 0.5f) : (h - 0.5f));

	int16_t local_wl = default_wl;
	if (node->samples) {
		int near_c = (u < 0.5f) ? col0 : col1;
		int near_r = (v < 0.5f) ? row0 : row1;
		const ar_sample_t *s_near = &node->samples[near_r * vdim + near_c];
		if (s_near->flags & 1) {
			int16_t s_wl = (int16_t)((uint16_t)s_near->attr[4] | ((uint16_t)s_near->attr[5] << 8));
			if (s_wl > local_wl)
				local_wl = s_wl;
		} else {
			const int rc[4][2] = { { row0, col0 }, { row0, col1 }, { row1, col0 }, { row1, col1 } };
			for (int qi = 0; qi < 4; qi++) {
				const ar_sample_t *sq = &node->samples[rc[qi][0] * vdim + rc[qi][1]];
				if (sq->attr[6] > 0) {
					int16_t s_wl = (int16_t)((uint16_t)sq->attr[4] | ((uint16_t)sq->attr[5] << 8));
					if (s_wl > local_wl)
						local_wl = s_wl;
				}
			}
		}
	}
	*out_wl = local_wl;
}

#define BLOCK_BB_CACHE_SLOTS 4096u
#define BLOCK_BB_ARENA_CAP   131072u
#define BLOCK_BB_MAX_PER_BLK 192u

typedef struct {
	int32_t  bx0, by0;
	int32_t  node_ox, node_oy;
	uint32_t node_rev;
	uint32_t atlas_ver;
	uint32_t forget_seq;
	uint64_t arena_seq;
	uint32_t arena_off;
	uint16_t count;
	int16_t  world, rz, w_level;
	uint8_t  node_level, valid;
} block_bb_cache_slot_t;

static block_bb_cache_slot_t s_block_bb_cache[BLOCK_BB_CACHE_SLOTS];
static lam_billboard_gpu_t   s_block_bb_arena[BLOCK_BB_ARENA_CAP];
static uint64_t              s_block_bb_arena_cursor = 1;

static int seven_emit_room_billboards_ex(const nd_client_state_t *st, const ar_node_t *node,
                                         const int16_t pos[4], int16_t w_level,
                                         uint32_t tint, bool pure_virgin,
                                         lam_billboard_gpu_t *arena_dst, uint16_t *inout_arena_cnt)
{
	if (tint == 0)
		return 0;

	ndw_obj_t scratch_objs[NDW_MAX_OBJ];
	int n_raw = 0;
	const ndw_obj_t *raw_objs = pure_virgin
		? nd_seven_resolve_virgin_objects_fast(pos, &n_raw)
		: nd_seven_resolve_room_objects_fast(st, pos, scratch_objs, &n_raw);
	if (n_raw <= 0 || !raw_objs)
		return 0;

	int32_t base_wx = (int32_t)pos[0] * NDW_TILE_W;
	int32_t base_wy = -(int32_t)pos[1] * NDW_TILE_H;
	int emitted = 0;

	for (int k = 0; k < n_raw; k++) {
		nd_seven_sprite_rect_t sr_val;
		if (!nd_seven_obj_rect(&raw_objs[k], &sr_val))
			continue;
		const nd_seven_sprite_rect_t *sr = &sr_val;

		int32_t wx = base_wx + raw_objs[k].x;
		int32_t wy = base_wy + raw_objs[k].y;

		int16_t elev_q = 0, local_wl = w_level;
		seven_sample_node_smooth(node, wx, wy, w_level, &elev_q, &local_wl);
		int16_t wz = nd_field_elevation_to_block(elev_q);

		if (wz <= local_wl && node->level >= 2) {
			int16_t canon_elev = nd_field_sample((uint16_t)pos[3], 0, wx, wy).elevation;
			if (canon_elev > local_wl)
				wz = canon_elev;
		}

		if (wz <= local_wl &&
		    raw_objs[k].kind >= NDW_K_DECID && raw_objs[k].kind <= NDW_K_CACTUS)
			continue; /* Anti-submersion for flora */

		float z_f = (float)elev_q / 16.0f;

		if (s_mid_batch_len == MID_STREAM_CHUNK) {
			nd_seven_gl_flush_billboards(s_mid_stream_batch, s_mid_batch_len, atlas_rgba());
			s_mid_batch_len = 0;
		}

		int flip = (raw_objs[k].variant >> 2) & 1;
		lam_billboard_gpu_t bb;
		bb.x = (float)wx;
		bb.y = (float)wy;
		bb.z = z_f;
		seven_calc_obj_billboard_dims(&raw_objs[k], sr, &bb.width, &bb.height, &bb.anchor_y);
		bb.water_z_off = bb.anchor_y;
		if (flip) {
			bb.u0 = (float)(sr->x + sr->w) / (float)ND_SEVEN_ATLAS_W;
			bb.u1 = (float)sr->x / (float)ND_SEVEN_ATLAS_W;
		} else {
			bb.u0 = (float)sr->x / (float)ND_SEVEN_ATLAS_W;
			bb.u1 = (float)(sr->x + sr->w) / (float)ND_SEVEN_ATLAS_W;
		}
		bb.v0 = (float)sr->y / (float)ND_SEVEN_RUNTIME_ATLAS_H;
		bb.v1 = (float)(sr->y + sr->h) / (float)ND_SEVEN_RUNTIME_ATLAS_H;
		seven_apply_obj_billboard_style(&raw_objs[k], sr, tint, &bb);

		if (arena_dst && inout_arena_cnt && *inout_arena_cnt < BLOCK_BB_MAX_PER_BLK)
			arena_dst[(*inout_arena_cnt)++] = bb;

		s_mid_stream_batch[s_mid_batch_len++] = bb;
		if (s_last_mid_gpu_count < MID_MAX_DRAW)
			s_mid_gpu_batch[s_last_mid_gpu_count] = bb;
		s_last_mid_gpu_count++;
		emitted++;
	}
	return emitted;
}

static inline int seven_emit_room_billboards(const nd_client_state_t *st, const ar_node_t *node,
                                             const int16_t pos[4], int16_t w_level,
                                             uint32_t tint)
{
	return seven_emit_room_billboards_ex(st, node, pos, w_level, tint, false, NULL, NULL);
}

enum { D_OBJ, D_ENTITY, D_LAMB };
typedef struct {
	int32_t wx, wy;        /* world position */
	int16_t wz;            /* ground elevation (block units) */
	float z_f;             /* continuous sub-pixel elevation */
	uint8_t what;
	ndw_obj_t obj;         /* D_OBJ */
	char ch;               /* D_ENTITY */
	uint8_t fg;
	uint8_t sprite_id;
	uint8_t flip;
	char name[32];
} drawable_3d_t;

static inline uint32_t meadow_hash32(int32_t x, int32_t y, uint32_t seed)
{
	uint32_t h = seed ^ ((uint32_t)x * 0x9E3779B1u) ^ ((uint32_t)y * 0x85EBCA77u);
	return ndw_mix(h);
}

static inline float meadow_noise_2d(float wx, float wy, float scale, uint32_t seed)
{
	float inv_s = 1.0f / scale;
	float px = wx * inv_s;
	float py = wy * inv_s;
	int32_t ix = (int32_t)floorf(px);
	int32_t iy = (int32_t)floorf(py);
	float fx = px - (float)ix;
	float fy = py - (float)iy;
	float ux = fx * fx * (3.0f - 2.0f * fx);
	float uy = fy * fy * (3.0f - 2.0f * fy);
	float v00 = (float)(meadow_hash32(ix,     iy,     seed) & 0xFFFFu) * (1.0f / 65535.0f);
	float v10 = (float)(meadow_hash32(ix + 1, iy,     seed) & 0xFFFFu) * (1.0f / 65535.0f);
	float v01 = (float)(meadow_hash32(ix,     iy + 1, seed) & 0xFFFFu) * (1.0f / 65535.0f);
	float v11 = (float)(meadow_hash32(ix + 1, iy + 1, seed) & 0xFFFFu) * (1.0f / 65535.0f);
	float a = v00 + (v10 - v00) * ux;
	float b = v01 + (v11 - v01) * ux;
	return a + (b - a) * uy;
}

static int meadow_lookup_rect(int zone, unsigned biome, uint32_t h, nd_seven_sprite_rect_t *out_rect)
{
	if (!out_rect)
		return 0;
	if (zone == ND_MEADOW_REED) {
		int rk = (h & 1u) ? NDB_REED_2 : NDB_REED_1;
		return nd_seven_botanical_rect(rk, out_rect);
	}
	if (zone == ND_MEADOW_WILDFLOWER) {
		if (biome == 3u) {
			/* Alpine meadow: crisp white-pink & golden subalpine blooms */
			*out_rect = nd_seven_sprite_rects[(h & 1u) ? NDS_DECOR_PINK : NDS_DECOR_YEL2];
			return 1;
		}
		if (biome == 4u) {
			/* Mediterranean garrigue: warm lavender-pink & sun-gold wild herbs */
			*out_rect = nd_seven_sprite_rects[(h & 1u) ? NDS_DECOR_PINK : NDS_DECOR_ORG];
			return 1;
		}
		if (biome == 5u) {
			/* Wetland / fen: marsh marigold yellow blooms */
			*out_rect = nd_seven_sprite_rects[(h & 1u) ? NDS_DECOR_YEL : NDS_DECOR_YEL2];
			return 1;
		}
		if (biome == 7u) {
			/* Cool shrubland / steppe: pale yellow & lavender-pink scrub blooms */
			*out_rect = nd_seven_sprite_rects[(h & 1u) ? NDS_DECOR_YEL2 : NDS_DECOR_PINK];
			return 1;
		}
		if (biome == 15u) {
			/* Savannah: warm sun-baked golden & amber blooms */
			*out_rect = nd_seven_sprite_rects[(h & 1u) ? NDS_DECOR_ORG : NDS_DECOR_YEL];
			return 1;
		}
		if (biome == 8u || biome == 9u || biome == 13u) {
			/* Coniferous, Boreal & Temperate rainforests: delicate woodland blooms */
			*out_rect = nd_seven_sprite_rects[(h & 1u) ? NDS_DECOR_YEL2 : NDS_DECOR_PINK];
			return 1;
		}
		if (biome == 16u) {
			/* Tropical seasonal / monsoon forest: synchronized golden, amber & magenta blooms */
			static const int tsf_flowers[4] = {
				NDS_DECOR_YEL, NDS_DECOR_ORG, NDS_DECOR_ORG, NDS_DECOR_PINK
			};
			*out_rect = nd_seven_sprite_rects[tsf_flowers[h & 3u]];
			return 1;
		}
		if (biome == 17u) {
			/* Tropical rainforest: vibrant orchid-pink & gold blossoms */
			static const int rf_flowers[4] = {
				NDS_DECOR_PINK, NDS_DECOR_PINK, NDS_DECOR_ORG, NDS_DECOR_YEL
			};
			*out_rect = nd_seven_sprite_rects[rf_flowers[h & 3u]];
			return 1;
		}
		static const int flowers[4] = {
			NDS_DECOR_YEL, NDS_DECOR_YEL2, NDS_DECOR_ORG, NDS_DECOR_PINK
		};
		*out_rect = nd_seven_sprite_rects[flowers[h & 3u]];
		return 1;
	}
	if (zone == ND_MEADOW_UNDERSTORY) {
		int uk = (h & 1u) ? NDB_GRASS_TUFT_1 : NDB_GRASS_TUFT_3;
		return nd_seven_botanical_rect(uk, out_rect);
	}
	if (zone == ND_MEADOW_TALL_GRASS) {
		int tk = (h & 1u) ? NDB_TALL_GRASS_2 : NDB_TALL_GRASS_1;
		return nd_seven_botanical_rect(tk, out_rect);
	}
	if (zone == ND_MEADOW_GRASS) {
		return nd_seven_botanical_rect(NDB_GRASS_TUFT_1 + (int)(h % 3u), out_rect);
	}
	return 0;
}

static uint32_t meadow_biome_tint(unsigned biome, int zone, uint32_t h, uint8_t alpha)
{
	if (zone == ND_MEADOW_WILDFLOWER)
		return 0xFFFFFF00u | (uint32_t)alpha;

	uint32_t rgb = 0xFAFFF400u;
	uint32_t idx = h & 3u;
	if (biome == 2u) {
		/* Tundra: frosted sage, lichen & russet sedge */
		static const uint32_t tundra_tints[4] = {
			0xB8BEAA00u, 0xC4B89E00u, 0xAEB6A400u, 0xBAB49C00u
		};
		rgb = tundra_tints[idx];
	} else if (biome == 3u) {
		/* Alpine meadow: crisp subalpine meadow turf */
		static const uint32_t alpine_tints[4] = {
			0xAED6BC00u, 0xA4CEB400u, 0xB6DEC200u, 0x9EC8AE00u
		};
		rgb = alpine_tints[idx];
	} else if (biome == 4u) {
		/* Mediterranean garrigue: warm olive-sage & sun-cured thyme */
		static const uint32_t med_tints[4] = {
			0xDCBE8200u, 0xD2B47A00u, 0xE4C68A00u, 0xC8AC7200u
		};
		rgb = med_tints[idx];
	} else if (biome == 5u) {
		/* Wetland / fen: deep lush marsh sedge & moss-teal */
		static const uint32_t wetland_tints[4] = {
			0x8CBEAC00u, 0x84B6A400u, 0x96C6B400u, 0x7CAE9C00u
		};
		rgb = wetland_tints[idx];
	} else if (biome == 6u || biome == 14u) {
		/* Cold desert (6) & Desert (14): parched straw, weathered sage & dry bunchgrass */
		static const uint32_t desert_tints[4] = {
			0xDEC08200u, 0xD2B67C00u, 0xE6C88A00u, 0xC8AE7400u
		};
		rgb = desert_tints[idx];
	} else if (biome == 7u) {
		/* Cool shrubland: dusty sagebrush-grey, cool olive-ochre & dry steppe bunchgrass */
		static const uint32_t shrub_tints[4] = {
			0xC4BC9400u, 0xCEBE9000u, 0xBAB48C00u, 0xC8B68800u
		};
		rgb = shrub_tints[idx];
	} else if (biome == 15u) {
		/* Savannah: sun-cured golden-ochre & warm straw */
		static const uint32_t savanna_tints[4] = {
			0xF2BA6800u, 0xE8B06000u, 0xFAC27000u, 0xDEA65800u
		};
		rgb = savanna_tints[idx];
	} else if (biome == 8u || biome == 9u) {
		/* Coniferous & Boreal forest: cool spruce & deep pine moss */
		static const uint32_t boreal_tints[4] = {
			0x94B89E00u, 0x8CB09800u, 0x9EC0A600u, 0x84A89000u
		};
		rgb = boreal_tints[idx];
	} else if (biome == 11u) {
		/* Open Woodland: fresh sunlit clover & olive-emerald */
		static const uint32_t woodland_tints[4] = {
			0xC2D49E00u, 0xCCCE9600u, 0xB8CE9800u, 0xD4CA9000u
		};
		rgb = woodland_tints[idx];
	} else if (biome == 12u) {
		/* Temperate seasonal forest: warm olive-gold & autumn leaf-litter accents */
		static const uint32_t temp_seasonal_tints[4] = {
			0xC8C48600u, 0xD4BA7C00u, 0xBEBE8000u, 0xDCB27400u
		};
		rgb = temp_seasonal_tints[idx];
	} else if (biome == 13u) {
		/* Temperate rainforest: cool misty Pacific-Northwest fern & spruce-moss emerald */
		static const uint32_t temp_rf_tints[4] = {
			0x92C89E00u, 0x8AC09600u, 0x9AD0A600u, 0x82B88E00u
		};
		rgb = temp_rf_tints[idx];
	} else if (biome == 16u) {
		/* Tropical seasonal / monsoon forest: warm chartreuse, sun-lit olive & dry-season gold */
		static const uint32_t monsoon_tints[4] = {
			0xC6C47800u, 0xD2BC7000u, 0xB8C07400u, 0xDAB46800u
		};
		rgb = monsoon_tints[idx];
	} else if (biome == 17u) {
		/* Tropical rainforest: rich saturated equatorial jungle emerald */
		static const uint32_t jungle_tints[4] = {
			0x9CD49C00u, 0x94CC9400u, 0xA6DCA600u, 0x8CC48C00u
		};
		rgb = jungle_tints[idx];
	} else {
		/* Temperate grassland (10) & default: fresh sunlit meadow emerald */
		static const uint32_t meadow_tints[4] = {
			0xCEDC9E00u, 0xC6D69600u, 0xD6E2A600u, 0xBECE9000u
		};
		rgb = meadow_tints[idx];
	}

	if (zone == ND_MEADOW_UNDERSTORY) {
		uint32_t r = (((rgb >> 24) & 0xFFu) * 228u) >> 8u;
		uint32_t g = (((rgb >> 16) & 0xFFu) * 236u) >> 8u;
		uint32_t b = (((rgb >>  8) & 0xFFu) * 224u) >> 8u;
		rgb = (r << 24) | (g << 16) | (b << 8);
	}
	return rgb | (uint32_t)alpha;
}

/* Position-invariant ground-cover painter's order helper:
 * Never culls (returns 1) and never shrinks (*io_scale unchanged) grass tufts near the Lamb.
 * Instead, ground-rooted tufts use a constant position-invariant painter's sort offset (-8.5f,
 * matching the 9px hoof-to-center offset LAMB_FOOT_Y - LAMB_AY = 25 - 16) so tufts rooted north
 * of the Lamb's hooves sort behind the Lamb without ever jumping or shrinking as the Lamb walks,
 * while 3D vertex trampling flattens blades directly underfoot. */
int nd_seven_eval_meadow_lamb_occlusion(float wx_f, float wy_f,
                                        float lwx_f, float lwy_f,
                                        float *io_sort_y, float *io_scale)
{
	(void)wx_f;
	(void)lwx_f;
	(void)lwy_f;
	(void)io_scale;
	if (io_sort_y)
		*io_sort_y = wy_f;
	return 1;
}

int nd_seven_eval_meadow_area(int32_t wx, int32_t wy,
                              float elev_z, float water_level, float slope_mag,
                              unsigned biome, float near_tree_dist_sq,
                              int *out_zone, float *out_scale, int *out_flip)
{
	if (out_zone) *out_zone = ND_MEADOW_NONE;
	if (out_scale) *out_scale = 1.0f;
	if (out_flip) *out_flip = 0;

	/* 1. Strict anti-submersion: never spawn meadow flora in or under water */
	float elev_above_water = elev_z - water_level;
	if (elev_above_water <= 0.10f)
		return 0;

	/* 2. Substrate check: vegetated grass/forest/savanna biomes (or tundra sedge), or sparse arid bunchgrass */
	ndw_ground_t g = ndw_biome_style(biome);
	int is_arid = (g.kind == NDW_G_SAND || biome == 6u || biome == 14u);
	if (g.kind != NDW_G_GRASS && g.kind != NDW_G_TUNDRA && !is_arid)
		return 0;

	/* 3. Suppress flora on sheer cliff faces or directly inside a large tree trunk (r < 9 units) */
	if (slope_mag >= 0.58f)
		return 0;
	if (near_tree_dist_sq >= 0.0f && near_tree_dist_sq < 81.0f)
		return 0;

	uint32_t h = meadow_hash32(wx, wy, 0x4D454144u);
	if (out_flip)
		*out_flip = (int)((h >> 7u) & 1u);

	float slope_pen = 0.0f;
	if (slope_mag > 0.20f) {
		slope_pen = (slope_mag - 0.20f) * 1.80f;
		if (slope_pen > 0.70f)
			slope_pen = 0.70f;
	}

	float wx_f = (float)wx;
	float wy_f = (float)wy;

	/* 4. Littoral shoreline reed & cattail stands (0.10 < Z - W < 1.45 on gentle banks, or < 1.85 in wetlands) */
	float reed_ceil = (biome == 5u) ? 1.85f : 1.45f;
	if (elev_above_water < reed_ceil && g.kind == NDW_G_GRASS && slope_mag < 0.24f) {
		float reed_macro = meadow_noise_2d(wx_f, wy_f, 96.0f, 0x52454544u);
		float reed_fine  = meadow_noise_2d(wx_f, wy_f, 28.0f, 0x52454545u);
		float shore_prox = 1.0f - (elev_above_water * (1.0f / reed_ceil));
		float reed_env   = 0.58f * reed_macro + 0.27f * reed_fine + 0.22f * shore_prox - slope_pen;
		float reed_thresh = (biome == 5u) ? 0.36f : 0.42f;
		if (reed_env > reed_thresh) {
			float rt = (reed_env - reed_thresh) * (1.0f / 0.55f);
			if (rt > 1.0f) rt = 1.0f;
			if (out_zone) *out_zone = ND_MEADOW_REED;
			if (out_scale) *out_scale = 0.48f + 0.16f * rt;
			return 1;
		}
		if (biome != 5u)
			return 0;
	}

	/* 5. Tree canopy drip-line understory (81 <= d^2 < 576, i.e. 9..24 units from trunk) */
	if (near_tree_dist_sq >= 81.0f && near_tree_dist_sq < 576.0f) {
		if (is_arid && elev_above_water > 1.85f)
			return 0;
		float u_macro = meadow_noise_2d(wx_f, wy_f, 48.0f, 0x554E4452u);
		float u_fine  = meadow_noise_2d(wx_f, wy_f, 20.0f, 0x554E4432u);
		float u_env   = 0.62f * u_macro + 0.38f * u_fine - slope_pen;
		float u_thresh = is_arid ? 0.52f : 0.34f;
		if (u_env > u_thresh) {
			if (out_zone) *out_zone = ND_MEADOW_UNDERSTORY;
			if (out_scale) *out_scale = (is_arid ? 0.28f : 0.38f) + 0.14f * u_env;
			return 1;
		}
		return 0;
	}

	/* 6. Domain-warped multi-octave meadow noise + land moisture/topography coupling */
	float warp_x = (meadow_noise_2d(wx_f, wy_f, 96.0f, 0x57415250u) - 0.5f) * 44.0f;
	float warp_y = (meadow_noise_2d(wx_f, wy_f, 96.0f, 0x57415251u) - 0.5f) * 44.0f;

	float n_macro = meadow_noise_2d(wx_f + warp_x, wy_f + warp_y, 176.0f, 0x464C4F52u);
	float n_meso  = meadow_noise_2d(wx_f - warp_y * 0.5f, wy_f + warp_x * 0.5f, 64.0f, 0x4D454132u);
	float n_fine  = meadow_noise_2d(wx_f, wy_f, 24.0f, 0x4D454133u);
	float noise_env = 0.54f * n_macro + 0.33f * n_meso + 0.13f * n_fine;

	/* Fertile moist lowland basins & gentle plains boost meadow formation */
	float moist_t = 1.0f - ((elev_above_water - 1.85f) * (1.0f / 14.15f));
	if (moist_t < 0.0f) moist_t = 0.0f;
	if (moist_t > 1.0f) moist_t = 1.0f;
	float flat_factor = 1.0f - (slope_mag * 2.5f);
	if (flat_factor < 0.25f) flat_factor = 0.25f;
	float moisture_bonus = 0.16f * moist_t * flat_factor;

	/* High dry wind-swept ridges thin out meadow coverage */
	float ridge_pen = 0.0f;
	if (elev_above_water > 22.0f) {
		ridge_pen = (elev_above_water - 22.0f) * (0.35f / 18.0f);
		if (ridge_pen > 0.35f)
			ridge_pen = 0.35f;
	}

	/* Biome-coupled fertility multiplier & structural profile */
	float bio_mult = 1.0f;
	float base_sc  = 0.40f;
	float span_sc  = 0.22f;
	if (is_arid) {
		/* Deserts (6, 14): sparse xerophytic bunchgrass restricted to low oasis margins */
		bio_mult = 0.52f;
		base_sc  = 0.26f;
		span_sc  = 0.12f;
	} else if (g.kind == NDW_G_TUNDRA) {
		/* Tundra (2): sparse, low-profile hardy sedge */
		bio_mult = 0.78f;
		base_sc  = 0.30f;
		span_sc  = 0.12f;
	} else if (biome == 3u) {
		/* Alpine meadow (3): crisp, flower-dotted short subalpine turf */
		bio_mult = 0.96f;
		base_sc  = 0.35f;
		span_sc  = 0.16f;
	} else if (biome == 4u) {
		/* Mediterranean garrigue (4): warm aromatic herb & scrub tufts */
		bio_mult = 0.88f;
		base_sc  = 0.37f;
		span_sc  = 0.18f;
	} else if (biome == 5u) {
		/* Wetland / fen (5): dense waterlogged marsh sedge & rushes */
		bio_mult = 1.08f;
		base_sc  = 0.43f;
		span_sc  = 0.22f;
	} else if (biome == 7u) {
		/* Cool shrubland (7): compact, wind-hardy sagebrush & steppe bunchgrass */
		bio_mult = 0.84f;
		base_sc  = 0.34f;
		span_sc  = 0.16f;
	} else if (biome == 15u) {
		/* Savannah (15): sun-baked tall golden C4 grass stands */
		bio_mult = 0.94f;
		base_sc  = 0.42f;
		span_sc  = 0.24f;
	} else if (biome == 8u || biome == 9u) {
		/* Coniferous (8) & Boreal (9) forests: compact mossy forest-floor turf */
		bio_mult = 0.86f;
		base_sc  = 0.34f;
		span_sc  = 0.14f;
	} else if (biome == 13u) {
		/* Temperate rainforest (13): lush fern & mossy old-growth floor */
		bio_mult = 1.08f;
		base_sc  = 0.40f;
		span_sc  = 0.18f;
	} else if (biome == 16u) {
		/* Tropical seasonal / monsoon forest (16): sunlit tangled bamboo & tall monsoon grass */
		bio_mult = 1.10f;
		base_sc  = 0.44f;
		span_sc  = 0.24f;
	} else if (biome == 17u) {
		/* Tropical rainforest (17): dense equatorial jungle groundcover */
		bio_mult = 1.12f;
		base_sc  = 0.44f;
		span_sc  = 0.22f;
	}

	float fertility = bio_mult * (noise_env + moisture_bonus - ridge_pen - slope_pen);

	/* Contiguous area threshold: outside meadow areas (fertility <= threshold) is open ground */
	if (is_arid) {
		/* Arid clumping: suppress grass tufts on open desert sand dunes; allow only near oasis water */
		if (elev_above_water > 1.35f || fertility <= 0.40f || ((h >> 12u) & 7u) > 0u)
			return 0;
	} else if (fertility <= 0.42f) {
		return 0;
	}

	float core_t = (fertility - (is_arid ? 0.40f : 0.42f)) * (1.0f / 0.36f);
	if (core_t < 0.0f)
		core_t = 0.0f;
	if (core_t > 1.0f)
		core_t = 1.0f;

	float sc = base_sc + span_sc * core_t + ((float)(int32_t)(h & 7u) - 3.5f) * 0.008f;
	if (out_scale)
		*out_scale = sc;

	if (is_arid) {
		/* Arid bunchgrass & sparse dry seed plumes */
		if (core_t > 0.65f && ((h >> 10u) & 3u) == 0u) {
			if (out_zone) *out_zone = ND_MEADOW_TALL_GRASS;
		} else {
			if (out_zone) *out_zone = ND_MEADOW_GRASS;
		}
		return 1;
	}

	if (g.kind == NDW_G_TUNDRA) {
		/* Tundra has hardy low sedge only — no summer wildflowers or tall seed-plumes */
		if (out_zone) *out_zone = ND_MEADOW_GRASS;
		return 1;
	}

	float flower_env = meadow_noise_2d(wx_f + 113.0f, wy_f - 79.0f, 52.0f, 0x424C4F4Du);
	if (biome == 3u) {
		/* Alpine Meadow: rich high-country wildflower carpet with crisp short turf */
		if (core_t > 0.18f && flower_env > 0.48f && ((h >> 8u) & 3u) <= 1u) {
			if (out_zone) *out_zone = ND_MEADOW_WILDFLOWER;
			return 1;
		}
		if (out_zone) *out_zone = ND_MEADOW_GRASS;
		return 1;
	} else if (biome == 5u) {
		/* Wetland: inland cattail reed stands restricted to low waterlogged flats */
		if (elev_above_water < 1.35f && slope_mag < 0.22f && core_t > 0.32f && ((h >> 9u) & 3u) == 0u) {
			if (out_zone) *out_zone = ND_MEADOW_REED;
			return 1;
		}
		if (core_t > 0.28f && flower_env > 0.58f && ((h >> 8u) & 3u) == 0u) {
			if (out_zone) *out_zone = ND_MEADOW_WILDFLOWER;
			return 1;
		}
		if (core_t > 0.40f && ((h >> 10u) & 3u) != 0u) {
			if (out_zone) *out_zone = ND_MEADOW_TALL_GRASS;
			return 1;
		}
	} else if (biome == 8u || biome == 9u) {
		/* Boreal / Coniferous: rare forest blooms & mostly short moss/turf */
		if (core_t > 0.45f && flower_env > 0.66f && ((h >> 8u) & 7u) == 0u) {
			if (out_zone) *out_zone = ND_MEADOW_WILDFLOWER;
			return 1;
		}
		if (core_t > 0.68f && ((h >> 10u) & 3u) == 0u) {
			if (out_zone) *out_zone = ND_MEADOW_TALL_GRASS;
			return 1;
		}
	} else if (biome == 7u) {
		/* Cool shrubland: sparse dry steppe seed-plumes & hardy scrub blooms */
		if (core_t > 0.36f && flower_env > 0.62f && ((h >> 8u) & 3u) == 0u) {
			if (out_zone) *out_zone = ND_MEADOW_WILDFLOWER;
			return 1;
		}
		if (core_t > 0.52f && ((h >> 10u) & 3u) == 0u) {
			if (out_zone) *out_zone = ND_MEADOW_TALL_GRASS;
			return 1;
		}
	} else if (biome == 15u) {
		/* Savannah: abundant tall golden seed-plume grass & warm blooms */
		if (core_t > 0.30f && flower_env > 0.60f && ((h >> 8u) & 3u) == 0u) {
			if (out_zone) *out_zone = ND_MEADOW_WILDFLOWER;
			return 1;
		}
		if (core_t > 0.26f && ((h >> 10u) & 3u) != 0u) {
			if (out_zone) *out_zone = ND_MEADOW_TALL_GRASS;
			return 1;
		}
	} else if (biome == 13u) {
		/* Temperate rainforest: fern/moss floor with delicate woodland blooms */
		if (core_t > 0.34f && flower_env > 0.60f && ((h >> 8u) & 3u) == 0u) {
			if (out_zone) *out_zone = ND_MEADOW_WILDFLOWER;
			return 1;
		}
		if (core_t > 0.50f && ((h >> 10u) & 3u) != 0u) {
			if (out_zone) *out_zone = ND_MEADOW_TALL_GRASS;
			return 1;
		}
	} else if (biome == 16u) {
		/* Tropical seasonal forest: sunlit monsoon tall-grass/bamboo thickets & seasonal blooms */
		if (core_t > 0.20f && flower_env > 0.50f && ((h >> 8u) & 3u) == 0u) {
			if (out_zone) *out_zone = ND_MEADOW_WILDFLOWER;
			return 1;
		}
		if (core_t > 0.24f && ((h >> 10u) & 3u) != 0u) {
			if (out_zone) *out_zone = ND_MEADOW_TALL_GRASS;
			return 1;
		}
	} else if (biome == 17u) {
		/* Tropical rainforest: rich tropical blooms & dense tall jungle grass */
		if (core_t > 0.22f && flower_env > 0.52f && ((h >> 8u) & 3u) == 0u) {
			if (out_zone) *out_zone = ND_MEADOW_WILDFLOWER;
			return 1;
		}
		if (core_t > 0.36f && ((h >> 10u) & 3u) != 0u) {
			if (out_zone) *out_zone = ND_MEADOW_TALL_GRASS;
			return 1;
		}
	} else {
		/* Temperate Grassland (10), Woodland (11), Seasonal Forest (12) */
		if (core_t > 0.25f && flower_env > 0.56f && ((h >> 8u) & 3u) == 0u) {
			if (out_zone) *out_zone = ND_MEADOW_WILDFLOWER;
			return 1;
		}
		if (core_t > 0.46f && ((h >> 10u) & 3u) != 0u) {
			if (out_zone) *out_zone = ND_MEADOW_TALL_GRASS;
			return 1;
		}
	}

	if (out_zone) *out_zone = ND_MEADOW_GRASS;
	return 1;
}

int nd_seven_eval_meadow_tuft(int32_t wx, int32_t wy,
                              float elev_z, float water_level,
                              unsigned biome, float near_tree_dist_sq,
                              int *out_zone, int *out_flip)
{
	int z = ND_MEADOW_NONE;
	if (!nd_seven_eval_meadow_area(wx, wy, elev_z, water_level, 0.0f,
	                               biome, near_tree_dist_sq, &z, NULL, out_flip)) {
		if (out_zone) *out_zone = ND_MEADOW_NONE;
		return 0;
	}
	if (z == ND_MEADOW_TALL_GRASS)
		z = ND_MEADOW_GRASS;
	if (out_zone)
		*out_zone = z;
	return 1;
}

#define MAX_DRAW 512
#define ALL_GPU_MAX_DRAW 24576

int nd_seven_shaders_ready(void)
{
	return nd_seven_gl_pipelines_ready() ? 1 : 0;
}

void nd_seven_render(uint32_t wu, uint32_t hu, const nd_client_state_t *st)
{
	int w = (int)wu, h = (int)hu;
	static drawable_3d_t dr[MAX_DRAW];
	static lam_billboard_gpu_t s_all_gpu_batch[ALL_GPU_MAX_DRAW];

	if (w <= 0 || h <= 0)
		return;

	nd_seven_sync_origin(st);

	if (!nd_seven_gl_pipelines_ready()) {
		glViewport(0, 0, (GLsizei)w, (GLsizei)h);
		glClearColor(0.039f, 0.039f, 0.047f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		return;
	}

	atlas_load();

	float zoom = nd_seven_get_zoom();
	float eff_cam_dist = ND_SEVEN_CAM_DIST / zoom;

	/* Continuous 3D Terrain: Dispatch multi-scale leaf chunks from libareole (view_id = 0) */
	seven_dispatch_macro_backdrop(w, h, st, zoom, eff_cam_dist);

	int map_w = (st && st->map_data.width > 0) ? (int)st->map_data.width : 7;
	int map_h = (st && st->map_data.height > 0) ? (int)st->map_data.height : 7;
	int px = st ? (int)st->map_data.player_x : 3;
	int py = st ? (int)st->map_data.player_y : 3;

	int obj_i0 = -px;
	int obj_i1 = map_w - 1 - px;
	int obj_j0 = -py;
	int obj_j1 = map_h - 1 - py;

	int lamb_row = ndw_fdiv(nd_seven_get_ly(), NDW_TILE_H);
	int eff_lamb_row = lamb_row;
	if (eff_lamb_row < obj_j0) eff_lamb_row = obj_j0;
	if (eff_lamb_row > obj_j1) eff_lamb_row = obj_j1;

	uint32_t all_gpu_count = 0;

	for (int tj = obj_j0; tj <= obj_j1; tj++) {
		int n = 0;

		if (eff_cam_dist < SCENERY_ALT_FADE_END) {
			for (int ti = obj_i0; ti <= obj_i1 && n < MAX_DRAW - NDW_MAX_OBJ; ti++) {
				nd_spatial_tile_t sp;
				if (!nd_spatial_tile_resolve(st, ti, tj, &sp))
					continue;

				for (int k = 0; k < sp.obj_count && n < MAX_DRAW; k++) {
					if (sp.objs[k].harvested)
						continue;

					int32_t base_wx = (int32_t)sp.pos[0] * NDW_TILE_W;
					int32_t base_wy = -(int32_t)sp.pos[1] * NDW_TILE_H;

					if (sp.objs[k].is_entity) {
						dr[n].what = D_ENTITY;
						dr[n].ch = (char)sp.objs[k].glyph;
						dr[n].fg = sp.objs[k].fg;
						dr[n].wx = base_wx + sp.objs[k].x;
						dr[n].wy = base_wy + sp.objs[k].y;
						int16_t elev_q = nd_seven_ground_elevation_q12_4(dr[n].wx, dr[n].wy);
						dr[n].wz = nd_field_elevation_to_block(elev_q);
						dr[n].z_f = (float)elev_q / 16.0f;
						dr[n].flip = sp.objs[k].flip;
						dr[n].sprite_id = entity_atlas_sprite(sp.objs[k].sprite_id);
						snprintf(dr[n].name, sizeof(dr[n].name), "%s", sp.objs[k].name);
						n++;
					} else {
						dr[n].what = D_OBJ;
						dr[n].obj.x = sp.objs[k].x;
						dr[n].obj.y = sp.objs[k].y;
						dr[n].obj.kind = sp.objs[k].kind;
						dr[n].obj.variant = sp.objs[k].variant;
						dr[n].obj.slot_id = sp.objs[k].slot_id;
						dr[n].obj.hw = sp.objs[k].hw ? sp.objs[k].hw : (uint8_t)(sp.objs[k].kind == NDW_K_ROCK ? 6 : sp.objs[k].kind == NDW_K_BOULDER ? 10 : 0);
						dr[n].obj.hh = sp.objs[k].hh;
						dr[n].obj.anchor_y = sp.objs[k].anchor_y;
						dr[n].obj.plant_id = sp.objs[k].plant_id;
						dr[n].obj.col_mask = sp.objs[k].col_mask;
						dr[n].obj.glyph = sp.objs[k].glyph;
						dr[n].obj.fg = sp.objs[k].fg;
						dr[n].obj.big = sp.objs[k].big;
						dr[n].wx = base_wx + sp.objs[k].x;
						dr[n].wy = base_wy + sp.objs[k].y;
						int16_t elev_q = nd_seven_ground_elevation_q12_4(dr[n].wx, dr[n].wy);
						dr[n].wz = nd_field_elevation_to_block(elev_q);
						dr[n].z_f = (float)elev_q / 16.0f;
						dr[n].sprite_id = 0;
						dr[n].flip = 0;
						snprintf(dr[n].name, sizeof(dr[n].name), "%s", sp.objs[k].name);
						n++;
					}
				}
			}
		}
		if (tj == eff_lamb_row && n < MAX_DRAW) {
			int64_t lwx = 0, lwy = 0;
			nd_seven_world_pos(st, &lwx, &lwy);
			dr[n].what = D_LAMB;
			dr[n].wx = (int32_t)lwx;
			dr[n].wy = (int32_t)lwy;
			float lamb_f = nd_seven_lamb_elevation_f(st, (int32_t)lwx, (int32_t)lwy);
			dr[n].wz = (int16_t)lamb_f;
			dr[n].z_f = lamb_f;
			dr[n].name[0] = '\0';
			n++;
		}

		for (int a = 0; a < n && all_gpu_count < ALL_GPU_MAX_DRAW; a++) {
			int has_sprite = 0;
			int entity_spr = 0;
			nd_seven_sprite_rect_t obj_sr = { 0, 0, 0, 0 };
			float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
			float bw = 0.0f, bh = 0.0f;
			float bay = 0.0f, bwz_off = 0.0f;

			if (dr[a].what == D_LAMB) {
				int row, col;
				if (nd_seven_get_walking()) {
					row = nd_seven_get_dir();
					col = (int)((nd_seven_get_anim() / 4) % 6);
				} else {
					row = 4 + nd_seven_get_dir();
					col = (int)((nd_seven_get_anim() / 24) % 4);
				}
				bw = (float)LAMB_SIZE;
				bh = (float)LAMB_SIZE;
				bay = (float)(LAMB_SIZE - LAMB_AY);
				bwz_off = (float)((LAMB_SIZE - 1) - LAMB_FOOT_Y);
				u0 = (float)(col * LAMB_SIZE) / (float)ND_SEVEN_ATLAS_W;
				v0 = (float)(row * LAMB_SIZE) / (float)ND_SEVEN_RUNTIME_ATLAS_H;
				u1 = (float)((col + 1) * LAMB_SIZE) / (float)ND_SEVEN_ATLAS_W;
				v1 = (float)((row + 1) * LAMB_SIZE) / (float)ND_SEVEN_RUNTIME_ATLAS_H;
				has_sprite = 1;
			} else if (dr[a].what == D_ENTITY) {
				int sprite = 0;
				if (dr[a].sprite_id) {
					sprite = dr[a].sprite_id;
				} else if (strstr(dr[a].name, "stone") || strstr(dr[a].name, "Stone") ||
				           dr[a].ch == '*' || dr[a].ch == 'o') {
					sprite = NDS_STONE_FLAT;
				} else {
					sprite = NDS_STONE_TINY;
				}
				entity_spr = sprite;
				const nd_seven_sprite_rect_t *sr = &nd_seven_sprite_rects[sprite];
				bw = (float)sr->w;
				bh = (float)sr->h;
				int off_y = (sr->h <= 24) ? (sr->h - (NDW_CELL_PX / 2)) : 6;
				if (off_y < 0) off_y = 0;
				if (off_y > sr->h - 4) off_y = sr->h - 4;
				bay = (float)off_y;
				bwz_off = bay;
				u0 = (float)sr->x / (float)ND_SEVEN_ATLAS_W;
				v0 = (float)sr->y / (float)ND_SEVEN_RUNTIME_ATLAS_H;
				u1 = (float)(sr->x + sr->w) / (float)ND_SEVEN_ATLAS_W;
				v1 = (float)(sr->y + sr->h) / (float)ND_SEVEN_RUNTIME_ATLAS_H;
				has_sprite = 1;
			} else if (dr[a].what == D_OBJ) {
				if (dr[a].obj.kind >= NDW_K_DECID && dr[a].obj.kind <= NDW_K_CACTUS) {
					uint16_t world = (st && st->map_data.width > 0) ? (uint16_t)st->map_data.pos[3] : 0;
					int16_t w_level = 0;
					if (seven_has_water_at(world, dr[a].wx, dr[a].wy, &w_level) && dr[a].wz <= w_level)
						continue; /* Anti-submersion: omit trees/flora in water */
				}
				nd_seven_sprite_rect_t sr_val;
				if (nd_seven_obj_rect(&dr[a].obj, &sr_val)) {
					has_sprite = 1;
				} else {
					int sprite = 0;
					if (strstr(dr[a].name, "stone") || strstr(dr[a].name, "Stone") || dr[a].obj.kind == NDW_K_STONE)
						sprite = NDS_STONE_FLAT;
					else if (dr[a].obj.glyph)
						sprite = NDS_STONE_TINY;
					if (sprite > 0 && sprite < NDS_COUNT) {
						sr_val = nd_seven_sprite_rects[sprite];
						has_sprite = 1;
					}
				}
				if (has_sprite) {
					obj_sr = sr_val;
					seven_calc_obj_billboard_dims(&dr[a].obj, &sr_val, &bw, &bh, &bay);
					bwz_off = bay;
					int flip = (dr[a].obj.variant >> 2) & 1;
					if (flip) {
						u0 = (float)(sr_val.x + sr_val.w) / (float)ND_SEVEN_ATLAS_W;
						u1 = (float)sr_val.x / (float)ND_SEVEN_ATLAS_W;
					} else {
						u0 = (float)sr_val.x / (float)ND_SEVEN_ATLAS_W;
						u1 = (float)(sr_val.x + sr_val.w) / (float)ND_SEVEN_ATLAS_W;
					}
					v0 = (float)sr_val.y / (float)ND_SEVEN_RUNTIME_ATLAS_H;
					v1 = (float)(sr_val.y + sr_val.h) / (float)ND_SEVEN_RUNTIME_ATLAS_H;
				}
			}

			if (has_sprite) {
				float dist_b = (eff_cam_dist > 1.0f) ? eff_cam_dist : ND_SEVEN_CAM_DIST;
				float alt_fade_b = (dr[a].what == D_LAMB) ? 1.0f : seven_smooth_fade(dist_b, SCENERY_ALT_FADE_START, SCENERY_ALT_FADE_END);
				uint32_t tint_b = seven_pack_white_alpha(alt_fade_b);
				if (tint_b == 0)
					continue;

				lam_billboard_gpu_t *dst_bb = &s_all_gpu_batch[all_gpu_count];
				dst_bb->x = (float)dr[a].wx;
				/* Give the lamb a microscopic +0.01f wy bias so it always wins exact row ties */
				dst_bb->y = (float)dr[a].wy + (dr[a].what == D_LAMB ? 0.01f : 0.0f);
				dst_bb->z = dr[a].z_f;
				dst_bb->width = bw;
				dst_bb->height = bh;
				dst_bb->anchor_y = bay;
				dst_bb->water_z_off = bwz_off;
				dst_bb->u0 = u0;
				dst_bb->v0 = v0;
				dst_bb->u1 = u1;
				dst_bb->v1 = v1;
				if (dr[a].what == D_OBJ) {
					seven_apply_obj_billboard_style(&dr[a].obj, &obj_sr, tint_b, dst_bb);
				} else if (dr[a].what == D_LAMB) {
					bool lamb_glows = (nd_seven_is_universe_owner(st) != 0);
					dst_bb->tint_rgba = tint_b;
					dst_bb->style = lamb_glows ? ND_BB_STYLE_GLOW : ND_BB_STYLE_STANDARD;
					dst_bb->shadow_rx = lamb_glows ? 0 : 8;
					dst_bb->shadow_ry = lamb_glows ? 0 : 4;
					dst_bb->pad0 = 0;
				} else {
					dst_bb->tint_rgba = tint_b;
					dst_bb->pad0 = 0;
					if (entity_spr >= NDS_ROCK_TAN_BIG && entity_spr <= NDS_STONE_TINY) {
						dst_bb->style = ND_BB_STYLE_MINERAL;
					} else if (entity_spr >= NDS_DECOR_YEL && entity_spr <= NDS_DECOR_PINK) {
						dst_bb->style = ND_BB_STYLE_GRASS;
						dst_bb->pad0 = 1u;
					} else if (entity_spr >= NDS_TREE_OAK && entity_spr <= NDS_BUSH_TEAL) {
						dst_bb->style = ND_BB_STYLE_FLORA;
						dst_bb->pad0 = (bh <= 30.0f) ? 1u : 2u;
					} else {
						dst_bb->style = ND_BB_STYLE_STANDARD;
					}
					dst_bb->shadow_rx = (uint8_t)(bw * 0.34f > 4.0f ? bw * 0.34f : 4.0f);
					dst_bb->shadow_ry = (uint8_t)(dst_bb->shadow_rx * 0.45f > 2.0f ? dst_bb->shadow_rx * 0.45f : 2.0f);
				}
				all_gpu_count++;
			}
		}
	}

	/* Emit deterministic Living Meadow areas, shoreline reed beds, and wildflower glades:
	 * Spread out on a 26-unit lattice with natural jitter so tufts are less frequent and never cluttered,
	 * filling the entire viewport out to far distance and fading out only when zoomed out far. */
	float dist_meadow = (eff_cam_dist > 1.0f) ? eff_cam_dist : ND_SEVEN_CAM_DIST;
	float meadow_fade = seven_smooth_fade(dist_meadow, 1800.0f, 3400.0f);
	if (meadow_fade > 0.02f && all_gpu_count + 16 < ALL_GPU_MAX_DRAW) {
		uint16_t m_world = (st && st->map_data.width > 0) ? (uint16_t)st->map_data.pos[3] : 0;
		int16_t base_wl = nd_field_water_level(m_world);
		int64_t lwx64 = 0, lwy64 = 0;
		nd_seven_world_pos(st, &lwx64, &lwy64);
		int32_t lwx = (int32_t)lwx64;
		int32_t lwy = (int32_t)lwy64;
		float lwx_f = (float)lwx;
		float lwy_f = (float)lwy;

		/* Sample a spread-out 26-unit world-locked lattice using ndw_fdiv so grid coordinates
		 * are 100% invariant across negative/positive world axes and room transitions.
		 * Dynamic view-filling radius scales with camera distance (624..1404 wu) so grass fills
		 * the entire visible viewport and only fades out when zoomed out far. */
		const int32_t step = 26;
		int32_t dyn_rad = (int32_t)(dist_meadow * 1.65f);
		if (dyn_rad < 624) dyn_rad = 624;
		if (dyn_rad > 1404) dyn_rad = 1404;
		const int32_t rad = (dyn_rad / step) * step;
		int32_t cx0 = ndw_fdiv(lwx - rad, step) * step;
		int32_t cy0 = ndw_fdiv(lwy - rad, step) * step;
		const ar_node_t *m_node = NULL;
		int cached_ti = 0x7FFFFFFF, cached_tj = 0x7FFFFFFF;
		unsigned cached_bio = 10u;

		for (int32_t gy = cy0; gy <= lwy + rad && all_gpu_count + 4 < ALL_GPU_MAX_DRAW; gy += step) {
			if (s_frame_frustum_valid &&
			    !seven_test_frustum_xy((float)(lwx - rad) - 32.0f, (float)gy - 32.0f,
			                           (float)(lwx + rad) + 32.0f, (float)gy + 32.0f,
			                           lwx_f, lwy_f, -256.0f, 320.0f))
				continue;
			for (int32_t gx = cx0; gx <= lwx + rad && all_gpu_count + 4 < ALL_GPU_MAX_DRAW; gx += step) {
				uint32_t jh = meadow_hash32(gx, gy, 0x4A495454u);
				int32_t wx = gx + (int32_t)(jh % 19u) - 9;
				int32_t wy = gy + (int32_t)((jh >> 5u) % 19u) - 9;

				float ddx = (float)(wx - lwx);
				float ddy = (float)(wy - lwy);
				float r2 = ddx * ddx + ddy * ddy;
				if (r2 > (float)(rad * rad))
					continue;
				if (s_frame_frustum_valid &&
				    !seven_test_frustum_xy((float)wx - 24.0f, (float)wy - 24.0f,
				                           (float)wx + 24.0f, (float)wy + 24.0f,
				                           lwx_f, lwy_f, -256.0f, 320.0f))
					continue;

				/* Resolve biome at (wx, wy) with room-level caching */
				int ti = ndw_fdiv(wx, NDW_TILE_W) - (st ? (int)st->map_data.pos[0] : 0);
				int tj = (st ? (int)st->map_data.pos[1] : 0) - (-ndw_fdiv(wy, NDW_TILE_H));
				if (ti != cached_ti || tj != cached_tj) {
					cached_ti = ti;
					cached_tj = tj;
					cached_bio = 10u;
					ndw_tile_t mt;
					int16_t mpos[4];
					if (nd_seven_tile_at(st, ti, tj, &mt, mpos))
						cached_bio = mt.biome;
				}
				unsigned bio = cached_bio;

				/* Fast O(1) active leaf node lookup for elevation, slope, and water level */
				if (!m_node || wx < m_node->ox || wx >= m_node->ox + m_node->w ||
				    wy < m_node->oy || wy >= m_node->oy + m_node->h) {
					m_node = NULL;
					for (size_t li = 0; li < s_visible_leaf_count; li++) {
						const ar_node_t *cand = s_visible_leaves[li];
						if (cand && cand->values &&
						    wx >= cand->ox && wx < cand->ox + cand->w &&
						    wy >= cand->oy && wy < cand->oy + cand->h) {
							if (!m_node || cand->level < m_node->level)
								m_node = cand;
						}
					}
				}

				int16_t elev_q = 0, eq_x = 0, eq_y = 0;
				int16_t local_wl = base_wl;
				if (m_node && wx + 8 < m_node->ox + m_node->w && wy + 8 < m_node->oy + m_node->h) {
					int16_t dummy_wl = base_wl;
					seven_sample_node_smooth(m_node, wx,     wy,     base_wl, &elev_q, &local_wl);
					seven_sample_node_smooth(m_node, wx + 8, wy,     base_wl, &eq_x,   &dummy_wl);
					seven_sample_node_smooth(m_node, wx,     wy + 8, base_wl, &eq_y,   &dummy_wl);
					int pos_y_grid = -ndw_fdiv(wy, NDW_TILE_H);
					if (ndw_fdiv(wx, NDW_TILE_W) == -3 && pos_y_grid == 0 && local_wl < base_wl)
						local_wl = base_wl;
				} else {
					elev_q = nd_seven_ground_elevation_q12_4(wx, wy);
					eq_x   = nd_seven_ground_elevation_q12_4(wx + 8, wy);
					eq_y   = nd_seven_ground_elevation_q12_4(wx, wy + 8);
					seven_has_water_at(m_world, wx, wy, &local_wl);
				}

				float z_f = (float)elev_q / 16.0f;
				float dz_dx = (float)(eq_x - elev_q) * (1.0f / 128.0f);
				float dz_dy = (float)(eq_y - elev_q) * (1.0f / 128.0f);
				float slope_mag = sqrtf(dz_dx * dz_dx + dz_dy * dz_dy);

				int zone = ND_MEADOW_NONE;
				float scale_m = 1.0f;
				int flip = 0;
				if (!nd_seven_eval_meadow_area(wx, wy, z_f, (float)local_wl, slope_mag,
				                               bio, 999999.0f, &zone, &scale_m, &flip))
					continue;

				nd_seven_sprite_rect_t sr;
				if (!meadow_lookup_rect(zone, bio, jh >> 8u, &sr))
					continue;

				float r_norm = sqrtf(r2) / (float)rad;
				float radial_fade = (r_norm <= 0.78f) ? 1.0f : ((1.0f - r_norm) * (1.0f / 0.22f));
				if (radial_fade < 0.0f) radial_fade = 0.0f;
				float eff_alpha = meadow_fade * (0.25f + 0.75f * radial_fade);
				uint32_t base_alpha_pack = seven_pack_white_alpha(eff_alpha);
				if (base_alpha_pack == 0)
					continue;
				uint8_t alpha_u8 = (uint8_t)(base_alpha_pack & 0xFFu);
				uint32_t tint_m = meadow_biome_tint(bio, zone, jh >> 12u, alpha_u8);

				float sort_y1 = (float)wy;
				float sc1 = scale_m;
				if (nd_seven_eval_meadow_lamb_occlusion((float)wx, (float)wy, lwx_f, lwy_f, &sort_y1, &sc1)) {
					lam_billboard_gpu_t *bb = &s_all_gpu_batch[all_gpu_count++];
					bb->x = (float)wx;
					bb->y = sort_y1;
					bb->z = z_f;
					bb->width = (float)sr.w * sc1;
					bb->height = (float)sr.h * sc1;
					bb->anchor_y = 1.6f * sc1;
					bb->water_z_off = 1.6f * sc1;
					if (flip) {
						bb->u0 = (float)(sr.x + sr.w) / (float)ND_SEVEN_ATLAS_W;
						bb->u1 = (float)sr.x / (float)ND_SEVEN_ATLAS_W;
					} else {
						bb->u0 = (float)sr.x / (float)ND_SEVEN_ATLAS_W;
						bb->u1 = (float)(sr.x + sr.w) / (float)ND_SEVEN_ATLAS_W;
					}
					bb->v0 = (float)sr.y / (float)ND_SEVEN_RUNTIME_ATLAS_H;
					bb->v1 = (float)(sr.y + sr.h) / (float)ND_SEVEN_RUNTIME_ATLAS_H;
					bb->tint_rgba = tint_m;
					bb->style = ND_BB_STYLE_GRASS;
					{
						float grx = bb->width * 0.28f;
						if (grx < 2.0f) grx = 2.0f;
						float gry = grx * 0.45f;
						if (gry < 1.0f) gry = 1.0f;
						bb->shadow_rx = (uint8_t)grx;
						bb->shadow_ry = (uint8_t)gry;
					}
					bb->pad0 = (uint8_t)zone;
				}
			}
		}
	}

	/* Append active 3D water splash & surf-spray particles into s_all_gpu_batch */
	if (s_water_particles && eff_cam_dist < 1800.0f && all_gpu_count < ALL_GPU_MAX_DRAW) {
		lam_particle_draw_t p_draw[256];
		size_t max_p = ALL_GPU_MAX_DRAW - all_gpu_count;
		if (max_p > 256) max_p = 256;
		size_t n_p = lam_particle_gather(s_water_particles, p_draw, max_p);
		for (size_t pi = 0; pi < n_p && all_gpu_count < ALL_GPU_MAX_DRAW; pi++) {
			lam_billboard_gpu_t *dst_bb = &s_all_gpu_batch[all_gpu_count++];
			uint32_t p_alpha = p_draw[pi].rgba & 0xFFu;
			uint8_t p_seed = p_draw[pi].pad[0];
			uint32_t rgb_mask;
			if ((p_seed & 7u) <= 2u) {
				/* Pure brilliant white aerated foam/spray droplet (~37.5%) */
				rgb_mask = 0xFFFFFF00u;
			} else if ((p_seed & 7u) <= 4u) {
				/* Sunlit pearl-white droplet (~25%) */
				rgb_mask = 0xEAF8FF00u;
			} else if ((p_seed & 7u) <= 6u) {
				/* Crisp ice-cyan / seafoam droplet (~25%) */
				rgb_mask = 0xA4ECFF00u;
			} else {
				/* Deep sky / cerulean water bead (~12.5%) */
				rgb_mask = 0x64B8F600u;
			}
			dst_bb->x = p_draw[pi].x;
			dst_bb->y = p_draw[pi].y;
			dst_bb->z = p_draw[pi].z;
			dst_bb->width = p_draw[pi].size;
			dst_bb->height = p_draw[pi].size * 1.15f;
			dst_bb->anchor_y = dst_bb->height * 0.5f;
			dst_bb->water_z_off = 0.0f;
			dst_bb->u0 = 0.0f;
			dst_bb->v0 = 0.0f;
			dst_bb->u1 = 1.0f;
			dst_bb->v1 = 1.0f;
			dst_bb->tint_rgba = rgb_mask | p_alpha;
			dst_bb->style = ND_BB_STYLE_PARTICLE;
			dst_bb->shadow_rx = 0;
			dst_bb->shadow_ry = 0;
			dst_bb->pad0 = p_draw[pi].kind;
		}
	}

	/* Sort near-field 3D billboards globally back-to-front by world wy ascending (tie-break by wx ascending).
	 * Give airborne splash droplets (ND_BB_STYLE_PARTICLE) a +14.0f painter-order key offset so droplets
	 * splashing around the lamb's feet render in front of the 32x32 lamb sprite instead of behind it,
	 * and give ground-rooted grass/groundcover tufts (ND_BB_STYLE_GRASS) a constant -8.5f painter-order
	 * offset matching LAMB_FOOT_Y - LAMB_AY (9px) so tufts rooted north of the lamb's hooves sort cleanly
	 * behind the lamb without ever jumping or shrinking as the lamb walks. */
	if (all_gpu_count > 1) {
		static const uint32_t gaps[8] = { 701, 301, 132, 57, 23, 10, 4, 1 };
		for (int gi = 0; gi < 8; gi++) {
			uint32_t gap = gaps[gi];
			for (uint32_t a = gap; a < all_gpu_count; a++) {
				lam_billboard_gpu_t k = s_all_gpu_batch[a];
				float ky = k.y + ((k.style == ND_BB_STYLE_PARTICLE) ? 14.0f
				                : (k.style == ND_BB_STYLE_GRASS ? -8.5f : 0.0f));
				uint32_t b = a;
				while (b >= gap) {
					float by = s_all_gpu_batch[b - gap].y +
					           ((s_all_gpu_batch[b - gap].style == ND_BB_STYLE_PARTICLE) ? 14.0f
					          : (s_all_gpu_batch[b - gap].style == ND_BB_STYLE_GRASS ? -8.5f : 0.0f));
					if (by < ky || (by == ky && s_all_gpu_batch[b - gap].x <= k.x))
						break;
					s_all_gpu_batch[b] = s_all_gpu_batch[b - gap];
					b -= gap;
				}
				s_all_gpu_batch[b] = k;
			}
		}
	}

	/* Unified Homogeneous Scenery Pass across visible libareole leaves */
	s_mid_batch_len = 0;
	s_last_mid_gpu_count = 0;
	int near_flushed = 0;
	float dist_scenery = (eff_cam_dist > 1.0f) ? eff_cam_dist : ND_SEVEN_CAM_DIST;
	float alt_fade = seven_smooth_fade(dist_scenery, SCENERY_ALT_FADE_START, SCENERY_ALT_FADE_END);
	uint32_t tint_scenery = seven_pack_white_alpha(alt_fade);
	if (tint_scenery != 0) {
		nd_seven_set_scenery_synth_budget(512);
		uint16_t world = (st && st->map_data.width > 0) ? (uint16_t)st->map_data.pos[3] : 0;
		int16_t w_level = nd_field_water_level(world);
		int16_t base_rx = st ? st->map_data.pos[0] : 0;
		int16_t base_ry = st ? st->map_data.pos[1] : 0;
		int16_t base_rz = st ? st->map_data.pos[2] : 0;
		int32_t center_ry_grid = -(int32_t)base_ry;

		int64_t lwx = 0, lwy = 0;
		nd_seven_world_pos(st, &lwx, &lwy);

		/* Sort visible leaves back-to-front (North-to-South: oy ascending, tie-break by ox ascending) */
		for (size_t a = 1; a < s_visible_leaf_count; a++) {
			const ar_node_t *key_node = s_visible_leaves[a];
			int32_t key_oy = key_node ? key_node->oy : 0;
			int32_t key_ox = key_node ? key_node->ox : 0;
			size_t b = a;
			while (b > 0) {
				const ar_node_t *prev = s_visible_leaves[b - 1];
				int32_t prev_oy = prev ? prev->oy : 0;
				int32_t prev_ox = prev ? prev->ox : 0;
				if (prev_oy < key_oy || (prev_oy == key_oy && prev_ox <= key_ox))
					break;
				s_visible_leaves[b] = prev;
				b--;
			}
			s_visible_leaves[b] = key_node;
		}

		int32_t full_x0 = (int32_t)base_rx - SCENERY_RAD_MAX_ROOMS;
		int32_t full_x1 = (int32_t)base_rx + SCENERY_RAD_MAX_ROOMS;
		int32_t full_y0 = center_ry_grid - SCENERY_RAD_MAX_ROOMS;
		int32_t full_y1 = center_ry_grid + SCENERY_RAD_MAX_ROOMS;

		for (size_t m = 0; m < s_visible_leaf_count; m++) {
			const ar_node_t *node = s_visible_leaves[m];
			if (!node || !node->values || node->w <= 0 || node->h <= 0)
				continue;

			int32_t rx0 = ndw_fdiv(node->ox, NDW_TILE_W);
			int32_t ry0 = ndw_fdiv(node->oy, NDW_TILE_H);
			int32_t span_x = node->w / NDW_TILE_W;
			int32_t span_y = node->h / NDW_TILE_H;
			if (span_x < 1) span_x = 1;
			if (span_y < 1) span_y = 1;
			int32_t rx1 = rx0 + span_x;
			int32_t ry1 = ry0 + span_y;

			if (!near_flushed && ry0 > center_ry_grid + obj_j1) {
				if (s_mid_batch_len > 0) {
					nd_seven_gl_flush_billboards(s_mid_stream_batch, s_mid_batch_len, atlas_rgba());
					s_mid_batch_len = 0;
				}
				if (all_gpu_count > 0)
					nd_seven_gl_flush_billboards(s_all_gpu_batch, all_gpu_count, atlas_rgba());
				near_flushed = 1;
			}

			float nz_min = (float)node->min_val / 16.0f;
			float nz_max = (float)node->max_val / 16.0f;

			int32_t cx0 = (rx0 > full_x0) ? rx0 : full_x0;
			int32_t cx1 = (rx1 < full_x1 + 1) ? rx1 : (full_x1 + 1);
			int32_t cy0 = (ry0 > full_y0) ? ry0 : full_y0;
			int32_t cy1 = (ry1 < full_y1 + 1) ? ry1 : (full_y1 + 1);

			uint32_t cur_atlas_ver = atlas_rgba_version();
			uint32_t cur_forget_seq = nd_seven_get_forget_seq();

			for (int32_t by0 = cy0; by0 < cy1; by0 += 8) {
				int32_t by1 = (by0 + 8 < cy1) ? (by0 + 8) : cy1;
				if (!near_flushed && by0 > center_ry_grid + obj_j1) {
					if (s_mid_batch_len > 0) {
						nd_seven_gl_flush_billboards(s_mid_stream_batch, s_mid_batch_len, atlas_rgba());
						s_mid_batch_len = 0;
					}
					if (all_gpu_count > 0)
						nd_seven_gl_flush_billboards(s_all_gpu_batch, all_gpu_count, atlas_rgba());
					near_flushed = 1;
				}
				for (int32_t bx0 = cx0; bx0 < cx1; bx0 += 8) {
					int32_t bx1 = (bx0 + 8 < cx1) ? (bx0 + 8) : cx1;
					int b_frustum_state = 2;
					if (s_frame_frustum_valid) {
						float bmin_x = (float)(bx0 * NDW_TILE_W) - 512.0f;
						float bmax_x = (float)(bx1 * NDW_TILE_W) + 512.0f;
						float bmin_y = (float)(by0 * NDW_TILE_H) - 512.0f;
						float bmax_y = (float)(by1 * NDW_TILE_H) + 512.0f;
						b_frustum_state = seven_test_frustum_xy_state(bmin_x, bmin_y, bmax_x, bmax_y,
						                                              (float)lwx, (float)lwy, nz_min, nz_max);
						if (b_frustum_state == 0)
							continue;
					}

					bool overlaps_near =
						!(bx1 - 1 < (int32_t)base_rx + obj_i0 ||
						  bx0     > (int32_t)base_rx + obj_i1 ||
						  by1 - 1 < center_ry_grid + obj_j0   ||
						  by0     > center_ry_grid + obj_j1);
					bool straddles_near_flush =
						(!near_flushed && by0 <= center_ry_grid + obj_j1 && by1 - 1 > center_ry_grid + obj_j1);

					if (b_frustum_state == 2 && tint_scenery != 0 &&
					    (bx1 - bx0) == 8 && (by1 - by0) == 8 &&
					    !overlaps_near && !straddles_near_flush &&
					    nd_seven_rect_is_pure_virgin(st, (int16_t)world, base_rz,
					                                 (int16_t)bx0, (int16_t)(bx1 - 1),
					                                 (int16_t)(-(by1 - 1)), (int16_t)(-by0))) {
						uint32_t slot_idx =
							((((uint32_t)(bx0 >> 3) & 63u) |
							  (((uint32_t)(by0 >> 3) & 63u) << 6)) ^
							 (((uint32_t)world & 3u) << 10)) & (BLOCK_BB_CACHE_SLOTS - 1u);
						block_bb_cache_slot_t *slot = &s_block_bb_cache[slot_idx];
						if (slot->valid &&
						    slot->bx0 == bx0 && slot->by0 == by0 &&
						    slot->world == (int16_t)world && slot->rz == base_rz &&
						    slot->w_level == w_level &&
						    slot->node_level == node->level &&
						    slot->node_ox == node->ox && slot->node_oy == node->oy &&
						    slot->node_rev == node->rev &&
						    slot->atlas_ver == cur_atlas_ver &&
						    slot->forget_seq == cur_forget_seq &&
						    (s_block_bb_arena_cursor - slot->arena_seq) <= BLOCK_BB_ARENA_CAP) {
							const lam_billboard_gpu_t *cached = &s_block_bb_arena[slot->arena_off];
							uint16_t cnt = slot->count;
							for (uint16_t ci = 0; ci < cnt; ci++) {
								if (s_mid_batch_len == MID_STREAM_CHUNK) {
									nd_seven_gl_flush_billboards(s_mid_stream_batch, s_mid_batch_len, atlas_rgba());
									s_mid_batch_len = 0;
								}
								lam_billboard_gpu_t bb = cached[ci];
								bb.tint_rgba = tint_scenery;
								s_mid_stream_batch[s_mid_batch_len++] = bb;
								if (s_last_mid_gpu_count < MID_MAX_DRAW)
									s_mid_gpu_batch[s_last_mid_gpu_count] = bb;
								s_last_mid_gpu_count++;
							}
							continue;
						}

						uint32_t cur_off = (uint32_t)(s_block_bb_arena_cursor & (BLOCK_BB_ARENA_CAP - 1u));
						if (cur_off + BLOCK_BB_MAX_PER_BLK > BLOCK_BB_ARENA_CAP) {
							s_block_bb_arena_cursor += (uint64_t)(BLOCK_BB_ARENA_CAP - cur_off);
							cur_off = 0;
						}
						uint64_t start_seq = s_block_bb_arena_cursor;
						lam_billboard_gpu_t *arena_dst = &s_block_bb_arena[cur_off];
						uint16_t arena_cnt = 0;

						for (int32_t ry_grid = by0; ry_grid < by1; ry_grid++) {
							for (int32_t rx_grid = bx0; rx_grid < bx1; rx_grid++) {
								int16_t pos[4] = {
									(int16_t)rx_grid,
									(int16_t)(-ry_grid),
									base_rz,
									(int16_t)world
								};
								seven_emit_room_billboards_ex(st, node, pos, w_level,
								                              tint_scenery, true,
								                              arena_dst, &arena_cnt);
							}
						}

						s_block_bb_arena_cursor += (uint64_t)arena_cnt;
						slot->bx0 = bx0;
						slot->by0 = by0;
						slot->node_ox = node->ox;
						slot->node_oy = node->oy;
						slot->node_rev = node->rev;
						slot->atlas_ver = cur_atlas_ver;
						slot->forget_seq = cur_forget_seq;
						slot->arena_seq = start_seq + (uint64_t)arena_cnt;
						slot->arena_off = cur_off;
						slot->count = arena_cnt;
						slot->world = (int16_t)world;
						slot->rz = base_rz;
						slot->w_level = w_level;
						slot->node_level = node->level;
						slot->valid = 1;
						continue;
					}

					for (int32_t ry_grid = by0; ry_grid < by1; ry_grid++) {
						if (!near_flushed && ry_grid > center_ry_grid + obj_j1) {
							if (s_mid_batch_len > 0) {
								nd_seven_gl_flush_billboards(s_mid_stream_batch, s_mid_batch_len, atlas_rgba());
								s_mid_batch_len = 0;
							}
							if (all_gpu_count > 0)
								nd_seven_gl_flush_billboards(s_all_gpu_batch, all_gpu_count, atlas_rgba());
							near_flushed = 1;
						}
						for (int32_t rx_grid = bx0; rx_grid < bx1; rx_grid++) {
							int16_t pos[4] = {
								(int16_t)rx_grid,
								(int16_t)(-ry_grid),
								base_rz,
								(int16_t)world
							};
							if (overlaps_near) {
								int ti = (int)pos[0] - (int)base_rx;
								int tj = (int)base_ry - (int)pos[1];
								if (ti >= obj_i0 && ti <= obj_i1 && tj >= obj_j0 && tj <= obj_j1)
									continue; /* Covered by near-field server window */
							}

							if (b_frustum_state == 1) {
								float rmin_x = (float)(rx_grid * NDW_TILE_W) - 320.0f;
								float rmax_x = (float)((rx_grid + 1) * NDW_TILE_W) + 320.0f;
								float rmin_y = (float)(ry_grid * NDW_TILE_H) - 320.0f;
								float rmax_y = (float)((ry_grid + 1) * NDW_TILE_H) + 320.0f;
								if (!seven_test_frustum_xy(rmin_x, rmin_y, rmax_x, rmax_y, (float)lwx, (float)lwy, nz_min, nz_max))
									continue;
							}

							seven_emit_room_billboards(st, node, pos, w_level, tint_scenery);
						}
					}
				}
			}
		}
		if (s_mid_batch_len > 0)
			nd_seven_gl_flush_billboards(s_mid_stream_batch, s_mid_batch_len, atlas_rgba());
		nd_seven_set_scenery_synth_budget(0xFFFFFFFFu);
	}
	if (!near_flushed && all_gpu_count > 0) {
		nd_seven_gl_flush_billboards(s_all_gpu_batch, all_gpu_count, atlas_rgba());
	}
	/* Water pass over submerged sprites (refraction, absorption, surface layer), then clouds and present */
	nd_seven_gl_flush_water();
	nd_seven_gl_flush_clouds();
	nd_seven_gl_end_frame();
}

uint32_t nd_seven_last_mid_billboard_count(void)
{
	return s_last_mid_gpu_count;
}

int nd_seven_last_mid_billboard(uint32_t idx, float *x, float *y, float *z, float *w, float *h)
{
	if (idx >= s_last_mid_gpu_count || idx >= MID_MAX_DRAW)
		return 0;
	if (x) *x = s_mid_gpu_batch[idx].x;
	if (y) *y = s_mid_gpu_batch[idx].y;
	if (z) *z = s_mid_gpu_batch[idx].z;
	if (w) *w = s_mid_gpu_batch[idx].width;
	if (h) *h = s_mid_gpu_batch[idx].height;
	return 1;
}
