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

typedef struct {
	uint8_t threshold; /* libchaff [0..255] threshold to spawn flora (255 = barren) */
	char    glyph_sm;  /* small plant glyph ('x', 't', 'a', 'i') */
	char    glyph_big; /* big plant glyph ('X', 'T', 'A', 'I') */
	uint8_t canopy_r;  /* far-field canopy crown R */
	uint8_t canopy_g;  /* far-field canopy crown G */
	uint8_t canopy_b;  /* far-field canopy crown B */
} seven_biome_flora_t;

static const seven_biome_flora_t s_biome_flora[19] = {
	/* 0  water               */ { 255,   0,   0,   0,   0,   0 },
	/* 1  permanent ice       */ { 255,   0,   0,   0,   0,   0 },
	/* 2  tundra              */ { 196, 'a', 'a',  54,  84,  68 },
	/* 3  tundra2             */ { 196, 'a', 'a',  52,  82,  70 },
	/* 4  tundra3             */ { 198, 'a', 'a',  56,  80,  64 },
	/* 5  tundra4             */ { 196, 'a', 'a',  50,  82,  68 },
	/* 6  cold desert         */ { 192, 'i', 'I',  76, 104,  72 },
	/* 7  shrubland           */ { 164, 'x', 'X',  68,  96,  48 },
	/* 8  coniferous forest   */ {  96, 'a', 'A',  30,  74,  46 },
	/* 9  boreal forest       */ {  98, 'a', 'A',  32,  78,  52 },
	/* 10 temperate grassland */ { 172, 'x', 'X',  54,  98,  44 },
	/* 11 woodland            */ { 104, 'x', 'X',  44,  92,  42 },
	/* 12 temperate seasonal  */ { 102, 'x', 'X',  48,  94,  40 },
	/* 13 temperate rainforest*/ {  84, 't', 'T',  32,  86,  46 },
	/* 14 desert              */ { 188, 'i', 'I',  72, 110,  58 },
	/* 15 savannah            */ { 168, 't', 'T',  78,  98,  42 },
	/* 16 tropical seasonal   */ {  88, 't', 'T',  38,  96,  42 },
	/* 17 tropical rainforest */ {  76, 't', 'T',  26,  84,  38 },
	/* 18 volcanic            */ { 255,   0,   0,   0,   0,   0 },
};

static inline uint8_t seven_eval_flora_mask(uint16_t world, int32_t wx, int32_t wy)
{
	uint32_t seed = 0x464C4F52u ^ ((uint32_t)world * 0x9E3779B1u);
	int16_t p_macro[2] = { (int16_t)wy, (int16_t)wx };
	int16_t rx = (int16_t)wx, ry = (int16_t)wy;
	chaff_rotate_q15(&rx, &ry);
	int16_t p_micro[2] = { ry, rx };
	uint32_t m0 = noise_wpoint_n(p_macro, 6, 8, seed, 2, NOISE_FADE5) >> 16;
	uint32_t m1 = noise_wpoint_n(p_micro, 4, 8, seed ^ 0x85EBCA77u, 2, NOISE_FADE5) >> 16;
	uint32_t combined = (m0 * 3u + m1) >> 2;
	return (uint8_t)(combined > 255u ? 255u : combined);
}

