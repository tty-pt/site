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
	int16_t eff_wl = (level <= 1) ? fs.water_level : base_wl;
	bool eff_is_water = (level <= 1) ? fs.is_water : ((fs.elevation_q12_4 >> 4) < base_wl);
	int32_t eff_depth = eff_is_water ? ((int32_t)eff_wl - (fs.elevation_q12_4 >> 4)) : 0;
	if (eff_depth < 0) eff_depth = 0;
	out->attr[3] = (uint8_t)(eff_depth > 255 ? 255 : eff_depth);
	int32_t shore_ceil_q12_4 = ((int32_t)eff_wl + 4) << 4;
	out->flags = (eff_depth > 0 || (int32_t)fs.elevation_q12_4 <= shore_ceil_q12_4) ? 1 : 0;
	if (out->flags) {
		uint16_t wl_u16 = (uint16_t)eff_wl;
		out->attr[4] = (uint8_t)(wl_u16 & 0xFFu);
		out->attr[5] = (uint8_t)(wl_u16 >> 8);
	}

	uint32_t c1 = ndw_biome_base_rgb(fs.bio);
	uint32_t c2 = ndw_biome_base_rgb(fs.bio2);
	uint8_t blend = fs.blend;
	uint32_t col = (blend > 0 && fs.bio != fs.bio2)
		? macro_blend_rgb(c1, c2, (blend * 255) / 128) : c1;

	/* Smooth world-coordinate hypsometric + band-limited meso relief tinting:
	 * uses only continuous rotated fBm (fs.fine_elev) and elevation (elev_rel) — zero
	 * uncorrelated per-vertex white-noise hashes that would cause bilinear diamonds. */
	{
		int fine_rel = (int)fs.fine_elev;
		int elev_rel = ((int)fs.elevation_q12_4 - ((int)fs.water_level << 4)) >> 4;
		if (elev_rel < -32) elev_rel = -32;
		if (elev_rel > 64) elev_rel = 64;
		int shade_q8 = 250 + (elev_rel >> 2) + fine_rel;

		int r = (int)(col & 0xFF);
		int g = (int)((col >> 8) & 0xFF);
		int b = (int)((col >> 16) & 0xFF);
		r = (r * shade_q8) >> 8;
		g = (g * (shade_q8 + (fine_rel >> 2))) >> 8;
		b = (b * (shade_q8 - (elev_rel >> 4) - (fine_rel >> 2))) >> 8;
		if (r < 0) r = 0; else if (r > 255) r = 255;
		if (g < 0) g = 0; else if (g > 255) g = 255;
		if (b < 0) b = 0; else if (b > 255) b = 255;
		col = 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
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
	int16_t ground_elev = nd_seven_ground_elevation(wx, wy);
	if (st && st->map_data.width > 0) {
		uint16_t world = (uint16_t)st->map_data.pos[3];
		int16_t w_level = 0;
		if (seven_has_water_at(world, wx, wy, &w_level) && ground_elev < w_level) {
			/* Buoyancy: swimming elevation in deep water keeps the lamb realistically half-submerged */
			int16_t swim_floor = (int16_t)(w_level - 5);
			if (ground_elev < swim_floor)
				return swim_floor;
		}
	}
	return ground_elev;
}

float nd_seven_lamb_elevation_f(const nd_client_state_t *st, int32_t wx, int32_t wy)
{
	float ground_elev_f = nd_seven_ground_elevation_f(wx, wy);
	if (st && st->map_data.width > 0) {
		uint16_t world = (uint16_t)st->map_data.pos[3];
		int16_t w_level = 0;
		if (seven_has_water_at(world, wx, wy, &w_level) && ground_elev_f < (float)w_level) {
			float swim_floor = (float)(w_level - 5);
			if (ground_elev_f < swim_floor)
				return swim_floor;
		}
	}
	return ground_elev_f;
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
	{ .world_id = 0, .center = { 0.0f, 0.0f, 0.0f },               .major_r = 2000000.0f, .minor_r = 1335000.0f },
	{ .world_id = 1, .center = { 0.0f, 0.0f, 0.0f },               .major_r = 2000000.0f, .minor_r = 1335000.0f }, /* Citadel (Large Torus) */
	{ .world_id = 2, .center = { 4500000.0f, -14000000.0f, -8500000.0f }, .major_r =  800000.0f, .minor_r =  500000.0f }, /* Aethelgard / Moon (Far Behind Main Planet) */
};

