#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <string.h>
#include <math.h>
#include <time.h>
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
static stl_wq_t *s_seven_wq = NULL;
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

static inline void seven_pack_field_sample(uint16_t world, uint8_t level,
                                           int32_t wx, int32_t wy, int16_t base_wl,
                                           const nd_field_sample_t *pfs, ar_sample_t *out)
{
	(void)level;
	nd_field_sample_t fs = *pfs;
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
	/* Canonical LOD-invariant sample packing:
	 * Because libareole reuses parent vertex samples across quadtree subdivisions
	 * and interpolates children across LOD levels, every vertex sample at (wx, wy)
	 * must depend ONLY on (world, wx, wy) and never on the quadtree level at which
	 * it was first populated. */
	uint8_t river_carve_q4 = fs.river_carve_q4;
	int16_t eff_wl = fs.water_level;
	bool eff_is_water = fs.is_water;
	int32_t eff_depth = eff_is_water ? ((int32_t)eff_wl - (fs.elevation_q12_4 >> 4)) : 0;
	if (eff_depth < 0) eff_depth = 0;
	out->attr[3] = (uint8_t)(eff_depth > 255 ? 255 : eff_depth);
	out->attr[6] = river_carve_q4;
	int32_t shore_ceil_q12_4 = ((int32_t)eff_wl + 4) << 4;
	uint8_t flags = (eff_depth > 0 || (int32_t)fs.elevation_q12_4 <= shore_ceil_q12_4 || river_carve_q4 > 0) ? 1u : 0u;
	if (fs.water_level > base_wl && fs.river_carve_q4 == 0)
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
		static _CHAFF_TLS noise_wcache_t s_eco_c5, s_eco_c3;
		int16_t p_eco[2] = { (int16_t)wy, (int16_t)wx };
		uint32_t n_meso = noise_wpoint_c(&s_eco_c5, p_eco, 5, 8, 0x45434F31u ^ world, 2, NOISE_FADE5) >> 16;
		uint32_t n_loc  = noise_wpoint_c(&s_eco_c3, p_eco, 3, 8, 0x45434F32u ^ world, 2, NOISE_FADE5) >> 16;
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
		if (elev_rel > ND_FIELD_MAX_ELEV_WU) elev_rel = ND_FIELD_MAX_ELEV_WU;

		/* Natural alpine snowline transition on mountain ranges (> 1500..2400 wu):
		 * Lowlands, plains, and foothills (< 1500 wu) preserve their authentic biome color in
		 * uSubstrateTex, while high cordilleras and summits (> 1500..2400+ wu) are capped in white snow. */
		int snow_w = 0;
		if (elev_rel > 1500 && !fs.is_water && fs.bio != BIOME_PERMANENT_ICE) {
			uint32_t c_snow = 0xFFFFFAF6u; /* Pure alpine snow RGB(246, 250, 255) */
			snow_w = (elev_rel <= 2400) ? ((elev_rel - 1500) * 255 / 900) : 255;
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

static void seven_macro_chunk_sampler(void *user_ctx, uint16_t world, uint8_t level,
                                     int32_t wx, int32_t wy, ar_sample_t *out)
{
	(void)user_ctx;
	/* Always evaluate the canonical multi-octave field (step_q16 = 0) at every quadtree
	 * level so H(wx, wy) is 100% invariant across all LOD levels and parent-child
	 * vertex reuse in libareole is exact and contamination-free. */
	nd_field_sample_t fs = nd_field_sample_step(world, 0, wx, wy);
	seven_pack_field_sample(world, level, wx, wy, nd_field_water_level(world), &fs, out);
}

static void seven_macro_chunk_row_sampler(void *user_ctx, uint16_t world, uint8_t level,
                                          int32_t wy, int32_t ox, int32_t span_w,
                                          uint32_t count, ar_sample_t *out_row)
{
	(void)user_ctx;
	if (!out_row || count == 0)
		return;
	int16_t base_wl = nd_field_water_level(world);
	int32_t gdim = (count > 1u) ? (int32_t)(count - 1u) : 1;
	nd_field_sample_t row_fs[64];
	if (count <= 64u) {
		nd_field_sample_row_step(world, 0, wy, ox, span_w, count, row_fs);
		for (uint32_t c = 0; c < count; c++) {
			int32_t wx = ox + (int32_t)(((int64_t)c * span_w) / gdim);
			seven_pack_field_sample(world, level, wx, wy, base_wl, &row_fs[c], &out_row[c]);
		}
	} else {
		for (uint32_t c = 0; c < count; c++) {
			int32_t wx = ox + (int32_t)(((int64_t)c * span_w) / gdim);
			nd_field_sample_t fs = nd_field_sample_step(world, 0, wx, wy);
			seven_pack_field_sample(world, level, wx, wy, base_wl, &fs, &out_row[c]);
		}
	}
}

static void seven_macro_chunk_eval_row4(void *user_ctx, uint16_t world, uint8_t level,
                                        const int32_t wx[4], int32_t wy, ar_sample_t out[4])
{
	(void)user_ctx;
	int16_t base_wl = nd_field_water_level(world);
	nd_field_sample_t fs[4];
	nd_field_sample_4x(world, 0, wx, wy, fs);
	for (int k = 0; k < 4; k++)
		seven_pack_field_sample(world, level, wx[k], wy, base_wl, &fs[k], &out[k]);
}

void nd_seven_drain_async(void)
{
	if (s_seven_wq)
		stl_wq_drain(s_seven_wq);
}

ar_system_t *nd_seven_get_chunk_sys(void)
{
	if (!s_seven_chunk_sys) {
		if (!s_seven_wq)
			s_seven_wq = stl_wq_create("seven", 0, 256);
		ar_config_t cfg;
		memset(&cfg, 0, sizeof(cfg));
		cfg.base_w = NDW_TILE_W;
		cfg.base_h = NDW_TILE_H;
		cfg.grid_dim = 48;
		cfg.sampler = seven_macro_chunk_sampler;
		cfg.row_sampler = seven_macro_chunk_row_sampler;
		cfg.eval_row4 = seven_macro_chunk_eval_row4;
		cfg.max_active = 224;
		cfg.pool_capacity = 768;
		cfg.max_levels = 17;
		cfg.default_rgba = 0xFF3C804Eu;
		cfg.period_x = (int32_t)ND_SEVEN_WORLD_DIM_W;
		cfg.period_y = (int32_t)ND_SEVEN_WORLD_DIM_H;
		cfg.max_populates_per_frame = 2;
		cfg.wq = s_seven_wq;
		cfg.samples_pool_capacity = 256;
		cfg.samples_max_level = 2;
		s_seven_chunk_sys = ar_system_create_ex(&cfg);
		if (s_seven_chunk_sys && nd_seven_perf_get_enabled())
			ar_system_set_timing_enabled(s_seven_chunk_sys, true);
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
	corolla_pose_t    pose;
	double            ref_origin[3];
	double            tube_center[3];
	double            tube_r_solid;
	float             eye_abs[3];
	uint16_t          world;
	float             water_level;
	bool              use_analytical_bounds;
	bool              has_tube_occ;
} seven_bounds_3d_ctx_t;

static void seven_torus_bounds_3d_cb(void *bounds_ctx, float ox, float oy, float w, float h,
                                     float z_min, float z_max,
                                     float out_min[3], float out_max[3],
                                     float out_center[3],
                                     float *out_span_w, float *out_span_h)
{
	const seven_bounds_3d_ctx_t *ctx = (const seven_bounds_3d_ctx_t *)bounds_ctx;
	float z0 = z_min, z1 = z_max;
	if (ctx->use_analytical_bounds) {
		float az0 = z0, az1 = z1;
		nd_field_elevation_bounds(ctx->world,
		                          (int32_t)floorf(ox), (int32_t)floorf(oy),
		                          (int32_t)ceilf(w), (int32_t)ceilf(h),
		                          &az0, &az1);
		if (az0 > z0) z0 = az0;
		if (az1 < z1) z1 = az1;
	}
	/* Preserve ocean / sea surface within the bounding volume so water chunks are never clipped */
	if (z0 <= ctx->water_level + 8.0f && z1 < ctx->water_level + 8.0f)
		z1 = ctx->water_level + 8.0f;
	if (z0 > z1)
		z0 = z1;

	double u0 = (double)ox / (double)ND_SEVEN_WORLD_DIM_W;
	double du = (double)w / (double)ND_SEVEN_WORLD_DIM_W;
	double v0 = ((double)oy / (double)ND_SEVEN_WORLD_DIM_H) + 0.5;
	double dv = (double)h / (double)ND_SEVEN_WORLD_DIM_H;

	double bmin[3], bmax[3];
	corolla_patch_aabb(&ctx->surf, &ctx->pose, ctx->ref_origin, u0, v0, du, dv,
	                   (double)z0, (double)z1, bmin, bmax);
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
		double h_mid = 0.5 * ((double)z0 + (double)z1);
		corolla_frame_t fr = {0};
		corolla_eval_frame(&ctx->surf, u_mid, v_mid, h_mid, &fr);
		double p_world[3];
		corolla_pose_point_to_inertial(&ctx->pose, fr.pos, p_world);
		out_center[0] = (float)(p_world[0] - ctx->ref_origin[0]);
		out_center[1] = (float)(p_world[1] - ctx->ref_origin[1]);
		out_center[2] = (float)(p_world[2] - ctx->ref_origin[2]);
	}
	double R_maj = ctx->surf.r_major;
	double r_min = ctx->surf.r_minor;
	double theta_mid = COROLLA_TWO_PI * (v0 + 0.5 * dv - 0.5);
	double r_ring = R_maj + r_min * cos(theta_mid);
	if (out_span_w) *out_span_w = (float)(COROLLA_TWO_PI * r_ring * fabs(du));
	if (out_span_h) *out_span_h = (float)(COROLLA_TWO_PI * r_min * fabs(dv));
}

static void seven_torus_point_3d_cb(void *bounds_ctx, float wx, float wy, float wz,
                                    float out_xyz[3])
{
	const seven_bounds_3d_ctx_t *ctx = (const seven_bounds_3d_ctx_t *)bounds_ctx;
	double u = (double)wx / (double)ND_SEVEN_WORLD_DIM_W;
	double v = ((double)wy / (double)ND_SEVEN_WORLD_DIM_H) + 0.5;
	corolla_frame_t fr = {0};
	corolla_eval_frame(&ctx->surf, u, v, (double)wz, &fr);
	double p_world[3];
	corolla_pose_point_to_inertial(&ctx->pose, fr.pos, p_world);
	out_xyz[0] = (float)(p_world[0] - ctx->ref_origin[0]);
	out_xyz[1] = (float)(p_world[1] - ctx->ref_origin[1]);
	out_xyz[2] = (float)(p_world[2] - ctx->ref_origin[2]);
}

/* Planetary Normal-Cone / Horizon Limb Culling Callback:
 * Rejects manifold quadtree nodes whose entire surface normal cone faces away from the camera eye
 * (beyond the planetary horizon limb or on the far side of the torus), accounting for normal
 * curvature alpha, finite patch parallax asin(r_patch / vlen), and analytical mountain limb dip. */
static bool seven_torus_cull_3d_cb(void *bounds_ctx, float ox, float oy, float w, float h,
                                   float z_min, float z_max,
                                   const float center_xyz[3], float span_w, float span_h)
{
	(void)center_xyz;
	const seven_bounds_3d_ctx_t *ctx = (const seven_bounds_3d_ctx_t *)bounds_ctx;
	if (!ctx)
		return true;

	double du = fabs((double)w / (double)ND_SEVEN_WORLD_DIM_W);
	double dv = fabs((double)h / (double)ND_SEVEN_WORLD_DIM_H);
	/* Maximum angular half-angle of normal variation across the patch [u0..u0+du, v0..v0+dv]:
	 * On a torus, normal depends on phi = 2*pi*u and theta = 2*pi*v, so max normal angle from center is pi*sqrt(du^2 + dv^2). */
	float alpha = (float)(3.14159265358979323846 * sqrt(du * du + dv * dv));
	if (alpha >= 1.15f)
		return true; /* Allow very coarse planetary root quadrants to subdivide first */

	double u_mid = ((double)ox + 0.5 * (double)w) / (double)ND_SEVEN_WORLD_DIM_W;
	double v_mid = (((double)oy + 0.5 * (double)h) / (double)ND_SEVEN_WORLD_DIM_H) + 0.5;
	double h_mid = 0.5 * ((double)z_min + (double)z_max);

	corolla_frame_t fr = {0};
	corolla_eval_frame(&ctx->surf, u_mid, v_mid, h_mid, &fr);

	double p_world[3], n_world[3];
	corolla_pose_point_to_inertial(&ctx->pose, fr.pos, p_world);
	corolla_pose_vec_to_inertial(&ctx->pose, fr.norm, n_world);

	double vx = (double)ctx->eye_abs[0] - p_world[0];
	double vy = (double)ctx->eye_abs[1] - p_world[1];
	double vz = (double)ctx->eye_abs[2] - p_world[2];
	double vlen = sqrt(vx * vx + vy * vy + vz * vz);
	double r_patch = 0.5 * sqrt((double)span_w * (double)span_w + (double)span_h * (double)span_h) +
	                 0.5 * fabs((double)(z_max - z_min)) + 64.0;
	if (vlen <= r_patch * 1.15)
		return true; /* Eye is inside or adjacent to patch bounding sphere: never horizon-cull */

	/* Exact inscribed solid-rock tube-ball eclipse test:
	 * The ball B(tube_center, r_min - 2500) centered on the torus core ring directly beneath the
	 * camera lies 100% inside solid planetary rock at every latitude (outer, polar, and concave inner)
	 * whenever R > r + 2500. Any patch whose bounding sphere lies inside its shadow cone is invisible. */
	if (ctx->has_tube_occ && ctx->tube_r_solid > 1.0) {
		double ux = ctx->tube_center[0] - (double)ctx->eye_abs[0];
		double uy = ctx->tube_center[1] - (double)ctx->eye_abs[1];
		double uz = ctx->tube_center[2] - (double)ctx->eye_abs[2];
		double dc = sqrt(ux * ux + uy * uy + uz * uz);
		double sag_pad = (1.0 - cos(0.5 * (double)alpha)) * (ctx->surf.r_major + ctx->surf.r_minor);
		double r_occ_patch = r_patch * 1.05 + sag_pad;
		if (dc > ctx->tube_r_solid && vlen > dc && vlen > r_occ_patch * 1.05) {
			double cos_gamma = -(vx * ux + vy * uy + vz * uz) / (vlen * dc);
			if (cos_gamma > 0.0) {
				double theta_occ = asin(ctx->tube_r_solid / dc);
				double sin_p = r_occ_patch / vlen;
				if (sin_p < 1.0) {
					double theta_p = asin(sin_p);
					if (theta_occ > theta_p + 0.01 &&
					    cos_gamma > cos(theta_occ - theta_p - 0.01)) {
						return false;
					}
				}
			}
		}
	}

	/* Planetary horizon limb / back-hemisphere normal-cone test:
	 * Anchor the sightline vector at the solid-rock datum h_base = min(z_min, -2500) rather than
	 * h_mid so a tall mountain rising above a low-altitude camera (z_eye < h_mid) never yields a
	 * negative normal dot product that gets misclassified as being over the curved planetary limb. */
	double r_min = (ctx->surf.r_minor > 1.0) ? ctx->surf.r_minor : 1335000.0;
	double h_base = ((double)z_min < -2500.0) ? (double)z_min : -2500.0;
	double dh_base = h_base - h_mid;
	double bx = vx - dh_base * n_world[0];
	double by = vy - dh_base * n_world[1];
	double bz = vz - dh_base * n_world[2];
	double blen = sqrt(bx * bx + by * by + bz * bz);
	if (blen <= r_patch * 1.15)
		return true;
	double dot_vn = (bx * n_world[0] + by * n_world[1] + bz * n_world[2]) / blen;
	double r_base = (r_min + h_base > 1.0) ? (r_min + h_base) : 1.0;
	double h_above = ((double)z_max > h_base) ? ((double)z_max - h_base) : 0.0;
	float limb_dip = (h_above > 0.0) ? acosf(fminf(1.0f, (float)(r_base / (r_base + h_above)))) : 0.0f;
	float max_ang = alpha + limb_dip + 0.03f;
	if (max_ang >= 1.55f)
		return true;
	return dot_vn >= (double)(-sinf(max_ang));
}

/* Fast O(1) planetary occlusion check for the companion planet (alt_world):
 * 1. When near the primary planet's surface (dist < 40,000), if the entire bounding sphere
 *    of alt_world lies below the local ground horizon tangent plane, it is occluded by the ground under our feet.
 * 2. When in orbit, if the primary planet's solid torus body blocks the entire sight cone to alt_world, it is eclipsed. */
int nd_seven_is_alt_planet_occluded(const float eye_3d[3], float dist,
                                    float lwx, float lwy, float lwz,
                                    const nd_planet_def_t *prim_pdef,
                                    const nd_planet_def_t *alt_pdef,
                                    float alt_body_rad)
{
	if (!eye_3d || !prim_pdef || !alt_pdef)
		return 0;

	float dx = alt_pdef->center[0] - eye_3d[0];
	float dy = alt_pdef->center[1] - eye_3d[1];
	float dz = alt_pdef->center[2] - eye_3d[2];
	float d_alt = sqrtf(dx * dx + dy * dy + dz * dz);
	if (d_alt <= alt_body_rad + 1.0f)
		return 0;

	if (dist < 40000.0f) {
		float pos[3], N[3];
		nd_seven_gl_torus_frame(lwx, lwy, lwz, prim_pdef, pos, NULL, NULL, N);
		/* Horizon dip angle below local tangent plane at altitude h = max(0, dist):
		 * sin(dip) ~ sqrt(2*h / r_min). Below local horizon + dip, the ground occludes the sky. */
		float h_eye = fmaxf(0.0f, (eye_3d[0] - pos[0]) * N[0] +
		                          (eye_3d[1] - pos[1]) * N[1] +
		                          (eye_3d[2] - pos[2]) * N[2]);
		float r_min = (prim_pdef->minor_r > 1.0f) ? prim_pdef->minor_r : 1335000.0f;
		float sin_dip = sqrtf(fminf(0.25f, (2.0f * h_eye) / r_min)) + 0.03f;
		float elev_dot = dx * N[0] + dy * N[1] + dz * N[2];
		if (elev_dot < -alt_body_rad - d_alt * sin_dip)
			return 1;
	}

	/* Orbital solid-body eclipse check against the primary planet's solid torus core:
	 * Because the torus SDF has Lipschitz constant 1, if any point Q(t) along the centerline ray
	 * from eye_3d to alt_pdef->center satisfies sdf_torus(Q(t)) < -(t / d_alt) * alt_body_rad,
	 * the entire cone subtended by alt_world lies strictly inside the primary planet's solid tube. */
	float R_maj = (prim_pdef->major_r > 1.0f) ? prim_pdef->major_r : 2000000.0f;
	float r_solid = ((prim_pdef->minor_r > 1.0f) ? prim_pdef->minor_r : 1335000.0f) - 2500.0f;
	float R_outer = R_maj + r_solid;
	float ux = dx / d_alt, uy = dy / d_alt, uz = dz / d_alt;
	float p0x = eye_3d[0] - prim_pdef->center[0];
	float p0y = eye_3d[1] - prim_pdef->center[1];
	float p0z = eye_3d[2] - prim_pdef->center[2];
	float t_close = -(p0x * ux + p0y * uy + p0z * uz);
	float t_min = fmaxf(1000.0f, t_close - R_outer);
	float t_max = fminf(d_alt - alt_body_rad, t_close + R_outer);
	if (t_max > t_min) {
		bool has_rot = (prim_pdef->rot[0] != 0.0f || prim_pdef->rot[4] != 0.0f || prim_pdef->rot[8] != 0.0f);
		for (int s = 0; s <= 16; s++) {
			float t = t_min + ((float)s / 16.0f) * (t_max - t_min);
			float qx = p0x + t * ux;
			float qy = p0y + t * uy;
			float qz = p0z + t * uz;
			float lx = qx, ly = qy, lz = qz;
			if (has_rot) {
				lx = prim_pdef->rot[0] * qx + prim_pdef->rot[3] * qy + prim_pdef->rot[6] * qz;
				ly = prim_pdef->rot[1] * qx + prim_pdef->rot[4] * qy + prim_pdef->rot[7] * qz;
				lz = prim_pdef->rot[2] * qx + prim_pdef->rot[5] * qy + prim_pdef->rot[8] * qz;
			}
			float rho = sqrtf(lx * lx + ly * ly);
			float dr = rho - R_maj;
			float sdf = sqrtf(dr * dr + lz * lz) - r_solid;
			float cone_r = (t / d_alt) * alt_body_rad;
			if (sdf < -cone_r)
				return 1;
		}
	}

	return 0;
}

#define SEVEN_MAX_VISIBLE_LEAVES 224
static const ar_node_t *s_visible_leaves[SEVEN_MAX_VISIBLE_LEAVES];
static size_t s_visible_leaf_count = 0;
typedef struct {
	int32_t  ox, oy, w, h;
	uint8_t  level;
	uint32_t rev;
} seven_leaf_bounds_t;
static seven_leaf_bounds_t s_front_leaf_bounds[SEVEN_MAX_VISIBLE_LEAVES];
static size_t s_front_leaf_count = 0;
static uint32_t s_last_ridge_culled_count = 0;
static uint32_t s_last_alt_planet_chunk_count = 0;
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
	                           dx0, dy0, base_z_min - sag - 256.0f,
	                           dx1, dy1, base_z_max + 720.0f);
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
	float max_z = base_z_max + 720.0f;
	int all_inside = 1;
	for (int i = 0; i < 6; i++) {
		float a = s_frame_frustum.planes[i][0];
		float b = s_frame_frustum.planes[i][1];
		float c = s_frame_frustum.planes[i][2];
		float d = s_frame_frustum.planes[i][3];
		float px = (a > 0.0f) ? dx1 : dx0;
		float py = (b > 0.0f) ? dy1 : dy0;
		float pz = (c > 0.0f) ? max_z : min_z;
		if (a * px + b * py + c * pz + d < 0.0f)
			return 0;
		if (all_inside) {
			float nx = (a > 0.0f) ? dx0 : dx1;
			float ny = (b > 0.0f) ? dy0 : dy1;
			float nz = (c > 0.0f) ? min_z : max_z;
			if (a * nx + b * ny + c * nz + d < 0.0f)
				all_inside = 0;
		}
	}
	return all_inside ? 2 : 1;
}

/* Universal size/distance billboard fade & culling:
 * Every billboard's cull distance and opacity depend solely on its physical
 * dimensions (bw, bh) and 3D camera distance d = sqrt(cam_dist^2 + dx^2 + dy^2 + dz^2),
 * scaled by the user-adjustable detail multiplier s_frame_detail_scale. */
