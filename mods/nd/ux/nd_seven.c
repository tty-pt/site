#include <string.h>
#include <math.h>
#include <ttypt/lam.h>
#include <ttypt/areole.h>
#include <nd/seven-world.h>
#include <nd/field.h>
#include "nd_seven.h"
#include "nd_seven_atlas_draw.h"
#include "nd_seven_ground.h"
#include "nd_seven_motion.h"
#include "nd_seven_objects.h"
#include "nd_seven_gl.h"

/*
 * The seven stage: a continuous world drawn from the NeverDark map.
 *
 * Every map tile is a small block (nd/seven-world.h). The blocks are laid
 * side by side as far as the framebuffer reaches and the camera follows the
 * lamb, so there are no rooms to enter and leave: crossing a block border just
 * asks the server for the matching move (h/j/k/l), and when the new map
 * arrives the origin tile shifts under the lamb without it moving on screen.
 */

/* --- macro chunk hierarchy integration ---------------------------------- */

static ar_system_t *s_seven_chunk_sys = NULL;

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
	/* Keep lod=3 for levels 0..3 so near/mid chunks evaluate the exact same heightmap function */
	uint8_t lod = (level >= 8) ? 0 : ((level >= 6) ? 1 : ((level >= 4) ? 2 : 3));
	nd_field_sample_t fs = nd_field_sample(world, lod, wx, wy);
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
	out->flags = (water_depth > 0) ? 1 : 0;

	uint32_t c1 = ndw_biome_base_rgb(fs.bio);
	uint32_t c2 = ndw_biome_base_rgb(fs.bio2);
	uint8_t blend = fs.blend;
	uint32_t col = (blend > 0 && fs.bio != fs.bio2)
		? macro_blend_rgb(c1, c2, (blend * 255) / 128) : c1;

	if (water_depth > 0 && !nd_seven_get_3d_mode()) {
		col = water_color_at_depth((int)water_depth, 128, 128, 128);
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
		cfg.sampler = seven_macro_chunk_sampler;
		cfg.max_active = 96;
		cfg.pool_capacity = 256;
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

/* --- drawing ------------------------------------------------------------ */

enum { D_OBJ, D_ENTITY, D_LAMB };
typedef struct {
	int sx, sy;            /* screen position of the feet */
	int32_t wx, wy;        /* world position */
	int16_t wz;            /* ground elevation (block units) */
	float z_f;             /* continuous sub-pixel elevation */
	uint8_t what;
	ndw_obj_t obj;         /* D_OBJ */
	char ch;               /* D_ENTITY */
	uint8_t fg;
	uint8_t sprite_id;     /* if non-zero, drawn as rock/stone sprite instead of pawn */
	uint8_t flip;
	char name[32];         /* name rendered on top of the object */
} drawable_t;

#define MAX_DRAW 192

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
	/* Generic atlas sprite mapping: if sprite_id is a valid NDS_* sprite index, return directly */
	if (sprite_id > 0 && sprite_id < NDS_COUNT)
		return sprite_id;
	return 0;
}

static int drawable_top_y(const drawable_t *d)
{
	if (d->sprite_id > 0 && d->sprite_id < NDS_COUNT) {
		const nd_seven_sprite_rect_t *sr = &nd_seven_sprite_rects[d->sprite_id];
		return d->sy - sr->h * SCALE;
	}
	if (d->what == D_ENTITY) {
		return d->sy - 28 * SCALE;
	}
	if (d->what == D_OBJ) {
		int spr = nd_seven_obj_sprite(&d->obj);
		if (spr > 0 && spr < NDS_COUNT) {
			return d->sy - nd_seven_sprite_rects[spr].h * SCALE;
		}
		switch (d->obj.kind) {
		case NDW_K_FIR:
			return d->sy - (d->obj.big ? 38 : 28) * SCALE;
		case NDW_K_CACTUS:
			return d->sy - (d->obj.big ? 38 : 27) * SCALE;
		case NDW_K_STONE:
			return d->sy - 4 * SCALE;
		case NDW_K_ROCK:
			return d->sy - 9 * SCALE;
		case NDW_K_SHOP:
			return d->sy - 22 * SCALE;
		case NDW_K_STAIRS_DOWN:
		case NDW_K_STAIRS_UP:
			return d->sy - 8 * SCALE;
		case NDW_K_POND:
			return d->sy - 4 * SCALE;
		default:
			return d->sy - 16 * SCALE;
		}
	}
	return d->sy - 16 * SCALE;
}

