#include <string.h>
#include <nd/seven-world.h>
#include "nd_seven.h"
#include "nd_seven_atlas.h"

/*
 * The seven stage: a continuous world drawn from the NeverDark map.
 *
 * Every map tile is a small block (nd/seven-world.h). The blocks are laid
 * side by side as far as the framebuffer reaches and the camera follows the
 * lamb, so there are no rooms to enter and leave: crossing a block border just
 * asks the server for the matching move (h/j/k/l), and when the new map
 * arrives the origin tile shifts under the lamb without it moving on screen.
 */

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

#define SCALE      2
#define LAMB_SIZE  32
#define LAMB_AX    16   /* feet, inside a 32x32 frame */
#define LAMB_AY    28
#define SPEED      72.0f /* world units per second */
#define PENDING_S  2.0f  /* wait this long for the server to confirm a move */

#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

/* The ASCII map's colours (nd.css --c0..--c15), 0xAABBGGRR: a map glyph reads
 * the same colour here as it does on the map. */
static const uint32_t s_ansi[16] = {
	0xFF2C2C2C, 0xFF6A4ABD, 0xFF465E3A, 0xFF5A97A3,
	0xFFBD8973, 0xFFAC778A, 0xFF5F5C41, 0xFFA5858B,
	0xFFC1AAB4, 0xFF874ABD, 0xFF8AD353, 0xFF2ECDB8,
	0xFFBD9C73, 0xFFC58995, 0xFFBAD016, 0xFFF5F5F5
};

typedef struct { uint8_t r, g, b, leaf; } tint_t;   /* 128 = unchanged */

static uint32_t tint_col(uint32_t c, int r, int g, int b)
{
	uint32_t cr = (uint32_t)((int)(c & 0xFF) * r / 128);
	uint32_t cg = (uint32_t)((int)((c >> 8) & 0xFF) * g / 128);
	uint32_t cb = (uint32_t)((int)((c >> 16) & 0xFF) * b / 128);
	if (cr > 255) cr = 255;
	if (cg > 255) cg = 255;
	if (cb > 255) cb = 255;
	return (c & 0xFF000000u) | (cb << 16) | (cg << 8) | cr;
}

static uint32_t shade(uint32_t c, int num)   /* num/100 */
{
	return tint_col(c, num * 128 / 100, num * 128 / 100, num * 128 / 100);
}

static void put_px(uint32_t *fb, int w, int h, int x, int y, uint32_t c)
{
	uint32_t a = c >> 24;
	uint32_t *d;
	if (x < 0 || y < 0 || x >= w || y >= h || !a)
		return;
	d = &fb[y * w + x];
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

static void fill_px(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, uint32_t c)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > w) x1 = w;
	if (y1 > h) y1 = h;
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			put_px(fb, w, h, x, y, c);
}

/*
 * Draw the (sx,sy,sw,sh) part of sprite `id` with its anchor (ax,ay) -- a
 * point inside that part -- at screen (dx,dy), magnified `scale` times.
 * Transparent pixels are skipped, translucent ones (shadows) are blended.
 * `tint` recolours the sprite (leaf-only: just its green pixels).
 */
static void blit(uint32_t *fb, int w, int h, int id,
                 int sx, int sy, int sw, int sh,
                 int ax, int ay, int dx, int dy, int scale, const tint_t *tint,
                 int flip)
{
	const nd_seven_sprite_rect_t *r = &nd_seven_sprite_rects[id];
	int ox = dx - ax * scale, oy = dy - ay * scale;

	if (ox >= w || oy >= h || ox + sw * scale <= 0 || oy + sh * scale <= 0)
		return;
	for (int y = 0; y < sh; y++) {
		const uint8_t *row = &s_atlas[(r->y + sy + y) * ND_SEVEN_ATLAS_W + r->x + sx];
		int yy0 = oy + y * scale;
		if (yy0 >= h || yy0 + scale <= 0)
			continue;
		for (int x = 0; x < sw; x++) {
			uint8_t srcx = (uint8_t)(flip ? (sw - 1 - x) : x);
			uint32_t c = nd_seven_palette[row[srcx]];
			int xx0 = ox + x * scale;
			if (!(c >> 24) || xx0 >= w || xx0 + scale <= 0)
				continue;
			if (tint) {
				int pr = (int)(c & 0xFF), pg = (int)((c >> 8) & 0xFF), pb = (int)((c >> 16) & 0xFF);
				if (!tint->leaf || (pg > pr + 8 && pg > pb + 8))
					c = tint_col(c, tint->r, tint->g, tint->b);
			}
			for (int py = 0; py < scale; py++)
				for (int px = 0; px < scale; px++)
					put_px(fb, w, h, xx0 + px, yy0 + py, c);
		}
	}
}

static void blit_all(uint32_t *fb, int w, int h, int id, int ax, int ay, int dx, int dy, int scale,
                     const tint_t *tint)
{
	const nd_seven_sprite_rect_t *r = &nd_seven_sprite_rects[id];
	blit(fb, w, h, id, 0, 0, r->w, r->h, ax, ay, dx, dy, scale, tint, 0);
}

static void blit_all_f(uint32_t *fb, int w, int h, int id, int ax, int ay, int dx, int dy,
                       int scale, const tint_t *tint, int flip)
{
	const nd_seven_sprite_rect_t *r = &nd_seven_sprite_rects[id];
	blit(fb, w, h, id, 0, 0, r->w, r->h, ax, ay, dx, dy, scale, tint, flip);
}