static void seven_macro_chunk_sampler(void *user_ctx, uint16_t world, uint8_t level,
                                     int32_t wx, int32_t wy, ar_sample_t *out)
{
	(void)user_ctx;
	/* Preserve continuous multi-octave gradients across zoom levels so LODs do not drop abruptly into squares */
	uint8_t lod = (level >= 8) ? 0 : ((level >= 4) ? 1 : (uint8_t)(3 - (level > 2 ? 2 : level)));
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

	if (water_depth > 0) {
		col = water_color_at_depth((int)water_depth, 128, 128, 128);
	} else if (fs.bio < 19 && s_biome_flora[fs.bio].threshold < 255) {
		const seven_biome_flora_t *bf = &s_biome_flora[fs.bio];
		uint8_t mask = seven_eval_flora_mask(world, wx, wy);
		if (mask > bf->threshold) {
			int span = 255 - (int)bf->threshold;
			int cover = ((int)(mask - bf->threshold) * 210) / (span > 0 ? span : 1);
			if (cover > 210) cover = 210;
			out->attr[4] = (uint8_t)cover;
			out->attr[5] = bf->canopy_r;
			out->attr[6] = bf->canopy_g;
			out->attr[7] = bf->canopy_b;
			out->flags |= 2u;
			if (cover > 0) {
				uint32_t h = (uint32_t)(wx * 0x85EBCA77u) ^ (uint32_t)(wy * 0xC2B2AE3Du) ^ ((uint32_t)level * 0x27D4EB2Fu);
				h ^= h >> 15;
				h *= 0x85EBCA6Bu;
				h ^= h >> 13;
				int shade = 224 + (int)(h & 63);
				int cr = ((int)bf->canopy_r * shade) >> 8;
				int cg = ((int)bf->canopy_g * shade) >> 8;
				int cb = ((int)bf->canopy_b * shade) >> 8;
				if (cr > 255) cr = 255;
				if (cg > 255) cg = 255;
				if (cb > 255) cb = 255;
				uint32_t base = col ? col : 0xFF303030u;
				int r0 = (int)(base & 0xFF);
				int g0 = (int)((base >> 8) & 0xFF);
				int b0 = (int)((base >> 16) & 0xFF);
				int r_out = r0 + (((cr - r0) * cover) >> 8);
				int g_out = g0 + (((cg - g0) * cover) >> 8);
				int b_out = b0 + (((cb - b0) * cover) >> 8);
				col = 0xFF000000u | (uint32_t)r_out | ((uint32_t)g_out << 8) | ((uint32_t)b_out << 16);
				if (level >= 1) {
					int16_t bump = (int16_t)((((int)((h >> 8) & 7) - 2) * cover) >> 5);
					out->val = (int16_t)(out->val + bump);
				}
			}
		}
	}
	out->rgba = col;
}