#define SCENERY_RAD_MAX_ROOMS        96
#define SEVEN_BB_MAX_EFF_SIZE        68.0f
#define SEVEN_BB_MAX_RIDGE_EFF_SIZE  88.0f
#define SEVEN_BB_DIST_PER_SIZE       120.0f
#define SEVEN_BB_FADE_START_RATIO    0.60f

static float s_cam_focal_x = 0.0f;
static float s_cam_focal_y = 0.0f;
static float s_cam_focal_z = 0.0f;
static float s_cam_dist = ND_SEVEN_CAM_DIST;
static float s_frame_detail_scale = 1.0f;
static float s_frame_anim_time = 0.0f;
static uint32_t s_frame_dispatch_count = 0u;

static inline float seven_max_eff_bb_size(void)
{
	/* Preserve 0.35x detail test cutoff (2,856 < 3,500) while extending forested ridge silhouettes at >= 0.45x */
	return (s_frame_detail_scale >= 0.45f) ? SEVEN_BB_MAX_RIDGE_EFF_SIZE : SEVEN_BB_MAX_EFF_SIZE;
}

static inline float seven_billboard_cull_dist(float bw, float bh)
{
	float max_eff = seven_max_eff_bb_size();
	float sz = (bw > bh) ? bw : bh;
	if (sz < 1.0f)
		sz = 1.0f;
	if (sz > max_eff)
		sz = max_eff;
	return SEVEN_BB_DIST_PER_SIZE * s_frame_detail_scale * sz;
}