/* --- state -------------------------------------------------------------- */

enum { DIR_DOWN, DIR_UP, DIR_LEFT, DIR_RIGHT };
enum { HELD_U = 1, HELD_D = 2, HELD_L = 4, HELD_R = 8 };

static int16_t s_pos[4];       /* absolute (x, y, z, world) of the origin tile */
static int s_have;
static int s_lx, s_ly;         /* lamb feet, in origin tile units (may leave 0..TILE) */
static int s_dir = DIR_DOWN;
static int s_held;
static int s_last;             /* most recently pressed held direction bit */
static int s_walking;
static int s_pending_key;      /* move key sent, waiting for the new map */
static float s_pending_t;
static float s_acc;
static uint32_t s_anim;

static void recentre(int drop_keys)
{
	s_lx = NDW_CENTER_X;
	s_ly = NDW_CENTER_Y;
	s_pending_key = 0;
	s_pending_t = 0;
	if (drop_keys) {
		s_held = 0;
		s_last = 0;
	}
}

/* Expected origin shift (columns east, rows south) of a move key. */
static void key_delta(int key, int *dx, int *dj)
{
	*dx = key == 'l' ? 1 : key == 'h' ? -1 : 0;
	*dj = key == 'j' ? 1 : key == 'k' ? -1 : 0;
}

/*
 * Keep the lamb on the map. The origin tile is the one the server says we
 * are in. When it moved exactly as the pending key predicted, the lamb stays
 * where it is in the world (its origin-relative position shifts by a tile);
 * any other change (teleport, command, other level) recentres it.
 */
static void sync_origin(const nd_client_state_t *st)
{
	const int16_t *pos;

	if (!st || st->map_data.width == 0) {
		if (!s_have) {
			recentre(1);
			s_have = 1;
		}
		return;
	}
	pos = st->map_data.pos;
	if (!s_have) {
		memcpy(s_pos, pos, sizeof(s_pos));
		recentre(1);
		s_have = 1;
		return;
	}
	if (!memcmp(pos, s_pos, sizeof(s_pos)))
		return;
	{
		int dx = pos[0] - s_pos[0], dj = -(pos[1] - s_pos[1]);
		int ex = 0, ej = 0;
		if (s_pending_key)
			key_delta(s_pending_key, &ex, &ej);
		if (s_pending_key && pos[2] == s_pos[2] && pos[3] == s_pos[3] && dx == ex && dj == ej) {
			s_lx -= dx * NDW_TILE_W;
			s_ly -= dj * NDW_TILE_H;
			s_pending_key = 0;
			s_pending_t = 0;
		} else {
			recentre(1);
		}
		memcpy(s_pos, pos, sizeof(s_pos));
	}
}

/* --- the tiles ---------------------------------------------------------- */

typedef struct { const nd_client_state_t *st; } world_t;

static ndw_tile_t convert_tile(const nd_map_tile_t *m)
{
	ndw_tile_t t;
	memset(&t, 0, sizeof(t));
	t.biome = m->bio_idx;
	t.exits = m->exits & (NDW_EXIT_W | NDW_EXIT_N | NDW_EXIT_E | NDW_EXIT_S);
	if (m->flags & ND_MAP_FLAG_ROOM)   t.flags |= NDW_F_ROOM;
	if (m->flags & ND_MAP_FLAG_PLAYER) t.flags |= NDW_F_PLAYER;
	if (m->flags & ND_MAP_FLAG_ENTITY) t.flags |= NDW_F_ENTITY;
	t.glyph[0] = m->side_l; t.glyph[1] = m->symbol; t.glyph[2] = m->side_r;
	t.fg[0] = m->fg_l & 15; t.fg[1] = m->fg & 15; t.fg[2] = m->fg_r & 15;
	return t;
}

/*
 * The server paints '!' over the centre cell of the tile the player stands in,
 * erasing that tile's plant. Remember each tile's last non-player centre glyph
 * by absolute position (seen from outside) and restore it under the player.
 */
#define CENTRE_SETS 256
#define CENTRE_WAYS 4
typedef struct { int16_t pos[4]; uint8_t glyph, fg, used; } centre_ent_t;
static centre_ent_t s_centre[CENTRE_SETS][CENTRE_WAYS];
static uint8_t s_centre_next[CENTRE_SETS];     /* round-robin victim per set */

static unsigned centre_set(const int16_t pos[4])
{
	uint32_t h = (uint32_t)(uint16_t)pos[0] * 73856093u ^ (uint32_t)(uint16_t)pos[1] * 19349663u ^
	             (uint32_t)(uint16_t)pos[2] * 83492791u ^ (uint32_t)(uint16_t)pos[3] * 2654435761u;
	return (h ^ (h >> 15)) % CENTRE_SETS;
}

/* The way holding exactly this position (full pos[4] key), or NULL. */
static centre_ent_t *centre_find(unsigned set, const int16_t pos[4])
{
	for (int w = 0; w < CENTRE_WAYS; w++) {
		centre_ent_t *e = &s_centre[set][w];
		if (e->used && !memcmp(e->pos, pos, sizeof(e->pos)))
			return e;
	}
	return NULL;
}

void nd_seven_forget(void)
{
	memset(s_centre, 0, sizeof(s_centre));
	memset(s_centre_next, 0, sizeof(s_centre_next));
}