const nd_planet_def_t *nd_seven_get_planet(uint16_t world)
{
	for (size_t i = 0; i < sizeof(s_planets) / sizeof(s_planets[0]); i++) {
		if (s_planets[i].world_id == world)
			return &s_planets[i];
	}
	return &s_planets[0];
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
	ar_system_t *cs = nd_seven_get_chunk_sys();
	if (!cs) {
		s_visible_leaf_count = 0;
		s_frame_frustum_valid = 0;
		return;
	}

	int64_t lwx = 0, lwy = 0;
	nd_seven_world_pos(st, &lwx, &lwy);
	uint16_t world = (uint16_t)(st ? st->map_data.pos[3] : 0);
	const nd_planet_def_t *pdef = nd_seven_get_planet(world);

	float dist = (eff_cam_dist > 1.0f) ? eff_cam_dist : ND_SEVEN_CAM_DIST;
	float lwz_est = nd_seven_ground_elevation_f((int32_t)lwx, (int32_t)lwy);

	/* Compute canonical left-handed flat tangent-space MVP for libareole culling */
	float mvp[16];
	nd_seven_gl_compute_flat_mvp((uint32_t)w, (uint32_t)h, (float)lwx, (float)lwy, lwz_est,
	                             dist, ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_FOV, mvp);
	ar_frustum_from_matrix(&s_frame_frustum, mvp);
	s_frame_frustum_valid = 1;

	float r_planet = 0.0f;
	float z_min_cull = lwz_est - 96.0f;
	float z_max_cull = lwz_est + 96.0f;
	float cull_margin = 48.0f;
	if (dist >= 1200.0f) {
		r_planet = dist * 1.25f;
		if (r_planet < 16000.0f) r_planet = 16000.0f;
		if (r_planet > 80000.0f) r_planet = 80000.0f;
		z_min_cull = -2000.0f;
		z_max_cull = 2000.0f;
		cull_margin = 150.0f;
	}
	s_frame_sag_radius = r_planet;

	float target_quad_px = 2.0f * (32.0f / 48.0f);
	size_t max_leaves = SEVEN_MAX_VISIBLE_LEAVES;
	uint8_t min_lvl = 0;

	/* Continuous logarithmic LOD stepping function from atmosphere through orbital space:
	 * replaces discrete switch-cases with a single smooth log2(dist / 2000) scale law. */
	if (dist >= 2500.0f) {
		float atmo_t = fminf(1.0f, fmaxf(0.0f, (dist - 2500.0f) / 42000.0f));
		target_quad_px = 1.80f + 10.20f * atmo_t;
		max_leaves = (size_t)(96.0f - 56.0f * atmo_t);
		int lvl_step = (int)floorf(log2f(dist * (1.0f / 2000.0f)));
		if (lvl_step < 1) lvl_step = 1;
		if (lvl_step > 6) lvl_step = 6;
		min_lvl = (uint8_t)lvl_step;
	}

	ar_cull_opts_t opts = {
		.z_min = z_min_cull,
		.z_max = z_max_cull,
		.sag_radius = r_planet,
		.focal_x = (float)lwx,
		.focal_y = (float)lwy,
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

	s_visible_leaf_count = ar_system_query_matrix(cs, world, mvp, &opts, s_visible_leaves, max_leaves);
	size_t macro_count = s_visible_leaf_count;

	/* Re-evaluate focal elevation on the newly active View 0 leaves */
	float lwz = nd_seven_ground_elevation_f((int32_t)lwx, (int32_t)lwy);
	float lamb_light_z = nd_seven_lamb_elevation_f(st, (int32_t)lwx, (int32_t)lwy);
	float anim_t = nd_seven_get_anim_time();

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

			if (lamb_in_water && walking && dist < 1800.0f) {
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
		if (node->level <= 1 && (node->flags & 1) != 0 && node->samples &&
		    node->min_val > ((int32_t)(w_lvl_i + 8) << 4)) {
			uint32_t vcnt = node->vert_count;
			for (uint32_t vi = 0; vi < vcnt; vi++) {
				if (node->samples[vi].flags & 1) {
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

	/* Multi-Planet Rendering: When in orbital/space view, dispatch the full 4x4 Level 14 torus
	 * of the secondary celestial body (located far behind the main planet) */
	if (zoom <= 0.05f) {
		uint16_t alt_world = (world == 2) ? 1 : 2;
		const nd_planet_def_t *alt_pdef = nd_seven_get_planet(alt_world);
		if (alt_pdef) {
			ar_cull_opts_t alt_opts = opts;
			alt_opts.view_id = 2;
			alt_opts.min_level = 14;
			alt_opts.max_level = 14;
			const ar_node_t *alt_visible[16];
			size_t alt_count = ar_system_query_matrix(cs, alt_world, mvp, &alt_opts, alt_visible, 16);
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
				                                (float)alt_wl_i, alt_pdef);
			}
		}
	}

	/* Flush deferred water queue across all active chunks in pure C */
	nd_seven_gl_flush_water();
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
	uint32_t alpha_byte = base_tint & 0xFFu;
	bb->tint_rgba = base_tint;
	bb->style = ND_BB_STYLE_STANDARD;
	bb->shadow_rx = 0;
	bb->shadow_ry = 0;
	bb->pad0 = 0;
	if (!o)
		return;

	switch (o->kind) {
	case NDW_K_DECID:
	case NDW_K_TALL:
		bb->shadow_rx = (uint8_t)(o->big ? 14 : 10);
		bb->shadow_ry = (uint8_t)(o->big ? 6 : 4);
		if (!sr || sr->y < ND_SEVEN_ATLAS_H_PX) {
			tint_t pt;
			plant_tint(o->fg, &pt);
			bb->tint_rgba = ((uint32_t)(pt.r & 0xFF) << 24) |
			                ((uint32_t)(pt.g & 0xFF) << 16) |
			                ((uint32_t)(pt.b & 0xFF) << 8)  |
			                alpha_byte;
			bb->style = ND_BB_STYLE_LEAF_TINT;
		} else {
			bb->style = ND_BB_STYLE_FLORA;
		}
		break;
	case NDW_K_FIR:
		bb->shadow_rx = (uint8_t)(o->big ? 10 : 7);
		bb->shadow_ry = 4;
		bb->style = ND_BB_STYLE_FLORA;
		break;
	case NDW_K_CACTUS:
		bb->shadow_rx = (uint8_t)(o->big ? 9 : 6);
		bb->shadow_ry = 4;
		bb->style = ND_BB_STYLE_FLORA;
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
		}
	}
	*out_wl = local_wl;
}

static int seven_emit_room_billboards(const nd_client_state_t *st, const ar_node_t *node,
                                      const int16_t pos[4], int16_t w_level,
                                      uint32_t tint)
{
	if (tint == 0)
		return 0;

	ndw_obj_t scratch_objs[NDW_MAX_OBJ];
	int n_raw = 0;
	const ndw_obj_t *raw_objs = nd_seven_resolve_room_objects_fast(st, pos, scratch_objs, &n_raw);
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
		bb.width = (float)sr->w;
		bb.height = (float)sr->h;
		bb.anchor_y = (float)ndw_obj_anchor_y(&raw_objs[k], sr->h);
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

		s_mid_stream_batch[s_mid_batch_len++] = bb;
		if (s_last_mid_gpu_count < MID_MAX_DRAW)
			s_mid_gpu_batch[s_last_mid_gpu_count] = bb;
		s_last_mid_gpu_count++;
		emitted++;
	}
	return emitted;
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

#define MAX_DRAW 192
#define ALL_GPU_MAX_DRAW 2048

void nd_seven_render(uint32_t wu, uint32_t hu, const nd_client_state_t *st)
{
	int w = (int)wu, h = (int)hu;
	static drawable_3d_t dr[MAX_DRAW];
	static lam_billboard_gpu_t s_all_gpu_batch[ALL_GPU_MAX_DRAW];

	if (w <= 0 || h <= 0)
		return;

	atlas_load();
	nd_seven_sync_origin(st);

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
					bw = (float)sr_val.w;
					bh = (float)sr_val.h;
					bay = (float)ndw_obj_anchor_y(&dr[a].obj, sr_val.h);
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
					dst_bb->tint_rgba = tint_b;
					dst_bb->style = nd_seven_is_universe_owner(st)
					                ? ND_BB_STYLE_GLOW : ND_BB_STYLE_STANDARD;
					dst_bb->shadow_rx = 8;
					dst_bb->shadow_ry = 4;
					dst_bb->pad0 = 0;
				} else {
					dst_bb->tint_rgba = tint_b;
					dst_bb->style = (entity_spr >= NDS_ROCK_TAN_BIG && entity_spr <= NDS_STONE_TINY)
					                ? ND_BB_STYLE_MINERAL : ND_BB_STYLE_STANDARD;
					dst_bb->shadow_rx = (uint8_t)(bw * 0.34f > 4.0f ? bw * 0.34f : 4.0f);
					dst_bb->shadow_ry = (uint8_t)(dst_bb->shadow_rx * 0.45f > 2.0f ? dst_bb->shadow_rx * 0.45f : 2.0f);
					dst_bb->pad0 = 0;
				}
				all_gpu_count++;
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
	 * splashing around the lamb's feet render in front of the 32x32 lamb sprite instead of behind it. */
	if (all_gpu_count > 0) {
		for (uint32_t a = 1; a < all_gpu_count; a++) {
			lam_billboard_gpu_t k = s_all_gpu_batch[a];
			float ky = k.y + ((k.style == ND_BB_STYLE_PARTICLE) ? 14.0f : 0.0f);
			int32_t b = (int32_t)a - 1;
			while (b >= 0) {
				float by = s_all_gpu_batch[b].y + ((s_all_gpu_batch[b].style == ND_BB_STYLE_PARTICLE) ? 14.0f : 0.0f);
				if (by < ky || (by == ky && s_all_gpu_batch[b].x <= k.x))
					break;
				s_all_gpu_batch[b + 1] = s_all_gpu_batch[b];
				b--;
			}
			s_all_gpu_batch[b + 1] = k;
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

			for (int32_t by0 = cy0; by0 < cy1; by0 += 8) {
				int32_t by1 = (by0 + 8 < cy1) ? (by0 + 8) : cy1;
				for (int32_t bx0 = cx0; bx0 < cx1; bx0 += 8) {
					int32_t bx1 = (bx0 + 8 < cx1) ? (bx0 + 8) : cx1;
					if (s_frame_frustum_valid) {
						float bmin_x = (float)(bx0 * NDW_TILE_W) - 384.0f;
						float bmax_x = (float)(bx1 * NDW_TILE_W) + 384.0f;
						float bmin_y = (float)(by0 * NDW_TILE_H) - 384.0f;
						float bmax_y = (float)(by1 * NDW_TILE_H) + 384.0f;
						if (!seven_test_frustum_xy(bmin_x, bmin_y, bmax_x, bmax_y, (float)lwx, (float)lwy, nz_min, nz_max))
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
							int ti = (int)pos[0] - (int)base_rx;
							int tj = (int)base_ry - (int)pos[1];
							if (ti >= obj_i0 && ti <= obj_i1 && tj >= obj_j0 && tj <= obj_j1)
								continue; /* Covered by near-field server window */

							if (s_frame_frustum_valid) {
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
	nd_seven_gl_flush_clouds();
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