static inline float seven_eval_billboard_alpha(float wx, float wy, float wz,
                                               float bw, float bh)
{
	float cull_d = seven_billboard_cull_dist(bw, bh);
	if (cull_d <= 1.0f)
		return 0.0f;
	float dx = wx - s_cam_focal_x;
	float dy = wy - s_cam_focal_y;
	float dz = wz - s_cam_focal_z;
	float d2 = s_cam_dist * s_cam_dist + 0.12f * (dx * dx + dy * dy + dz * dz);
	if (d2 >= cull_d * cull_d)
		return 0.0f;
	float start_d = cull_d * SEVEN_BB_FADE_START_RATIO;
	if (d2 <= start_d * start_d)
		return 1.0f;
	float d = sqrtf(d2);
	float t = (d - start_d) / (cull_d - start_d);
	return 1.0f - t * t * (3.0f - 2.0f * t);
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
	s_frame_anim_time = anim_t;
	s_frame_dispatch_count++;

	nd_planet_def_t cur_pdef;
	nd_seven_eval_planet_at_time(world, world, anim_t, &cur_pdef);
	const nd_planet_def_t *pdef = &cur_pdef;

	float dist = (eff_cam_dist > 1.0f) ? eff_cam_dist : ND_SEVEN_CAM_DIST;
	float lwz_est = nd_seven_ground_elevation_f((int32_t)lwx, (int32_t)lwy);
	float yaw_deg = nd_seven_get_yaw_deg();
	int16_t lamb_wl_pre = 0;
	bool has_pre_water = seven_has_water_at(world, (int32_t)lwx, (int32_t)lwy, &lamb_wl_pre);
	float target_z = (has_pre_water && (float)lamb_wl_pre > lwz_est) ? (float)lamb_wl_pre : lwz_est;
	float pitch_deg = nd_seven_clamp_cam_pitch(world, (float)lwx, (float)lwy, target_z,
	                                           dist, yaw_deg, nd_seven_get_pitch_deg());
	float yaw_rad = yaw_deg * (3.14159265358979323846f / 180.0f);
	float cos_y = cosf(yaw_rad);
	float sin_y = sinf(yaw_rad);
	float pitch_rad = pitch_deg * (3.14159265358979323846f / 180.0f);
	float cos_p = cosf(pitch_rad);
	float sin_p = sinf(pitch_rad);

	/* Compute origin-relative torus-Jacobian-calibrated tangent-space MVP for near-field scenery
	 * culling so horizontal/vertical frustum bounds match the 3D torus GPU projection at every
	 * latitude (2.0x East-West metric at outer equator, 0.40x at concave inner hub). */
	double theta_cam = 6.28318530717958647692 * ((double)lwy / (double)ND_SEVEN_WORLD_DIM_H);
	float torus_sx = (float)((6.28318530717958647692 * ((double)pdef->major_r + (double)pdef->minor_r * cos(theta_cam))) /
	                         (double)ND_SEVEN_WORLD_DIM_W);
	float torus_sy = (float)((6.28318530717958647692 * (double)pdef->minor_r) / (double)ND_SEVEN_WORLD_DIM_H);
	if (torus_sx < 0.05f) torus_sx = 0.05f;
	if (torus_sy < 0.05f) torus_sy = 0.05f;
	float flat_mvp[16];
	nd_seven_gl_compute_flat_mvp_ex((uint32_t)w, (uint32_t)h, 0.0f, 0.0f, lwz_est,
	                                dist, yaw_deg, pitch_deg, ND_SEVEN_CAM_FOV, flat_mvp);
	for (int r = 0; r < 4; r++) {
		flat_mvp[0 + r] *= torus_sx;
		flat_mvp[4 + r] *= torus_sy;
	}
	ar_frustum_from_matrix(&s_frame_frustum, flat_mvp);
	s_frame_frustum_valid = 1;

	/* Compute camera-origin-relative 3D curved-torus camera MVP (matches nd_seven_gl_begin_frame_ex bit-for-bit) */
	float mvp_3d[16], eye_rel[3], eye_abs[3];
	double ref_origin[3];
	nd_seven_gl_compute_mvp_rel_ex((uint32_t)w, (uint32_t)h, (float)lwx, (float)lwy, lwz_est,
	                               dist, yaw_deg, pitch_deg, ND_SEVEN_CAM_FOV,
	                               pdef, mvp_3d, ref_origin, eye_rel, eye_abs);

	float r_planet = pdef->minor_r;
	/* Global analytical Z ceiling tightened per patch by nd_field_elevation_bounds & cached node bounds */
	float z_min_cull = -2150.0f;
	float z_max_cull =  ND_FIELD_GLOBAL_MAX_Z;
	float cull_margin = 180.0f;
	s_frame_sag_radius = r_planet;

	s_frame_detail_scale = nd_seven_get_detail_scale();
	nd_seven_alt_profile_t alt_prof;
	nd_seven_eval_alt_profile(dist, s_frame_detail_scale,
	                          nd_seven_get_res_scale(),
	                          nd_seven_get_cloud_quality(), &alt_prof);
	float eff_res = alt_prof.eff_res_scale * nd_seven_get_drs_scale();
	if (eff_res < ND_SEVEN_RES_MIN) eff_res = ND_SEVEN_RES_MIN;
	if (eff_res > ND_SEVEN_RES_MAX) eff_res = ND_SEVEN_RES_MAX;
	nd_seven_gl_set_render_scales(eff_res, alt_prof.eff_cloud_scale);
	float target_quad_px = alt_prof.target_quad_px;
	float ar_vp_w = fmaxf(1.0f, (float)w * eff_res);
	float ar_vp_h = fmaxf(1.0f, (float)h * eff_res);
	float focal_px = (0.5f * ar_vp_h) / tanf(ND_SEVEN_CAM_FOV * 0.5f * (3.14159265358979323846f / 180.0f));
	float split_thresh = target_quad_px * 48.0f;
	float lod_px_scale = (split_thresh > 1e-4f) ? (focal_px / split_thresh) : 0.0f;
	nd_seven_gl_set_lod_morph_params(lod_px_scale, torus_sx, torus_sy);
	size_t max_leaves = SEVEN_MAX_VISIBLE_LEAVES;
	uint8_t min_lvl = alt_prof.min_level;

	seven_bounds_3d_ctx_t primary_bctx;
	memset(&primary_bctx, 0, sizeof(primary_bctx));
	primary_bctx.surf = corolla_torus((double)pdef->major_r, (double)pdef->minor_r);
	for (int i = 0; i < 9; i++)
		primary_bctx.pose.rot[i] = (double)pdef->rot[i];
	primary_bctx.pose.center[0] = (double)pdef->center[0];
	primary_bctx.pose.center[1] = (double)pdef->center[1];
	primary_bctx.pose.center[2] = (double)pdef->center[2];
	primary_bctx.ref_origin[0] = ref_origin[0];
	primary_bctx.ref_origin[1] = ref_origin[1];
	primary_bctx.ref_origin[2] = ref_origin[2];
	primary_bctx.eye_abs[0] = eye_abs[0];
	primary_bctx.eye_abs[1] = eye_abs[1];
	primary_bctx.eye_abs[2] = eye_abs[2];
	primary_bctx.world = world;
	primary_bctx.water_level = (float)nd_field_water_level(world);
	primary_bctx.use_analytical_bounds = true;
	if ((double)pdef->major_r > (double)pdef->minor_r + 2500.0 && (double)pdef->minor_r > 2500.0) {
		double u_cam = (double)lwx / (double)ND_SEVEN_WORLD_DIM_W;
		double v_cam = ((double)lwy / (double)ND_SEVEN_WORLD_DIM_H) + 0.5;
		corolla_frame_t cam_fr = {0};
		corolla_eval_frame(&primary_bctx.surf, u_cam, v_cam, 0.0, &cam_fr);
		double cam_p[3], cam_n[3];
		corolla_pose_point_to_inertial(&primary_bctx.pose, cam_fr.pos, cam_p);
		corolla_pose_vec_to_inertial(&primary_bctx.pose, cam_fr.norm, cam_n);
		double r_min_d = (double)pdef->minor_r;
		primary_bctx.tube_center[0] = cam_p[0] - r_min_d * cam_n[0];
		primary_bctx.tube_center[1] = cam_p[1] - r_min_d * cam_n[1];
		primary_bctx.tube_center[2] = cam_p[2] - r_min_d * cam_n[2];
		primary_bctx.tube_r_solid = r_min_d - 2500.0;
		primary_bctx.has_tube_occ = true;
	}

	ar_occluder_t *occ = nd_seven_gl_begin_terrain_occluder(mvp_3d, ref_origin, pdef);
	float prim_inner_r = pdef->major_r - pdef->minor_r;
	if (prim_inner_r < 1.0f) prim_inner_r = 1.0f;

	static uint16_t s_last_pop_world = 0xFFFFu;
	static float s_last_pop_dist = 0.0f;
	bool instant_jump = (s_visible_leaf_count == 0 || s_last_pop_world != world ||
	                     s_last_pop_dist <= 1.0f ||
	                     dist > s_last_pop_dist * 2.5f || dist < s_last_pop_dist * 0.4f);
	uint32_t max_pops_query = instant_jump ? 0u : 20u;
	s_last_pop_world = world;
	s_last_pop_dist = dist;

	ar_cull_opts_t opts = {
		.z_min = z_min_cull,
		.z_max = z_max_cull,
		.sag_radius = r_planet,
		.focal_x = (float)lwx,
		.focal_y = (float)lwy,
		.focal_z = lwz_est,
		.bounds_3d_fn = seven_torus_bounds_3d_cb,
		.cull_3d_fn = seven_torus_cull_3d_cb,
		.point_3d_fn = seven_torus_point_3d_cb,
		.bounds_ctx = &primary_bctx,
		.margin = cull_margin,
		.target_quad_px = target_quad_px,
		.viewport_w = ar_vp_w,
		.viewport_h = ar_vp_h,
		.min_level = min_lvl,
		.max_level = 14,
		.view_id = 0,
		.max_populates_per_query = max_pops_query,
		.split_hysteresis = 0.25f,
		.occluder = occ,
		.occluder_z_bias = 1.5f,
		.occluder_texel_bias = 0.0f,
		.occluder_concave_inv_r = 1.0f / prim_inner_r,
		.occluder_box_z_pad = 16.0f,
		.occluder_no_write = false,
		.use_cached_bounds = true,
		.cached_bounds_max_level = 3,
		.cached_bounds_pad = 32.0f
	};

	ar_system_begin_frame(cs);
	s_visible_leaf_count = ar_system_query_matrix(cs, world, mvp_3d,
	                                              &opts, s_visible_leaves, max_leaves);
	size_t macro_count = s_visible_leaf_count;
	s_last_ridge_culled_count = occ ? occ->culled_count : 0u;
	nd_seven_gl_end_terrain_occluder(occ != NULL, s_last_ridge_culled_count);

	/* Re-evaluate focal elevation on the newly active View 0 leaves */
	float lwz = nd_seven_ground_elevation_f((int32_t)lwx, (int32_t)lwy);
	float lamb_light_z = nd_seven_lamb_elevation_f(st, (int32_t)lwx, (int32_t)lwy);

	s_cam_focal_x = (float)lwx;
	s_cam_focal_y = (float)lwy;
	s_cam_focal_z = lwz;
	s_cam_dist = (dist > 1.0f) ? dist : ND_SEVEN_CAM_DIST;

	/* Begin 3D WebGL frame in pure C */
	nd_seven_gl_set_lamb_light((float)lwx, (float)lwy, lamb_light_z,
	                           nd_seven_is_universe_owner(st) ? 1.0f : 0.0f);
	nd_seven_gl_begin_frame_ex((uint32_t)w, (uint32_t)h, (float)lwx, (float)lwy, lwz,
	                           dist, yaw_deg, pitch_deg, ND_SEVEN_CAM_FOV,
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
		/* Always lock focal_sea_level to the Lamb's actual local water table (or base sea level on dry land)
		 * so coarse terrain chunks containing distant perched mountain lakes never override billboard uSeaLevel. */
		nd_seven_gl_set_focal_water(lamb_in_water ? eff_wl : (float)base_wl);
		float wake_ground_z = lamb_in_water ? (lwz - ((float)lamb_wl - (float)base_wl)) : (float)base_wl + 10.0f;

		float lamb_render_z = lamb_light_z;
		float waterline_dy = lamb_in_water
		                     ? fmaxf(0.0f, fminf(6.5f, (lamb_render_z + 6.0f) - eff_wl))
		                     : 4.5f;
		float wake_inv_sin = 1.0f / fmaxf(0.18f, sin_p);
		float wake_wx = (float)lwx + (waterline_dy * wake_inv_sin) * sin_y;
		float wake_wy = (float)lwy + (waterline_dy * wake_inv_sin) * cos_y;

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

	/* s_visible_leaves is already ordered front-to-back by ar_system_update_view();
	 * copy leaf bounds into s_front_leaf_bounds[] in O(N) for introspection & diagnostics. */
	(void)cos_p;
	if (macro_count >= 1) {
		size_t copy_n = (macro_count <= SEVEN_MAX_VISIBLE_LEAVES) ? macro_count : SEVEN_MAX_VISIBLE_LEAVES;
		for (size_t a = 0; a < copy_n; a++) {
			const ar_node_t *nd = s_visible_leaves[a];
			if (!nd) {
				memset(&s_front_leaf_bounds[a], 0, sizeof(s_front_leaf_bounds[a]));
				continue;
			}
			s_front_leaf_bounds[a].ox = nd->ox;
			s_front_leaf_bounds[a].oy = nd->oy;
			s_front_leaf_bounds[a].w  = nd->w;
			s_front_leaf_bounds[a].h  = nd->h;
			s_front_leaf_bounds[a].level = nd->level;
			s_front_leaf_bounds[a].rev   = nd->rev;
		}
	}

	for (size_t m = 0; m < macro_count; m++) {
		const ar_node_t *node = s_visible_leaves[m];
		if (!node) continue;

		int16_t w_lvl_i = nd_field_water_level(world);
		bool has_water = ((node->flags & 1) != 0 || node->min_val <= ((int32_t)(w_lvl_i + 8) << 4));
		if (node->level <= 2 && (node->flags & 2) != 0 && node->samples &&
		    node->min_val > ((int32_t)(w_lvl_i + 8) << 4)) {
			uint32_t vcnt = node->vert_count;
			int16_t lake_wl = w_lvl_i;
			bool has_dry_below_lake = false;
			for (uint32_t vi = 0; vi < vcnt; vi++) {
				if (node->samples[vi].flags & 2) {
					int16_t s_wl = (int16_t)((uint16_t)node->samples[vi].attr[4] |
					                         ((uint16_t)node->samples[vi].attr[5] << 8));
					if (s_wl > lake_wl) {
						lake_wl = s_wl;
						break;
					}
				}
			}
			if (lake_wl > w_lvl_i) {
				int32_t lake_floor_q = ((int32_t)lake_wl - 1) << 4;
				for (uint32_t vi = 0; vi < vcnt; vi++) {
					if ((node->samples[vi].flags & 2) == 0 &&
					    node->samples[vi].val < lake_floor_q) {
						has_dry_below_lake = true;
						break;
					}
				}
				if (!has_dry_below_lake)
					w_lvl_i = lake_wl;
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
	s_visible_leaf_count = macro_count;
	s_front_leaf_count = (macro_count <= SEVEN_MAX_VISIBLE_LEAVES) ? macro_count : SEVEN_MAX_VISIBLE_LEAVES;
	s_last_alt_planet_chunk_count = 0;

	/* Multi-Planet 3D Manifold Rendering:
	 * Evaluate the companion body's dynamic 3D Keplerian orbit & tilted spin pose at anim_t,
	 * and query libareole in camera-origin-relative space with true 3D curved-torus bounding boxes,
	 * normal-cone horizon culling, and the primary planet's shared 2D Hi-Z occluder (occluder_no_write). */
	{
		uint16_t alt_world = (world == 2) ? 1 : 2;
		nd_planet_def_t alt_pdef;
		nd_seven_eval_planet_at_time(alt_world, world, anim_t, &alt_pdef);

		ar_frustum_t frustum_3d;
		ar_frustum_from_matrix(&frustum_3d, mvp_3d);
		float body_rad = alt_pdef.major_r + alt_pdef.minor_r + 8000.0f;
		float alt_cx = (float)((double)alt_pdef.center[0] - ref_origin[0]);
		float alt_cy = (float)((double)alt_pdef.center[1] - ref_origin[1]);
		float alt_cz = (float)((double)alt_pdef.center[2] - ref_origin[2]);
		if (!nd_seven_is_alt_planet_occluded(eye_abs, dist, (float)lwx, (float)lwy, lwz,
		                                     pdef, &alt_pdef, body_rad) &&
		    ar_frustum_test_box(&frustum_3d,
		                        alt_cx - body_rad,
		                        alt_cy - body_rad,
		                        alt_cz - body_rad,
		                        alt_cx + body_rad,
		                        alt_cy + body_rad,
		                        alt_cz + body_rad)) {
			seven_bounds_3d_ctx_t alt_bctx;
			memset(&alt_bctx, 0, sizeof(alt_bctx));
			alt_bctx.surf = corolla_torus((double)alt_pdef.major_r, (double)alt_pdef.minor_r);
			for (int i = 0; i < 9; i++)
				alt_bctx.pose.rot[i] = (double)alt_pdef.rot[i];
			alt_bctx.pose.center[0] = (double)alt_pdef.center[0];
			alt_bctx.pose.center[1] = (double)alt_pdef.center[1];
			alt_bctx.pose.center[2] = (double)alt_pdef.center[2];
			alt_bctx.ref_origin[0] = ref_origin[0];
			alt_bctx.ref_origin[1] = ref_origin[1];
			alt_bctx.ref_origin[2] = ref_origin[2];
			alt_bctx.eye_abs[0] = eye_abs[0];
			alt_bctx.eye_abs[1] = eye_abs[1];
			alt_bctx.eye_abs[2] = eye_abs[2];
			alt_bctx.world = alt_world;
			alt_bctx.water_level = (float)nd_field_water_level(alt_world);
			alt_bctx.use_analytical_bounds = true;

			float alt_inner_r = alt_pdef.major_r - alt_pdef.minor_r;
			if (alt_inner_r < 1.0f) alt_inner_r = 1.0f;

			ar_cull_opts_t alt_opts = opts;
			alt_opts.view_id = 2;
			alt_opts.min_level = 10;
			alt_opts.max_level = 14;
			alt_opts.bounds_3d_fn = seven_torus_bounds_3d_cb;
			alt_opts.cull_3d_fn = seven_torus_cull_3d_cb;
			alt_opts.point_3d_fn = seven_torus_point_3d_cb;
			alt_opts.bounds_ctx = &alt_bctx;
			alt_opts.occluder = occ;
			alt_opts.occluder_no_write = true;
			alt_opts.occluder_concave_inv_r = 1.0f / alt_inner_r;
			alt_opts.occluder_box_z_pad = 0.0f;
			alt_opts.use_cached_bounds = false;

			const ar_node_t *alt_visible[32];
			size_t alt_count = ar_system_query_matrix(cs, alt_world, mvp_3d, &alt_opts, alt_visible, 32);
			s_last_alt_planet_chunk_count = (uint32_t)alt_count;
			if (occ)
				s_last_ridge_culled_count = occ->culled_count;
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

	/* Sky-after-terrain Early-Z pass: draws only uncovered sky pixels at depth == 1.0 (GL_LEQUAL). */
	nd_seven_gl_flush_sky();

	/* The water queue is flushed after the sprites (nd_seven_render): the water pass composites what stands below the surface */
}

#define MID_MAX_DRAW 262144
#define MID_STREAM_CHUNK 16384
#ifndef __wasm__
static lam_billboard_gpu_t s_mid_gpu_batch[MID_MAX_DRAW];
#endif
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
	bb->mesh_id = 0;
	if (!o)
		return;
	bb->mesh_id = nd_seven_obj_mesh3d_id(o);

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
		if (sr && sr->w > 24) {
			uint32_t rx = (uint32_t)(sr->w * 0.34f);
			if (rx < 11u) rx = 11u;
			if (rx > 22u) rx = 22u;
			bb->shadow_rx = (uint8_t)rx;
			bb->shadow_ry = (uint8_t)(rx * 0.45f > 5.0f ? rx * 0.45f : 5.0f);
		} else {
			bb->shadow_rx = 11;
			bb->shadow_ry = 5;
		}
		bb->style = ND_BB_STYLE_MINERAL;
		break;
	default:
		break;
	}
}

void nd_seven_calc_obj_dims(const ndw_obj_t *o, float sr_w, float sr_h,
                            float *out_w, float *out_h, float *out_ay)
{
	float w = sr_w;
	float h = sr_h;
	float ay = o ? (float)ndw_obj_anchor_y(o, (int)sr_h) : 2.0f;
	if (o && h > 1.0f) {
		int is_plant = 0;
		unsigned sp_idx = 10u;
		unsigned is_big = o->big ? 1u : 0u;
		if (ND_PLANT_UNPACK_PRESENT(o->plant_id)) {
			sp_idx = ND_PLANT_UNPACK_IDX(o->plant_id);
			is_big = ND_PLANT_UNPACK_BIG(o->plant_id);
			nd_m3d_plant_metric_dims(sp_idx, is_big, &w, &h, &ay);
			is_plant = 1;
		} else if (o->kind == NDW_K_DECID || o->kind == NDW_K_TALL ||
		           o->kind == NDW_K_FIR || o->kind == NDW_K_CACTUS) {
			sp_idx = (o->kind == NDW_K_FIR) ? 1u :
			         ((o->kind == NDW_K_TALL) ? 2u :
			         ((o->kind == NDW_K_CACTUS) ? 6u : 10u));
			nd_m3d_plant_metric_dims(sp_idx, is_big, &w, &h, &ay);
			is_plant = 1;
		} else if (o->kind == NDW_K_STONE || o->kind == NDW_K_ROCK || o->kind == NDW_K_BOULDER) {
			nd_m3d_stone_metric_dims(o->kind, o->variant, &w, &h, &ay);
		}
		if (is_plant) {
			/* Deterministic per-instance allometric stature and crown spread variation */
			uint32_t ph = ((uint32_t)o->x * 0x9E3779B1u) ^ ((uint32_t)o->y * 0x85EBCA77u) ^
			              ((uint32_t)o->plant_id * 0xC2B2AE3Du) ^ ((uint32_t)o->variant * 0x27D4EB2Fu);
			ph ^= ph >> 16;
			ph *= 0x7FEB352Du;
			ph ^= ph >> 15;
			float u01 = (float)(ph & 0xFFFFu) * (1.0f / 65535.0f);
			float v01 = (float)((ph >> 16) & 0xFFFFu) * (1.0f / 65535.0f);
			if (sp_idx < ND_PLANT_MAX && s_nd_plant_specs[sp_idx].big_col > 0u && sp_idx != 6u) {
				/* Continuous tree age/size spectrum across seedlings, saplings, pole trees, and veterans
				 * with seamless overlap between small (0.25..0.62 * big_h) and big (0.52..1.18 * big_h). */
				float big_w = (float)s_nd_plant_specs[sp_idx].genes.big_w;
				float big_h = (float)s_nd_plant_specs[sp_idx].genes.big_h;
				float age_t = is_big
					? (0.52f + 0.66f * (u01 * 0.80f + v01 * 0.20f))
					: (0.25f + 0.37f * (u01 * 0.75f + v01 * 0.25f));
				float crown_aspect = is_big
					? ((0.86f + 0.24f * age_t) * (0.92f + 0.16f * v01))
					: (0.78f + 0.24f * v01);
				w = big_w * age_t * crown_aspect;
				h = big_h * age_t;
				ay = fmaxf(1.5f, fminf(10.0f, h * 0.06f));
			} else {
				float s_w = 0.85f + 0.30f * u01;
				float s_h = 0.85f + 0.30f * v01;
				w *= s_w;
				h *= s_h;
			}
		}
	}
	if (out_w)  *out_w  = w;
	if (out_h)  *out_h  = h;
	if (out_ay) *out_ay = ay;
}

static void seven_calc_obj_billboard_dims(const ndw_obj_t *o,
                                          const nd_seven_sprite_rect_t *sr,
                                          float *out_w, float *out_h, float *out_ay)
{
	float sr_w = sr ? (float)sr->w : 32.0f;
	float sr_h = sr ? (float)sr->h : 32.0f;
	nd_seven_calc_obj_dims(o, sr_w, sr_h, out_w, out_h, out_ay);
}

static inline void seven_sample_node_smooth(const ar_node_t *node, int32_t wx, int32_t wy,
                                            int16_t default_wl, int32_t *out_elev_q, int16_t *out_wl)
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
	*out_elev_q = (int32_t)(h >= 0.0f ? (h + 0.5f) : (h - 0.5f));

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

static inline void seven_sample_node_slope_at(const ar_node_t *node, int32_t wx, int32_t wy,
                                              int16_t base_wl, int32_t elev_q,
                                              const ar_node_t *const *adj_leaves, size_t adj_cnt,
                                              int32_t *out_eq_x, int32_t *out_eq_y)
{
	int16_t dummy_wl = base_wl;
	int32_t eq_x = elev_q;
	int32_t eq_y = elev_q;

	if (wx + 8 < node->ox + node->w) {
		seven_sample_node_smooth(node, wx + 8, wy, base_wl, &eq_x, &dummy_wl);
	} else {
		const ar_node_t *nx_node = NULL;
		for (size_t i = 0; i < adj_cnt; i++) {
			const ar_node_t *cand = adj_leaves[i];
			if (cand && cand->values && cand->level <= 1 &&
			    wx + 8 >= cand->ox && wx + 8 < cand->ox + cand->w &&
			    wy >= cand->oy && wy < cand->oy + cand->h) {
				if (!nx_node || cand->level < nx_node->level)
					nx_node = cand;
			}
		}
		if (nx_node) {
			seven_sample_node_smooth(nx_node, wx + 8, wy, base_wl, &eq_x, &dummy_wl);
		} else {
			int32_t bx = (wx - 8 >= node->ox) ? (wx - 8) : node->ox;
			int32_t eq_bx = elev_q;
			seven_sample_node_smooth(node, bx, wy, base_wl, &eq_bx, &dummy_wl);
			int32_t ext_x = 2 * elev_q - eq_bx;
			if (ext_x < -(32000 << 4)) ext_x = -(32000 << 4);
			if (ext_x >  (32000 << 4)) ext_x =  (32000 << 4);
			eq_x = ext_x;
		}
	}

	if (wy + 8 < node->oy + node->h) {
		seven_sample_node_smooth(node, wx, wy + 8, base_wl, &eq_y, &dummy_wl);
	} else {
		const ar_node_t *ny_node = NULL;
		for (size_t i = 0; i < adj_cnt; i++) {
			const ar_node_t *cand = adj_leaves[i];
			if (cand && cand->values && cand->level <= 1 &&
			    wx >= cand->ox && wx < cand->ox + cand->w &&
			    wy + 8 >= cand->oy && wy + 8 < cand->oy + cand->h) {
				if (!ny_node || cand->level < ny_node->level)
					ny_node = cand;
			}
		}
		if (ny_node) {
			seven_sample_node_smooth(ny_node, wx, wy + 8, base_wl, &eq_y, &dummy_wl);
		} else {
			int32_t by = (wy - 8 >= node->oy) ? (wy - 8) : node->oy;
			int32_t eq_by = elev_q;
			seven_sample_node_smooth(node, wx, by, base_wl, &eq_by, &dummy_wl);
			int32_t ext_y = 2 * elev_q - eq_by;
			if (ext_y < -(32000 << 4)) ext_y = -(32000 << 4);
			if (ext_y >  (32000 << 4)) ext_y =  (32000 << 4);
			eq_y = ext_y;
		}
	}

	*out_eq_x = eq_x;
	*out_eq_y = eq_y;
}

static inline void seven_sample_node_elev_slope(const ar_node_t *node, int32_t wx, int32_t wy,
                                                int16_t base_wl,
                                                const ar_node_t *const *adj_leaves, size_t adj_cnt,
                                                int32_t *out_elev_q, int32_t *out_eq_x,
                                                int32_t *out_eq_y, int16_t *out_local_wl)
{
	seven_sample_node_smooth(node, wx, wy, base_wl, out_elev_q, out_local_wl);
	int pos_y_grid = -ndw_fdiv(wy, NDW_TILE_H);
	if (ndw_fdiv(wx, NDW_TILE_W) == -3 && pos_y_grid == 0 && *out_local_wl < base_wl)
		*out_local_wl = base_wl;
	seven_sample_node_slope_at(node, wx, wy, base_wl, *out_elev_q, adj_leaves, adj_cnt, out_eq_x, out_eq_y);
}

#define BLOCK_BB_CACHE_SLOTS 4096u
#define BLOCK_BB_ARENA_CAP   262144u
#define BLOCK_BB_MAX_PER_BLK 192u
#define BLOCK_BB_STAGING_SLOTS 64u

typedef struct {
	int32_t       bx0, by0;
	int32_t       node_ox, node_oy;
	uint32_t      node_rev;
	uint32_t      atlas_ver;
	uint32_t      forget_seq;
	uint64_t      arena_seq;
	uint32_t      arena_off;
	float         max_dim;
	float         fade_start_t;
	uint16_t      count;
	int16_t       world, rz, w_level;
	uint8_t       node_level, valid;
	uint8_t       bw, bh;
	uint8_t       staging_idx;
	stl_wq_task_t async_task;
	const ar_node_t *async_node;
} block_bb_cache_slot_t;

static block_bb_cache_slot_t s_block_bb_cache[BLOCK_BB_CACHE_SLOTS];
static lam_billboard_gpu_t   s_block_bb_arena[BLOCK_BB_ARENA_CAP];
static lam_billboard_gpu_t   s_block_bb_staging[BLOCK_BB_STAGING_SLOTS][BLOCK_BB_MAX_PER_BLK];
static uint16_t              s_block_bb_staging_cnt[BLOCK_BB_STAGING_SLOTS];
static float                 s_block_bb_staging_max_dim[BLOCK_BB_STAGING_SLOTS];
static uint32_t              s_block_bb_staging_cursor = 0;
static uint64_t              s_block_bb_arena_cursor = 1;

static void seven_async_block_bb_worker(void *ctx)
{
	block_bb_cache_slot_t *slot = (block_bb_cache_slot_t *)ctx;
	const ar_node_t *node = slot ? slot->async_node : NULL;
	if (!slot || !node || !node->values || slot->staging_idx >= BLOCK_BB_STAGING_SLOTS)
		return;

	lam_billboard_gpu_t *dst = s_block_bb_staging[slot->staging_idx];
	uint16_t cnt = 0;
	float max_dim = 0.0f;
	int32_t bx0 = slot->bx0;
	int32_t by0 = slot->by0;
	int32_t bw  = (slot->bw > 0 && slot->bw <= 8) ? (int32_t)slot->bw : 8;
	int32_t bh  = (slot->bh > 0 && slot->bh <= 8) ? (int32_t)slot->bh : 8;
	int16_t world = slot->world;
	int16_t rz = slot->rz;
	int16_t w_level = slot->w_level;

	for (int32_t ry_grid = by0; ry_grid < by0 + bh; ry_grid++) {
		for (int32_t rx_grid = bx0; rx_grid < bx0 + bw; rx_grid++) {
			int16_t pos[4] = {
				(int16_t)rx_grid,
				(int16_t)(-ry_grid),
				rz,
				world
			};
			ndw_tile_t tile;
			ndw_synth_virgin_tile(pos, &tile);
			if (!tile.glyph[0] && !tile.glyph[1] && !tile.glyph[2])
				continue;
			ndw_obj_t raw_objs[NDW_MAX_OBJ];
			int n_raw = ndw_tile_objects(&tile, pos, 0, raw_objs);
			if (n_raw <= 0)
				continue;

			int32_t base_wx = (int32_t)pos[0] * NDW_TILE_W;
			int32_t base_wy = -(int32_t)pos[1] * NDW_TILE_H;
			for (int k = 0; k < n_raw; k++) {
				if (cnt >= BLOCK_BB_MAX_PER_BLK)
					break;
				nd_seven_sprite_rect_t sr_val;
				if (!nd_seven_obj_rect(&raw_objs[k], &sr_val))
					continue;
				int32_t wx = base_wx + raw_objs[k].x;
				int32_t wy = base_wy + raw_objs[k].y;
				int32_t elev_q = 0;
				int16_t local_wl = w_level;
				seven_sample_node_smooth(node, wx, wy, w_level, &elev_q, &local_wl);
				int16_t wz = nd_field_elevation_to_block(elev_q);
				if (wz <= local_wl && node->level >= 2 &&
				    raw_objs[k].kind >= NDW_K_DECID && raw_objs[k].kind <= NDW_K_CACTUS) {
					int16_t canon_elev = nd_field_sample((uint16_t)world, 0, wx, wy).elevation;
					if (canon_elev > local_wl)
						wz = canon_elev;
				}
				if (wz <= local_wl &&
				    raw_objs[k].kind >= NDW_K_DECID && raw_objs[k].kind <= NDW_K_CACTUS)
					continue;
				float z_f = (float)elev_q / 16.0f;
				int flip = (raw_objs[k].variant >> 2) & 1;
				lam_billboard_gpu_t bb;
				bb.x = (float)wx;
				bb.y = (float)wy;
				bb.z = z_f;
				seven_calc_obj_billboard_dims(&raw_objs[k], &sr_val, &bb.width, &bb.height, &bb.anchor_y);
				if (raw_objs[k].kind == NDW_K_DECID || raw_objs[k].kind == NDW_K_TALL || raw_objs[k].kind == NDW_K_FIR) {
					int32_t eq_x = elev_q, eq_y = elev_q;
					seven_sample_node_slope_at(node, wx, wy, w_level, elev_q, NULL, 0, &eq_x, &eq_y);
					float dz_dx = (float)(eq_x - elev_q) * (1.0f / 128.0f);
					float dz_dy = (float)(eq_y - elev_q) * (1.0f / 128.0f);
					float slope_m = sqrtf(dz_dx * dz_dx + dz_dy * dz_dy);
					float krumm = 1.0f;
					float grove = nd_seven_eval_grove_ecology(wx, wy, z_f, (float)local_wl, slope_m, tile.biome, &krumm);
					float eco_sc = krumm * (0.86f + 0.26f * grove);
					bb.width    *= eco_sc;
					bb.height   *= eco_sc;
					bb.anchor_y *= eco_sc;
				}
				float dim = (bb.width > bb.height) ? bb.width : bb.height;
				if (dim > max_dim)
					max_dim = dim;
				bb.water_z_off = bb.anchor_y;
				if (flip) {
					bb.u0 = (float)(sr_val.x + sr_val.w) / (float)ND_SEVEN_ATLAS_W;
					bb.u1 = (float)sr_val.x / (float)ND_SEVEN_ATLAS_W;
				} else {
					bb.u0 = (float)sr_val.x / (float)ND_SEVEN_ATLAS_W;
					bb.u1 = (float)(sr_val.x + sr_val.w) / (float)ND_SEVEN_ATLAS_W;
				}
				bb.v0 = (float)sr_val.y / (float)ND_SEVEN_RUNTIME_ATLAS_H;
				bb.v1 = (float)(sr_val.y + sr_val.h) / (float)ND_SEVEN_RUNTIME_ATLAS_H;
				seven_apply_obj_billboard_style(&raw_objs[k], &sr_val, 0xFFFFFFFFu, &bb);
				if ((bb.style == ND_BB_STYLE_STANDARD || bb.style == ND_BB_STYLE_MINERAL) &&
				    z_f < (float)local_wl - 0.05f) {
					bb.pad0 |= 0x80u;
				}
				dst[cnt++] = bb;
			}
		}
	}
	nd_seven_sort_billboards_by_size(dst, cnt);
	s_block_bb_staging_cnt[slot->staging_idx] = cnt;
	s_block_bb_staging_max_dim[slot->staging_idx] = max_dim;
}

static inline float seven_block_fade_alpha(float fade_start_t)
{
	if (s_frame_anim_time <= 0.0f || fade_start_t < 0.0f)
		return 1.0f;
	float age = s_frame_anim_time - fade_start_t;
	if (age < 0.0f || age >= 0.18f)
		return 1.0f;
	float t = age / 0.18f;
	return t * t * (3.0f - 2.0f * t);
}

static inline int seven_stream_faded_billboard(const lam_billboard_gpu_t *src_bb, float fade_scale)
{
	float alpha = seven_eval_billboard_alpha(src_bb->x, src_bb->y, src_bb->z,
	                                         src_bb->width, src_bb->height) * fade_scale;
	uint32_t alpha_pack = seven_pack_white_alpha(alpha);
	if (alpha_pack == 0)
		return 0;
	if (s_mid_batch_len == MID_STREAM_CHUNK) {
		nd_seven_gl_queue_billboards(s_mid_stream_batch, s_mid_batch_len, atlas_rgba());
		s_mid_batch_len = 0;
	}
	lam_billboard_gpu_t bb = *src_bb;
	bb.tint_rgba = (src_bb->tint_rgba & 0xFFFFFF00u) | (alpha_pack & 0xFFu);
	s_mid_stream_batch[s_mid_batch_len++] = bb;
#ifndef __wasm__
	if (s_last_mid_gpu_count < MID_MAX_DRAW)
		s_mid_gpu_batch[s_last_mid_gpu_count] = bb;
#endif
	s_last_mid_gpu_count++;
	return 1;
}

static int seven_emit_room_billboards_ex(const nd_client_state_t *st, const ar_node_t *node,
                                         const int16_t pos[4], int16_t w_level,
                                         uint32_t tint, bool pure_virgin,
                                         lam_billboard_gpu_t *arena_dst, uint16_t *inout_arena_cnt,
                                         float *inout_max_dim)
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

		lam_billboard_gpu_t bb;
		bb.x = (float)wx;
		bb.y = (float)wy;
		seven_calc_obj_billboard_dims(&raw_objs[k], sr, &bb.width, &bb.height, &bb.anchor_y);
		if (!arena_dst) {
			float cull_d = seven_billboard_cull_dist(bb.width, bb.height);
			float dx_c = bb.x - s_cam_focal_x;
			float dy_c = bb.y - s_cam_focal_y;
			if (cull_d <= 1.0f ||
			    s_cam_dist * s_cam_dist + 0.12f * (dx_c * dx_c + dy_c * dy_c) >= cull_d * cull_d)
				continue;
		}

		int32_t elev_q = 0;
		int16_t local_wl = w_level;
		seven_sample_node_smooth(node, wx, wy, w_level, &elev_q, &local_wl);
		int16_t wz = nd_field_elevation_to_block(elev_q);

		if (wz <= local_wl && node->level >= 2 &&
		    raw_objs[k].kind >= NDW_K_DECID && raw_objs[k].kind <= NDW_K_CACTUS) {
			int16_t canon_elev = nd_field_sample((uint16_t)pos[3], 0, wx, wy).elevation;
			if (canon_elev > local_wl)
				wz = canon_elev;
		}

		if (wz <= local_wl &&
		    raw_objs[k].kind >= NDW_K_DECID && raw_objs[k].kind <= NDW_K_CACTUS)
			continue; /* Anti-submersion for flora */

		float z_f = (float)elev_q / 16.0f;
		bb.z = z_f;
		if (raw_objs[k].kind == NDW_K_DECID || raw_objs[k].kind == NDW_K_TALL || raw_objs[k].kind == NDW_K_FIR) {
			int32_t eq_x = elev_q, eq_y = elev_q;
			if (node) {
				seven_sample_node_slope_at(node, wx, wy, w_level, elev_q, NULL, 0, &eq_x, &eq_y);
			} else {
				eq_x = nd_seven_ground_elevation_q12_4(wx + 8, wy);
				eq_y = nd_seven_ground_elevation_q12_4(wx, wy + 8);
			}
			float dz_dx = (float)(eq_x - elev_q) * (1.0f / 128.0f);
			float dz_dy = (float)(eq_y - elev_q) * (1.0f / 128.0f);
			float slope_m = sqrtf(dz_dx * dz_dx + dz_dy * dz_dy);
			unsigned e_bio = 10u;
			if (st) {
				int dti = (int)pos[0] - (int)st->map_data.pos[0];
				int dtj = (int)st->map_data.pos[1] - (int)pos[1];
				ndw_tile_t e_mt;
				int16_t e_mpos[4];
				if (nd_seven_tile_at(st, dti, dtj, &e_mt, e_mpos))
					e_bio = e_mt.biome;
			}
			float krumm = 1.0f;
			float grove = nd_seven_eval_grove_ecology(wx, wy, z_f, (float)local_wl, slope_m, e_bio, &krumm);
			float eco_sc = krumm * (0.86f + 0.26f * grove);
			bb.width    *= eco_sc;
			bb.height   *= eco_sc;
			bb.anchor_y *= eco_sc;
		}
		if (!arena_dst) {
			float test_alpha = seven_eval_billboard_alpha(bb.x, bb.y, bb.z, bb.width, bb.height);
			if (test_alpha <= (0.5f / 255.0f))
				continue;
		}
		int flip = (raw_objs[k].variant >> 2) & 1;
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
		seven_apply_obj_billboard_style(&raw_objs[k], sr, 0xFFFFFFFFu, &bb);
		if ((bb.style == ND_BB_STYLE_STANDARD || bb.style == ND_BB_STYLE_MINERAL) &&
		    z_f < (float)local_wl - 0.05f) {
			bb.pad0 |= 0x80u;
		}

		if (arena_dst && inout_arena_cnt) {
			if (*inout_arena_cnt < BLOCK_BB_MAX_PER_BLK) {
				arena_dst[(*inout_arena_cnt)++] = bb;
				if (inout_max_dim) {
					float dim = (bb.width > bb.height) ? bb.width : bb.height;
					if (dim > *inout_max_dim)
						*inout_max_dim = dim;
				}
			}
		} else {
			emitted += seven_stream_faded_billboard(&bb, 1.0f);
		}
	}
	return emitted;
}

static inline int seven_emit_room_billboards(const nd_client_state_t *st, const ar_node_t *node,
                                             const int16_t pos[4], int16_t w_level,
                                             uint32_t tint)
{
	return seven_emit_room_billboards_ex(st, node, pos, w_level, tint, false, NULL, NULL, NULL);
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

float nd_seven_eval_grove_ecology(int32_t wx, int32_t wy,
                                  float elev_z, float water_level, float slope_mag,
                                  unsigned biome, float *out_krummholz_scale)
{
	float wx_f = (float)wx;
	float wy_f = (float)wy;
	float warp_x = (meadow_noise_2d(wx_f, wy_f, 220.0f, 0x47525750u) - 0.5f) * 96.0f;
	float warp_y = (meadow_noise_2d(wx_f, wy_f, 220.0f, 0x47525751u) - 0.5f) * 96.0f;
	float n_grove = meadow_noise_2d(wx_f + warp_x, wy_f + warp_y, 520.0f, 0x47524F56u);
	float n_copse = meadow_noise_2d(wx_f - warp_y * 0.5f, wy_f + warp_x * 0.5f, 190.0f, 0x434F5053u);
	float g = 0.65f * n_grove + 0.35f * n_copse;

	bool forest_bio = (biome == 8u || biome == 9u || biome == 11u ||
	                   biome == 12u || biome == 13u || biome == 16u || biome == 17u);
	bool open_bio   = (biome == 2u || biome == 3u || biome == 4u ||
	                   biome == 7u || biome == 10u || biome == 15u);
	if (forest_bio)
		g += 0.14f;
	else if (open_bio)
		g -= 0.10f;
	if (slope_mag > 0.32f)
		g -= (slope_mag - 0.32f) * 0.45f;
	if (g < 0.0f) g = 0.0f;
	if (g > 1.0f) g = 1.0f;

	if (out_krummholz_scale) {
		float alt_above = elev_z - water_level;
		float alt_sc = 1.0f;
		if (alt_above > 120.0f) {
			float t = (alt_above - 120.0f) * (1.0f / 160.0f);
			if (t > 1.0f) t = 1.0f;
			alt_sc = 1.0f - 0.56f * t;
		}
		float slp_sc = 1.0f;
		if (slope_mag > 0.28f) {
			float t = (slope_mag - 0.28f) * (1.0f / 0.50f);
			if (t > 1.0f) t = 1.0f;
			slp_sc = 1.0f - 0.48f * t;
		}
		float k = alt_sc * slp_sc;
		if (k < 0.35f) k = 0.35f;
		if (k > 1.0f)  k = 1.0f;
		*out_krummholz_scale = k;
	}
	return g;
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
	(void)zone;
	uint32_t rgb = 0xFAFFF400u;
	uint32_t idx = h & 3u;
	if (biome == 2u) {
		/* Tundra: frosted sage, lichen & russet sedge */
		static const uint32_t tundra_tints[4] = {
			0xB8BEAA00u, 0xC4B89E00u, 0xAEB6A400u, 0xBAB49C00u
		};
		rgb = tundra_tints[idx];
	} else if (biome == 3u) {
		/* Alpine meadow: rich subalpine olive-emerald turf */
		static const uint32_t alpine_tints[4] = {
			0x8EB87800u, 0x84AE7000u, 0x98C08000u, 0x7CA66A00u
		};
		rgb = alpine_tints[idx];
	} else if (biome == 4u) {
		/* Mediterranean garrigue: warm olive-sage & sun-cured thyme */
		static const uint32_t med_tints[4] = {
			0xB89E6A00u, 0xAE966400u, 0xC2A67000u, 0xA48E5E00u
		};
		rgb = med_tints[idx];
	} else if (biome == 5u) {
		/* Wetland / fen: deep lush marsh sedge & olive-moss */
		static const uint32_t wetland_tints[4] = {
			0x628E5C00u, 0x5A845600u, 0x6A986400u, 0x527A4E00u
		};
		rgb = wetland_tints[idx];
	} else if (biome == 6u || biome == 14u) {
		/* Cold desert (6) & Desert (14): parched straw, weathered sage & dry bunchgrass */
		static const uint32_t desert_tints[4] = {
			0xC8AC7400u, 0xBEA46E00u, 0xD0B47C00u, 0xB49C6800u
		};
		rgb = desert_tints[idx];
	} else if (biome == 7u) {
		/* Cool shrubland: dusty sagebrush-grey, cool olive-ochre & dry steppe bunchgrass */
		static const uint32_t shrub_tints[4] = {
			0xA6A07A00u, 0xB0A27600u, 0x9C987400u, 0xAA9A7000u
		};
		rgb = shrub_tints[idx];
	} else if (biome == 15u) {
		/* Savannah: sun-cured golden-ochre & warm straw */
		static const uint32_t savanna_tints[4] = {
			0xD4A25800u, 0xCA985200u, 0xDCAA6000u, 0xC08E4A00u
		};
		rgb = savanna_tints[idx];
	} else if (biome == 8u || biome == 9u) {
		/* Coniferous & Boreal forest: deep spruce moss & warm olive-pine */
		static const uint32_t boreal_tints[4] = {
			0x607E4E00u, 0x58764800u, 0x6A885600u, 0x526E4400u
		};
		rgb = boreal_tints[idx];
	} else if (biome == 11u) {
		/* Open Woodland: rich sunlit clover & grounded olive-emerald */
		static const uint32_t woodland_tints[4] = {
			0x7E9C5400u, 0x86A05200u, 0x76944E00u, 0x8EA25000u
		};
		rgb = woodland_tints[idx];
	} else if (biome == 12u) {
		/* Temperate seasonal forest: warm olive-moss & russet leaf-litter accents */
		static const uint32_t temp_seasonal_tints[4] = {
			0x989A5C00u, 0xA4905600u, 0x8E945800u, 0xAA885000u
		};
		rgb = temp_seasonal_tints[idx];
	} else if (biome == 13u) {
		/* Temperate rainforest: deep misty Pacific-Northwest fern & moss-olive */
		static const uint32_t temp_rf_tints[4] = {
			0x64925400u, 0x5C884C00u, 0x6E9C5C00u, 0x547E4600u
		};
		rgb = temp_rf_tints[idx];
	} else if (biome == 16u) {
		/* Tropical seasonal / monsoon forest: grounded olive-emerald & warm dry-season ochre */
		static const uint32_t monsoon_tints[4] = {
			0x86944C00u, 0x948E4800u, 0x7A904A00u, 0x9C864400u
		};
		rgb = monsoon_tints[idx];
	} else if (biome == 17u) {
		/* Tropical rainforest: deep saturated equatorial jungle chlorophyll */
		static const uint32_t jungle_tints[4] = {
			0x60984800u, 0x588E4200u, 0x6AA25000u, 0x52843E00u
		};
		rgb = jungle_tints[idx];
	} else {
		/* Temperate grassland (10) & default: grounded sunlit meadow olive-green */
		static const uint32_t meadow_tints[4] = {
			0x88A45600u, 0x7E9A5000u, 0x92AC5E00u, 0x76904800u
		};
		rgb = meadow_tints[idx];
	}

	if (zone == ND_MEADOW_UNDERSTORY) {
		uint32_t r = (((rgb >> 24) & 0xFFu) * 218u) >> 8u;
		uint32_t g = (((rgb >> 16) & 0xFFu) * 228u) >> 8u;
		uint32_t b = (((rgb >>  8) & 0xFFu) * 214u) >> 8u;
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

static inline float seven_smoothstep(float e0, float e1, float x)
{
	float t = (x - e0) / (e1 - e0);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	return t * t * (3.0f - 2.0f * t);
}

float nd_seven_eval_trail_weight(float wx, float wy, float slope_mag)
{
	if (slope_mag >= 0.40f)
		return 0.0f;
	float u1 = wx * 0.0042f + wy * 0.0029f
	         + 0.85f * sinf(wy * 0.0061f - wx * 0.0033f)
	         + 0.40f * cosf(wx * 0.0117f + wy * 0.0089f);
	float u2 = wx * -0.0031f + wy * 0.0046f
	         + 0.80f * sinf(wx * 0.0057f + wy * 0.0037f)
	         + 0.38f * cosf(wy * 0.0109f - wx * 0.0083f);
	float d1 = fabsf(sinf(u1));
	float d2 = fabsf(sinf(u2));
	float g1 = seven_smoothstep(0.15f, 0.55f, sinf(wx * 0.0011f - wy * 0.0014f + 0.6f));
	float g2 = seven_smoothstep(0.20f, 0.60f, cosf(wx * 0.0013f + wy * 0.0009f - 0.4f));
	float t1 = (1.0f - seven_smoothstep(0.038f, 0.095f, d1)) * g1;
	float t2 = (1.0f - seven_smoothstep(0.035f, 0.088f, d2)) * g2;
	float t = (t1 > t2) ? t1 : t2;
	if (t > 1.0f) t = 1.0f;
	return t * (1.0f - seven_smoothstep(0.20f, 0.40f, slope_mag));
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

	/* Synchronized winding footpath/trail breakup: clear beaten dirt track core, lush edge shoulders */
	float trail_w = (elev_above_water > 1.2f) ? nd_seven_eval_trail_weight(wx_f, wy_f, slope_mag) : 0.0f;
	if (trail_w > 0.42f)
		return 0;
	int on_trail_shoulder = (trail_w > 0.08f);

	/* 5. Tree canopy drip-line understory (81 <= d^2 < 576, i.e. 9..24 units from trunk) */
	float grove_val = nd_seven_eval_grove_ecology(wx, wy, elev_z, water_level, slope_mag, biome, NULL);
	if (near_tree_dist_sq >= 81.0f && near_tree_dist_sq < 576.0f) {
		if (is_arid && elev_above_water > 1.85f)
			return 0;
		float u_macro = meadow_noise_2d(wx_f, wy_f, 48.0f, 0x554E4452u);
		float u_fine  = meadow_noise_2d(wx_f, wy_f, 20.0f, 0x554E4432u);
		float u_env   = 0.62f * u_macro + 0.38f * u_fine + (grove_val - 0.45f) * 0.14f - slope_pen;
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
	float n_clump = meadow_noise_2d(wx_f, wy_f, 42.0f, 0x434C4D50u);
	float noise_env = 0.50f * n_macro + 0.30f * n_meso + 0.12f * n_fine + 0.08f * n_clump;

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
		/* Tundra (2): sparse rocky/mossy hardy sedge */
		bio_mult = 0.54f;
		base_sc  = 0.28f;
		span_sc  = 0.14f;
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
		bio_mult = 1.12f;
		base_sc  = 0.43f;
		span_sc  = 0.22f;
	} else if (biome == 7u) {
		/* Cool shrubland (7): compact, wind-hardy sagebrush & steppe bunchgrass */
		bio_mult = 0.68f;
		base_sc  = 0.32f;
		span_sc  = 0.16f;
	} else if (biome == 15u) {
		/* Savannah (15): sun-baked tall golden C4 grass stands */
		bio_mult = 0.94f;
		base_sc  = 0.42f;
		span_sc  = 0.24f;
	} else if (biome == 8u || biome == 9u) {
		/* Coniferous (8) & Boreal (9) forests: mossy forest-floor turf & ferns */
		bio_mult = 0.72f;
		base_sc  = 0.34f;
		span_sc  = 0.14f;
	} else if (biome == 13u) {
		/* Temperate rainforest (13): lush fern & mossy old-growth floor */
		bio_mult = 1.10f;
		base_sc  = 0.40f;
		span_sc  = 0.18f;
	} else if (biome == 16u) {
		/* Tropical seasonal / monsoon forest (16): sunlit tangled bamboo & tall monsoon grass */
		bio_mult = 1.12f;
		base_sc  = 0.44f;
		span_sc  = 0.24f;
	} else if (biome == 17u) {
		/* Tropical rainforest (17): dense equatorial jungle groundcover */
		bio_mult = 1.16f;
		base_sc  = 0.44f;
		span_sc  = 0.22f;
	}

	int is_woodland = (biome == 8u || biome == 9u || biome == 11u || biome == 12u ||
	                   biome == 13u || biome == 16u || biome == 17u);
	float duff_glade = (is_woodland && !is_arid && grove_val > 0.68f)
	                 ? (grove_val - 0.68f) * 0.48f
	                 : 0.0f;

	float fertility = bio_mult * (noise_env + moisture_bonus - ridge_pen - slope_pen - duff_glade);
	if (on_trail_shoulder && !is_arid && g.kind != NDW_G_TUNDRA)
		fertility += 0.04f;

	/* Lowered bare-ground cutoff (0.18) builds a lush interlocking groundcover carpet in fertile biomes */
	if (is_arid) {
		/* Arid clumping: suppress grass tufts on open desert sand dunes; allow only near oasis water */
		if (elev_above_water > 1.35f || fertility <= 0.40f || ((h >> 12u) & 7u) > 0u)
			return 0;
	} else if (fertility <= 0.18f) {
		return 0;
	}

	float thresh = is_arid ? 0.40f : 0.18f;
	float core_t = (fertility - thresh) * (1.0f / 0.52f);
	if (core_t < 0.0f)
		core_t = 0.0f;
	if (core_t > 1.0f)
		core_t = 1.0f;

	/* Natural outer meadow margin breakups so patches fray organically while keeping dense cores full */
	if (!is_arid && core_t < 0.14f && ((h >> 14u) & 3u) == 0u)
		return 0;

	float sc = base_sc + span_sc * core_t + ((float)(int32_t)(h & 7u) - 3.5f) * 0.008f;
	if (on_trail_shoulder && !is_arid && g.kind != NDW_G_TUNDRA) {
		float shoulder_t = (trail_w - 0.08f) * (1.0f / 0.34f);
		if (shoulder_t > 1.0f) shoulder_t = 1.0f;
		sc += 0.035f * shoulder_t;
		if (sc > 0.695f) sc = 0.695f;
	}
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

	float flower_env = meadow_noise_2d(wx_f + 113.0f, wy_f - 79.0f, 52.0f, 0x424C4F4Du) +
	                   (0.50f - grove_val) * 0.14f;
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

typedef struct {
	const nd_client_state_t *st;
	float eff_cam_dist;
	float max_eff_sz;
	float max_scenery_cull;
	float cam_cy;
	float cam_sy;
	int obj_i0, obj_i1;
	int obj_j0, obj_j1;
	int eff_lamb_row;
	drawable_3d_t *dr;
	lam_billboard_gpu_t *batch;
	uint32_t batch_count;
} seven_frame_ctx_t;

static void
seven_emit_near_room_billboards(seven_frame_ctx_t *ctx)
{
	const nd_client_state_t *st = ctx->st;
	const float eff_cam_dist = ctx->eff_cam_dist;
	const float max_scenery_cull = ctx->max_scenery_cull;
	const int obj_i0 = ctx->obj_i0, obj_i1 = ctx->obj_i1;
	const int obj_j0 = ctx->obj_j0, obj_j1 = ctx->obj_j1;
	const int eff_lamb_row = ctx->eff_lamb_row;
	drawable_3d_t *dr = ctx->dr;
	lam_billboard_gpu_t *s_all_gpu_batch = ctx->batch;
	uint32_t all_gpu_count = ctx->batch_count;

	for (int tj = obj_j0; tj <= obj_j1; tj++) {
		int n = 0;

		if (s_cam_dist < max_scenery_cull) {
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
						int32_t elev_q = nd_seven_ground_elevation_q12_4(dr[n].wx, dr[n].wy);
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
						int32_t elev_q = nd_seven_ground_elevation_q12_4(dr[n].wx, dr[n].wy);
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
					if (dr[a].obj.kind == NDW_K_DECID || dr[a].obj.kind == NDW_K_TALL || dr[a].obj.kind == NDW_K_FIR) {
						int32_t eq0 = nd_seven_ground_elevation_q12_4(dr[a].wx, dr[a].wy);
						int32_t eqx = nd_seven_ground_elevation_q12_4(dr[a].wx + 8, dr[a].wy);
						int32_t eqy = nd_seven_ground_elevation_q12_4(dr[a].wx, dr[a].wy + 8);
						float dz_dx = (float)(eqx - eq0) * (1.0f / 128.0f);
						float dz_dy = (float)(eqy - eq0) * (1.0f / 128.0f);
						float slope_m = sqrtf(dz_dx * dz_dx + dz_dy * dz_dy);
						uint16_t d_world = (st && st->map_data.width > 0) ? (uint16_t)st->map_data.pos[3] : 0;
						int16_t d_wl = nd_field_water_level(d_world);
						seven_has_water_at(d_world, dr[a].wx, dr[a].wy, &d_wl);
						int d_ti = ndw_fdiv(dr[a].wx, NDW_TILE_W) - (st ? (int)st->map_data.pos[0] : 0);
						int d_tj = (st ? (int)st->map_data.pos[1] : 0) - (-ndw_fdiv(dr[a].wy, NDW_TILE_H));
						unsigned d_bio = 10u;
						ndw_tile_t d_mt;
						int16_t d_mpos[4];
						if (nd_seven_tile_at(st, d_ti, d_tj, &d_mt, d_mpos))
							d_bio = d_mt.biome;
						float krumm = 1.0f;
						float grove = nd_seven_eval_grove_ecology(dr[a].wx, dr[a].wy, dr[a].z_f, (float)d_wl, slope_m, d_bio, &krumm);
						float eco_sc = krumm * (0.86f + 0.26f * grove);
						bw  *= eco_sc;
						bh  *= eco_sc;
						bay *= eco_sc;
					}
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
				float alt_fade_b = (dr[a].what == D_LAMB)
					? 1.0f
					: seven_eval_billboard_alpha((float)dr[a].wx, (float)dr[a].wy, dr[a].z_f, bw, bh);
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
					if (dst_bb->style == ND_BB_STYLE_STANDARD || dst_bb->style == ND_BB_STYLE_MINERAL) {
						uint16_t world_id = (st && st->map_data.width > 0) ? (uint16_t)st->map_data.pos[3] : 0;
						int16_t true_wl = 0;
						if (seven_has_water_at(world_id, dr[a].wx, dr[a].wy, &true_wl) &&
						    dr[a].z_f < (float)true_wl - 0.05f)
							dst_bb->pad0 |= 0x80u;
					}
				} else if (dr[a].what == D_LAMB) {
					bool lamb_glows = (nd_seven_is_universe_owner(st) != 0);
					dst_bb->tint_rgba = tint_b;
					dst_bb->style = lamb_glows ? ND_BB_STYLE_GLOW : ND_BB_STYLE_STANDARD;
					dst_bb->shadow_rx = lamb_glows ? 0 : 8;
					dst_bb->shadow_ry = lamb_glows ? 0 : 4;
					dst_bb->pad0 = s_prev_lamb_in_water ? 0x80u : 0;
					dst_bb->mesh_id = ND_MESH3D_LAMB_ID;
				} else {
					dst_bb->tint_rgba = tint_b;
					dst_bb->pad0 = 0;
					dst_bb->mesh_id = nd_seven_entity_mesh3d_id(entity_spr);
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
					if (dst_bb->style == ND_BB_STYLE_STANDARD || dst_bb->style == ND_BB_STYLE_MINERAL) {
						uint16_t world_id = (st && st->map_data.width > 0) ? (uint16_t)st->map_data.pos[3] : 0;
						int16_t true_wl = 0;
						if (seven_has_water_at(world_id, dr[a].wx, dr[a].wy, &true_wl) &&
						    dr[a].z_f < (float)true_wl - 0.05f)
							dst_bb->pad0 |= 0x80u;
					}
					dst_bb->shadow_rx = (uint8_t)(bw * 0.34f > 4.0f ? bw * 0.34f : 4.0f);
					dst_bb->shadow_ry = (uint8_t)(dst_bb->shadow_rx * 0.45f > 2.0f ? dst_bb->shadow_rx * 0.45f : 2.0f);
				}
				all_gpu_count++;

				/* Deterministic Trunk & Boulder Anchor Collars + 3D Tree Root Buttress Mounds:
				 * Ground tree trunks into flared mossy root mounds and tuck root-collar ferns,
				 * creeping juniper, alpine heather, hazel thickets, or mossy stumps around bases. */
				if (dr[a].what == D_OBJ && eff_cam_dist < 2100.0f && all_gpu_count + 4 < ALL_GPU_MAX_DRAW &&
				    (dr[a].obj.kind == NDW_K_DECID || dr[a].obj.kind == NDW_K_TALL ||
				     dr[a].obj.kind == NDW_K_FIR || dr[a].obj.kind == NDW_K_ROCK ||
				     dr[a].obj.kind == NDW_K_BOULDER)) {
					bool is_tree_anchor = (dr[a].obj.kind == NDW_K_DECID ||
					                       dr[a].obj.kind == NDW_K_TALL ||
					                       dr[a].obj.kind == NDW_K_FIR);
					uint32_t ch = meadow_hash32(dr[a].wx, dr[a].wy, 0x434F4C4Cu ^ (uint32_t)dr[a].obj.variant);
					int c_ti = ndw_fdiv(dr[a].wx, NDW_TILE_W) - (st ? (int)st->map_data.pos[0] : 0);
					int c_tj = (st ? (int)st->map_data.pos[1] : 0) - (-ndw_fdiv(dr[a].wy, NDW_TILE_H));
					unsigned c_bio = (dr[a].obj.col_mask < 19u) ? (unsigned)dr[a].obj.col_mask : 10u;
					ndw_tile_t c_mt;
					int16_t c_mpos[4];
					if (nd_seven_tile_at(st, c_ti, c_tj, &c_mt, c_mpos))
						c_bio = c_mt.biome;
					if (dst_bb->style == ND_BB_STYLE_FLORA)
						dst_bb->pad0 = nd_seven_gl_pack_pad0(dst_bb->pad0 & 3u, c_bio, (dst_bb->pad0 & 0x80u) != 0);
					int barren_bio = (c_bio == 0u || c_bio == 1u || c_bio == 6u || c_bio == 14u || c_bio == 18u);
					if (!barren_bio) {
						uint16_t c_world = (st && st->map_data.width > 0) ? (uint16_t)st->map_data.pos[3] : 0;
						int64_t clwx = 0, clwy = 0;
						nd_seven_world_pos(st, &clwx, &clwy);

						/* 3D Flared Mossy Root Buttress Mound around trunk base (100% of mature trees) */
						if (is_tree_anchor && all_gpu_count < ALL_GPU_MAX_DRAW) {
							float bang = ((float)((ch >> 5u) & 255u) / 255.0f) * 6.2831853f;
							float brad = 3.0f + ((float)((ch >> 13u) & 7u) / 7.0f) * 4.5f;
							int32_t bwx = dr[a].wx + (int32_t)(cosf(bang) * brad);
							int32_t bwy = dr[a].wy + (int32_t)(sinf(bang) * brad);
							float bldx = (float)(bwx - (int32_t)clwx);
							float bldy = (float)(bwy - (int32_t)clwy);
							if (bldx * bldx + bldy * bldy >= 225.0f) {
								float bz_f = nd_seven_ground_elevation_f(bwx, bwy);
								int16_t b_wl = nd_field_water_level(c_world);
								seven_has_water_at(c_world, bwx, bwy, &b_wl);
								uint8_t b_mid = (uint8_t)(ND_MESH3D_GEOBREAK_BASE + (ch & 1u));
								const nd_mesh3d_slice_t *b_sl = nd_seven_mesh3d_slice(b_mid);
								if (bz_f > (float)b_wl + 0.3f && b_sl && b_sl->mesh_h_wu > 0.1f) {
									float b_sc = 0.68f + 0.32f * ((float)((ch >> 17u) & 15u) / 15.0f);
									float bbw = b_sl->mesh_w_wu * b_sc;
									float bbh = b_sl->mesh_h_wu * b_sc * 0.85f;
									float ba = seven_eval_billboard_alpha((float)bwx, (float)bwy, bz_f, bbw, bbh);
									uint32_t ba_pack = seven_pack_white_alpha(ba);
									if (ba_pack != 0) {
										int bflip = (int)((ch >> 21u) & 1u);
										lam_billboard_gpu_t *bbb = &s_all_gpu_batch[all_gpu_count++];
										bbb->x = (float)bwx;
										bbb->y = (float)bwy;
										bbb->z = bz_f;
										bbb->width = bbw;
										bbb->height = bbh;
										bbb->anchor_y = 0.0f;
										bbb->water_z_off = 0.0f;
										bbb->u0 = bflip ? 1.0f : 0.0f;
										bbb->u1 = bflip ? 0.0f : 1.0f;
										bbb->v0 = 0.0f;
										bbb->v1 = 0.0f;
										bbb->tint_rgba = 0xE8EDE000u | (ba_pack & 0xFFu);
										bbb->style = ND_BB_STYLE_STANDARD;
										bbb->shadow_rx = (uint8_t)(bbw * 0.32f > 4.0f ? bbw * 0.32f : 4.0f);
										bbb->shadow_ry = (uint8_t)(bbb->shadow_rx * 0.48f > 2.0f ? bbb->shadow_rx * 0.48f : 2.0f);
										bbb->pad0 = nd_seven_gl_pack_pad0(0u, c_bio, false);
										bbb->mesh_id = b_mid;
									}
								}
							}
						}

						/* 100% of mature trees and 72% of boulders receive clustered base flora (ferns, stones, shrubs) */
						if (is_tree_anchor || ((ch % 100u) < 72u)) {
							int n_collars = is_tree_anchor ? (((ch >> 3u) % 3u == 0u) ? 3 : 2) : 1;
							for (int ci = 0; ci < n_collars && all_gpu_count < ALL_GPU_MAX_DRAW; ci++) {
								uint32_t ch_i = ch ^ ((uint32_t)ci * 0x9E3779B1u);
								float ang = ((float)(ch_i & 255u) / 255.0f) * 6.2831853f;
								float rad = (is_tree_anchor ? 13.0f : 10.5f) +
								            ((float)((ch_i >> 8u) & 15u) / 15.0f) * (is_tree_anchor ? 9.0f : 6.5f);
								int32_t cwx = dr[a].wx + (int32_t)(cosf(ang) * rad);
								int32_t cwy = dr[a].wy + (int32_t)(sinf(ang) * rad);
								float ldx = (float)(cwx - (int32_t)clwx);
								float ldy = (float)(cwy - (int32_t)clwy);
								if (ldx * ldx + ldy * ldy < 256.0f)
									continue;
								float cz_f = nd_seven_ground_elevation_f(cwx, cwy);
								int16_t c_wl = nd_field_water_level(c_world);
								seven_has_water_at(c_world, cwx, cwy, &c_wl);
								if (cz_f <= (float)c_wl + 0.4f)
									continue;

								uint8_t c_mid = nd_seven_shrub_mesh3d_id(c_bio, ch_i >> 12u);
								if (is_tree_anchor && ((ch_i >> 16u) & 3u) == 0u &&
								    (c_bio == 8u || c_bio == 9u || c_bio == 11u ||
								     c_bio == 12u || c_bio == 13u || c_bio == 16u || c_bio == 17u)) {
									c_mid = (uint8_t)(ND_MESH3D_UNDERSTORY_BASE + 2u); /* Mossy stump/root collar */
								} else if (c_mid >= ND_MESH3D_SHRUB_BASE + 8u) {
									/* Never spawn a tall deadwood snag directly overlapping a live trunk/boulder */
									c_mid = (uint8_t)(ND_MESH3D_SHRUB_BASE + ((ch_i & 1u) ? 6u : 0u) + ((ch_i >> 1u) & 1u));
								}
								const nd_mesh3d_slice_t *c_sl = nd_seven_mesh3d_slice(c_mid);
								if (!c_sl)
									continue;
								float c_sc = (is_tree_anchor ? 0.68f : 0.56f) +
								             0.26f * ((float)((ch_i >> 20u) & 15u) / 15.0f);
								float cbw = c_sl->mesh_w_wu * c_sc;
								float cbh = c_sl->mesh_h_wu * c_sc;
								float ca = seven_eval_billboard_alpha((float)cwx, (float)cwy, cz_f, cbw, cbh);
								uint32_t ca_pack = seven_pack_white_alpha(ca);
								if (ca_pack == 0)
									continue;
								int cflip = (int)((ch_i >> 24u) & 1u);
								lam_billboard_gpu_t *cbb = &s_all_gpu_batch[all_gpu_count++];
								cbb->x = (float)cwx;
								cbb->y = (float)cwy;
								cbb->z = cz_f;
								cbb->width = cbw;
								cbb->height = cbh;
								cbb->anchor_y = 0.0f;
								cbb->water_z_off = 0.0f;
								cbb->u0 = cflip ? 1.0f : 0.0f;
								cbb->u1 = cflip ? 0.0f : 1.0f;
								cbb->v0 = 0.0f;
								cbb->v1 = 0.0f;
								cbb->tint_rgba = 0xF0F4E800u | (ca_pack & 0xFFu);
								cbb->style = ND_BB_STYLE_FLORA;
								cbb->shadow_rx = (uint8_t)(cbw * 0.30f > 4.0f ? cbw * 0.30f : 4.0f);
								cbb->shadow_ry = (uint8_t)(cbb->shadow_rx * 0.45f > 2.0f ? cbb->shadow_rx * 0.45f : 2.0f);
								cbb->pad0 = nd_seven_gl_pack_pad0(1u, c_bio, false);
								cbb->mesh_id = c_mid;
							}
						}
					}
				}
			}
		}
	}

	ctx->batch_count = all_gpu_count;
}

/* --- Phase 2: 5-pass 8x8 lattice-cell flora & relief block cache ----------
 * Caches camera-independent deterministic placement (elevation, slope, biome,
 * trail/grove/patch noise, sprite/mesh selection, base RGB tint) per 8x8
 * lattice block across Passes 1-5 (living meadow, forest-floor logs, medium
 * botanical, large undergrowth, geobreak relief). Replay applies fast frustum,
 * Lamb-radius, and distance-alpha fade checks with 0 steady-state ground probes. */
#define FLORA_BLK_DIM       8
#define FLORA_BLK_MAX_CELLS (FLORA_BLK_DIM * FLORA_BLK_DIM)
#define FLORA_BLK_SETS      256u
#define FLORA_BLK_WAYS      4u
#define FLORA_BLK_SLOTS     (FLORA_BLK_SETS * FLORA_BLK_WAYS)
#define FLORA_BLK_ARENA_CAP 32768u

typedef struct {
	lam_billboard_gpu_t bb;      /* tint_rgba stores RGB<<8 (alpha = 0) */
	float               cull_w;  /* width passed to seven_eval_billboard_alpha */
	uint8_t             cell_xy; /* (ly << 3) | lx within 8x8 block */
} flora_blk_entry_t;

typedef struct {
	int32_t  bx, by;
	int32_t  step;
	uint32_t last_leaf_sig;
	uint32_t map_bio_sig;
	uint32_t atlas_ver;
	uint32_t forget_seq;
	uint64_t filled_mask; /* bit (ly<<3)|lx = 1 once cell is deterministically resolved */
	uint64_t arena_seq;   /* start_seq in s_flora_blk_arena */
	uint32_t arena_off;
	uint32_t last_use;
	uint16_t world;
	int16_t  base_wl;
	uint8_t  pass_id;
	uint8_t  count;
	uint8_t  valid;
} flora_blk_slot_t;

static flora_blk_slot_t  s_flora_blk_cache[FLORA_BLK_SLOTS];
static flora_blk_entry_t s_flora_blk_arena[FLORA_BLK_ARENA_CAP];
static uint64_t          s_flora_blk_arena_cursor = 1;
static uint32_t          s_flora_blk_lru_tick = 1;

static inline float
flora_blk_min_dist2(float px, float py, float x0, float y0, float x1, float y1)
{
	float dx = (px < x0) ? (x0 - px) : ((px > x1) ? (px - x1) : 0.0f);
	float dy = (py < y0) ? (y0 - py) : ((py > y1) ? (py - y1) : 0.0f);
	return dx * dx + dy * dy;
}

static uint32_t
flora_map_bio_sig(const nd_client_state_t *st)
{
	if (!st || st->map_data.width == 0 || st->map_data.height == 0)
		return 0u;
	uint32_t h = (uint32_t)(uint16_t)st->map_data.pos[0] * 0x9E3779B1u
	           ^ (uint32_t)(uint16_t)st->map_data.pos[1] * 0x85EBCA77u
	           ^ ((uint32_t)st->map_data.width << 8u)
	           ^ (uint32_t)st->map_data.height;
	int n = (int)st->map_data.width * (int)st->map_data.height;
	if (n > ND_MAP_GRID_WIDTH * ND_MAP_GRID_HEIGHT)
		n = ND_MAP_GRID_WIDTH * ND_MAP_GRID_HEIGHT;
	for (int i = 0; i < n; i++)
		h = (h * 33u) ^ (uint32_t)st->map_data.tiles[i].bio_idx;
	return h ? h : 1u;
}

static uint32_t
flora_blk_collect_leaves(int32_t aabb_x0, int32_t aabb_y0,
                         int32_t aabb_x1, int32_t aabb_y1,
                         const ar_node_t **out_leaves, size_t max_leaves,
                         size_t *out_cnt)
{
	uint32_t xor_sig = 0u, sum_sig = 0u;
	size_t cnt = 0;
	for (size_t li = 0; li < s_visible_leaf_count; li++) {
		const ar_node_t *cand = s_visible_leaves[li];
		if (!cand || !cand->values)
			continue;
		if (cand->ox + cand->w <= aabb_x0 || cand->ox > aabb_x1 ||
		    cand->oy + cand->h <= aabb_y0 || cand->oy > aabb_y1)
			continue;
		uint32_t h = (uint32_t)cand->ox * 0x9E3779B1u
		           ^ (uint32_t)cand->oy * 0x85EBCA77u
		           ^ ((uint32_t)cand->level << 24u)
		           ^ (cand->rev * 0xC2B2AE3Du);
		h ^= h >> 15u;
		h *= 0x2C1B3C6Du;
		xor_sig ^= h;
		sum_sig += h;
		if (cnt < max_leaves)
			out_leaves[cnt] = cand;
		cnt++;
	}
	*out_cnt = cnt;
	return xor_sig ^ (sum_sig * 0x27D4EB2Fu) ^ ((uint32_t)cnt << 16u);
}

static inline const ar_node_t *
flora_blk_find_leaf(int32_t wx, int32_t wy,
                    const ar_node_t *cached,
                    const ar_node_t *const *blk_leaves, size_t blk_cnt,
                    size_t max_blk_leaves)
{
	if (cached && wx >= cached->ox && wx < cached->ox + cached->w &&
	    wy >= cached->oy && wy < cached->oy + cached->h)
		return cached;
	const ar_node_t *best = NULL;
	const ar_node_t *const *arr = (blk_cnt <= max_blk_leaves) ? blk_leaves : (const ar_node_t *const *)s_visible_leaves;
	size_t n = (blk_cnt <= max_blk_leaves) ? blk_cnt : s_visible_leaf_count;
	for (size_t li = 0; li < n; li++) {
		const ar_node_t *cand = arr[li];
		if (cand && cand->values &&
		    wx >= cand->ox && wx < cand->ox + cand->w &&
		    wy >= cand->oy && wy < cand->oy + cand->h) {
			if (!best || cand->level < best->level)
				best = cand;
		}
	}
	return best;
}

static inline unsigned
flora_lookup_biome(const nd_client_state_t *st, int32_t wx, int32_t wy,
                   int *cached_ti, int *cached_tj, unsigned *cached_bio)
{
	int ti = ndw_fdiv(wx, NDW_TILE_W) - (st ? (int)st->map_data.pos[0] : 0);
	int tj = (st ? (int)st->map_data.pos[1] : 0) - (-ndw_fdiv(wy, NDW_TILE_H));
	if (ti != *cached_ti || tj != *cached_tj) {
		*cached_ti = ti;
		*cached_tj = tj;
		*cached_bio = 10u;
		ndw_tile_t mt;
		int16_t mpos[4];
		if (nd_seven_tile_at(st, ti, tj, &mt, mpos))
			*cached_bio = mt.biome;
	}
	return *cached_bio;
}

static flora_blk_slot_t *
flora_blk_lookup(uint8_t pass_id, uint16_t world, int16_t base_wl,
                 int32_t step, int32_t bx, int32_t by,
                 uint32_t map_bio_sig, uint32_t atlas_ver, uint32_t forget_seq,
                 int *out_hit)
{
	uint32_t h = (uint32_t)bx * 0x9E3779B1u
	           ^ (uint32_t)by * 0x85EBCA77u
	           ^ ((uint32_t)pass_id * 0xC2B2AE3Du)
	           ^ ((uint32_t)step * 0x27D4EB2Fu);
	uint32_t set = (h ^ (h >> 16u)) & (FLORA_BLK_SETS - 1u);
	flora_blk_slot_t *base = &s_flora_blk_cache[set * FLORA_BLK_WAYS];
	flora_blk_slot_t *victim = &base[0];
	uint32_t oldest = victim->valid ? victim->last_use : 0u;

	for (uint32_t w = 0; w < FLORA_BLK_WAYS; w++) {
		flora_blk_slot_t *s = &base[w];
		if (s->valid &&
		    s->pass_id == pass_id &&
		    s->world == world &&
		    s->base_wl == base_wl &&
		    s->step == step &&
		    s->bx == bx &&
		    s->by == by &&
		    s->map_bio_sig == map_bio_sig &&
		    s->atlas_ver == atlas_ver &&
		    s->forget_seq == forget_seq) {
			if (s->count > 0u && (s_flora_blk_arena_cursor - s->arena_seq) > FLORA_BLK_ARENA_CAP) {
				s->filled_mask = 0ULL;
				s->count = 0u;
				s->last_leaf_sig = 0u;
				*out_hit = 0;
				return s;
			}
			s->last_use = ++s_flora_blk_lru_tick;
			*out_hit = 1;
			return s;
		}
		if (!s->valid) {
			victim = s;
			oldest = 0u;
		} else if (oldest != 0u && s->last_use < oldest) {
			victim = s;
			oldest = s->last_use;
		}
	}
	*out_hit = 0;
	return victim;
}

static void
flora_blk_commit(flora_blk_slot_t *slot,
                 uint8_t pass_id, uint16_t world, int16_t base_wl,
                 int32_t step, int32_t bx, int32_t by,
                 uint32_t last_leaf_sig, uint64_t filled_mask,
                 uint32_t map_bio_sig, uint32_t atlas_ver, uint32_t forget_seq,
                 const flora_blk_entry_t *tmp_entries, uint8_t cnt)
{
	uint32_t cur_off = 0u;
	uint64_t start_seq = s_flora_blk_arena_cursor;
	if (cnt > 0u) {
		cur_off = (uint32_t)(s_flora_blk_arena_cursor & (FLORA_BLK_ARENA_CAP - 1u));
		if (cur_off + (uint32_t)cnt > FLORA_BLK_ARENA_CAP) {
			s_flora_blk_arena_cursor += (uint64_t)(FLORA_BLK_ARENA_CAP - cur_off);
			cur_off = 0u;
		}
		start_seq = s_flora_blk_arena_cursor;
		memcpy(&s_flora_blk_arena[cur_off], tmp_entries, (size_t)cnt * sizeof(flora_blk_entry_t));
		s_flora_blk_arena_cursor += (uint64_t)cnt;
	}
	slot->bx = bx;
	slot->by = by;
	slot->step = step;
	slot->last_leaf_sig = last_leaf_sig;
	slot->filled_mask = filled_mask;
	slot->map_bio_sig = map_bio_sig;
	slot->atlas_ver = atlas_ver;
	slot->forget_seq = forget_seq;
	slot->arena_seq = start_seq;
	slot->arena_off = cur_off;
	slot->last_use = ++s_flora_blk_lru_tick;
	slot->world = world;
	slot->base_wl = base_wl;
	slot->pass_id = pass_id;
	slot->count = cnt;
	slot->valid = 1u;
}

typedef struct {
	int32_t  wx, wy;
	uint32_t h;
	unsigned bio;
	uint8_t  pre_mesh_id;
} flora_cell_prep_t;

typedef struct {
	uint8_t  pass_id;
	uint8_t  use_lamb_occl;
	int32_t  step;
	int32_t  rad;
	int32_t  j_max;
	int32_t  j_span;
	float    min_r2;
	float    frustum_pad;
	float    frustum_zmin;
	float    frustum_zmax;
	float    max_cull2;
	uint32_t atlas_ver;
	int (*prefilter)(int32_t gx, int32_t gy, int32_t j_span,
	                 const nd_client_state_t *st,
	                 int *cached_ti, int *cached_tj, unsigned *cached_bio,
	                 flora_cell_prep_t *out);
	int (*eval_cell)(const flora_cell_prep_t *prep,
	                 float z_f, int16_t local_wl, float dz_dx, float dz_dy,
	                 flora_blk_entry_t *out_entry);
} flora_pass_desc_t;

static void
flora_emit_pass(seven_frame_ctx_t *ctx, const flora_pass_desc_t *desc)
{
	const nd_client_state_t *st = ctx->st;
	lam_billboard_gpu_t *s_all_gpu_batch = ctx->batch;
	uint32_t all_gpu_count = ctx->batch_count;

	if (desc->step <= 0 || desc->rad <= 0 || all_gpu_count + 16 >= ALL_GPU_MAX_DRAW)
		return;

	const uint16_t world = (st && st->map_data.width > 0) ? (uint16_t)st->map_data.pos[3] : 0;
	const int16_t base_wl = nd_field_water_level(world);
	int64_t lwx64 = 0, lwy64 = 0;
	nd_seven_world_pos(st, &lwx64, &lwy64);
	const int32_t lwx = (int32_t)lwx64;
	const int32_t lwy = (int32_t)lwy64;
	const float lwx_f = (float)lwx;
	const float lwy_f = (float)lwy;

	const int32_t step = desc->step;
	const int32_t rad = desc->rad;
	const int32_t j_max = desc->j_max;
	const float rad2_f = (float)rad * (float)rad;
	const int32_t ix0 = ndw_fdiv(lwx - rad, step);
	const int32_t ix1 = ndw_fdiv(lwx + rad, step);
	const int32_t iy0 = ndw_fdiv(lwy - rad, step);
	const int32_t iy1 = ndw_fdiv(lwy + rad, step);
	const int32_t bx0 = ndw_fdiv(ix0, FLORA_BLK_DIM);
	const int32_t bx1 = ndw_fdiv(ix1, FLORA_BLK_DIM);
	const int32_t by0 = ndw_fdiv(iy0, FLORA_BLK_DIM);
	const int32_t by1 = ndw_fdiv(iy1, FLORA_BLK_DIM);
	const uint32_t map_sig = flora_map_bio_sig(st);
	const uint32_t forget_seq = nd_seven_get_forget_seq();

	for (int32_t by = by0; by <= by1 && all_gpu_count + 4 < ALL_GPU_MAX_DRAW; by++) {
		int32_t blk_gy0 = by * FLORA_BLK_DIM * step;
		int32_t blk_gy1 = (by * FLORA_BLK_DIM + (FLORA_BLK_DIM - 1)) * step;
		float blk_min_y = (float)(blk_gy0 - j_max);
		float blk_max_y = (float)(blk_gy1 + j_max);
		for (int32_t bx = bx0; bx <= bx1 && all_gpu_count + 4 < ALL_GPU_MAX_DRAW; bx++) {
			int32_t blk_gx0 = bx * FLORA_BLK_DIM * step;
			int32_t blk_gx1 = (bx * FLORA_BLK_DIM + (FLORA_BLK_DIM - 1)) * step;
			float blk_min_x = (float)(blk_gx0 - j_max);
			float blk_max_x = (float)(blk_gx1 + j_max);

			if (flora_blk_min_dist2(lwx_f, lwy_f, blk_min_x, blk_min_y, blk_max_x, blk_max_y) > rad2_f)
				continue;
			if (desc->max_cull2 > 0.0f &&
			    s_cam_dist * s_cam_dist +
			    0.12f * flora_blk_min_dist2(s_cam_focal_x, s_cam_focal_y,
			                                blk_min_x, blk_min_y, blk_max_x, blk_max_y) >= desc->max_cull2)
				continue;
			if (s_frame_frustum_valid &&
			    !seven_test_frustum_xy(blk_min_x - desc->frustum_pad, blk_min_y - desc->frustum_pad,
			                           blk_max_x + desc->frustum_pad, blk_max_y + desc->frustum_pad,
			                           lwx_f, lwy_f, desc->frustum_zmin, desc->frustum_zmax))
				continue;

			int hit = 0;
			flora_blk_slot_t *slot = flora_blk_lookup(desc->pass_id, world, base_wl, step, bx, by,
			                                          map_sig, desc->atlas_ver, forget_seq, &hit);

			int need_fill = !hit || (slot->filled_mask != ~0ULL);
			if (need_fill) {
				const ar_node_t *blk_leaves[16];
				size_t blk_leaf_cnt = 0;
				uint32_t lod_sig = flora_blk_collect_leaves(blk_gx0 - j_max, blk_gy0 - j_max,
				                                            blk_gx1 + j_max + 8, blk_gy1 + j_max + 8,
				                                            blk_leaves, 16, &blk_leaf_cnt);
				if (!(hit && slot->last_leaf_sig == lod_sig)) {
					flora_blk_entry_t tmp[FLORA_BLK_MAX_CELLS];
					uint8_t orig_cnt = hit ? slot->count : 0u;
					uint8_t cnt = orig_cnt;
					uint64_t filled = hit ? slot->filled_mask : 0ULL;
					if (orig_cnt > 0u)
						memcpy(tmp, &s_flora_blk_arena[slot->arena_off],
						       (size_t)orig_cnt * sizeof(flora_blk_entry_t));

					const ar_node_t *const *adj_arr = (blk_leaf_cnt <= 16)
						? blk_leaves : (const ar_node_t *const *)s_visible_leaves;
					size_t adj_cnt = (blk_leaf_cnt <= 16) ? blk_leaf_cnt : s_visible_leaf_count;
					const ar_node_t *m_node = NULL;
					int cached_ti = 0x7FFFFFFF, cached_tj = 0x7FFFFFFF;
					unsigned cached_bio = 10u;

					for (int ly = 0; ly < FLORA_BLK_DIM; ly++) {
						int32_t gy = (by * FLORA_BLK_DIM + ly) * step;
						for (int lx = 0; lx < FLORA_BLK_DIM; lx++) {
							unsigned cell_idx = (unsigned)((ly << 3) | lx);
							uint64_t bit = 1ULL << cell_idx;
							if (filled & bit)
								continue;

							int32_t gx = (bx * FLORA_BLK_DIM + lx) * step;
							flora_cell_prep_t prep;
							if (!desc->prefilter(gx, gy, desc->j_span, st,
							                     &cached_ti, &cached_tj, &cached_bio, &prep)) {
								filled |= bit;
								continue;
							}

							int32_t elev_q = 0, eq_x = 0, eq_y = 0;
							int16_t local_wl = base_wl;
							if (s_visible_leaf_count > 0) {
								m_node = flora_blk_find_leaf(prep.wx, prep.wy, m_node,
								                             blk_leaves, blk_leaf_cnt, 16);
								if (!m_node || m_node->level > 1)
									continue; /* leave bit unset for incremental fill */
								seven_sample_node_elev_slope(m_node, prep.wx, prep.wy, base_wl,
								                             adj_arr, adj_cnt,
								                             &elev_q, &eq_x, &eq_y, &local_wl);
							} else {
								elev_q = nd_seven_ground_elevation_q12_4(prep.wx, prep.wy);
								eq_x   = nd_seven_ground_elevation_q12_4(prep.wx + 8, prep.wy);
								eq_y   = nd_seven_ground_elevation_q12_4(prep.wx, prep.wy + 8);
								seven_has_water_at(world, prep.wx, prep.wy, &local_wl);
							}

							filled |= bit;
							float z_f = (float)elev_q / 16.0f;
							float dz_dx = (float)(eq_x - elev_q) * (1.0f / 128.0f);
							float dz_dy = (float)(eq_y - elev_q) * (1.0f / 128.0f);

							if (cnt < FLORA_BLK_MAX_CELLS &&
							    desc->eval_cell(&prep, z_f, local_wl, dz_dx, dz_dy, &tmp[cnt])) {
								tmp[cnt].cell_xy = (uint8_t)cell_idx;
								cnt++;
							}
						}
					}

					if (hit && cnt == orig_cnt) {
						slot->filled_mask = filled;
						slot->last_leaf_sig = lod_sig;
						slot->last_use = ++s_flora_blk_lru_tick;
					} else {
						flora_blk_commit(slot, desc->pass_id, world, base_wl, step, bx, by,
						                 lod_sig, filled, map_sig, desc->atlas_ver, forget_seq,
						                 tmp, cnt);
					}
				}
			}

			if (slot->count == 0u)
				continue;
			int check_bounds = (bx == bx0 || bx == bx1 || by == by0 || by == by1);
			int32_t base_ix = bx * FLORA_BLK_DIM;
			int32_t base_iy = by * FLORA_BLK_DIM;
			const flora_blk_entry_t *entries = &s_flora_blk_arena[slot->arena_off];
			for (uint8_t ci = 0; ci < slot->count && all_gpu_count + 4 < ALL_GPU_MAX_DRAW; ci++) {
				const flora_blk_entry_t *e = &entries[ci];
				if (check_bounds) {
					int32_t ix = base_ix + (int32_t)(e->cell_xy & 7u);
					int32_t iy = base_iy + (int32_t)(e->cell_xy >> 3u);
					if (ix < ix0 || ix > ix1 || iy < iy0 || iy > iy1)
						continue;
				}
				float ddx = e->bb.x - lwx_f;
				float ddy = e->bb.y - lwy_f;
				float r2 = ddx * ddx + ddy * ddy;
				if (r2 > rad2_f || (desc->min_r2 > 0.0f && r2 < desc->min_r2))
					continue;
				if (desc->max_cull2 > 0.0f) {
					float mdx = e->bb.x - s_cam_focal_x;
					float mdy = e->bb.y - s_cam_focal_y;
					if (s_cam_dist * s_cam_dist + 0.12f * (mdx * mdx + mdy * mdy) >= desc->max_cull2)
						continue;
				}
				if (s_frame_frustum_valid &&
				    !seven_test_frustum_xy(e->bb.x - desc->frustum_pad, e->bb.y - desc->frustum_pad,
				                           e->bb.x + desc->frustum_pad, e->bb.y + desc->frustum_pad,
				                           lwx_f, lwy_f, desc->frustum_zmin, desc->frustum_zmax))
					continue;
				float sort_y1 = e->bb.y;
				if (desc->use_lamb_occl) {
					float sc_dummy = 1.0f;
					if (!nd_seven_eval_meadow_lamb_occlusion(e->bb.x, e->bb.y, lwx_f, lwy_f, &sort_y1, &sc_dummy))
						continue;
				}
				float eff_alpha = seven_eval_billboard_alpha(e->bb.x, e->bb.y, e->bb.z, e->cull_w, e->bb.height);
				uint32_t base_alpha_pack = seven_pack_white_alpha(eff_alpha);
				if (base_alpha_pack == 0)
					continue;
				lam_billboard_gpu_t *dst = &s_all_gpu_batch[all_gpu_count++];
				*dst = e->bb;
				if (desc->use_lamb_occl)
					dst->y = sort_y1;
				dst->tint_rgba = e->bb.tint_rgba | (base_alpha_pack & 0xFFu);
			}
		}
	}

	ctx->batch_count = all_gpu_count;
}

static int
flora_prefilter_meadow(int32_t gx, int32_t gy, int32_t j_span,
                       const nd_client_state_t *st,
                       int *cached_ti, int *cached_tj, unsigned *cached_bio,
                       flora_cell_prep_t *out)
{
	uint32_t jh = meadow_hash32(gx, gy, 0x4A495454u);
	int32_t wx = gx + (int32_t)(jh % (uint32_t)j_span) - (j_span >> 1);
	int32_t wy = gy + (int32_t)((jh >> 6u) % (uint32_t)j_span) - (j_span >> 1);
	out->wx = wx;
	out->wy = wy;
	out->h = jh;
	out->bio = flora_lookup_biome(st, wx, wy, cached_ti, cached_tj, cached_bio);
	out->pre_mesh_id = ND_MESH3D_NONE;
	return 1;
}

static int
flora_eval_meadow_cell(const flora_cell_prep_t *prep,
                       float z_f, int16_t local_wl, float dz_dx, float dz_dy,
                       flora_blk_entry_t *e)
{
	float slope_mag = sqrtf(dz_dx * dz_dx + dz_dy * dz_dy);
	int zone = ND_MEADOW_NONE;
	float scale_m = 1.0f;
	int flip = 0;
	if (!nd_seven_eval_meadow_area(prep->wx, prep->wy, z_f, (float)local_wl, slope_mag,
	                               prep->bio, 999999.0f, &zone, &scale_m, &flip))
		return 0;

	nd_seven_sprite_rect_t sr;
	if (!meadow_lookup_rect(zone, prep->bio, prep->h >> 8u, &sr))
		return 0;

	uint8_t m_id = nd_seven_meadow_mesh3d_id(zone, prep->bio, prep->h >> 16u);
	const nd_mesh3d_slice_t *m_sl = (m_id != ND_MESH3D_NONE) ? nd_seven_mesh3d_slice(m_id) : NULL;
	float h_var = 0.58f + 0.92f * ((float)((prep->h >> 20u) & 255u) * (1.0f / 255.0f));
	float w_var = 0.94f + 0.44f * ((float)((prep->h >> 12u) & 255u) * (1.0f / 255.0f));
	float bw_m = m_sl ? (m_sl->mesh_w_wu * scale_m * w_var) : ((float)sr.w * scale_m * 1.15f * w_var);
	float bh_m = m_sl ? (m_sl->mesh_h_wu * scale_m * h_var) : ((float)sr.h * scale_m * h_var);
	if (zone != ND_MEADOW_REED && zone != ND_MEADOW_TALL_GRASS) {
		if (bh_m < 4.0f) bh_m = 4.0f;
		if (bh_m > 12.0f) bh_m = 12.0f;
	} else if (bh_m < 6.0f) {
		bh_m = 6.0f;
	}

	lam_billboard_gpu_t *bb = &e->bb;
	bb->x = (float)prep->wx;
	bb->y = (float)prep->wy;
	bb->z = z_f;
	bb->width = bw_m;
	bb->height = bh_m;
	bb->anchor_y = 1.6f * scale_m;
	bb->water_z_off = 1.6f * scale_m;
	if (flip) {
		bb->u0 = (float)(sr.x + sr.w) / (float)ND_SEVEN_ATLAS_W;
		bb->u1 = (float)sr.x / (float)ND_SEVEN_ATLAS_W;
	} else {
		bb->u0 = (float)sr.x / (float)ND_SEVEN_ATLAS_W;
		bb->u1 = (float)(sr.x + sr.w) / (float)ND_SEVEN_ATLAS_W;
	}
	bb->v0 = (float)sr.y / (float)ND_SEVEN_RUNTIME_ATLAS_H;
	bb->v1 = (float)(sr.y + sr.h) / (float)ND_SEVEN_RUNTIME_ATLAS_H;
	bb->tint_rgba = meadow_biome_tint(prep->bio, zone, prep->h >> 12u, 0u);
	bb->style = ND_BB_STYLE_GRASS;
	{
		float grx = bb->width * 0.28f;
		if (grx < 2.0f) grx = 2.0f;
		float gry = grx * 0.45f;
		if (gry < 1.0f) gry = 1.0f;
		bb->shadow_rx = (uint8_t)grx;
		bb->shadow_ry = (uint8_t)gry;
	}
	bb->pad0 = nd_seven_gl_pack_pad0((uint8_t)zone, prep->bio, false);
	bb->mesh_id = m_id;
	e->cull_w = (float)sr.w * scale_m;
	return 1;
}

static void
seven_emit_living_meadow(seven_frame_ctx_t *ctx)
{
	/* Emit deterministic Living Meadow areas, shoreline reed beds, and wildflower glades. */
	float max_meadow_cull = seven_billboard_cull_dist(18.0f, 18.0f);
	int32_t step = nd_seven_meadow_lattice_step(s_cam_dist, max_meadow_cull);
	if (step <= 0)
		return;
	float max_meadow_horiz = 2.88f * sqrtf(max_meadow_cull * max_meadow_cull - s_cam_dist * s_cam_dist);
	int32_t dyn_rad = (int32_t)(s_cam_dist * 1.65f);
	if (dyn_rad < 624) dyn_rad = 624;
	if (dyn_rad > (int32_t)max_meadow_horiz) dyn_rad = (int32_t)max_meadow_horiz;
	if (dyn_rad > 1404) dyn_rad = 1404;
	int32_t max_rad_for_step = step * 24;
	if (dyn_rad > max_rad_for_step) dyn_rad = max_rad_for_step;
	int32_t rad = (dyn_rad / step) * step;
	int32_t j_span = (step > 4) ? (step - 2) : step;
	const flora_pass_desc_t desc = {
		.pass_id = 1u,
		.use_lamb_occl = 1u,
		.step = step,
		.rad = rad,
		.j_max = (j_span >> 1) + 1,
		.j_span = j_span,
		.min_r2 = 0.0f,
		.frustum_pad = 24.0f,
		.frustum_zmin = -256.0f,
		.frustum_zmax = 320.0f,
		.max_cull2 = max_meadow_cull * max_meadow_cull,
		.atlas_ver = atlas_rgba_version(),
		.prefilter = flora_prefilter_meadow,
		.eval_cell = flora_eval_meadow_cell,
	};
	flora_emit_pass(ctx, &desc);
}

static int
flora_prefilter_logs(int32_t gx, int32_t gy, int32_t j_span,
                     const nd_client_state_t *st,
                     int *cached_ti, int *cached_tj, unsigned *cached_bio,
                     flora_cell_prep_t *out)
{
	(void)j_span;
	uint32_t uh = meadow_hash32(gx, gy, 0x4C4F4753u);
	/* Sparse natural occurrence (~16.7% of forest cells) */
	if ((uh % 6u) != 0u)
		return 0;
	int32_t wx = gx + (int32_t)((uh >> 4u) % 49u) - 24;
	int32_t wy = gy + (int32_t)((uh >> 10u) % 49u) - 24;
	unsigned bio = flora_lookup_biome(st, wx, wy, cached_ti, cached_tj, cached_bio);
	uint8_t u_mesh_id = nd_seven_understory_mesh3d_id(bio, uh >> 16u);
	if (u_mesh_id == ND_MESH3D_NONE)
		return 0;
	out->wx = wx;
	out->wy = wy;
	out->h = uh;
	out->bio = bio;
	out->pre_mesh_id = u_mesh_id;
	return 1;
}

static int
flora_eval_logs_cell(const flora_cell_prep_t *prep,
                     float z_f, int16_t local_wl, float dz_dx, float dz_dy,
                     flora_blk_entry_t *e)
{
	if (z_f <= (float)local_wl + 0.8f)
		return 0;
	if (dz_dx * dz_dx + dz_dy * dz_dy > 0.36f * 0.36f)
		return 0;
	const nd_mesh3d_slice_t *sl = nd_seven_mesh3d_slice(prep->pre_mesh_id);
	if (!sl)
		return 0;
	float sc_u = 0.88f + 0.36f * ((float)((prep->h >> 20u) & 15u) / 15.0f);
	float bw_u = sl->mesh_w_wu * sc_u;
	float bh_u = sl->mesh_h_wu * sc_u;
	int flip_u = (int)((prep->h >> 24u) & 1u);

	lam_billboard_gpu_t *bb = &e->bb;
	bb->x = (float)prep->wx;
	bb->y = (float)prep->wy;
	bb->z = z_f;
	bb->width = bw_u;
	bb->height = bh_u;
	bb->anchor_y = 0.0f;
	bb->water_z_off = 0.0f;
	bb->u0 = flip_u ? 1.0f : 0.0f;
	bb->u1 = flip_u ? 0.0f : 1.0f;
	bb->v0 = 0.0f;
	bb->v1 = 0.0f;
	bb->tint_rgba = 0xF2F6EC00u;
	bb->style = ND_BB_STYLE_FLORA;
	bb->shadow_rx = (uint8_t)(bw_u * 0.32f > 4.0f ? bw_u * 0.32f : 4.0f);
	bb->shadow_ry = (uint8_t)(bb->shadow_rx * 0.45f > 2.0f ? bb->shadow_rx * 0.45f : 2.0f);
	bb->pad0 = nd_seven_gl_pack_pad0(1u, prep->bio, false);
	bb->mesh_id = prep->pre_mesh_id;
	e->cull_w = bw_u;
	return 1;
}

static void
seven_emit_forest_floor_logs(seven_frame_ctx_t *ctx)
{
	/* Emit deterministic 3D forest-floor fallen logs & ancient mossy stumps across woodland biomes */
	if (ctx->eff_cam_dist >= 1600.0f)
		return;
	static const flora_pass_desc_t desc = {
		.pass_id = 2u,
		.use_lamb_occl = 0u,
		.step = 112,
		.rad = 1120,
		.j_max = 24,
		.j_span = 0,
		.min_r2 = 36.0f * 36.0f,
		.frustum_pad = 36.0f,
		.frustum_zmin = -256.0f,
		.frustum_zmax = 320.0f,
		.max_cull2 = 0.0f,
		.atlas_ver = 0u,
		.prefilter = flora_prefilter_logs,
		.eval_cell = flora_eval_logs_cell,
	};
	flora_emit_pass(ctx, &desc);
}

static int
flora_prefilter_medium(int32_t gx, int32_t gy, int32_t j_span,
                       const nd_client_state_t *st,
                       int *cached_ti, int *cached_tj, unsigned *cached_bio,
                       flora_cell_prep_t *out)
{
	(void)j_span;
	uint32_t sh = meadow_hash32(gx, gy, 0x53485242u);
	/* Hash-first rejection: (sh%3)==0 && (sh%9)!=0 fails both in_patch and !in_patch */
	if ((sh % 3u) == 0u && (sh % 9u) != 0u)
		return 0;
	int32_t wx = gx + (int32_t)((sh >> 4u) % 33u) - 16;
	int32_t wy = gy + (int32_t)((sh >> 10u) % 33u) - 16;
	unsigned bio = flora_lookup_biome(st, wx, wy, cached_ti, cached_tj, cached_bio);
	if (bio == 0u || bio == 1u || bio == 6u || bio == 14u || bio == 18u)
		return 0;
	out->wx = wx;
	out->wy = wy;
	out->h = sh;
	out->bio = bio;
	out->pre_mesh_id = ND_MESH3D_NONE;
	return 1;
}

static int
flora_eval_medium_cell(const flora_cell_prep_t *prep,
                       float z_f, int16_t local_wl, float dz_dx, float dz_dy,
                       flora_blk_entry_t *e)
{
	if (z_f <= (float)local_wl + 0.6f)
		return 0;
	float slope2 = dz_dx * dz_dx + dz_dy * dz_dy;
	if (slope2 > 0.44f * 0.44f)
		return 0;
	float slope_m = sqrtf(slope2);
	float trail_w = (z_f > (float)local_wl + 1.2f)
	              ? nd_seven_eval_trail_weight((float)prep->wx, (float)prep->wy, slope_m)
	              : 0.0f;
	if (trail_w > 0.35f)
		return 0;

	/* Macro-patch clustering: lush mid-story drifts & riverbank bands separated by open turf */
	float med_patch = meadow_noise_2d((float)prep->wx, (float)prep->wy, 148.0f, 0x4D454432u);
	int near_bank = (z_f <= (float)local_wl + 7.5f);
	int in_patch = (med_patch > 0.44f) || (near_bank && med_patch > 0.30f);
	uint32_t sh = prep->h;
	if (in_patch) {
		if ((sh % 3u) == 0u)
			return 0;
	} else {
		if ((sh % 9u) != 0u)
			return 0;
	}

	unsigned bio = prep->bio;
	unsigned fam = (sh >> 16u) % 10u;
	uint8_t s_mesh_id = ND_MESH3D_NONE;
	uint8_t s_style = ND_BB_STYLE_FLORA;
	uint8_t s_pad0 = nd_seven_gl_pack_pad0(1u, bio, false);
	int s_grass_zone = ND_MEADOW_TALL_GRASS;
	int is_grass_clump = 0;

	if (fam < 5u) {
		/* Medium 3D shrubs: creeping juniper (0), heather/sage (2), giant bracken fern (3) */
		s_mesh_id = nd_seven_shrub_mesh3d_id(bio, sh >> 18u);
		if (s_mesh_id >= ND_MESH3D_SHRUB_BASE + 2u && s_mesh_id < ND_MESH3D_SHRUB_BASE + 4u) {
			unsigned sub = (sh >> 18u) & 1u;
			unsigned m_arch = (bio == 5u || bio == 8u || bio == 9u || bio == 11u ||
			                   bio == 12u || bio == 13u || bio == 16u || bio == 17u)
			                ? 3u : (((sh >> 19u) & 1u) ? 2u : 0u);
			s_mesh_id = (uint8_t)(ND_MESH3D_SHRUB_BASE + m_arch * 2u + sub);
		} else if (s_mesh_id >= ND_MESH3D_SHRUB_BASE + 8u) {
			s_mesh_id = (uint8_t)(ND_MESH3D_SHRUB_BASE + (((sh >> 19u) & 1u) ? 4u : 0u) + ((sh >> 18u) & 1u));
		}
	} else if (fam < 8u) {
		/* Scaled tall meadow grass / forest fern clumps (16-28 wu) */
		int is_wood = (bio == 8u || bio == 9u || bio == 11u ||
		               bio == 12u || bio == 13u || bio == 16u || bio == 17u);
		s_grass_zone = (is_wood && !near_bank && ((sh >> 19u) & 1u))
		             ? ND_MEADOW_UNDERSTORY : ND_MEADOW_TALL_GRASS;
		s_mesh_id = nd_seven_meadow_mesh3d_id(s_grass_zone, bio, sh >> 18u);
		s_style = ND_BB_STYLE_GRASS;
		s_pad0 = nd_seven_gl_pack_pad0((uint8_t)s_grass_zone, bio, false);
		is_grass_clump = 1;
	} else {
		/* Wild tall flowering herbs (flax, wild carrot umbels, leafy berry bushes) */
		if (bio == 2u || bio == 3u || bio == 7u || bio == 15u) {
			s_mesh_id = (uint8_t)(ND_MESH3D_SHRUB_BASE + 4u + ((sh >> 18u) & 1u));
		} else {
			static const uint8_t herb_sp[3] = { 3u, 8u, 9u };
			unsigned hsp = herb_sp[(sh >> 18u) % 3u];
			s_mesh_id = nd_seven_plant_mesh3d_id(ND_PLANT_PACK_ID(hsp, 1u, (sh >> 20u) & 3u));
		}
	}

	const nd_mesh3d_slice_t *sl = (s_mesh_id != ND_MESH3D_NONE) ? nd_seven_mesh3d_slice(s_mesh_id) : NULL;
	if (!sl || sl->mesh_h_wu <= 0.1f)
		return 0;

	float t_h = (float)((sh >> 22u) & 15u) / 15.0f;
	float target_h = is_grass_clump ? (16.0f + 12.0f * t_h) : (16.0f + 16.0f * t_h);
	float sc_s = target_h / sl->mesh_h_wu;
	float bw_s = sl->mesh_w_wu * sc_s * (is_grass_clump ? 1.12f : 1.0f);
	float bh_s = target_h;
	int flip_s = (int)((sh >> 26u) & 1u);

	lam_billboard_gpu_t *bb = &e->bb;
	bb->x = (float)prep->wx;
	bb->y = (float)prep->wy;
	bb->z = z_f;
	bb->width = bw_s;
	bb->height = bh_s;
	bb->anchor_y = 0.0f;
	bb->water_z_off = 0.0f;
	bb->u0 = flip_s ? 1.0f : 0.0f;
	bb->u1 = flip_s ? 0.0f : 1.0f;
	bb->v0 = 0.0f;
	bb->v1 = 0.0f;
	bb->tint_rgba = is_grass_clump
	              ? meadow_biome_tint(bio, s_grass_zone, sh >> 12u, 0u)
	              : 0xF4F7EE00u;
	bb->style = s_style;
	bb->shadow_rx = (uint8_t)(bw_s * 0.30f > 4.0f ? bw_s * 0.30f : 4.0f);
	bb->shadow_ry = (uint8_t)(bb->shadow_rx * 0.45f > 2.0f ? bb->shadow_rx * 0.45f : 2.0f);
	bb->pad0 = s_pad0;
	bb->mesh_id = s_mesh_id;
	e->cull_w = bw_s;
	return 1;
}

static void
seven_emit_medium_botanical(seven_frame_ctx_t *ctx)
{
	/* Emit deterministic 3D Medium botanical stratum (16-32 wu) */
	if (ctx->eff_cam_dist >= 2200.0f)
		return;
	static const flora_pass_desc_t desc = {
		.pass_id = 3u,
		.use_lamb_occl = 0u,
		.step = 52,
		.rad = 1456,
		.j_max = 16,
		.j_span = 0,
		.min_r2 = 28.0f * 28.0f,
		.frustum_pad = 32.0f,
		.frustum_zmin = -256.0f,
		.frustum_zmax = 320.0f,
		.max_cull2 = 0.0f,
		.atlas_ver = 0u,
		.prefilter = flora_prefilter_medium,
		.eval_cell = flora_eval_medium_cell,
	};
	flora_emit_pass(ctx, &desc);
}

static int
flora_prefilter_large(int32_t gx, int32_t gy, int32_t j_span,
                      const nd_client_state_t *st,
                      int *cached_ti, int *cached_tj, unsigned *cached_bio,
                      flora_cell_prep_t *out)
{
	(void)j_span;
	uint32_t lh = meadow_hash32(gx, gy, 0x4C524753u);
	if ((lh % 3u) == 0u)
		return 0;
	int32_t wx = gx + (int32_t)((lh >> 4u) % 53u) - 26;
	int32_t wy = gy + (int32_t)((lh >> 10u) % 53u) - 26;
	unsigned bio = flora_lookup_biome(st, wx, wy, cached_ti, cached_tj, cached_bio);
	if (bio == 0u || bio == 1u || bio == 18u)
		return 0;
	out->wx = wx;
	out->wy = wy;
	out->h = lh;
	out->bio = bio;
	out->pre_mesh_id = ND_MESH3D_NONE;
	return 1;
}

static int
flora_eval_large_cell(const flora_cell_prep_t *prep,
                      float z_f, int16_t local_wl, float dz_dx, float dz_dy,
                      flora_blk_entry_t *e)
{
	int near_bank = (z_f <= (float)local_wl + 8.5f);
	if (z_f <= (float)local_wl + (near_bank ? 0.15f : 0.8f))
		return 0;
	float slope2 = dz_dx * dz_dx + dz_dy * dz_dy;
	if (slope2 > 0.44f * 0.44f)
		return 0;
	float slope_m = sqrtf(slope2);
	float trail_w = (z_f > (float)local_wl + 1.2f)
	              ? nd_seven_eval_trail_weight((float)prep->wx, (float)prep->wy, slope_m)
	              : 0.0f;
	if (trail_w > 0.35f)
		return 0;

	unsigned bio = prep->bio;
	uint32_t lh = prep->h;
	float krumm = 1.0f;
	float grove = nd_seven_eval_grove_ecology(prep->wx, prep->wy, z_f, (float)local_wl, slope_m, bio, &krumm);
	float large_patch = meadow_noise_2d((float)prep->wx, (float)prep->wy, 210.0f, 0x4C524733u);

	uint8_t l_mesh_id = ND_MESH3D_NONE;
	float t_h = (float)((lh >> 20u) & 15u) / 15.0f;
	float target_h = 36.0f + 39.0f * t_h;

	if ((near_bank || bio == 5u) && bio != 6u && bio != 14u &&
	    large_patch > 0.36f && ((lh >> 14u) & 3u) < 2u) {
		l_mesh_id = nd_seven_plant_mesh3d_id(ND_PLANT_PACK_ID(15u, 1u, (lh >> 16u) & 3u));
		target_h = 36.0f + 24.0f * t_h;
	} else {
		if (grove < 0.26f && large_patch < 0.54f)
			return 0;
		if (bio == 6u || bio == 14u) {
			if (large_patch < 0.62f)
				return 0;
			l_mesh_id = nd_seven_sapling_mesh3d_id(bio, lh >> 16u);
			target_h = 36.0f + 26.0f * t_h;
		} else if (((lh >> 14u) % 100u) < 45u) {
			l_mesh_id = nd_seven_sapling_mesh3d_id(bio, lh >> 16u);
			target_h = 42.0f + 33.0f * t_h;
		} else if (((lh >> 14u) % 100u) >= 88u &&
		           (bio == 2u || bio == 3u || bio == 8u || bio == 9u ||
		            bio == 11u || bio == 12u || bio == 13u)) {
			l_mesh_id = (uint8_t)(ND_MESH3D_SHRUB_BASE + 8u + ((lh >> 16u) & 1u));
			target_h = 42.0f + 30.0f * t_h;
		} else {
			l_mesh_id = (uint8_t)(ND_MESH3D_SHRUB_BASE + 2u + ((lh >> 16u) & 1u));
			target_h = 36.0f + 28.0f * t_h;
		}
	}

	const nd_mesh3d_slice_t *sl = (l_mesh_id != ND_MESH3D_NONE) ? nd_seven_mesh3d_slice(l_mesh_id) : NULL;
	if (!sl || sl->mesh_h_wu <= 0.1f)
		return 0;
	target_h = 36.0f + (target_h - 36.0f) * krumm;
	if (target_h < 36.0f) target_h = 36.0f;
	if (target_h > 75.0f) target_h = 75.0f;

	float sc_l = target_h / sl->mesh_h_wu;
	float bw_l = sl->mesh_w_wu * sc_l;
	float bh_l = target_h;
	int flip_l = (int)((lh >> 24u) & 1u);

	lam_billboard_gpu_t *bb = &e->bb;
	bb->x = (float)prep->wx;
	bb->y = (float)prep->wy;
	bb->z = z_f;
	bb->width = bw_l;
	bb->height = bh_l;
	bb->anchor_y = 0.0f;
	bb->water_z_off = 0.0f;
	bb->u0 = flip_l ? 1.0f : 0.0f;
	bb->u1 = flip_l ? 0.0f : 1.0f;
	bb->v0 = 0.0f;
	bb->v1 = 0.0f;
	bb->tint_rgba = 0xF2F6EC00u;
	bb->style = ND_BB_STYLE_FLORA;
	bb->shadow_rx = (uint8_t)(bw_l * 0.32f > 5.0f ? bw_l * 0.32f : 5.0f);
	bb->shadow_ry = (uint8_t)(bb->shadow_rx * 0.45f > 2.0f ? bb->shadow_rx * 0.45f : 2.0f);
	bb->pad0 = nd_seven_gl_pack_pad0(2u, bio, false);
	bb->mesh_id = l_mesh_id;
	e->cull_w = bw_l;
	return 1;
}

static void
seven_emit_large_undergrowth(seven_frame_ctx_t *ctx)
{
	/* Emit deterministic 3D Large undergrowth stratum (36-75 wu) */
	if (ctx->eff_cam_dist >= 2600.0f)
		return;
	static const flora_pass_desc_t desc = {
		.pass_id = 4u,
		.use_lamb_occl = 0u,
		.step = 92,
		.rad = 1564,
		.j_max = 26,
		.j_span = 0,
		.min_r2 = 40.0f * 40.0f,
		.frustum_pad = 48.0f,
		.frustum_zmin = -256.0f,
		.frustum_zmax = 360.0f,
		.max_cull2 = 0.0f,
		.atlas_ver = 0u,
		.prefilter = flora_prefilter_large,
		.eval_cell = flora_eval_large_cell,
	};
	flora_emit_pass(ctx, &desc);
}

static int
flora_prefilter_geobreak(int32_t gx, int32_t gy, int32_t j_span,
                         const nd_client_state_t *st,
                         int *cached_ti, int *cached_tj, unsigned *cached_bio,
                         flora_cell_prep_t *out)
{
	(void)j_span;
	uint32_t gh = meadow_hash32(gx, gy, 0x47454F42u);
	if ((gh % 100u) >= 64u)
		return 0;
	int32_t wx = gx + (int32_t)((gh >> 4u) % 41u) - 20;
	int32_t wy = gy + (int32_t)((gh >> 10u) % 41u) - 20;
	unsigned bio = flora_lookup_biome(st, wx, wy, cached_ti, cached_tj, cached_bio);
	uint8_t g_mesh_id = nd_seven_geobreak_mesh3d_id(bio, gh >> 16u);
	if (g_mesh_id == ND_MESH3D_NONE)
		return 0;
	out->wx = wx;
	out->wy = wy;
	out->h = gh;
	out->bio = bio;
	out->pre_mesh_id = g_mesh_id;
	return 1;
}

static int
flora_eval_geobreak_cell(const flora_cell_prep_t *prep,
                         float z_f, int16_t local_wl, float dz_dx, float dz_dy,
                         flora_blk_entry_t *e)
{
	if (z_f <= (float)local_wl + 0.7f)
		return 0;
	float slope2 = dz_dx * dz_dx + dz_dy * dz_dy;
	if (slope2 > 0.44f * 0.44f)
		return 0;
	float slope_m = sqrtf(slope2);
	float trail_w = (z_f > (float)local_wl + 1.2f)
	              ? nd_seven_eval_trail_weight((float)prep->wx, (float)prep->wy, slope_m)
	              : 0.0f;
	if (trail_w > 0.38f)
		return 0;

	unsigned bio = prep->bio;
	uint32_t gh = prep->h;
	float krumm = 1.0f;
	float grove = nd_seven_eval_grove_ecology(prep->wx, prep->wy, z_f, (float)local_wl, slope_m, bio, &krumm);
	float hummock_patch = meadow_noise_2d((float)prep->wx, (float)prep->wy, 165.0f, 0x48554D32u);
	int on_shoulder = (trail_w >= 0.05f && trail_w <= 0.38f);
	int on_grove_edge = (grove >= 0.24f && grove <= 0.68f);
	int on_riverbank = (z_f <= (float)local_wl + 10.0f);
	if (!on_shoulder && !on_grove_edge && !on_riverbank &&
	    hummock_patch < 0.44f && ((gh >> 3u) % 3u) != 0u)
		return 0;

	const nd_mesh3d_slice_t *sl = nd_seven_mesh3d_slice(prep->pre_mesh_id);
	if (!sl || sl->mesh_h_wu <= 0.1f)
		return 0;
	float sc_g = 0.78f + 0.46f * ((float)((gh >> 20u) & 15u) / 15.0f);
	float bw_g = sl->mesh_w_wu * sc_g;
	float bh_g = sl->mesh_h_wu * sc_g;
	int flip_g = (int)((gh >> 24u) & 1u);

	lam_billboard_gpu_t *bb = &e->bb;
	bb->x = (float)prep->wx;
	bb->y = (float)prep->wy;
	bb->z = z_f;
	bb->width = bw_g;
	bb->height = bh_g;
	bb->anchor_y = 0.0f;
	bb->water_z_off = 0.0f;
	bb->u0 = flip_g ? 1.0f : 0.0f;
	bb->u1 = flip_g ? 0.0f : 1.0f;
	bb->v0 = 0.0f;
	bb->v1 = 0.0f;
	bb->tint_rgba = 0xEEF2EA00u;
	bb->style = ND_BB_STYLE_STANDARD;
	bb->shadow_rx = (uint8_t)(bw_g * 0.32f > 4.0f ? bw_g * 0.32f : 4.0f);
	bb->shadow_ry = (uint8_t)(bb->shadow_rx * 0.48f > 2.0f ? bb->shadow_rx * 0.48f : 2.0f);
	bb->pad0 = nd_seven_gl_pack_pad0(0u, bio, false);
	bb->mesh_id = prep->pre_mesh_id;
	e->cull_w = bw_g;
	return 1;
}

static void
seven_emit_geobreak_props(seven_frame_ctx_t *ctx)
{
	/* Emit deterministic 3D ground-breaker relief props (mossy hummocks, scree outcrops, turf berms) */
	if (ctx->eff_cam_dist >= 2200.0f)
		return;
	static const flora_pass_desc_t desc = {
		.pass_id = 5u,
		.use_lamb_occl = 0u,
		.step = 52,
		.rad = 1456,
		.j_max = 20,
		.j_span = 0,
		.min_r2 = 26.0f * 26.0f,
		.frustum_pad = 36.0f,
		.frustum_zmin = -256.0f,
		.frustum_zmax = 320.0f,
		.max_cull2 = 0.0f,
		.atlas_ver = 0u,
		.prefilter = flora_prefilter_geobreak,
		.eval_cell = flora_eval_geobreak_cell,
	};
	flora_emit_pass(ctx, &desc);
}

static void
seven_emit_particles_and_sort_batch(seven_frame_ctx_t *ctx)
{
	const float eff_cam_dist = ctx->eff_cam_dist;
	lam_billboard_gpu_t *s_all_gpu_batch = ctx->batch;
	uint32_t all_gpu_count = ctx->batch_count;

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
			dst_bb->mesh_id = 0;
		}
	}

	/* Sort near-field 2D/crossfade/particle/glow billboards back-to-front along camera
	 * horizontal backward axis (sy, cy). Pure-3D meshes strictly inside their 3D LOD
	 * crossfade start are depth-tested and bucketed by mesh_id in flush_mesh3d_staging(),
	 * so they skip sorting completely; remaining items sort via compact 12-byte keys. */
	float cam_yaw_rad = nd_seven_get_yaw_deg() * (3.14159265358979323846f / 180.0f);
	float cam_cy = cosf(cam_yaw_rad);
	float cam_sy = sinf(cam_yaw_rad);
	if (all_gpu_count > 1) {
		typedef struct {
			float    d;
			float    r;
			uint32_t idx;
		} bb_sort_key_t;
		static bb_sort_key_t       s_sort_keys[ALL_GPU_MAX_DRAW];
		static uint32_t            s_sort_slots[ALL_GPU_MAX_DRAW];
		static lam_billboard_gpu_t s_sort_tmp[ALL_GPU_MAX_DRAW];

		float det_scale = nd_seven_get_detail_scale();
		float cam_d2 = s_cam_dist * s_cam_dist;
		float grass_cs = fmaxf(720.0f, 1150.0f * det_scale) * 0.92f;
		float tall_cs  = fmaxf(1550.0f, 2500.0f * det_scale) * 0.92f;
		float norm_cs  = fmaxf(1050.0f, 1650.0f * det_scale) * 0.92f;
		float grass_cs2 = grass_cs * grass_cs;
		float tall_cs2  = tall_cs * tall_cs;
		float norm_cs2  = norm_cs * norm_cs;
		uint32_t n_sort = 0;

		for (uint32_t i = 0; i < all_gpu_count; i++) {
			const lam_billboard_gpu_t *bb = &s_all_gpu_batch[i];
			bool is_other = (bb->style == ND_BB_STYLE_GLOW) || (bb->style == ND_BB_STYLE_PARTICLE);
			if (!is_other && bb->mesh_id > 0 && bb->mesh_id < ND_SEVEN_MESH3D_TOTAL) {
				if (nd_seven_gl_billboard_is_mesh3d_only(bb))
					continue;
				bool is_grass = (bb->style == ND_BB_STYLE_GRASS);
				bool is_understory = (bb->mesh_id >= ND_MESH3D_UNDERSTORY_BASE &&
				                      bb->mesh_id < ND_MESH3D_TOTAL);
				bool is_tall_tree = (!is_grass &&
				                     (bb->height >= 90.0f ||
				                      (is_understory && bb->height >= 64.0f)) &&
				                     bb->mesh_id >= ND_MESH3D_PLANT_BASE &&
				                     bb->mesh_id < ND_MESH3D_TOTAL);
				float cs2 = is_grass ? grass_cs2 : (is_tall_tree ? tall_cs2 : norm_cs2);
				float dx_m = bb->x - s_cam_focal_x;
				float dy_m = bb->y - s_cam_focal_y;
				float d2_m = cam_d2 + 0.35f * (dx_m * dx_m + dy_m * dy_m);
				if (d2_m <= cs2) {
					const nd_mesh3d_slice_t *sl = nd_seven_mesh3d_slice(bb->mesh_id);
					if (sl && sl->idx_count > 0)
						continue;
				}
			}
			float off = (bb->style == ND_BB_STYLE_PARTICLE) ? 14.0f
			          : (bb->style == ND_BB_STYLE_GRASS ? -8.5f : 0.0f);
			s_sort_slots[n_sort] = i;
			s_sort_keys[n_sort].d   = bb->x * cam_sy + bb->y * cam_cy + off;
			s_sort_keys[n_sort].r   = bb->x * cam_cy - bb->y * cam_sy;
			s_sort_keys[n_sort].idx = i;
			n_sort++;
		}

		if (n_sort > 1) {
			static const uint32_t gaps[8] = { 701, 301, 132, 57, 23, 10, 4, 1 };
			for (int gi = 0; gi < 8; gi++) {
				uint32_t gap = gaps[gi];
				if (gap >= n_sort)
					continue;
				for (uint32_t a = gap; a < n_sort; a++) {
					bb_sort_key_t k = s_sort_keys[a];
					uint32_t b = a;
					while (b >= gap) {
						const bb_sort_key_t *prev = &s_sort_keys[b - gap];
						if (prev->d < k.d || (prev->d == k.d && prev->r <= k.r))
							break;
						s_sort_keys[b] = *prev;
						b -= gap;
					}
					s_sort_keys[b] = k;
				}
			}
			for (uint32_t k = 0; k < n_sort; k++)
				s_sort_tmp[k] = s_all_gpu_batch[s_sort_keys[k].idx];
			for (uint32_t k = 0; k < n_sort; k++)
				s_all_gpu_batch[s_sort_slots[k]] = s_sort_tmp[k];
		}
	}

	ctx->cam_cy = cam_cy;
	ctx->cam_sy = cam_sy;
	ctx->batch_count = all_gpu_count;
}

static void
seven_emit_homogeneous_scenery(seven_frame_ctx_t *ctx)
{
	const nd_client_state_t *st = ctx->st;
	const float max_eff_sz = ctx->max_eff_sz;
	const float max_scenery_cull = ctx->max_scenery_cull;
	const float cam_cy = ctx->cam_cy;
	const float cam_sy = ctx->cam_sy;
	const int obj_i0 = ctx->obj_i0, obj_i1 = ctx->obj_i1;
	const int obj_j0 = ctx->obj_j0, obj_j1 = ctx->obj_j1;
	lam_billboard_gpu_t *s_all_gpu_batch = ctx->batch;
	const uint32_t all_gpu_count = ctx->batch_count;

	/* Unified Homogeneous Scenery Pass across visible libareole leaves */
	s_mid_batch_len = 0;
	s_last_mid_gpu_count = 0;
	int near_flushed = 0;
	uint32_t tint_scenery = (s_cam_dist < max_scenery_cull) ? 0xFFFFFFFFu : 0u;
	if (tint_scenery != 0) {
		nd_seven_set_scenery_synth_budget(0xFFFFFFFFu);
		uint16_t world = (st && st->map_data.width > 0) ? (uint16_t)st->map_data.pos[3] : 0;
		int16_t w_level = nd_field_water_level(world);
		int16_t base_rx = st ? st->map_data.pos[0] : 0;
		int16_t base_ry = st ? st->map_data.pos[1] : 0;
		int16_t base_rz = st ? st->map_data.pos[2] : 0;
		int32_t center_ry_grid = -(int32_t)base_ry;

		int64_t lwx = 0, lwy = 0;
		nd_seven_world_pos(st, &lwx, &lwy);

		float near_x0 = (float)((int32_t)base_rx + obj_i0) * (float)NDW_TILE_W;
		float near_x1 = (float)((int32_t)base_rx + obj_i1 + 1) * (float)NDW_TILE_W;
		float near_y0 = (float)(center_ry_grid + obj_j0) * (float)NDW_TILE_H;
		float near_y1 = (float)(center_ry_grid + obj_j1 + 1) * (float)NDW_TILE_H;
		float near_max_proj = ((cam_sy >= 0.0f) ? near_x1 : near_x0) * cam_sy
		                    + ((cam_cy >= 0.0f) ? near_y1 : near_y0) * cam_cy;

		/* Sort visible leaves back-to-front along camera backward axis (cam_sy, cam_cy) */
		for (size_t a = 1; a < s_visible_leaf_count; a++) {
			const ar_node_t *key_node = s_visible_leaves[a];
			float kx = key_node ? (float)((cam_sy >= 0.0f) ? key_node->ox : (key_node->ox + key_node->w)) : 0.0f;
			float ky = key_node ? (float)((cam_cy >= 0.0f) ? key_node->oy : (key_node->oy + key_node->h)) : 0.0f;
			float key_d = kx * cam_sy + ky * cam_cy;
			float key_r = kx * cam_cy - ky * cam_sy;
			size_t b = a;
			while (b > 0) {
				const ar_node_t *prev = s_visible_leaves[b - 1];
				float px_k = prev ? (float)((cam_sy >= 0.0f) ? prev->ox : (prev->ox + prev->w)) : 0.0f;
				float py_k = prev ? (float)((cam_cy >= 0.0f) ? prev->oy : (prev->oy + prev->h)) : 0.0f;
				float prev_d = px_k * cam_sy + py_k * cam_cy;
				float prev_r = px_k * cam_cy - py_k * cam_sy;
				if (prev_d < key_d || (prev_d == key_d && prev_r <= key_r))
					break;
				s_visible_leaves[b] = prev;
				b--;
			}
			s_visible_leaves[b] = key_node;
		}

		float max_horiz = 2.88f * sqrtf(max_scenery_cull * max_scenery_cull - s_cam_dist * s_cam_dist);
		int32_t rad_rx = (int32_t)(max_horiz / (float)NDW_TILE_W) + 2;
		int32_t rad_ry = (int32_t)(max_horiz / (float)NDW_TILE_H) + 2;
		if (rad_rx > SCENERY_RAD_MAX_ROOMS) rad_rx = SCENERY_RAD_MAX_ROOMS;
		if (rad_ry > SCENERY_RAD_MAX_ROOMS) rad_ry = SCENERY_RAD_MAX_ROOMS;

		int32_t full_x0 = (int32_t)base_rx - rad_rx;
		int32_t full_x1 = (int32_t)base_rx + rad_rx;
		int32_t full_y0 = center_ry_grid - rad_ry;
		int32_t full_y1 = center_ry_grid + rad_ry;

		uint32_t cur_atlas_ver = atlas_rgba_version();
		uint32_t cur_forget_seq = nd_seven_get_forget_seq();
		uint32_t sync_block_builds = 0;

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

			float leaf_min_proj = (float)((cam_sy >= 0.0f) ? node->ox : (node->ox + node->w)) * cam_sy
			                    + (float)((cam_cy >= 0.0f) ? node->oy : (node->oy + node->h)) * cam_cy;
			if (!near_flushed && leaf_min_proj >= near_max_proj - 0.5f) {
				if (s_mid_batch_len > 0) {
					nd_seven_gl_queue_billboards(s_mid_stream_batch, s_mid_batch_len, atlas_rgba());
					s_mid_batch_len = 0;
				}
				if (all_gpu_count > 0)
					nd_seven_gl_queue_billboards(s_all_gpu_batch, all_gpu_count, atlas_rgba());
				near_flushed = 1;
			}

			float nz_min = (float)node->min_val / 16.0f;
			float nz_max = (float)node->max_val / 16.0f;

			int32_t cx0 = (rx0 > full_x0) ? rx0 : full_x0;
			int32_t cx1 = (rx1 < full_x1 + 1) ? rx1 : (full_x1 + 1);
			int32_t cy0 = (ry0 > full_y0) ? ry0 : full_y0;
			int32_t cy1 = (ry1 < full_y1 + 1) ? ry1 : (full_y1 + 1);

			for (int32_t by0 = cy0; by0 < cy1; by0 += 8) {
				int32_t by1 = (by0 + 8 < cy1) ? (by0 + 8) : cy1;
				float row_wx0 = (float)(cx0 * NDW_TILE_W);
				float row_wx1 = (float)(cx1 * NDW_TILE_W);
				float row_wy0 = (float)(by0 * NDW_TILE_H);
				float row_wy1 = (float)(by1 * NDW_TILE_H);
				float row_min_proj = ((cam_sy >= 0.0f) ? row_wx0 : row_wx1) * cam_sy
				                   + ((cam_cy >= 0.0f) ? row_wy0 : row_wy1) * cam_cy;
				if (!near_flushed && cam_cy >= 0.0f && row_min_proj >= near_max_proj - 0.5f) {
					if (s_mid_batch_len > 0) {
						nd_seven_gl_queue_billboards(s_mid_stream_batch, s_mid_batch_len, atlas_rgba());
						s_mid_batch_len = 0;
					}
					if (all_gpu_count > 0)
						nd_seven_gl_queue_billboards(s_all_gpu_batch, all_gpu_count, atlas_rgba());
					near_flushed = 1;
				}
				for (int32_t bx0 = cx0; bx0 < cx1; bx0 += 8) {
					int32_t bx1 = (bx0 + 8 < cx1) ? (bx0 + 8) : cx1;
					float blk_wx0 = (float)(bx0 * NDW_TILE_W);
					float blk_wx1 = (float)(bx1 * NDW_TILE_W);
					float blk_wy0 = (float)(by0 * NDW_TILE_H);
					float blk_wy1 = (float)(by1 * NDW_TILE_H);
					float blk_min_proj = ((cam_sy >= 0.0f) ? blk_wx0 : blk_wx1) * cam_sy
					                   + ((cam_cy >= 0.0f) ? blk_wy0 : blk_wy1) * cam_cy;
					float blk_max_proj = ((cam_sy >= 0.0f) ? blk_wx1 : blk_wx0) * cam_sy
					                   + ((cam_cy >= 0.0f) ? blk_wy1 : blk_wy0) * cam_cy;
					bool straddles_near_flush =
						(!near_flushed && blk_min_proj < near_max_proj - 0.5f && blk_max_proj > near_max_proj + 0.5f);
					float dx_blk = (s_cam_focal_x < blk_wx0) ? (blk_wx0 - s_cam_focal_x)
					             : ((s_cam_focal_x > blk_wx1) ? (s_cam_focal_x - blk_wx1) : 0.0f);
					float dy_blk = (s_cam_focal_y < blk_wy0) ? (blk_wy0 - s_cam_focal_y)
					             : ((s_cam_focal_y > blk_wy1) ? (s_cam_focal_y - blk_wy1) : 0.0f);
					float d2_blk_min = s_cam_dist * s_cam_dist + 0.12f * (dx_blk * dx_blk + dy_blk * dy_blk);
					float min_vis_sz = sqrtf(d2_blk_min) / (SEVEN_BB_DIST_PER_SIZE * s_frame_detail_scale);
					if (!straddles_near_flush && min_vis_sz >= max_eff_sz)
						continue;

					int b_frustum_state = 2;
					if (s_frame_frustum_valid) {
						float bmin_x = blk_wx0 - 512.0f;
						float bmax_x = blk_wx1 + 512.0f;
						float bmin_y = blk_wy0 - 512.0f;
						float bmax_y = blk_wy1 + 512.0f;
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

					uint8_t blk_w = (uint8_t)(bx1 - bx0);
					uint8_t blk_h = (uint8_t)(by1 - by0);
					if (b_frustum_state >= 1 && tint_scenery != 0 &&
					    blk_w >= 1u && blk_w <= 8u && blk_h >= 1u && blk_h <= 8u &&
					    !overlaps_near && !straddles_near_flush &&
					    nd_seven_rect_is_pure_virgin(st, (int16_t)world, base_rz,
					                                 (int16_t)bx0, (int16_t)(bx1 - 1),
					                                 (int16_t)(-(by1 - 1)), (int16_t)(-by0))) {
						uint32_t slot_idx =
							(blk_w == 8u && blk_h == 8u)
							? (((((uint32_t)(bx0 >> 3) & 63u) |
							     (((uint32_t)(by0 >> 3) & 63u) << 6)) ^
							    (((uint32_t)world & 3u) << 10)) & (BLOCK_BB_CACHE_SLOTS - 1u))
							: (((((uint32_t)bx0 & 63u) |
							     (((uint32_t)by0 & 63u) << 6)) ^
							    (((uint32_t)node->level & 3u) << 10) ^
							    (((uint32_t)world & 3u) << 8)) & (BLOCK_BB_CACHE_SLOTS - 1u));
						block_bb_cache_slot_t *slot = &s_block_bb_cache[slot_idx];
						if (atomic_load_explicit(&slot->async_task.state, memory_order_acquire) == (uint8_t)STL_WQ_DONE) {
							if (slot->bx0 == bx0 && slot->by0 == by0 &&
							    slot->bw == blk_w && slot->bh == blk_h &&
							    slot->world == (int16_t)world && slot->rz == base_rz &&
							    slot->node_ox == node->ox && slot->node_oy == node->oy &&
							    slot->staging_idx < BLOCK_BB_STAGING_SLOTS) {
								uint16_t scnt = s_block_bb_staging_cnt[slot->staging_idx];
								uint32_t cur_off = (uint32_t)(s_block_bb_arena_cursor & (BLOCK_BB_ARENA_CAP - 1u));
								if (cur_off + BLOCK_BB_MAX_PER_BLK > BLOCK_BB_ARENA_CAP) {
									s_block_bb_arena_cursor += (uint64_t)(BLOCK_BB_ARENA_CAP - cur_off);
									cur_off = 0;
								}
								uint64_t start_seq = s_block_bb_arena_cursor;
								if (scnt > 0) {
									memcpy(&s_block_bb_arena[cur_off],
									       s_block_bb_staging[slot->staging_idx],
									       (size_t)scnt * sizeof(lam_billboard_gpu_t));
								}
								s_block_bb_arena_cursor += (uint64_t)scnt;
								slot->arena_seq = start_seq + (uint64_t)scnt;
								slot->arena_off = cur_off;
								slot->max_dim = s_block_bb_staging_max_dim[slot->staging_idx];
								slot->count = scnt;
								slot->w_level = w_level;
								slot->node_level = node->level;
								slot->node_rev = node->rev;
								slot->atlas_ver = cur_atlas_ver;
								slot->forget_seq = cur_forget_seq;
								slot->fade_start_t = (s_frame_anim_time > 0.0f && s_frame_dispatch_count > 1u)
								                     ? s_frame_anim_time : -10.0f;
								slot->valid = 1;
							}
							atomic_store_explicit(&slot->async_task.state, (uint8_t)STL_WQ_IDLE, memory_order_release);
						}
						if (stl_wq_busy(&slot->async_task)) {
							continue;
						}
						if (slot->valid &&
						    slot->bx0 == bx0 && slot->by0 == by0 &&
						    slot->bw == blk_w && slot->bh == blk_h &&
						    slot->world == (int16_t)world && slot->rz == base_rz &&
						    slot->w_level == w_level &&
						    slot->node_level == node->level &&
						    slot->node_ox == node->ox && slot->node_oy == node->oy &&
						    slot->node_rev == node->rev &&
						    slot->forget_seq == cur_forget_seq &&
						    (s_block_bb_arena_cursor - slot->arena_seq) <= BLOCK_BB_ARENA_CAP) {
							if (slot->max_dim <= min_vis_sz)
								continue;
							float blk_fade = seven_block_fade_alpha(slot->fade_start_t);
							if (blk_fade <= 0.0f)
								continue;
							const lam_billboard_gpu_t *cached = &s_block_bb_arena[slot->arena_off];
							uint16_t cnt = slot->count;
							for (uint16_t ci = 0; ci < cnt; ci++) {
								float bsz = (cached[ci].width > cached[ci].height) ? cached[ci].width : cached[ci].height;
								if (bsz <= min_vis_sz)
									break;
								if (b_frustum_state == 1) {
									float pad = 0.5f * bsz + 16.0f;
									if (!seven_test_frustum_xy(cached[ci].x - pad, cached[ci].y - pad,
									                           cached[ci].x + pad, cached[ci].y + pad,
									                           (float)lwx, (float)lwy,
									                           cached[ci].z - 16.0f,
									                           cached[ci].z + cached[ci].height + 16.0f))
										continue;
								}
								seven_stream_faded_billboard(&cached[ci], blk_fade);
							}
							continue;
						}

						if (s_seven_wq && sync_block_builds >= 1024u) {
							uint8_t sidx = (uint8_t)(s_block_bb_staging_cursor++ & (BLOCK_BB_STAGING_SLOTS - 1u));
							slot->bx0 = bx0;
							slot->by0 = by0;
							slot->bw = blk_w;
							slot->bh = blk_h;
							slot->node_ox = node->ox;
							slot->node_oy = node->oy;
							slot->node_rev = node->rev;
							slot->atlas_ver = cur_atlas_ver;
							slot->forget_seq = cur_forget_seq;
							slot->world = (int16_t)world;
							slot->rz = base_rz;
							slot->w_level = w_level;
							slot->node_level = node->level;
							slot->staging_idx = sidx;
							slot->async_node = node;
							slot->valid = 0;
							if (stl_wq_submit(s_seven_wq, &slot->async_task, seven_async_block_bb_worker, slot) == 0) {
								continue;
							}
						}
						sync_block_builds++;

						uint32_t cur_off = (uint32_t)(s_block_bb_arena_cursor & (BLOCK_BB_ARENA_CAP - 1u));
						if (cur_off + BLOCK_BB_MAX_PER_BLK > BLOCK_BB_ARENA_CAP) {
							s_block_bb_arena_cursor += (uint64_t)(BLOCK_BB_ARENA_CAP - cur_off);
							cur_off = 0;
						}
						uint64_t start_seq = s_block_bb_arena_cursor;
						lam_billboard_gpu_t *arena_dst = &s_block_bb_arena[cur_off];
						uint16_t arena_cnt = 0;
						float arena_max_dim = 0.0f;

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
								                              arena_dst, &arena_cnt,
								                              &arena_max_dim);
							}
						}

						nd_seven_sort_billboards_by_size(arena_dst, arena_cnt);
						bool same_blk = (slot->valid && slot->bx0 == bx0 && slot->by0 == by0 &&
						                 slot->world == (int16_t)world);
						float fade_start_t = same_blk
							? slot->fade_start_t
							: ((s_frame_anim_time > 0.0f && s_frame_dispatch_count > 1u)
							   ? s_frame_anim_time : -10.0f);
						float blk_fade = seven_block_fade_alpha(fade_start_t);
						if (arena_max_dim > min_vis_sz && blk_fade > 0.0f) {
							for (uint16_t ci = 0; ci < arena_cnt; ci++) {
								float bsz = (arena_dst[ci].width > arena_dst[ci].height)
									? arena_dst[ci].width : arena_dst[ci].height;
								if (bsz <= min_vis_sz)
									break;
								if (b_frustum_state == 1) {
									float pad = 0.5f * bsz + 16.0f;
									if (!seven_test_frustum_xy(arena_dst[ci].x - pad, arena_dst[ci].y - pad,
									                           arena_dst[ci].x + pad, arena_dst[ci].y + pad,
									                           (float)lwx, (float)lwy,
									                           arena_dst[ci].z - 16.0f,
									                           arena_dst[ci].z + arena_dst[ci].height + 16.0f))
										continue;
								}
								seven_stream_faded_billboard(&arena_dst[ci], blk_fade);
							}
						}
						s_block_bb_arena_cursor += (uint64_t)arena_cnt;
						slot->bx0 = bx0;
						slot->by0 = by0;
						slot->bw = blk_w;
						slot->bh = blk_h;
						slot->node_ox = node->ox;
						slot->node_oy = node->oy;
						slot->node_rev = node->rev;
						slot->atlas_ver = cur_atlas_ver;
						slot->forget_seq = cur_forget_seq;
						slot->arena_seq = start_seq + (uint64_t)arena_cnt;
						slot->arena_off = cur_off;
						slot->max_dim = arena_max_dim;
						slot->fade_start_t = fade_start_t;
						slot->count = arena_cnt;
						slot->world = (int16_t)world;
						slot->rz = base_rz;
						slot->w_level = w_level;
						slot->node_level = node->level;
						slot->valid = 1;
						continue;
					}

					for (int32_t ry_grid = by0; ry_grid < by1; ry_grid++) {
						float ry_min_proj = ((cam_sy >= 0.0f) ? blk_wx0 : blk_wx1) * cam_sy
						                  + ((cam_cy >= 0.0f) ? (float)(ry_grid * NDW_TILE_H) : (float)((ry_grid + 1) * NDW_TILE_H)) * cam_cy;
						if (!near_flushed && cam_cy >= 0.0f && ry_min_proj >= near_max_proj - 0.5f) {
							if (s_mid_batch_len > 0) {
								nd_seven_gl_queue_billboards(s_mid_stream_batch, s_mid_batch_len, atlas_rgba());
								s_mid_batch_len = 0;
							}
							if (all_gpu_count > 0)
								nd_seven_gl_queue_billboards(s_all_gpu_batch, all_gpu_count, atlas_rgba());
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
		if (s_mid_batch_len > 0) {
			nd_seven_gl_queue_billboards(s_mid_stream_batch, s_mid_batch_len, atlas_rgba());
			s_mid_batch_len = 0;
		}
		nd_seven_set_scenery_synth_budget(0xFFFFFFFFu);
	}
	if (!near_flushed && all_gpu_count > 0) {
		nd_seven_gl_queue_billboards(s_all_gpu_batch, all_gpu_count, atlas_rgba());
	}
	nd_seven_gl_submit_billboards(atlas_rgba());
}

static nd_seven_perf_t s_perf = {0};

static double seven_perf_now_ms(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0.0;
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}

void nd_seven_perf_set_enabled(int enabled)
{
	s_perf.enabled = enabled ? 1u : 0u;
	nd_seven_ground_perf_set(enabled ? 1 : 0);
	if (s_seven_chunk_sys)
		ar_system_set_timing_enabled(s_seven_chunk_sys, enabled != 0);
}

int nd_seven_perf_get_enabled(void)
{
	return (int)s_perf.enabled;
}

const nd_seven_perf_t *nd_seven_perf_last(void)
{
	return &s_perf;
}

void nd_seven_perf_reset(void)
{
	uint32_t en = s_perf.enabled;
	memset(&s_perf, 0, sizeof(s_perf));
	s_perf.enabled = en;
	(void)nd_seven_ground_perf_take_probes();
}

void nd_seven_render(uint32_t wu, uint32_t hu, const nd_client_state_t *st)
{
	int w = (int)wu, h = (int)hu;
	static drawable_3d_t dr[MAX_DRAW];
	static lam_billboard_gpu_t s_all_gpu_batch[ALL_GPU_MAX_DRAW];

	if (w <= 0 || h <= 0)
		return;

	int perf_on = (s_perf.enabled != 0u);
	double t0 = 0.0, t_sync = 0.0, t_macro = 0.0, t_near = 0.0;
	double t_bot = 0.0, t_sort = 0.0, t_scen = 0.0, t_gwc = 0.0, t_end = 0.0;
	size_t ar_pop_before = 0;
	if (perf_on) {
		(void)nd_seven_ground_perf_take_probes();
		if (s_seven_chunk_sys) {
			ar_stats_t as;
			ar_system_get_stats(s_seven_chunk_sys, &as);
			ar_pop_before = as.sampler_invocations_total;
			ar_system_reset_frame_metrics(s_seven_chunk_sys);
		}
		t0 = seven_perf_now_ms();
	}

	nd_seven_sync_origin(st);
	if (perf_on)
		t_sync = seven_perf_now_ms();

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
	if (perf_on)
		t_macro = seven_perf_now_ms();

	int map_w = (st && st->map_data.width > 0) ? (int)st->map_data.width : 7;
	int map_h = (st && st->map_data.height > 0) ? (int)st->map_data.height : 7;
	int px = st ? (int)st->map_data.player_x : 3;
	int py = st ? (int)st->map_data.player_y : 3;

	int obj_j0 = -py;
	int obj_j1 = map_h - 1 - py;
	int eff_lamb_row = ndw_fdiv(nd_seven_get_ly(), NDW_TILE_H);
	if (eff_lamb_row < obj_j0) eff_lamb_row = obj_j0;
	if (eff_lamb_row > obj_j1) eff_lamb_row = obj_j1;

	float max_eff_sz = seven_max_eff_bb_size();
	seven_frame_ctx_t ctx = {
		.st = st,
		.eff_cam_dist = eff_cam_dist,
		.max_eff_sz = max_eff_sz,
		.max_scenery_cull = seven_billboard_cull_dist(max_eff_sz, max_eff_sz),
		.obj_i0 = -px,
		.obj_i1 = map_w - 1 - px,
		.obj_j0 = obj_j0,
		.obj_j1 = obj_j1,
		.eff_lamb_row = eff_lamb_row,
		.dr = dr,
		.batch = s_all_gpu_batch,
		.batch_count = 0,
	};

	seven_emit_near_room_billboards(&ctx);
	if (perf_on)
		t_near = seven_perf_now_ms();

	seven_emit_living_meadow(&ctx);
	seven_emit_forest_floor_logs(&ctx);
	seven_emit_medium_botanical(&ctx);
	seven_emit_large_undergrowth(&ctx);
	seven_emit_geobreak_props(&ctx);
	if (perf_on)
		t_bot = seven_perf_now_ms();

	seven_emit_particles_and_sort_batch(&ctx);
	if (perf_on)
		t_sort = seven_perf_now_ms();

	seven_emit_homogeneous_scenery(&ctx);
	if (perf_on)
		t_scen = seven_perf_now_ms();

	/* GPU-instanced 3D grass & clover carpet, then water pass over submerged sprites, clouds, and present */
	nd_seven_gl_flush_grass();
	nd_seven_gl_flush_water();
	nd_seven_gl_flush_clouds();
	if (perf_on)
		t_gwc = seven_perf_now_ms();

	nd_seven_gl_end_frame();
	if (perf_on) {
		t_end = seven_perf_now_ms();
		s_perf.frame_count++;
		s_perf.total_ms              = (float)(t_end - t0);
		s_perf.sync_ms               = (float)(t_sync - t0);
		s_perf.macro_backdrop_ms     = (float)(t_macro - t_sync);
		s_perf.near_room_ms          = (float)(t_near - t_macro);
		s_perf.botanical_ms          = (float)(t_bot - t_near);
		s_perf.sort_batch_ms         = (float)(t_sort - t_bot);
		s_perf.scenery_ms            = (float)(t_scen - t_sort);
		s_perf.grass_water_clouds_ms = (float)(t_gwc - t_scen);
		s_perf.end_frame_ms          = (float)(t_end - t_gwc);
		s_perf.flora_probes          = nd_seven_ground_perf_take_probes();
		nd_seven_gl_get_perf_counters(&s_perf.terrain_chunks,
		                              &s_perf.bb_flushes,
		                              &s_perf.mesh3d_count,
		                              &s_perf.bb_count);
		nd_seven_gl_get_occlusion_stats(NULL, NULL, NULL,
		                                &s_perf.water_queued,
		                                &s_perf.cloud_queued);
		if (s_seven_chunk_sys) {
			ar_stats_t as;
			ar_metrics_t am;
			ar_system_get_stats(s_seven_chunk_sys, &as);
			ar_system_get_metrics(s_seven_chunk_sys, &am);
			s_perf.ar_populates = (as.sampler_invocations_total >= ar_pop_before)
				? (uint32_t)(as.sampler_invocations_total - ar_pop_before) : 0u;
			s_perf.ar_active_chunks = (uint32_t)as.active_chunks_last_frame;
			s_perf.ar_nodes_visited = am.frame_cands_evaluated;
			s_perf.ar_nodes_frustum_culled = am.frame_frustum_culled;
			s_perf.ar_nodes_occ_culled = am.frame_occ_culled;
			s_perf.ar_leaves_emitted = (uint32_t)s_visible_leaf_count;
			s_perf.ar_evictions = am.frame_evictions;
			s_perf.ar_probe_calls = am.frame_probes;
			s_perf.ar_probe_avg_steps_x100 = (am.frame_map_lookups > 0u)
				? (uint32_t)(((uint64_t)am.frame_map_probe_steps * 100ULL) / (uint64_t)am.frame_map_lookups) : 0u;
			s_perf.ar_update_ms = (float)am.us_query_total * 0.001f;
			s_perf.ar_populate_ms = (float)am.us_populate * 0.001f;
			s_perf.ar_occ_ms = (float)(am.us_occ_raster + am.us_occ_test) * 0.001f;
		} else {
			s_perf.ar_populates = 0u;
			s_perf.ar_active_chunks = 0u;
			s_perf.ar_nodes_visited = 0u;
			s_perf.ar_nodes_frustum_culled = 0u;
			s_perf.ar_nodes_occ_culled = 0u;
			s_perf.ar_leaves_emitted = 0u;
			s_perf.ar_evictions = 0u;
			s_perf.ar_probe_calls = 0u;
			s_perf.ar_probe_avg_steps_x100 = 0u;
			s_perf.ar_update_ms = 0.0f;
			s_perf.ar_populate_ms = 0.0f;
			s_perf.ar_occ_ms = 0.0f;
		}
	}
}

uint32_t nd_seven_last_mid_billboard_count(void)
{
	return s_last_mid_gpu_count;
}

int nd_seven_last_mid_billboard(uint32_t idx, float *x, float *y, float *z, float *w, float *h)
{
#ifdef __wasm__
	(void)idx; (void)x; (void)y; (void)z; (void)w; (void)h;
	return 0;
#else
	if (idx >= s_last_mid_gpu_count || idx >= MID_MAX_DRAW)
		return 0;
	if (x) *x = s_mid_gpu_batch[idx].x;
	if (y) *y = s_mid_gpu_batch[idx].y;
	if (z) *z = s_mid_gpu_batch[idx].z;
	if (w) *w = s_mid_gpu_batch[idx].width;
	if (h) *h = s_mid_gpu_batch[idx].height;
	return 1;
#endif
}

uint32_t nd_seven_last_visible_leaf_count(void)
{
	return (uint32_t)s_front_leaf_count;
}

uint32_t nd_seven_last_ridge_culled_count(void)
{
	return s_last_ridge_culled_count;
}

uint32_t nd_seven_last_alt_planet_chunk_count(void)
{
	return s_last_alt_planet_chunk_count;
}

int nd_seven_last_visible_leaf_bounds(uint32_t idx, int32_t *ox, int32_t *oy, int32_t *w, int32_t *h)
{
	if (idx >= s_front_leaf_count)
		return 0;
	if (ox) *ox = s_front_leaf_bounds[idx].ox;
	if (oy) *oy = s_front_leaf_bounds[idx].oy;
	if (w)  *w  = s_front_leaf_bounds[idx].w;
	if (h)  *h  = s_front_leaf_bounds[idx].h;
	return 1;
}