static void centre_remember(const int16_t pos[4], ndw_tile_t *t)
{
	unsigned set = centre_set(pos);
	centre_ent_t *e = centre_find(set, pos);

	if (t->glyph[1] == '!') {
		if (e) {
			t->glyph[1] = e->glyph;
			t->fg[1] = e->fg;
		} else {
			t->glyph[1] = 0;     /* never seen from outside: nothing known */
			t->fg[1] = 0;
		}
		return;
	}
	if (!e) {
		for (int w = 0; w < CENTRE_WAYS && !e; w++)
			if (!s_centre[set][w].used)
				e = &s_centre[set][w];
		if (!e) {                /* set full: evict round-robin */
			e = &s_centre[set][s_centre_next[set]];
			s_centre_next[set] = (uint8_t)((s_centre_next[set] + 1) % CENTRE_WAYS);
		}
		memcpy(e->pos, pos, sizeof(e->pos));
	}
	e->glyph = t->glyph[1];
	e->fg = t->fg[1];
	e->used = 1;
}

/*
 * Tile at offset (ti, tj) from the origin tile. 1: a tile the map describes;
 * 2: beyond the map, the nearest edge tile's biome and flora carry on; the
 * pos is the tile's absolute coordinates either way.
 */
static int tile_at(const nd_client_state_t *st, int ti, int tj, ndw_tile_t *t, int16_t pos[4])
{
	const nd_map_data_t *md = st ? &st->map_data : NULL;

	pos[0] = (int16_t)(s_pos[0] + ti);
	pos[1] = (int16_t)(s_pos[1] - tj);
	pos[2] = s_pos[2];
	pos[3] = s_pos[3];
	if (!md || md->width == 0 || md->height == 0) {
		memset(t, 0, sizeof(*t));
		t->biome = 10;                         /* grassland until a map arrives */
		t->exits = NDW_EXIT_W | NDW_EXIT_N | NDW_EXIT_E | NDW_EXIT_S;
		return 2;
	}
	{
		int w = md->width > ND_MAP_GRID_WIDTH ? ND_MAP_GRID_WIDTH : md->width;
		int h = md->height > ND_MAP_GRID_HEIGHT ? ND_MAP_GRID_HEIGHT : md->height;
		int col = md->player_x + ti, row = md->player_y + tj;
		int known = col >= 0 && row >= 0 && col < w && row < h;
		if (col < 0) col = 0;
		if (row < 0) row = 0;
		if (col >= w) col = w - 1;
		if (row >= h) row = h - 1;
		*t = convert_tile(&md->tiles[row * md->width + col]);
		if (known) {
			centre_remember(pos, t);
			return 1;
		}
		/* Beyond the described map: keep the biome so the ground continues,
		 * but drop the clamped neighbour's glyphs. They belong to a tile at
		 * a different coordinate; mirroring them (a room-centre tree, shop,
		 * stairs) leaves stray objects floating in void space (R1). */
		t->flags &= (uint8_t)~(NDW_F_ROOM | NDW_F_PLAYER);
		t->exits = NDW_EXIT_W | NDW_EXIT_N | NDW_EXIT_E | NDW_EXIT_S;
		memset(t->glyph, 0, sizeof(t->glyph));
		memset(t->fg, 0, sizeof(t->fg));
		return 2;
	}
}

/* Collision lookup: the lamb may only enter the origin tile and its eight
 * neighbours (the ones the server can confirm one step at a time). */
static int lookup(void *ctx, int ti, int tj, ndw_tile_t *t, int16_t pos[4])
{
	const world_t *wd = (const world_t *)ctx;
	if (ti < -1 || ti > 1 || tj < -1 || tj > 1)
		return 0;
	return tile_at(wd->st, ti, tj, t, pos) == 1 ? 1 : 0;
}

void nd_seven_lamb(int *fx, int *fy, int *dir, int *walking)
{
	if (fx) *fx = s_lx;
	if (fy) *fy = s_ly;
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
		s_last = bit;          /* newest key wins, like seven-ways */
	} else {
		s_held &= ~bit;
		if (s_last == bit)
			s_last = 0;
	}
	return 1;
}

/* Try to move one unit along an axis; blocked moves leave the lamb alone. */
static void move_axis(world_t *wd, int dx, int dy)
{
	int nx = s_lx + dx, ny = s_ly + dy;
	if (ndw_blocked(lookup, wd, nx, ny))
		return;
	s_lx = nx;
	s_ly = ny;
}

