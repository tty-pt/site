#include <string.h>
#include <nd/seven-room.h>
#include "nd_seven.h"
#include "nd_seven_atlas.h"

/* --- sprite atlas ------------------------------------------------------- */

static uint8_t s_atlas[ND_SEVEN_ATLAS_W * ND_SEVEN_ATLAS_H_PX];
static int s_atlas_ready;

static void atlas_load(void)
{
	size_t o = 0;
	if (s_atlas_ready)
		return;
	for (size_t i = 0; i + 1 < ND_SEVEN_RLE_LEN && o < sizeof(s_atlas); i += 2) {
		size_t run = (size_t)nd_seven_rle[i] + 1;
		uint8_t v = nd_seven_rle[i + 1];
		while (run-- && o < sizeof(s_atlas))
			s_atlas[o++] = v;
	}
	s_atlas_ready = 1;
}

/*
 * Draw the (sx,sy,sw,sh) part of sprite `id` with its anchor (ax,ay) -- a
 * point inside that part -- at screen (dx,dy), magnified `scale` times.
 * Transparent pixels are skipped, translucent ones (shadows) are blended.
 */
static void blit(uint32_t *fb, int w, int h, int id,
                 int sx, int sy, int sw, int sh,
                 int ax, int ay, int dx, int dy, int scale)
{
	const nd_seven_sprite_rect_t *r = &nd_seven_sprite_rects[id];
	int ox = dx - ax * scale, oy = dy - ay * scale;

	for (int y = 0; y < sh; y++) {
		const uint8_t *row = &s_atlas[(r->y + sy + y) * ND_SEVEN_ATLAS_W + r->x + sx];
		for (int x = 0; x < sw; x++) {
			uint32_t c = nd_seven_palette[row[x]];
			uint32_t a = c >> 24;
			if (!a)
				continue;
			for (int py = 0; py < scale; py++) {
				int yy = oy + y * scale + py;
				if (yy < 0 || yy >= h)
					continue;
				for (int px = 0; px < scale; px++) {
					int xx = ox + x * scale + px;
					if (xx < 0 || xx >= w)
						continue;
					uint32_t *d = &fb[yy * w + xx];
					if (a == 255) {
						*d = c;
					} else {
						uint32_t dr = *d & 0xFF, dg = (*d >> 8) & 0xFF, db = (*d >> 16) & 0xFF;
						uint32_t sr = c & 0xFF, sg = (c >> 8) & 0xFF, sb = (c >> 16) & 0xFF;
						dr = (sr * a + dr * (255 - a)) / 255;
						dg = (sg * a + dg * (255 - a)) / 255;
						db = (sb * a + db * (255 - a)) / 255;
						*d = 0xFF000000u | (db << 16) | (dg << 8) | dr;
					}
				}
			}
		}
	}
}

static void blit_all(uint32_t *fb, int w, int h, int id, int ax, int ay, int dx, int dy, int scale)
{
	const nd_seven_sprite_rect_t *r = &nd_seven_sprite_rects[id];
	blit(fb, w, h, id, 0, 0, r->w, r->h, ax, ay, dx, dy, scale);
}

/* --- state -------------------------------------------------------------- */

#define SCALE      2
#define LAMB_SIZE  32
#define LAMB_AX    16   /* feet, inside a 32x32 frame */
#define LAMB_AY    28
#define SPEED      58.0f /* room units per second (~3.6 cells/s) */

enum { DIR_DOWN, DIR_UP, DIR_LEFT, DIR_RIGHT };
enum { HELD_U = 1, HELD_D = 2, HELD_L = 4, HELD_R = 8 };

static nds_room_t s_room;
static int16_t s_pos[4];
static int s_have_room;
static int s_fx, s_fy;
static int s_dir = DIR_DOWN;
static int s_held;
static int s_walking;
static int s_pending_key;     /* gate key sent, waiting for the new room */
static float s_acc;
static uint32_t s_anim;

static uint8_t room_exits(const nd_client_state_t *st)
{
	if (!st || st->map_data.width == 0)
		return NDS_EXIT_W | NDS_EXIT_N | NDS_EXIT_E | NDS_EXIT_S;
	return st->map_data.tiles[st->map_data.player_y * ND_MAP_GRID_WIDTH + st->map_data.player_x].exits;
}