ar_system_t *nd_seven_get_chunk_sys(void)
{
	if (!s_seven_chunk_sys) {
		s_seven_chunk_sys = ar_create(NDW_TILE_W, NDW_TILE_H, seven_macro_chunk_sampler, NULL);
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

typedef struct {
	float x, y, z;
	float width, height;
	float u0, v0, u1, v1;
	uint32_t tint_rgba;
} lam_billboard_gpu_t;

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

static void seven_dispatch_macro_backdrop(int w, int h, const nd_client_state_t *st,
                                         float zoom, float eff_cam_dist)
{
	ar_system_t *cs = nd_seven_get_chunk_sys();
	if (!cs) return;

	int64_t lwx = 0, lwy = 0;
	nd_seven_world_pos(st, &lwx, &lwy);
	float lwz = nd_seven_ground_elevation_f((int32_t)lwx, (int32_t)lwy);
	uint16_t world = (uint16_t)(st ? st->map_data.pos[3] : 0);
	const nd_planet_def_t *pdef = nd_seven_get_planet(world);

	/* Compute exact camera MVP matrix matching WebGL lamina-bridge.js computeMVP */
	float pitch = ND_SEVEN_DIORAMA_PITCH * (3.14159265358979323846f / 180.0f);
	float cos_p = cosf(pitch);
	float sin_p = sinf(pitch);
	float dist = (eff_cam_dist > 1.0f) ? eff_cam_dist : ND_SEVEN_CAM_DIST;

	float eye[3] = {
		(float)lwx,
		(float)lwy + dist * cos_p,
		lwz + dist * sin_p
	};
	float target[3] = { (float)lwx, (float)lwy, lwz };
	float up[3] = { 0.0f, -sin_p, cos_p };

	float modelview[16], proj[16], mvp[16];
	ar_mat4_lookat(modelview, eye, target, up);

	float fov_rad = ND_SEVEN_CAM_FOV * (3.14159265358979323846f / 180.0f);
	float aspect = (h > 0) ? ((float)w / (float)h) : 1.333333f;
	float near_z = (eff_cam_dist * 0.005f > 2.0f) ? (eff_cam_dist * 0.005f) : 2.0f;
	float far_z = (eff_cam_dist * 20.0f > 200000.0f) ? (eff_cam_dist * 20.0f) : 200000.0f;
	ar_mat4_perspective(proj, fov_rad, aspect, near_z, far_z);
	ar_mat4_multiply(mvp, proj, modelview);

	float r_planet = dist * 1.25f;
	if (r_planet < 16000.0f) r_planet = 16000.0f;
	if (r_planet > 80000.0f) r_planet = 80000.0f;

	ar_cull_opts_t opts = {
		.z_min = -2000.0f,
		.z_max = 2000.0f,
		.sag_radius = r_planet,
		.focal_x = (float)lwx,
		.focal_y = (float)lwy,
		.margin = 150.0f,
		.target_quad_px = 2.0f,
		.viewport_w = (float)w,
		.viewport_h = (float)h,
		.min_level = 0,
		.max_level = 16
	};

	const ar_node_t *visible[AR_DEFAULT_MAX_ACTIVE];
	size_t macro_count = ar_system_query_matrix(cs, world, mvp, &opts, visible, AR_DEFAULT_MAX_ACTIVE);

	for (size_t m = 0; m < macro_count; m++) {
		const ar_node_t *node = visible[m];
		if (!node) continue;
		((ar_node_t *)node)->active_view_mask |= 1;
		((ar_node_t *)node)->last_touch_frame = nd_seven_get_anim();

		bool has_water = (node->flags & 1) != 0;
		float sea_lvl = has_water ? (float)nd_field_water_level(world) : ND_SEVEN_SEA_LEVEL_NONE;
		float cam_params[22] = {
			(float)lwx, (float)lwy, lwz,
			eff_cam_dist, ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_YAW, ND_SEVEN_CAM_FOV,
			(float)nd_seven_get_anim() * ND_SEVEN_ANIM_TIME_FACTOR,
			(float)node->ox,
			(float)node->oy,
			(float)node->w,
			(float)node->h,
			(float)node->level,
			(float)node->rev,
			pdef->major_r,
			pdef->minor_r,
			pdef->center[0],
			pdef->center[1],
			pdef->center[2],
			ND_SEVEN_WORLD_DIM_W,
			ND_SEVEN_WORLD_DIM_H,
			(float)world
		};

		const void *water_col = has_water ? node->rgba : NULL;
		lam_terrain_mesh_flush(node->rgba, node->values,
		                       AR_DEFAULT_VERT_DIM, AR_DEFAULT_VERT_DIM,
		                       (uint32_t)w, (uint32_t)h,
		                       sea_lvl, cam_params, water_col);
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
				float alt_cam_params[22] = {
					(float)lwx, (float)lwy, lwz,
					eff_cam_dist, ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_YAW, ND_SEVEN_CAM_FOV,
					(float)nd_seven_get_anim() * ND_SEVEN_ANIM_TIME_FACTOR,
					(float)node->ox,
					(float)node->oy,
					(float)node->w,
					(float)node->h,
					(float)node->level,
					(float)node->rev,
					alt_pdef->major_r,
					alt_pdef->minor_r,
					alt_pdef->center[0],
					alt_pdef->center[1],
					alt_pdef->center[2],
					ND_SEVEN_WORLD_DIM_W,
					ND_SEVEN_WORLD_DIM_H,
					(float)alt_world
				};
				const void *water_col = has_water ? node->rgba : NULL;
				lam_terrain_mesh_flush(node->rgba, node->values,
				                       AR_DEFAULT_VERT_DIM, AR_DEFAULT_VERT_DIM,
				                       (uint32_t)w, (uint32_t)h,
				                       sea_lvl, alt_cam_params, water_col);
			}
		}
	}
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
					dr[n].obj.hw = (uint8_t)(sp.objs[k].kind == NDW_K_ROCK ? 6 : sp.objs[k].kind == NDW_K_BOULDER ? 10 : 0);
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
		if (tj == lamb_row && n < MAX_DRAW) {
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

		static lam_billboard_gpu_t s_gpu_batch[MAX_DRAW];
		uint32_t gpu_count = 0;
		for (int a = 0; a < n && gpu_count < MAX_DRAW; a++) {
			int sprite = 0;
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
				v0 = (float)(row * LAMB_SIZE) / (float)ND_SEVEN_ATLAS_H_PX;
				u1 = (float)((col + 1) * LAMB_SIZE) / (float)ND_SEVEN_ATLAS_W;
				v1 = (float)((row + 1) * LAMB_SIZE) / (float)ND_SEVEN_ATLAS_H_PX;
				sprite = NDS_LAMB;
			} else if (dr[a].what == D_ENTITY) {
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
				v0 = (float)sr->y / (float)ND_SEVEN_ATLAS_H_PX;
				u1 = (float)(sr->x + sr->w) / (float)ND_SEVEN_ATLAS_W;
				v1 = (float)(sr->y + sr->h) / (float)ND_SEVEN_ATLAS_H_PX;
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
				sprite = nd_seven_obj_sprite(&dr[a].obj);
				if (!sprite) {
					if (strstr(dr[a].name, "stone") || strstr(dr[a].name, "Stone") || dr[a].obj.kind == NDW_K_STONE)
						sprite = NDS_STONE_FLAT;
					else if (dr[a].obj.glyph)
						sprite = NDS_STONE_TINY;
				}
				if (sprite > 0 && sprite < NDS_COUNT) {
					const nd_seven_sprite_rect_t *sr = &nd_seven_sprite_rects[sprite];
					bw = (float)sr->w;
					bh = (float)sr->h;
					int flip = (dr[a].obj.variant >> 2) & 1;
					if (flip) {
						u0 = (float)(sr->x + sr->w) / (float)ND_SEVEN_ATLAS_W;
						u1 = (float)sr->x / (float)ND_SEVEN_ATLAS_W;
					} else {
						u0 = (float)sr->x / (float)ND_SEVEN_ATLAS_W;
						u1 = (float)(sr->x + sr->w) / (float)ND_SEVEN_ATLAS_W;
					}
					v0 = (float)sr->y / (float)ND_SEVEN_ATLAS_H_PX;
					v1 = (float)(sr->y + sr->h) / (float)ND_SEVEN_ATLAS_H_PX;
				}
			}

			if (sprite > 0 && sprite < NDS_COUNT) {
				s_gpu_batch[gpu_count].x = (float)dr[a].wx;
				s_gpu_batch[gpu_count].y = (float)dr[a].wy;
				s_gpu_batch[gpu_count].z = dr[a].z_f;
				s_gpu_batch[gpu_count].width = bw;
				s_gpu_batch[gpu_count].height = bh;
				s_gpu_batch[gpu_count].u0 = u0;
				s_gpu_batch[gpu_count].v0 = v0;
				s_gpu_batch[gpu_count].u1 = u1;
				s_gpu_batch[gpu_count].v1 = v1;
				s_gpu_batch[gpu_count].tint_rgba = 0xFFFFFFFFu;
				gpu_count++;
			}
		}
		if (is_3d && gpu_count > 0)
			lam_billboard_batch_flush(s_gpu_batch, gpu_count, atlas_rgba());
	}

	/* Tier 2 Mid-Field Procedural Tree Scatter:
	 * Beyond the 7x7 server room window, sample deterministic libchaff vegetation masks
	 * across macro terrain tiles into a dedicated instanced batch buffer. */
	if (is_3d && zoom >= 0.03f) {
		#define MID_MAX_DRAW 1024
		static lam_billboard_gpu_t s_mid_gpu_batch[MID_MAX_DRAW];
		uint32_t mid_gpu_count = 0;
		uint16_t world = (st && st->map_data.width > 0) ? (uint16_t)st->map_data.pos[3] : 0;
		int16_t w_level = nd_field_water_level(world);
		int16_t base_rx = st ? st->map_data.pos[0] : 0;
		int16_t base_ry = st ? st->map_data.pos[1] : 0;
		int16_t base_rz = st ? st->map_data.pos[2] : 0;

		int mid_rad = (int)(12.0f / (zoom > 0.15f ? zoom : 0.15f));
		if (mid_rad < 6) mid_rad = 6;
		if (mid_rad > 14) mid_rad = 14;
		int mid_i0 = i0 < -mid_rad ? -mid_rad : i0;
		int mid_i1 = i1 >  mid_rad ?  mid_rad : i1;
		int mid_j0 = j0 < -mid_rad ? -mid_rad : j0;
		int mid_j1 = j1 >  mid_rad ?  mid_rad : j1;

		for (int tj = mid_j0; tj <= mid_j1 && mid_gpu_count < MID_MAX_DRAW; tj++) {
			for (int ti = mid_i0; ti <= mid_i1 && mid_gpu_count < MID_MAX_DRAW; ti++) {
				if (ti >= obj_i0 && ti <= obj_i1 && tj >= obj_j0 && tj <= obj_j1)
					continue; /* Covered by Tier 1 near-field server window */

				int16_t pos[4] = {
					(int16_t)(base_rx + ti),
					(int16_t)(base_ry - tj),
					base_rz,
					(int16_t)world
				};
				int32_t base_wx = (int32_t)pos[0] * NDW_TILE_W;
				int32_t base_wy = -(int32_t)pos[1] * NDW_TILE_H;
				nd_field_sample_t fs = nd_field_sample(world, 0, base_wx + NDW_CENTER_X, base_wy + NDW_CENTER_Y);
				if (fs.is_water || fs.bio >= 19)
					continue;
				const seven_biome_flora_t *bf = &s_biome_flora[fs.bio];
				if (bf->threshold == 255)
					continue;

				ndw_tile_t synth;
				memset(&synth, 0, sizeof(synth));
				synth.biome = fs.bio;
				synth.exits = NDW_EXIT_W | NDW_EXIT_N | NDW_EXIT_E | NDW_EXIT_S;
				int any_slot = 0;
				for (int s = 0; s < 3; s++) {
					int32_t swx = base_wx + (s * 2 + 1) * (NDW_TILE_W / 6);
					int32_t swy = base_wy + NDW_CENTER_Y;
					uint8_t m = seven_eval_flora_mask(world, swx, swy);
					if (m > bf->threshold) {
						synth.glyph[s] = (uint8_t)((m > (uint8_t)(bf->threshold + 36)) ? bf->glyph_big : bf->glyph_sm);
						synth.fg[s] = 10;
						any_slot = 1;
					}
				}
				if (!any_slot)
					continue;

				ndw_obj_t raw_objs[NDW_MAX_OBJ];
				int n_raw = ndw_tile_objects(&synth, pos, 1, raw_objs);
				for (int k = 0; k < n_raw && mid_gpu_count < MID_MAX_DRAW; k++) {
					int sprite = nd_seven_obj_sprite(&raw_objs[k]);
					if (sprite <= 0 || sprite >= NDS_COUNT)
						continue;
					int32_t wx = base_wx + raw_objs[k].x;
					int32_t wy = base_wy + raw_objs[k].y;
					int16_t elev_q = nd_seven_ground_elevation_q12_4(wx, wy);
					int16_t wz = nd_field_elevation_to_block(elev_q);
					if (w_level > 0 && wz <= w_level)
						continue; /* Anti-submersion */

					const nd_seven_sprite_rect_t *sr = &nd_seven_sprite_rects[sprite];
					int flip = (raw_objs[k].variant >> 2) & 1;
					s_mid_gpu_batch[mid_gpu_count].x = (float)wx;
					s_mid_gpu_batch[mid_gpu_count].y = (float)wy;
					s_mid_gpu_batch[mid_gpu_count].z = (float)elev_q / 16.0f;
					s_mid_gpu_batch[mid_gpu_count].width = (float)sr->w;
					s_mid_gpu_batch[mid_gpu_count].height = (float)sr->h;
					if (flip) {
						s_mid_gpu_batch[mid_gpu_count].u0 = (float)(sr->x + sr->w) / (float)ND_SEVEN_ATLAS_W;
						s_mid_gpu_batch[mid_gpu_count].u1 = (float)sr->x / (float)ND_SEVEN_ATLAS_W;
					} else {
						s_mid_gpu_batch[mid_gpu_count].u0 = (float)sr->x / (float)ND_SEVEN_ATLAS_W;
						s_mid_gpu_batch[mid_gpu_count].u1 = (float)(sr->x + sr->w) / (float)ND_SEVEN_ATLAS_W;
					}
					s_mid_gpu_batch[mid_gpu_count].v0 = (float)sr->y / (float)ND_SEVEN_ATLAS_H_PX;
					s_mid_gpu_batch[mid_gpu_count].v1 = (float)(sr->y + sr->h) / (float)ND_SEVEN_ATLAS_H_PX;
					s_mid_gpu_batch[mid_gpu_count].tint_rgba = 0xFFFFFFFFu;
					mid_gpu_count++;
				}
			}
		}
		if (mid_gpu_count > 0)
			lam_billboard_batch_flush(s_mid_gpu_batch, mid_gpu_count, atlas_rgba());
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