int16_t nd_seven_lamb_elevation(const nd_client_state_t *st, int32_t wx, int32_t wy)
{
	int16_t ground_elev = nd_seven_ground_elevation(wx, wy);
	if (st && st->map_data.width > 0) {
		uint16_t world = (uint16_t)st->map_data.pos[3];
		int16_t w_level = nd_field_water_level(world);
		if (w_level > 0 && ground_elev < w_level) {
			int ti = ndw_fdiv(wx, NDW_TILE_W);
			int pos_y = -ndw_fdiv(wy, NDW_TILE_H);
			int16_t cpos[4] = { (int16_t)ti, (int16_t)pos_y, 0, (int16_t)world };
			const nd_room_chunk_t *chk = nd_seven_chunk_find(cpos);
			if (chk && chk->has_water) {
				/* Buoyancy: swimming elevation in deep water keeps the lamb realistically half-submerged */
				int16_t swim_floor = (int16_t)(w_level - 8);
				if (ground_elev < swim_floor)
					return swim_floor;
			}
		}
	}
	return ground_elev;
}

float nd_seven_lamb_elevation_f(const nd_client_state_t *st, int32_t wx, int32_t wy)
{
	float ground_elev_f = nd_seven_ground_elevation_f(wx, wy);
	if (st && st->map_data.width > 0) {
		uint16_t world = (uint16_t)st->map_data.pos[3];
		int16_t w_level = nd_field_water_level(world);
		if (w_level > 0 && ground_elev_f < (float)w_level) {
			int ti = ndw_fdiv(wx, NDW_TILE_W);
			int pos_y = -ndw_fdiv(wy, NDW_TILE_H);
			int16_t cpos[4] = { (int16_t)ti, (int16_t)pos_y, 0, (int16_t)world };
			const nd_room_chunk_t *chk = nd_seven_chunk_find(cpos);
			if (chk && chk->has_water) {
				float swim_floor = (float)(w_level - 8);
				if (ground_elev_f < swim_floor)
					return swim_floor;
			}
		}
	}
	return ground_elev_f;
}

/* Celestial Body Definitions */
static const nd_planet_def_t s_planets[] = {
	{ .world_id = 0, .center = { 0.0f, 0.0f, 0.0f },             .major_r = 2000000.0f, .minor_r = 1335000.0f },
	{ .world_id = 1, .center = { 0.0f, 0.0f, 0.0f },             .major_r = 2000000.0f, .minor_r = 1335000.0f }, /* Citadel (Large Torus) */
	{ .world_id = 2, .center = { 6000000.0f, 3500000.0f, 2000000.0f }, .major_r =  800000.0f, .minor_r =  500000.0f }, /* Aethelgard / Moon (Medium Torus) */
};

const nd_planet_def_t *nd_seven_get_planet(uint16_t world)
{
	for (size_t i = 0; i < sizeof(s_planets) / sizeof(s_planets[0]); i++) {
		if (s_planets[i].world_id == world)
			return &s_planets[i];
	}
	return &s_planets[0];
}

#define SEVEN_MAX_VISIBLE_LEAVES 96
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

/* Uniform altitude fade: all scenery fades out simultaneously across the screen
 * starting around -20x (24,000), remaining visible at -22x (~37,947), and fully
 * dissolving before -23x (44,000) with zero radial distance rings. */
