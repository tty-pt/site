#include <string.h>
#include <nd/seven-world.h>
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

static int drawable_top_y(const drawable_t *d)
{
	if (d->what == D_ENTITY) {
		if (d->sprite_id) {
			const nd_seven_sprite_rect_t *sr = &nd_seven_sprite_rects[d->sprite_id];
			return d->sy - sr->h * SCALE;
		}
		return d->sy - 28 * SCALE;
	}
	if (d->what == D_OBJ) {
		switch (d->obj.kind) {
		case NDW_K_DECID: {
			int id = d->obj.big ? ((d->obj.variant & 1) ? NDS_TREE_OAK : NDS_TREE_APPLE) : NDS_TREE_ROUND;
			return d->sy - nd_seven_sprite_rects[id].h * SCALE;
		}
		case NDW_K_TALL: {
			int id = d->obj.big ? NDS_TREE_BIG : NDS_TREE_APPLE2;
			return d->sy - nd_seven_sprite_rects[id].h * SCALE;
		}
		case NDW_K_FIR:
			return d->sy - (d->obj.big ? 38 : 28) * SCALE;
		case NDW_K_CACTUS:
			return d->sy - (d->obj.big ? 38 : 27) * SCALE;
		case NDW_K_STONE:
			return d->sy - 4 * SCALE;
		case NDW_K_ROCK:
			if ((d->obj.variant & 3) < 2)
				return d->sy - 9 * SCALE;
			else {
				int id = (d->obj.variant & 1) ? NDS_ROCK_GREY : NDS_ROCK_TAN;
				return d->sy - nd_seven_sprite_rects[id].h * SCALE;
			}
		case NDW_K_BOULDER: {
			int id = (d->obj.variant & 1) ? NDS_ROCK_WHITE_BIG : NDS_ROCK_TAN_BIG;
			return d->sy - nd_seven_sprite_rects[id].h * SCALE;
		}
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

void nd_seven_render(uint32_t *fb, uint32_t wu, uint32_t hu, const nd_client_state_t *st)
{
	int w = (int)wu, h = (int)hu;
	int camx, camy, i0, i1, j0, j1;
	int lamb_row;
	static drawable_t dr[MAX_DRAW];

	if (!fb || w <= 0 || h <= 0)
		return;
	atlas_load();
	nd_seven_sync_origin(st);

	for (int i = 0; i < w * h; i++)
		fb[i] = 0xFF08080C;

	camx = nd_seven_get_lx() * SCALE - w / 2;
	camy = nd_seven_get_ly() * SCALE - h / 2;
	i0 = ndw_fdiv(camx, NDW_TILE_W * SCALE);
	i1 = ndw_fdiv(camx + w - 1, NDW_TILE_W * SCALE);
	j0 = ndw_fdiv(camy, NDW_TILE_H * SCALE);
	j1 = ndw_fdiv(camy + h - 1, NDW_TILE_H * SCALE);
	lamb_row = ndw_fdiv(nd_seven_get_ly(), NDW_TILE_H);

	for (int tj = j0; tj <= j1; tj++)
		for (int ti = i0; ti <= i1; ti++) {
			ndw_tile_t t;
			int16_t pos[4];
			ndw_rect_t walls[NDW_MAX_WALL];
			int tx = ti * NDW_TILE_W * SCALE - camx, ty = tj * NDW_TILE_H * SCALE - camy;

			nd_seven_tile_at(st, ti, tj, &t, pos);
			nd_seven_ground_tile_render(fb, w, h, st, ti, tj, pos, &t, tx, ty);
			for (int k = ndw_walls(&t, walls) - 1; k >= 0; k--)
				for (int cy = walls[k].y0 / NDW_CELL_PX; cy * NDW_CELL_PX < walls[k].y1; cy++)
					for (int cx = walls[k].x0 / NDW_CELL_PX; cx * NDW_CELL_PX < walls[k].x1; cx++)
						blit_all(fb, w, h, NDS_WALL + (int)((cx * 7 + cy * 13 + (cx ^ cy)) & 3), 0, 0,
						         tx + cx * NDW_CELL_PX * SCALE, ty + cy * NDW_CELL_PX * SCALE, SCALE, NULL);
		}

	for (int tj = j0; tj <= j1 + 1; tj++) {
		int n = 0;

		for (int ti = i0 - 1; ti <= i1 + 1 && n < MAX_DRAW - NDW_MAX_OBJ; ti++) {
			nd_spatial_tile_t sp;
			if (!nd_spatial_tile_resolve(st, ti, tj, &sp))
				continue;
			int tx = ti * NDW_TILE_W * SCALE - camx, ty = tj * NDW_TILE_H * SCALE - camy;

			for (int k = 0; k < sp.obj_count && n < MAX_DRAW; k++) {
				if (sp.objs[k].harvested)
					continue;

				if (sp.objs[k].is_entity) {
					dr[n].what = D_ENTITY;
					dr[n].ch = (char)sp.objs[k].glyph;
					dr[n].fg = sp.objs[k].fg;
					dr[n].sx = tx + sp.objs[k].x * SCALE;
					dr[n].sy = ty + sp.objs[k].y * SCALE;
					dr[n].flip = sp.objs[k].flip;
					if (sp.objs[k].sprite_id > 0 && sp.objs[k].sprite_id <= 8) {
						static const uint8_t mineral_sprites[] = {
							NDS_STONE_GREY, NDS_STONE_BLUE, NDS_STONE_BLUE_SM, NDS_STONE_FLAT,
							NDS_STONE_TINY, NDS_ROCK_TAN_SM, NDS_ROCK_TAN_TINY, NDS_ROCK_GREY
						};
						dr[n].sprite_id = mineral_sprites[sp.objs[k].sprite_id - 1];
					} else {
						dr[n].sprite_id = 0;
					}
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
					dr[n].sx = tx + sp.objs[k].x * SCALE;
					dr[n].sy = ty + sp.objs[k].y * SCALE;
					dr[n].sprite_id = 0;
					dr[n].flip = 0;
					snprintf(dr[n].name, sizeof(dr[n].name), "%s", sp.objs[k].name);
					n++;
				}
			}
		}
		if (tj == lamb_row && n < MAX_DRAW) {
			dr[n].what = D_LAMB;
			dr[n].sx = w / 2;
			dr[n].sy = h / 2;
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
		for (int a = 0; a < n; a++) {
			if (dr[a].what == D_LAMB) {
				draw_lamb(fb, w, h, dr[a].sx, dr[a].sy);
			} else if (dr[a].what == D_ENTITY) {
				if (dr[a].sprite_id) {
					const nd_seven_sprite_rect_t *sr = &nd_seven_sprite_rects[dr[a].sprite_id];
					blit_all_f(fb, w, h, dr[a].sprite_id, sr->w / 2, sr->h - 1,
					           dr[a].sx, dr[a].sy, SCALE, NULL, dr[a].flip);
				} else {
					ctx_t c = { fb, w, h, dr[a].sx, dr[a].sy };
					draw_pawn(&c, dr[a].ch, dr[a].fg);
				}
			} else {
				draw_world_obj(fb, w, h, &dr[a].obj, dr[a].sx, dr[a].sy, nd_seven_get_anim());
			}
			if (dr[a].name[0] && dr[a].what != D_LAMB) {
				int dx = dr[a].sx - w / 2;
				int dy = dr[a].sy - h / 2;
				if (dx < 0) dx = -dx;
				if (dy < 0) dy = -dy;
				if (dx <= 48 * SCALE && dy <= 36 * SCALE) {
					int top_y = drawable_top_y(&dr[a]);
					draw_object_name(fb, w, h, dr[a].sx, top_y, dr[a].name);
				}
			}
		}
	}

	{
		char act[64];
		if (nd_seven_action(st, act, sizeof(act)) > 0) {
			int hx = w / 2;
			int hy = h / 2 - (LAMB_AY - 4) * SCALE;
			draw_thinking_balloon(fb, w, h, hx, hy, act);
		}
	}
}