int nd_seven_tick(const nd_client_state_t *st, float dt)
{
	world_t wd = { st };
	int key = 0, dx = 0, dy = 0, steps;

	s_anim++;
	sync_origin(st);
	if (dt > 0.1f)
		dt = 0.1f;

	if (s_pending_key) {
		s_pending_t -= dt;
		if (s_pending_t <= 0) {
			/* The server never moved us (a wall it knows and we do not):
			 * the world did not change, so put the lamb back in it. */
			recentre(0);
		}
	}

	/* One cardinal direction at a time (seven-ways): the newest held key,
	 * else any key still held, so direction can be changed freely. */
	{
		int b = (s_last && (s_held & s_last)) ? s_last : (s_held & -s_held);
		if (b == HELD_L) { dx = -1; s_dir = DIR_LEFT; }
		else if (b == HELD_R) { dx = 1; s_dir = DIR_RIGHT; }
		else if (b == HELD_U) { dy = -1; s_dir = DIR_UP; }
		else if (b == HELD_D) { dy = 1; s_dir = DIR_DOWN; }
	}

	s_walking = 0;
	if (!(dx || dy)) {
		s_acc = 0;
		return 0;
	}
	s_acc += dt * SPEED;
	steps = (int)s_acc;
	s_acc -= (float)steps;
	while (steps-- > 0) {
		int ox = s_lx, oy = s_ly;
		move_axis(&wd, dx, dy);
		if (ox != s_lx || oy != s_ly)
			s_walking = 1;
	}

	/* Walked into a neighbouring tile: ask the server to follow. */
	if (!s_pending_key) {
		int ti = ndw_fdiv(s_lx, NDW_TILE_W), tj = ndw_fdiv(s_ly, NDW_TILE_H);
		if (ti || tj) {
			key = ti > 0 ? 'l' : ti < 0 ? 'h' : tj > 0 ? 'j' : 'k';
			s_pending_key = key;
			s_pending_t = PENDING_S;
		}
	}
	return key;
}

/* --- procedural pixel art ---------------------------------------------- */

typedef struct {
	uint32_t *fb;
	int w, h;
	int ox, oy;      /* screen pixel of unit (0,0) */
} ctx_t;

static void urect(const ctx_t *c, int ux, int uy, int uw, int uh, uint32_t col)
{
	fill_px(c->fb, c->w, c->h,
	        c->ox + ux * SCALE, c->oy + uy * SCALE,
	        c->ox + (ux + uw) * SCALE, c->oy + (uy + uh) * SCALE, col);
}

/* Filled ellipse centred on (ux, uy), radii in units. */
static void uellipse(const ctx_t *c, int ux, int uy, int rx, int ry, uint32_t col)
{
	for (int dy = -ry; dy <= ry; dy++) {
		int hw = rx;
		while (hw > 0 && hw * hw * ry * ry + dy * dy * rx * rx > rx * rx * ry * ry)
			hw--;
		urect(c, ux - hw, uy + dy, 2 * hw + 1, 1, col);
	}
}

static void shadow(const ctx_t *c, int rx)
{
	uellipse(c, 0, -1, rx, 3, 0x55000000u);
}

/* 3x5 pixel font for the glyphs map cells carry. */
static const uint8_t *font_rows(int ch)
{
	static const uint8_t f[128][5] = {
		['0'] = {7,5,5,5,7}, ['1'] = {2,6,2,2,7}, ['2'] = {7,1,7,4,7}, ['3'] = {7,1,7,1,7},
		['4'] = {5,5,7,1,1}, ['5'] = {7,4,7,1,7}, ['6'] = {7,4,7,5,7}, ['7'] = {7,1,1,1,1},
		['8'] = {7,5,7,5,7}, ['9'] = {7,5,7,1,7},
		['A'] = {2,5,7,5,5}, ['B'] = {6,5,6,5,6}, ['C'] = {3,4,4,4,3}, ['D'] = {6,5,5,5,6},
		['E'] = {7,4,6,4,7}, ['F'] = {7,4,6,4,4}, ['G'] = {3,4,5,5,3}, ['H'] = {5,5,7,5,5},
		['I'] = {7,2,2,2,7}, ['J'] = {1,1,1,5,2}, ['K'] = {5,5,6,5,5}, ['L'] = {4,4,4,4,7},
		['M'] = {5,7,7,5,5}, ['N'] = {6,5,5,5,5}, ['O'] = {2,5,5,5,2}, ['P'] = {6,5,6,4,4},
		['Q'] = {2,5,5,6,3}, ['R'] = {6,5,6,5,5}, ['S'] = {3,4,2,1,6}, ['T'] = {7,2,2,2,2},
		['U'] = {5,5,5,5,7}, ['V'] = {5,5,5,5,2}, ['W'] = {5,5,7,7,5}, ['X'] = {5,5,2,5,5},
		['Y'] = {5,5,2,2,2}, ['Z'] = {7,1,2,4,7},
		['!'] = {2,2,2,0,2}, ['$'] = {3,6,2,3,6}, ['~'] = {0,3,6,0,0}, ['*'] = {5,2,7,2,5},
		['>'] = {4,2,1,2,4}, ['<'] = {1,2,4,2,1}, ['.'] = {0,0,0,0,2}, ['#'] = {5,7,5,7,5},
		['+'] = {0,2,7,2,0}, ['@'] = {7,5,7,4,7}, ['?'] = {7,1,2,0,2},
	};
	if (ch >= 'a' && ch <= 'z')
		ch -= 32;
	if (ch <= 0 || ch >= 128)
		return NULL;
	for (int i = 0; i < 5; i++)
		if (f[ch][i])
			return f[ch];
	return NULL;
}

static void draw_glyph(const ctx_t *c, int ux, int uy, int ch, uint32_t col)
{
	const uint8_t *rows = font_rows(ch);
	if (!rows)
		return;
	for (int r = 0; r < 5; r++)
		for (int k = 0; k < 3; k++)
			if (rows[r] & (4 >> k))
				urect(c, ux + k, uy + r, 1, 1, col);
}

/* A small standing figure for whatever lives in a map cell or the room list:
 * coloured like the ASCII map glyph, carrying that glyph above its head. */