/* (Re)build the room when the cell, or what leads out of it, changed. */
static void sync_room(const nd_client_state_t *st)
{
	const int16_t *pos = st ? st->map_data.pos : s_pos;
	uint8_t exits = room_exits(st);

	if (s_have_room && !memcmp(pos, s_pos, sizeof(s_pos)) &&
	    (exits & 0x1B) == s_room.exits)
		return;

	int moved = !s_have_room || memcmp(pos, s_pos, sizeof(s_pos)) != 0 || (exits & 0x1B) != s_room.exits;
	memcpy(s_pos, pos, sizeof(s_pos));
	nds_room_build(&s_room, s_pos, exits);
	s_have_room = 1;
	if (moved) {
		nds_entry_point(s_pending_key, &s_fx, &s_fy);
		s_pending_key = 0;
		s_held = 0;  /* Clear held keys on room change to prevent stuck movement */
	}
}

static void lamb_pos(int *fx, int *fy)
{
	*fx = s_fx;
	*fy = s_fy;
}

void nd_seven_lamb(int *fx, int *fy, int *dir, int *walking)
{
	lamb_pos(fx, fy);
	if (dir) *dir = s_dir;
	if (walking) *walking = s_walking;
}

int nd_seven_key(const char *key, int down)
{
	int bit = 0;

	if (!key)
		return 0;
	if (!strcmp(key, "k") || !strcmp(key, "ArrowUp") || !strcmp(key, "w")) bit = HELD_U;
	else if (!strcmp(key, "j") || !strcmp(key, "ArrowDown") || !strcmp(key, "s")) bit = HELD_D;
	else if (!strcmp(key, "h") || !strcmp(key, "ArrowLeft") || !strcmp(key, "a")) bit = HELD_L;
	else if (!strcmp(key, "l") || !strcmp(key, "ArrowRight") || !strcmp(key, "d")) bit = HELD_R;
	if (!bit)
		return 0;
	if (down) {
		s_held |= bit;
	} else {
		s_held &= ~bit;
	}
	return 1;
}

/* Try to move one unit along an axis. Returns a gate key, else 0. */
static int move_axis(int dx, int dy)
{
	int nx = s_fx + dx, ny = s_fy + dy;
	int key = nds_gate_exit(&s_room, nx, ny);

	if (key)
		return key;
	if (nds_foot_blocked(&s_room, nx, ny))
		return 0;
	s_fx = nx;
	s_fy = ny;
	return 0;
}

int nd_seven_tick(const nd_client_state_t *st, float dt)
{
	int key = 0, dx = 0, dy = 0, steps;

	s_anim++;
	sync_room(st);
	if (s_held & HELD_L) dx -= 1;
	if (s_held & HELD_R) dx += 1;
	if (s_held & HELD_U) dy -= 1;
	if (s_held & HELD_D) dy += 1;

	s_walking = 0;
	if (!(dx || dy) || s_pending_key) {
		s_acc = 0;
		return 0;
	}
	if (dt > 0.1f)
		dt = 0.1f;
	s_acc += dt * SPEED;
	steps = (int)s_acc;
	s_acc -= (float)steps;
	while (steps-- > 0 && !key) {
		int ox = s_fx, oy = s_fy;
		if (dx) {
			int k = move_axis(dx, 0);
			if (k) key = k;
		}
		if (dy) {
			int k = move_axis(0, dy);
			if (k) key = k;
		}
		/* Detect actual movement by position change */
		int moved_dx = (ox != s_fx) && (dx != 0);
		int moved_dy = (oy != s_fy) && (dy != 0);
		
		/* Update direction based on actual movement this frame */
		if (moved_dx || moved_dy) {
			if (moved_dy)
				s_dir = (dy > 0) ? DIR_DOWN : DIR_UP;      /* Y-axis priority */
			else if (moved_dx)
				s_dir = (dx > 0) ? DIR_RIGHT : DIR_LEFT;    /* X-axis fallback */
		}
		if (ox != s_fx || oy != s_fy)
			s_walking = 1;
	}
	if (key)
		s_pending_key = key;
	return key;
}

/* --- drawing ------------------------------------------------------------ */

static int tree_sprite(uint8_t v)
{
	static const int t[] = { NDS_TREE_OAK, NDS_TREE_APPLE, NDS_TREE_APPLE2, NDS_TREE_ROUND, NDS_TREE_OAK };
	return t[v % 5];
}

static int bush_sprite(uint8_t v)
{
	return (v & 1) ? NDS_BUSH_TEAL : NDS_BUSH_DARK;
}

static int bigrock_sprite(uint8_t v)
{
	return (v & 1) ? NDS_ROCK_WHITE_BIG : NDS_ROCK_TAN;
}

static int rock_sprite(uint8_t v)
{
	static const int t[] = { NDS_ROCK_TAN_SM, NDS_ROCK_TAN_TINY, NDS_ROCK_WHITE,
	                         NDS_ROCK_GREY, NDS_STONE_BLUE, NDS_STONE_BLUE_SM };
	return t[v % 6];
}