#define SCENERY_ALT_FADE_START 24000.0f
#define SCENERY_ALT_FADE_END   44000.0f
#define SCENERY_RAD_MAX_ROOMS   250

static inline float seven_smooth_fade(float val, float start, float end)
{
	if (val <= start)
		return 1.0f;
	if (val >= end)
		return 0.0f;
	float t = (val - start) / (end - start);
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
	ar_system_t *cs = nd_seven_get_chunk_sys();
	if (!cs) {
		s_visible_leaf_count = 0;
		s_frame_frustum_valid = 0;
		return;
	}

	int64_t lwx = 0, lwy = 0;
	nd_seven_world_pos(st, &lwx, &lwy);
	float lwz = nd_seven_ground_elevation_f((int32_t)lwx, (int32_t)lwy);
	uint16_t world = (uint16_t)(st ? st->map_data.pos[3] : 0);
	const nd_planet_def_t *pdef = nd_seven_get_planet(world);

	float dist = (eff_cam_dist > 1.0f) ? eff_cam_dist : ND_SEVEN_CAM_DIST;

	/* Compute canonical left-handed flat tangent-space MVP for libareole culling */
	float mvp[16];
	nd_seven_gl_compute_flat_mvp((uint32_t)w, (uint32_t)h, (float)lwx, (float)lwy, lwz,
	                             dist, ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_FOV, mvp);
	ar_frustum_from_matrix(&s_frame_frustum, mvp);
	s_frame_frustum_valid = 1;

	float r_planet = 0.0f;
	float z_min_cull = lwz - 80.0f;
	float z_max_cull = lwz + 80.0f;
	float cull_margin = 64.0f;
	if (dist >= 1200.0f) {
		r_planet = dist * 1.25f;
		if (r_planet < 16000.0f) r_planet = 16000.0f;
		if (r_planet > 80000.0f) r_planet = 80000.0f;
		z_min_cull = -2000.0f;
		z_max_cull = 2000.0f;
		cull_margin = 150.0f;
	}
	s_frame_sag_radius = r_planet;

	ar_cull_opts_t opts = {
		.z_min = z_min_cull,
		.z_max = z_max_cull,
		.sag_radius = r_planet,
		.focal_x = (float)lwx,
		.focal_y = (float)lwy,
		.margin = cull_margin,
		.target_quad_px = 2.0f,
		.viewport_w = (float)w,
		.viewport_h = (float)h,
		.min_level = 0,
		.max_level = 16
	};

	s_visible_leaf_count = ar_system_query_matrix(cs, world, mvp, &opts, s_visible_leaves, SEVEN_MAX_VISIBLE_LEAVES);
	size_t macro_count = s_visible_leaf_count;

	/* Begin 3D WebGL frame in pure C */
	nd_seven_gl_begin_frame((uint32_t)w, (uint32_t)h, (float)lwx, (float)lwy, lwz,
	                        dist, ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_FOV,
	                        (float)nd_seven_get_anim() * ND_SEVEN_ANIM_TIME_FACTOR, pdef);

	for (size_t m = 0; m < macro_count; m++) {
		const ar_node_t *node = s_visible_leaves[m];
		if (!node) continue;
		((ar_node_t *)node)->active_view_mask |= 1;
		((ar_node_t *)node)->last_touch_frame = nd_seven_get_anim();

		bool has_water = (node->flags & 1) != 0;
		float sea_lvl = has_water ? (float)nd_field_water_level(world) : ND_SEVEN_SEA_LEVEL_NONE;

		nd_seven_gl_flush_terrain_chunk(world, node->level, node->rev,
		                                node->ox, node->oy, node->w, node->h,
		                                node->rgba, node->values,
		                                AR_DEFAULT_VERT_DIM, AR_DEFAULT_VERT_DIM,
		                                (uint32_t)w, (uint32_t)h,
		                                sea_lvl, pdef);
	}

	/* Multi-Planet Rendering: When in high-altitude/orbit space view, dispatch secondary celestial bodies */
	if (zoom <= 0.05f) {
		uint16_t alt_world = (world == 1) ? 2 : 1;
		const nd_planet_def_t *alt_pdef = nd_seven_get_planet(alt_world);
		if (alt_pdef) {
			const ar_node_t *alt_visible[AR_DEFAULT_MAX_ACTIVE];
			size_t alt_count = ar_system_query_matrix(cs, alt_world, mvp, &opts, alt_visible, 16);
			for (size_t m = 0; m < alt_count; m++) {
				const ar_node_t *node = alt_visible[m];
				if (!node) continue;
				bool has_water = (node->flags & 1) != 0;
				float sea_lvl = has_water ? (float)nd_field_water_level(alt_world) : ND_SEVEN_SEA_LEVEL_NONE;

				nd_seven_gl_flush_terrain_chunk(alt_world, node->level, node->rev,
				                                node->ox, node->oy, node->w, node->h,
				                                node->rgba, node->values,
				                                AR_DEFAULT_VERT_DIM, AR_DEFAULT_VERT_DIM,
				                                (uint32_t)w, (uint32_t)h,
				                                sea_lvl, alt_pdef);
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

	/* Homogeneous 1:1 room emission: every room emits its objects with the exact
	 * same rule and uniform altitude alpha across the entire visible screen. */
	for (int k = 0; k < n_raw; k++) {
		nd_seven_sprite_rect_t sr_val;
		if (!nd_seven_obj_rect(&raw_objs[k], &sr_val))
			continue;
		const nd_seven_sprite_rect_t *sr = &sr_val;

		int32_t wx = base_wx + raw_objs[k].x;
		int32_t wy = base_wy + raw_objs[k].y;

		int32_t gx_v = (int32_t)(((int64_t)(wx - node->ox) * (int64_t)node->grid_dim) / node->w);
		int32_t gy_v = (int32_t)(((int64_t)(wy - node->oy) * (int64_t)node->grid_dim) / node->h);
		if (gx_v < 0) gx_v = 0;
		else if (gx_v > (int32_t)node->grid_dim) gx_v = (int32_t)node->grid_dim;
		if (gy_v < 0) gy_v = 0;
		else if (gy_v > (int32_t)node->grid_dim) gy_v = (int32_t)node->grid_dim;

		int16_t elev_q = node->values[gy_v * node->vert_dim + gx_v];
		int16_t wz = nd_field_elevation_to_block(elev_q);
		if (w_level > 0 && wz <= w_level &&
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
		if (flip) {
			bb.u0 = (float)(sr->x + sr->w) / (float)ND_SEVEN_ATLAS_W;
			bb.u1 = (float)sr->x / (float)ND_SEVEN_ATLAS_W;
		} else {
			bb.u0 = (float)sr->x / (float)ND_SEVEN_ATLAS_W;
			bb.u1 = (float)(sr->x + sr->w) / (float)ND_SEVEN_ATLAS_W;
		}
		bb.v0 = (float)sr->y / (float)ND_SEVEN_RUNTIME_ATLAS_H;
		bb.v1 = (float)(sr->y + sr->h) / (float)ND_SEVEN_RUNTIME_ATLAS_H;
		bb.tint_rgba = tint;

		s_mid_stream_batch[s_mid_batch_len++] = bb;
		if (s_last_mid_gpu_count < MID_MAX_DRAW)
			s_mid_gpu_batch[s_last_mid_gpu_count] = bb;
		s_last_mid_gpu_count++;
		emitted++;
	}
	return emitted;
}

void nd_seven_render(uint32_t *fb, uint32_t wu, uint32_t hu, const nd_client_state_t *st)
{
	int w = (int)wu, h = (int)hu;
	int camx, camy, i0, i1, j0, j1;
	int lamb_row;
	static drawable_t dr[MAX_DRAW];

	if (w <= 0 || h <= 0)
		return;
	atlas_load();
	nd_seven_sync_origin(st);
	nd_seven_ground_frame_begin();

	float zoom = nd_seven_get_zoom();
	float eff_scale = (float)SCALE * zoom;
	float eff_cam_dist = ND_SEVEN_CAM_DIST / zoom;
	int is_3d = nd_seven_get_3d_mode();

	/* Software fb clear is only needed when a software framebuffer is provided (2D fallback / SSR) */
	if (fb) {
		for (int i = 0; i < w * h; i++)
			fb[i] = 0xFF08080C;
	}

	int spr_scale = (int)(eff_scale + 0.5f);
	if (spr_scale < 1) spr_scale = 1;

	camx = (int)((float)nd_seven_get_lx() * eff_scale) - w / 2;
	camy = (int)((float)nd_seven_get_ly() * eff_scale) - h / 2;

	int tile_w_eff = (int)((float)(NDW_TILE_W * SCALE) * zoom);
	int tile_h_eff = (int)((float)(NDW_TILE_H * SCALE) * zoom);
	if (tile_w_eff < 1) tile_w_eff = 1;
	if (tile_h_eff < 1) tile_h_eff = 1;

	i0 = ndw_fdiv(camx, tile_w_eff) - 2;
	i1 = ndw_fdiv(camx + w - 1, tile_w_eff) + 2;
	j0 = ndw_fdiv(camy, tile_h_eff) - 2;
	j1 = ndw_fdiv(camy + h - 1, tile_h_eff) + 2;
	if (i0 < -45) i0 = -45;
	if (i1 >  45) i1 =  45;
	if (j0 < -35) j0 = -35;
	if (j1 >  35) j1 =  35;
	lamb_row = ndw_fdiv(nd_seven_get_ly(), NDW_TILE_H);

	/* Continuous Terrain: Dispatch multi-scale leaf chunks from libchunk with zero overdraw */
	seven_dispatch_macro_backdrop(w, h, st, zoom, eff_cam_dist);

	if (fb) {
		for (int tj = j0; tj <= j1; tj++) {
			for (int ti = i0; ti <= i1; ti++) {
				ndw_tile_t t;
				int16_t pos[4];
				ndw_rect_t walls[NDW_MAX_WALL];
				int tx = (int)((float)(ti * NDW_TILE_W) * eff_scale) - camx;
				int ty = (int)((float)(tj * NDW_TILE_H) * eff_scale) - camy;

				nd_seven_tile_at(st, ti, tj, &t, pos);
				nd_seven_ground_tile_render(fb, w, h, st, ti, tj, pos, &t, tx, ty);

				/* Sub-pixel wall culling at distant zoom (2D software fallback only; 3D renders geometry) */
				if (eff_scale >= 0.25f) {
					for (int k = ndw_walls(&t, walls) - 1; k >= 0; k--)
						for (int cy = walls[k].y0 / NDW_CELL_PX; cy * NDW_CELL_PX < walls[k].y1; cy++)
							for (int cx = walls[k].x0 / NDW_CELL_PX; cx * NDW_CELL_PX < walls[k].x1; cx++)
								blit_all(fb, w, h, NDS_WALL + (int)((cx * 7 + cy * 13 + (cx ^ cy)) & 3), 0, 0,
								         tx + (int)((float)(cx * NDW_CELL_PX) * eff_scale),
								         ty + (int)((float)(cy * NDW_CELL_PX) * eff_scale),
								         spr_scale, NULL);
				}
			}
		}
	}

	int map_w = (st && st->map_data.width > 0) ? (int)st->map_data.width : 7;
	int map_h = (st && st->map_data.height > 0) ? (int)st->map_data.height : 7;
	int px = st ? (int)st->map_data.player_x : 3;
	int py = st ? (int)st->map_data.player_y : 3;

	int obj_i0 = -px;
	int obj_i1 = map_w - 1 - px;
	int obj_j0 = -py;
	int obj_j1 = map_h - 1 - py;

	int eff_lamb_row = lamb_row;
	if (eff_lamb_row < obj_j0) eff_lamb_row = obj_j0;
	if (eff_lamb_row > obj_j1) eff_lamb_row = obj_j1;

	#define ALL_GPU_MAX_DRAW 2048
	static lam_billboard_gpu_t s_all_gpu_batch[ALL_GPU_MAX_DRAW];
	uint32_t all_gpu_count = 0;

	for (int tj = obj_j0; tj <= obj_j1; tj++) {
		int n = 0;

		for (int ti = obj_i0; ti <= obj_i1 && n < MAX_DRAW - NDW_MAX_OBJ; ti++) {
			nd_spatial_tile_t sp;
			if (!nd_spatial_tile_resolve(st, ti, tj, &sp))
				continue;
			int tx = (int)((float)(ti * NDW_TILE_W) * eff_scale) - camx;
			int ty = (int)((float)(tj * NDW_TILE_H) * eff_scale) - camy;

			for (int k = 0; k < sp.obj_count && n < MAX_DRAW; k++) {
				if (sp.objs[k].harvested)
					continue;
				/* Sub-pixel scenery object culling at distant zoom (2D software fallback only; 3D renders geometry) */
				if (!is_3d && !sp.objs[k].is_entity && eff_scale < 0.25f)
					continue;

				int32_t base_wx = (int32_t)sp.pos[0] * NDW_TILE_W;
				int32_t base_wy = -(int32_t)sp.pos[1] * NDW_TILE_H;

				if (sp.objs[k].is_entity) {
					dr[n].what = D_ENTITY;
					dr[n].ch = (char)sp.objs[k].glyph;
					dr[n].fg = sp.objs[k].fg;
					dr[n].sx = tx + (int)((float)sp.objs[k].x * eff_scale);
					dr[n].sy = ty + (int)((float)sp.objs[k].y * eff_scale);
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
					dr[n].obj.plant_id = sp.objs[k].plant_id;
					dr[n].obj.col_mask = sp.objs[k].col_mask;
					dr[n].obj.glyph = sp.objs[k].glyph;
					dr[n].obj.fg = sp.objs[k].fg;
					dr[n].obj.big = sp.objs[k].big;
					dr[n].sx = tx + (int)((float)sp.objs[k].x * eff_scale);
					dr[n].sy = ty + (int)((float)sp.objs[k].y * eff_scale);
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
			dr[n].sx = w / 2;
			dr[n].sy = h / 2;
			dr[n].wx = (int32_t)lwx;
			dr[n].wy = (int32_t)lwy;
			float lamb_f = nd_seven_lamb_elevation_f(st, (int32_t)lwx, (int32_t)lwy);
			dr[n].wz = (int16_t)lamb_f;
			dr[n].z_f = lamb_f;
			dr[n].name[0] = '\0';
			n++;
		}
		for (int a = 1; a < n; a++) {
			drawable_t k = dr[a];
			int b = a - 1;
			while (b >= 0 && (dr[b].sy > k.sy || (dr[b].sy == k.sy && dr[b].sx > k.sx))) {
				dr[b + 1] = dr[b];
				b--;
			}
			dr[b + 1] = k;
		}
		if (fb) {
			uint16_t world = (st && st->map_data.width > 0) ? (uint16_t)st->map_data.pos[3] : 0;
			int16_t w_level = nd_field_water_level(world);

			for (int a = 0; a < n; a++) {
				int sub_units = 0;
				if (w_level > 0 && dr[a].wz < w_level) {
					int ti = ndw_fdiv(dr[a].wx, NDW_TILE_W);
					int pos_y = -ndw_fdiv(dr[a].wy, NDW_TILE_H);
					int16_t cpos[4] = { (int16_t)ti, (int16_t)pos_y, 0, (int16_t)world };
					const nd_room_chunk_t *chk = nd_seven_chunk_find(cpos);
					if (chk && chk->has_water) {
						if (dr[a].what == D_OBJ && dr[a].obj.kind >= NDW_K_DECID && dr[a].obj.kind <= NDW_K_CACTUS)
							continue; /* Anti-submersion: do not draw trees/flora in water */
						sub_units = (int)(w_level - dr[a].wz);
					}
				}

				if (dr[a].what == D_LAMB) {
					draw_lamb_scaled_sub(fb, w, h, dr[a].sx, dr[a].sy, spr_scale, sub_units);
				} else if (dr[a].what == D_ENTITY) {
					if (dr[a].sprite_id) {
						const nd_seven_sprite_rect_t *sr = &nd_seven_sprite_rects[dr[a].sprite_id];
						blit_sub(fb, w, h, dr[a].sprite_id, 0, 0, sr->w, sr->h,
						         sr->w / 2, sr->h - 1, dr[a].sx, dr[a].sy, spr_scale, NULL, dr[a].flip, sub_units);
					} else {
						ctx_t c = { fb, w, h, dr[a].sx, dr[a].sy, spr_scale };
						draw_pawn(&c, dr[a].ch, dr[a].fg);
					}
				} else {
					draw_world_obj_scaled(fb, w, h, &dr[a].obj, dr[a].sx, dr[a].sy, nd_seven_get_anim(), spr_scale);
				}
				if (dr[a].name[0] && dr[a].what != D_LAMB) {
					int dx = dr[a].sx - w / 2;
					int dy = dr[a].sy - h / 2;
					if (dx < 0) dx = -dx;
					if (dy < 0) dy = -dy;
					if (dx <= 48 * spr_scale && dy <= 36 * spr_scale) {
						int top_y = drawable_top_y(&dr[a]);
						draw_object_name(fb, w, h, dr[a].sx, top_y, dr[a].name);
					}
				}
			}
		}

		for (int a = 0; a < n && all_gpu_count < ALL_GPU_MAX_DRAW; a++) {
			int has_sprite = 0;
			float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
			float bw = 0.0f, bh = 0.0f;

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
				const nd_seven_sprite_rect_t *sr = &nd_seven_sprite_rects[sprite];
				bw = (float)sr->w;
				bh = (float)sr->h;
				u0 = (float)sr->x / (float)ND_SEVEN_ATLAS_W;
				v0 = (float)sr->y / (float)ND_SEVEN_RUNTIME_ATLAS_H;
				u1 = (float)(sr->x + sr->w) / (float)ND_SEVEN_ATLAS_W;
				v1 = (float)(sr->y + sr->h) / (float)ND_SEVEN_RUNTIME_ATLAS_H;
				has_sprite = 1;
			} else if (dr[a].what == D_OBJ) {
				if (dr[a].obj.kind >= NDW_K_DECID && dr[a].obj.kind <= NDW_K_CACTUS) {
					uint16_t world = (st && st->map_data.width > 0) ? (uint16_t)st->map_data.pos[3] : 0;
					int16_t w_level = nd_field_water_level(world);
					if (w_level > 0 && dr[a].wz < w_level) {
						int ti = ndw_fdiv(dr[a].wx, NDW_TILE_W);
						int pos_y = -ndw_fdiv(dr[a].wy, NDW_TILE_H);
						int16_t cpos[4] = { (int16_t)ti, (int16_t)pos_y, 0, (int16_t)world };
						const nd_room_chunk_t *chk = nd_seven_chunk_find(cpos);
						if (chk && chk->has_water)
							continue; /* Anti-submersion: omit trees/flora in 3D WebGL mode */
					}
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
					bw = (float)sr_val.w;
					bh = (float)sr_val.h;
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

				s_all_gpu_batch[all_gpu_count].x = (float)dr[a].wx;
				/* Give the lamb a microscopic +0.01f wy bias so it always wins exact row ties */
				s_all_gpu_batch[all_gpu_count].y = (float)dr[a].wy + (dr[a].what == D_LAMB ? 0.01f : 0.0f);
				s_all_gpu_batch[all_gpu_count].z = dr[a].z_f;
				s_all_gpu_batch[all_gpu_count].width = bw;
				s_all_gpu_batch[all_gpu_count].height = bh;
				s_all_gpu_batch[all_gpu_count].u0 = u0;
				s_all_gpu_batch[all_gpu_count].v0 = v0;
				s_all_gpu_batch[all_gpu_count].u1 = u1;
				s_all_gpu_batch[all_gpu_count].v1 = v1;
				s_all_gpu_batch[all_gpu_count].tint_rgba = tint_b;
				all_gpu_count++;
			}
		}
	}

	/* Sort near-field 3D billboards globally back-to-front by world wy ascending (tie-break by wx ascending) */
	if (is_3d && all_gpu_count > 0) {
		for (uint32_t a = 1; a < all_gpu_count; a++) {
			lam_billboard_gpu_t k = s_all_gpu_batch[a];
			int32_t b = (int32_t)a - 1;
			while (b >= 0 && (s_all_gpu_batch[b].y > k.y ||
			                 (s_all_gpu_batch[b].y == k.y && s_all_gpu_batch[b].x > k.x))) {
				s_all_gpu_batch[b + 1] = s_all_gpu_batch[b];
				b--;
			}
			s_all_gpu_batch[b + 1] = k;
		}
	}

	/* Unified Homogeneous Scenery Pass:
	 * Beyond the 7x7 server room window, resolve remembered tiles and isomorphic
	 * virgin tiles (flora + stones/rocks/boulders) at 100% homogeneous 1:1 room
	 * resolution across all visible terrain leaves in the camera frustum, fading
	 * smoothly by altitude before planetary views with zero square boundaries. */
	s_mid_batch_len = 0;
	s_last_mid_gpu_count = 0;
	int near_flushed = 0;
	float dist_scenery = (eff_cam_dist > 1.0f) ? eff_cam_dist : ND_SEVEN_CAM_DIST;
	float alt_fade = seven_smooth_fade(dist_scenery, SCENERY_ALT_FADE_START, SCENERY_ALT_FADE_END);
	uint32_t tint_scenery = seven_pack_white_alpha(alt_fade);
	if (is_3d && tint_scenery != 0) {
		uint16_t world = (st && st->map_data.width > 0) ? (uint16_t)st->map_data.pos[3] : 0;
		int16_t w_level = nd_field_water_level(world);
		int16_t base_rx = st ? st->map_data.pos[0] : 0;
		int16_t base_ry = st ? st->map_data.pos[1] : 0;
		int16_t base_rz = st ? st->map_data.pos[2] : 0;
		int32_t center_ry_grid = -(int32_t)base_ry;

		int64_t lwx = 0, lwy = 0;
		nd_seven_world_pos(st, &lwx, &lwy);

		/* Sort visible leaves back-to-front (North-to-South: oy ascending, tie-break by ox ascending)
		 * so distant northern tree crowns render before nearer southern crowns. */
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
	}
	if (is_3d && !near_flushed && all_gpu_count > 0) {
		nd_seven_gl_flush_billboards(s_all_gpu_batch, all_gpu_count, atlas_rgba());
	}

	{
		char act[64];
		if (nd_seven_action(st, act, sizeof(act)) > 0) {
			int hx = w / 2;
			int hy = h / 2 - (LAMB_AY - 4) * SCALE;
			draw_thinking_balloon(fb, w, h, hx, hy, act);
		}
	}
	nd_seven_ground_frame_end(st ? &st->map_data : NULL);
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