static void draw_pawn(const ctx_t *c, int glyph, int fg)
{
	uint32_t col = s_ansi[fg & 15];
	uint32_t dark = shade(col, 50), light = shade(col, 135);
	shadow(c, 7);
	urect(c, -5, -11, 10, 11, dark);
	urect(c, -4, -10, 8, 9, col);
	urect(c, -4, -10, 2, 9, light);
	urect(c, -3, -17, 6, 6, RGB(40, 30, 30));
	urect(c, -2, -16, 4, 4, RGB(232, 196, 160));
	urect(c, -1, -15, 1, 1, RGB(30, 30, 40));
	urect(c, 1, -15, 1, 1, RGB(30, 30, 40));
	if (font_rows(glyph)) {
		urect(c, -3, -25, 7, 7, 0xAA101018u);
		draw_glyph(c, -1, -24, glyph, col);
	}
}

static void draw_cactus(const ctx_t *c, int big, int fg, int variant)
{
	uint32_t body = shade(s_ansi[fg & 15], 85);
	uint32_t dark = shade(body, 52), light = shade(body, 130);
	int bw = big ? 8 : 6, bh = big ? 36 : 25, aw = big ? 5 : 4, ah = big ? 10 : 8;
	int a1 = bh * 55 / 100, a2 = bh * 38 / 100;
	int flip = variant & 1;
	int lx = -bw / 2 - aw, rx = bw / 2 + (bw & 1);
	int lh = a1, rh = a2;

	if (flip) { lh = a2; rh = a1; }
	shadow(c, bw + 4);

	/* outline pass, then fill pass: the arms read as one plant */
	for (int pass = 0; pass < 2; pass++) {
		int g = pass == 0 ? 1 : 0;
		uint32_t col = pass == 0 ? dark : body;
		urect(c, -bw / 2 - g, -bh - g, bw + 2 * g, bh + g, col);
		urect(c, lx - g, -lh - g, aw + bw / 2 + 2 * g, 3 + 2 * g, col);
		urect(c, lx - g, -lh - ah - g, 3 + 2 * g, ah + 3 + 2 * g, col);
		urect(c, -bw / 2 - g + bw, -rh - g, aw + 2 * g, 3 + 2 * g, col);
		urect(c, rx + aw - 3 - g, -rh - ah - g, 3 + 2 * g, ah + 3 + 2 * g, col);
	}
	/* lit side, ribs, spines */
	urect(c, -bw / 2, -bh, 2, bh, light);
	urect(c, lx, -lh - ah, 1, ah + 3, light);
	urect(c, rx + aw - 3, -rh - ah, 1, ah + 3, light);
	urect(c, 0, -bh + 2, 1, bh - 3, dark);
	for (int y = 4; y < bh - 2; y += 5) {
		urect(c, -bw / 2 - 1, -y, 1, 1, RGB(236, 226, 160));
		urect(c, bw / 2 + (bw & 1), -y - 2, 1, 1, RGB(236, 226, 160));
	}
	urect(c, -1, -bh - 1, 2, 1, RGB(230, 120, 150));   /* the flower on top */
}

static void draw_fir(const ctx_t *c, int big, int fg)
{
	uint32_t base = shade(s_ansi[fg & 15], 80);
	int tiers = big ? 4 : 3, tierh = big ? 10 : 9, maxhw = big ? 15 : 11;
	int top = -(tiers * (tierh - 3) + 9);

	shadow(c, maxhw);
	urect(c, -2, -7, 4, 7, RGB(60, 40, 24));
	urect(c, -1, -7, 2, 7, RGB(110, 76, 44));
	for (int t = 0; t < tiers; t++) {
		int y0 = top + t * (tierh - 3);
		int span = (maxhw - 2) / tiers;
		for (int r = 0; r < tierh; r++) {
			int hw = 2 + t * span + (r + 1) * span / tierh + (r > 0 ? 1 : 0);
			uint32_t col = shade(base, 128 - t * 8);
			urect(c, -hw - 1, y0 + r, 2 * hw + 2, 1, shade(base, 48));
			urect(c, -hw, y0 + r, hw, 1, shade(col, 118));
			urect(c, 0, y0 + r, hw, 1, shade(col, 84));
		}
		urect(c, -(2 + t * span + span + 1), y0 + tierh - 1, 2 * (2 + t * span + span + 1), 1, shade(base, 52));
	}
}

static void draw_pond(const ctx_t *c, int anim)
{
	uellipse(c, 0, -7, 24, 12, RGB(86, 98, 80));
	uellipse(c, 0, -7, 22, 10, RGB(44, 96, 170));
	uellipse(c, 0, -7, 15, 6, RGB(60, 120, 196));
	for (int i = 0; i < 4; i++) {
		int ux = -12 + i * 7 + ((anim / 14 + i) & 3);
		urect(c, ux, -11 + (i & 1) * 6, 4, 1, RGB(170, 210, 240));
	}
}