static int decor_sprite(uint8_t d)
{
	static const int t[] = { NDS_DECOR_YEL, NDS_DECOR_YEL2, NDS_DECOR_ORG, NDS_DECOR_PINK };
	return t[(d - 1) & 3];
}

static void draw_obj(uint32_t *fb, int w, int h, int ox, int oy, const nds_obj_t *o)
{
	int id;
	switch (o->type) {
	case NDS_O_TREE:    id = tree_sprite(o->variant); break;
	case NDS_O_BUSH:    id = bush_sprite(o->variant); break;
	case NDS_O_BIGROCK: id = bigrock_sprite(o->variant); break;
	default:            id = rock_sprite(o->variant); break;
	}
	const nd_seven_sprite_rect_t *r = &nd_seven_sprite_rects[id];
	/* bottom-centre of the sprite on the bottom-centre of the footprint */
	int bx = (o->x * NDS_CELL_PX + o->w * NDS_CELL_PX / 2) * SCALE;
	int by = ((o->y + 1) * NDS_CELL_PX - 1) * SCALE;
	blit_all(fb, w, h, id, r->w / 2, r->h - 1, ox + bx, oy + by, SCALE);
}

static void draw_lamb(uint32_t *fb, int w, int h, int ox, int oy)
{
	int row, col;
	if (s_walking || s_held) {
		row = s_dir;
		col = (int)((s_anim / 4) % 6);
	} else {
		row = 4 + s_dir;
		col = (int)((s_anim / 24) % 4);
	}
	blit(fb, w, h, NDS_LAMB, col * LAMB_SIZE, row * LAMB_SIZE, LAMB_SIZE, LAMB_SIZE,
	     LAMB_AX, LAMB_AY, ox + s_fx * SCALE, oy + s_fy * SCALE, SCALE);
}

void nd_seven_render(uint32_t *fb, uint32_t wu, uint32_t hu, const nd_client_state_t *st)
{
	int w = (int)wu, h = (int)hu;
	int ox, oy;

	if (!fb || w <= 0 || h <= 0)
		return;
	atlas_load();
	sync_room(st);

	ox = (w - NDS_ROOM_W * SCALE) / 2;
	oy = (h - NDS_ROOM_H * SCALE) / 2;
	for (int i = 0; i < w * h; i++)
		fb[i] = 0xFF08080C;

	/* ground */
	for (int cy = 0; cy < NDS_ROWS; cy++)
		for (int cx = 0; cx < NDS_COLS; cx++) {
			int idx = cy * NDS_COLS + cx;
			int v = (int)((cx * 7 + cy * 13 + (cx ^ cy)) & 3);   /* fixed per cell */
			int id = s_room.ground[idx] == NDS_G_WALL ? NDS_WALL + v :
			         s_room.ground[idx] == NDS_G_PATH ? NDS_PATH + v : NDS_GRASS;
			blit_all(fb, w, h, id, 0, 0, ox + cx * NDS_CELL_PX * SCALE, oy + cy * NDS_CELL_PX * SCALE, SCALE);
		}
	/* flowers */
	for (int cy = 0; cy < NDS_ROWS; cy++)
		for (int cx = 0; cx < NDS_COLS; cx++) {
			uint8_t d = s_room.decor[cy * NDS_COLS + cx];
			if (!d)
				continue;
			int id = decor_sprite(d);
			const nd_seven_sprite_rect_t *r = &nd_seven_sprite_rects[id];
			blit_all(fb, w, h, id, r->w / 2, r->h - 1,
			         ox + (cx * NDS_CELL_PX + NDS_CELL_PX / 2) * SCALE,
			         oy + (cy * NDS_CELL_PX + NDS_CELL_PX - 3) * SCALE, SCALE);
		}
	/* objects and the lamb, back to front */
	int lamb_done = 0;
	for (int i = 0; i < s_room.nobj; i++) {
		const nds_obj_t *o = &s_room.obj[i];
		if (!lamb_done && (o->y + 1) * NDS_CELL_PX > s_fy) {
			draw_lamb(fb, w, h, ox, oy);
			lamb_done = 1;
		}
		draw_obj(fb, w, h, ox, oy, o);
	}
	if (!lamb_done)
		draw_lamb(fb, w, h, ox, oy);
}

void nd_seven_debug(int *pending, int *pos_x, int *pos_y, int *have_room)
{
	if (pending) *pending = s_pending_key;
	if (pos_x) *pos_x = (int)s_pos[0];
	if (pos_y) *pos_y = (int)s_pos[1];
	if (have_room) *have_room = s_have_room;
}