static void draw_shop(const ctx_t *c, int fg)
{
	uint32_t roof = shade(s_ansi[fg & 15] ? s_ansi[fg & 15] : RGB(170, 60, 50), 100);
	shadow(c, 20);
	urect(c, -17, -23, 34, 23, RGB(70, 50, 36));
	urect(c, -16, -22, 32, 22, RGB(206, 176, 126));
	urect(c, -16, -22, 32, 2, RGB(226, 198, 150));
	for (int k = 0; k < 9; k++) {
		int hw = 11 + k * 2;
		urect(c, -hw - 1, -32 + k, 2 * hw + 2, 1, shade(roof, 55));
		urect(c, -hw, -32 + k, 2 * hw, 1, shade(roof, 90 + (k & 1) * 14));
	}
	urect(c, -4, -14, 8, 14, RGB(86, 52, 30));
	urect(c, -3, -13, 6, 13, RGB(120, 76, 42));
	urect(c, 8, -16, 6, 6, RGB(150, 200, 230));
	urect(c, -14, -16, 6, 6, RGB(150, 200, 230));
	draw_glyph(c, -1, -30, '$', RGB(250, 230, 120));
}

static void draw_stairs(const ctx_t *c, int up)
{
	if (up) {
		for (int k = 0; k < 4; k++) {
			urect(c, -12 + k * 2, -3 * (k + 1), 24 - k * 4, 3, RGB(110, 110, 124));
			urect(c, -12 + k * 2, -3 * (k + 1), 24 - k * 4, 1, RGB(170, 170, 186));
		}
	} else {
		urect(c, -13, -11, 26, 11, RGB(20, 18, 24));
		for (int k = 0; k < 4; k++)
			urect(c, -11 + k * 2, -9 + k * 2, 22 - k * 4, 1, RGB(90, 90, 104));
	}
}

/* --- ground ------------------------------------------------------------- */

static uint32_t unit_noise(uint32_t h, int ux, int uy)
{
	return ndw_mix(h ^ (uint32_t)(uy * 16 + ux + 1) * 0x9E3779B1u);
}

/* One 16x16 unit ground cell with its top-left corner at screen (px, py). */
static void ground_cell(uint32_t *fb, int w, int h, const ndw_ground_t *g,
                        uint32_t hash, int px, int py, uint32_t anim)
{
	ctx_t c = { fb, w, h, px, py };
	uint32_t base, alt1, alt2;
	int r = g->r, gg = g->g, b = g->b;

	if (px >= w || py >= h || px + NDW_CELL_PX * SCALE <= 0 || py + NDW_CELL_PX * SCALE <= 0)
		return;

	if (g->kind == NDW_G_GRASS) {
		tint_t t = { g->r, g->g, g->b, 0 };
		blit_all(fb, w, h, NDS_GRASS, 0, 0, px, py, SCALE, &t);
		if ((hash % 100) < 6) {
			int id = NDS_DECOR_YEL + (int)((hash >> 8) & 1);
			const nd_seven_sprite_rect_t *sr = &nd_seven_sprite_rects[id];
			blit_all(fb, w, h, id, sr->w / 2, sr->h - 1,
			         px + (4 + (int)((hash >> 12) & 7)) * SCALE, py + 14 * SCALE, SCALE, NULL);
		}
		return;
	}

	switch (g->kind) {
	case NDW_G_SAND:
		base = RGB(226, 200, 140); alt1 = RGB(210, 182, 122); alt2 = RGB(238, 214, 160);
		break;
	case NDW_G_SNOW:
		base = RGB(234, 240, 250); alt1 = RGB(210, 222, 240); alt2 = RGB(252, 252, 255);
		break;
	case NDW_G_TUNDRA:
		base = RGB(150, 166, 148); alt1 = RGB(128, 148, 126); alt2 = RGB(180, 190, 176);
		break;
	case NDW_G_WATER:
		base = RGB(46, 100, 172); alt1 = RGB(36, 84, 152); alt2 = RGB(98, 154, 214);
		break;
	default: /* ash */
		base = RGB(54, 48, 52); alt1 = RGB(40, 36, 40); alt2 = RGB(76, 66, 68);
		break;
	}
	base = tint_col(base, r, gg, b);
	alt1 = tint_col(alt1, r, gg, b);
	alt2 = tint_col(alt2, r, gg, b);

	urect(&c, 0, 0, NDW_CELL_PX, NDW_CELL_PX, base);
	for (int uy = 0; uy < NDW_CELL_PX; uy++)
		for (int ux = 0; ux < NDW_CELL_PX; ux++) {
			uint32_t n = unit_noise(hash, ux, uy) & 31;
			if (g->kind == NDW_G_WATER) {
				/* drifting wave crests */
				if (((ux + 3 * uy + (int)(anim / 12) + (int)(hash & 7)) % 11) == 0)
					urect(&c, ux, uy, 1, 1, alt2);
				else if (n == 0)
					urect(&c, ux, uy, 1, 1, alt1);
			} else if (n < 2) {
				urect(&c, ux, uy, 1, 1, alt1);
			} else if (n == 2) {
				urect(&c, ux, uy, 1, 1, alt2);
			}
		}

	if (g->kind == NDW_G_SAND && (((hash >> 8) & 3) == 0)) {
		/* a dune ripple */
		static const int8_t arc[8] = { 1, 0, 0, -1, -1, 0, 0, 1 };
		int ux0 = 2 + (int)((hash >> 12) & 3), uy0 = 5 + (int)((hash >> 15) & 7);
		for (int k = 0; k < 8; k++) {
			urect(&c, ux0 + k, uy0 + arc[k], 1, 1, shade(alt1, 92));
			urect(&c, ux0 + k, uy0 + arc[k] - 1, 1, 1, alt2);
		}
	}
	if (g->kind == NDW_G_ASH && (hash % 23) == 0) {
		/* a glowing crack */
		int ux0 = 3 + (int)((hash >> 8) & 3), uy0 = 4 + (int)((hash >> 12) & 7);
		for (int k = 0; k < 8; k++)
			urect(&c, ux0 + k, uy0 + ((k >> 1) & 1), 1, 1, RGB(226, 96, 28));
	}
	if (g->kind == NDW_G_SNOW && (hash % 17) == 0)
		urect(&c, 3 + (int)((hash >> 8) & 7), 3 + (int)((hash >> 12) & 7), 2, 1, RGB(255, 255, 255));
}

/* --- drawing ------------------------------------------------------------ */

static void plant_tint(int fg, tint_t *t)
{
	t->leaf = 1;
	switch (fg & 15) {
	case 3: case 11: t->r = 230; t->g = 118; t->b = 60; break;   /* yellow: autumn */
	case 1: case 9:  t->r = 255; t->g = 70;  t->b = 64; break;   /* red */
	case 7: case 15: t->r = 200; t->g = 150; t->b = 190; break;  /* white: pale */
	default:         t->r = t->g = t->b = 128; break;
	}
}

static void draw_world_obj(uint32_t *fb, int w, int h, const ndw_obj_t *o, int sx, int sy, uint32_t anim)
{
	ctx_t c = { fb, w, h, sx, sy };
	const nd_seven_sprite_rect_t *r;
	tint_t t;
	int id;

	switch (o->kind) {
	case NDW_K_DECID:
		id = o->big ? ((o->variant & 1) ? NDS_TREE_OAK : NDS_TREE_APPLE) : NDS_TREE_ROUND;
		goto tree;
	case NDW_K_TALL:
		id = o->big ? NDS_TREE_BIG : NDS_TREE_APPLE2;
	tree:
		plant_tint(o->fg, &t);
		r = &nd_seven_sprite_rects[id];
		blit_all(fb, w, h, id, r->w / 2, r->h - 1, sx, sy, SCALE, &t);
		break;
	case NDW_K_FIR:      draw_fir(&c, o->big, o->fg); break;
	case NDW_K_CACTUS:   draw_cactus(&c, o->big, o->fg, o->variant); break;
	case NDW_K_POND:     draw_pond(&c, (int)anim); break;
	case NDW_K_SHOP:     draw_shop(&c, o->fg); break;
	case NDW_K_STAIRS_DOWN: draw_stairs(&c, 0); break;
	case NDW_K_STAIRS_UP:   draw_stairs(&c, 1); break;
	default:             draw_pawn(&c, o->glyph, o->fg); break;
	}
}

static void draw_lamb(uint32_t *fb, int w, int h, int sx, int sy)
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
	     LAMB_AX, LAMB_AY, sx, sy, SCALE, NULL, 0);
}

/* One thing to paint in a row of tiles, sorted back to front by its feet. */
enum { D_OBJ, D_ENTITY, D_LAMB };
typedef struct {
	int sx, sy;            /* screen position of the feet */
	uint8_t what;
	ndw_obj_t obj;         /* D_OBJ */
	char ch;               /* D_ENTITY */
	uint8_t fg;
	uint8_t sprite_id;     /* if non-zero, drawn as rock/stone sprite instead of pawn */
	uint8_t flip;
} drawable_t;

#define MAX_DRAW 192

/* Entity figures stand on fixed spots around the arrival point. */
static const int8_t s_spots[16][2] = {
	{ -34, -8 }, { 34, -8 }, { -22, 16 }, { 22, 16 }, { 0, 24 }, { -12, -20 }, { 12, -20 }, { 0, -30 },
	{ -52, 0 }, { 52, 0 }, { -46, 24 }, { 46, 24 }, { -30, 34 }, { 30, 34 }, { -46, -22 }, { 46, -22 }
};

static int str_has_ci(const char *hay, const char *needle)
{
	if (!hay || !needle || !*needle) return 0;
	size_t nlen = strlen(needle);
	for (const char *h = hay; *h; h++) {
		size_t i = 0;
		while (i < nlen && h[i]) {
			char c1 = h[i], c2 = needle[i];
			if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 + 32);
			if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 + 32);
			if (c1 != c2) break;
			i++;
		}
		if (i == nlen) return 1;
	}
	return 0;
}

static int is_mineral_or_rock(const nd_entity_desc_t *e)
{
	if (!e) return 0;
	if (e->ch == '*')
		return 1;
	return str_has_ci(e->name, "stone") || str_has_ci(e->name, "rock") ||
	       str_has_ci(e->name, "mineral") || str_has_ci(e->name, "quartz") ||
	       str_has_ci(e->name, "flint");
}

static int add_entities(const nd_client_state_t *st, int camx, int camy, drawable_t *out, int n)
{
	uint32_t used = 0;
	uint32_t room = st ? st->room.ref : 0;

	if (!st)
		return n;
	for (size_t i = 0; i < st->entity_count && n < MAX_DRAW; i++) {
		const nd_entity_desc_t *e = &st->entities[i];
		uint32_t spot;
		int tries = 0;

		if (st->me && e->ref == st->me)
			continue;
		if (room && e->location != room)
			continue;
		spot = ndw_mix(e->ref) % 16;
		while ((used >> spot) & 1) {
			spot = (spot + 1) % 16;
			if (++tries >= 16)
				break;
		}
		if (tries >= 16)
			break;             /* the room list is longer than the room */
		used |= 1u << spot;
		out[n].what = D_ENTITY;
		out[n].ch = e->ch ? e->ch : '?';
		out[n].fg = (uint8_t)(e->fg < 8 ? e->fg + 8 : e->fg);
		out[n].sx = (NDW_CENTER_X + s_spots[spot][0]) * SCALE - camx;
		out[n].sy = (NDW_CENTER_Y + s_spots[spot][1]) * SCALE - camy;
		out[n].flip = (uint8_t)((ndw_mix(e->ref) >> 8) & 1);
		if (is_mineral_or_rock(e)) {
			/* Mineral entity drawn as rock/stone sprite, NOT tree or generic pawn */
			static const uint8_t mineral_sprites[] = {
				NDS_STONE_GREY, NDS_STONE_BLUE, NDS_STONE_BLUE_SM, NDS_STONE_FLAT,
				NDS_STONE_TINY, NDS_ROCK_TAN_SM, NDS_ROCK_TAN_TINY, NDS_ROCK_GREY
			};
			out[n].sprite_id = mineral_sprites[ndw_mix(e->ref) % (sizeof(mineral_sprites)/sizeof(mineral_sprites[0]))];
		} else {
			out[n].sprite_id = 0;
		}
		n++;
	}
	return n;
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
	sync_origin(st);

	for (int i = 0; i < w * h; i++)
		fb[i] = 0xFF08080C;

	/* The lamb's feet are the centre of the screen; the world moves. */
	camx = s_lx * SCALE - w / 2;
	camy = s_ly * SCALE - h / 2;
	i0 = ndw_fdiv(camx, NDW_TILE_W * SCALE);
	i1 = ndw_fdiv(camx + w - 1, NDW_TILE_W * SCALE);
	j0 = ndw_fdiv(camy, NDW_TILE_H * SCALE);
	j1 = ndw_fdiv(camy + h - 1, NDW_TILE_H * SCALE);
	lamb_row = ndw_fdiv(s_ly, NDW_TILE_H);

	/* ground, then walls, over every visible tile */
	for (int tj = j0; tj <= j1; tj++)
		for (int ti = i0; ti <= i1; ti++) {
			ndw_tile_t t;
			int16_t pos[4];
			ndw_ground_t g;
			ndw_rect_t walls[NDW_MAX_WALL];
			int tx = ti * NDW_TILE_W * SCALE - camx, ty = tj * NDW_TILE_H * SCALE - camy;

			tile_at(st, ti, tj, &t, pos);
			g = ndw_biome_style(t.biome);
			for (int cy = 0; cy < NDW_ROWS; cy++)
				for (int cx = 0; cx < NDW_COLS; cx++)
					ground_cell(fb, w, h, &g, ndw_cell_hash(pos, cx, cy),
					            tx + cx * NDW_CELL_PX * SCALE, ty + cy * NDW_CELL_PX * SCALE, s_anim);
			for (int k = ndw_walls(&t, walls) - 1; k >= 0; k--)
				for (int cy = walls[k].y0 / NDW_CELL_PX; cy * NDW_CELL_PX < walls[k].y1; cy++)
					for (int cx = walls[k].x0 / NDW_CELL_PX; cx * NDW_CELL_PX < walls[k].x1; cx++)
						blit_all(fb, w, h, NDS_WALL + (int)((cx * 7 + cy * 13 + (cx ^ cy)) & 3), 0, 0,
						         tx + cx * NDW_CELL_PX * SCALE, ty + cy * NDW_CELL_PX * SCALE, SCALE, NULL);
		}

	/* objects, entities and the lamb, row of tiles by row, back to front.
	 * A tile row below the screen still paints (trees reach up into it). */
	for (int tj = j0; tj <= j1 + 1; tj++) {
		int n = 0;

		for (int ti = i0 - 1; ti <= i1 + 1 && n < MAX_DRAW - NDW_MAX_OBJ; ti++) {
			ndw_tile_t t;
			int16_t pos[4];
			ndw_obj_t objs[NDW_MAX_OBJ];
			int kind = tile_at(st, ti, tj, &t, pos);
			int cnt = ndw_tile_objects(&t, pos, kind == 2, objs);
			int tx = ti * NDW_TILE_W * SCALE - camx, ty = tj * NDW_TILE_H * SCALE - camy;

			for (int k = 0; k < cnt; k++) {
				dr[n].what = D_OBJ;
				dr[n].obj = objs[k];
				dr[n].sx = tx + objs[k].x * SCALE;
				dr[n].sy = ty + objs[k].y * SCALE;
				n++;
			}
		}
		if (tj == 0)
			n = add_entities(st, camx, camy, dr, n);
		if (tj == lamb_row && n < MAX_DRAW) {
			dr[n].what = D_LAMB;
			dr[n].sx = w / 2;
			dr[n].sy = h / 2;
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
				draw_world_obj(fb, w, h, &dr[a].obj, dr[a].sx, dr[a].sy, s_anim);
			}
		}
	}
}

void nd_seven_debug(int *pending, int *pos_x, int *pos_y, int *have_room)
{
	if (pending) *pending = s_pending_key;
	if (pos_x) *pos_x = (int)s_pos[0];
	if (pos_y) *pos_y = (int)s_pos[1];
	if (have_room) *have_room = s_have;
}
