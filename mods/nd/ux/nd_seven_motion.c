#include "nd_seven_motion.h"
#include "nd_seven_objects.h"
#include "nd_seven_ground.h"
#include <string.h>

#define SCALE 2
#define SPEED 72.0f          /* units/second */
#define PENDING_S 2.0f        /* wait this long for the server to confirm a move */

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

int nd_seven_get_lx(void) { return s_lx; }
int nd_seven_get_ly(void) { return s_ly; }
int nd_seven_get_dir(void) { return s_dir; }
int nd_seven_get_walking(void) { return s_walking; }
uint32_t nd_seven_get_anim(void) { return s_anim; }
void nd_seven_step_anim(uint32_t delta) { s_anim += delta; }

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

static void key_delta(int key, int *dx, int *dj)
{
	*dx = key == 'l' ? 1 : key == 'h' ? -1 : 0;
	*dj = key == 'j' ? 1 : key == 'k' ? -1 : 0;
}

void nd_seven_sync_origin(const nd_client_state_t *st)
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
			recentre(0);
		}
		memcpy(s_pos, pos, sizeof(s_pos));
	}
}

ndw_tile_t nd_seven_convert_tile(const nd_map_tile_t *m)
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

#define CENTRE_SETS 256
#define CENTRE_WAYS 4
typedef struct { int16_t pos[4]; uint8_t glyph, fg, used; } centre_ent_t;
static centre_ent_t s_centre[CENTRE_SETS][CENTRE_WAYS];
static uint8_t s_centre_next[CENTRE_SETS];

static unsigned centre_set(const int16_t pos[4])
{
	uint32_t h = (uint32_t)(uint16_t)pos[0] * 73856093u ^ (uint32_t)(uint16_t)pos[1] * 19349663u ^
	             (uint32_t)(uint16_t)pos[2] * 83492791u ^ (uint32_t)(uint16_t)pos[3] * 2654435761u;
	return (h ^ (h >> 15)) % CENTRE_SETS;
}

static centre_ent_t *centre_find(unsigned set, const int16_t pos[4])
{
	for (int w = 0; w < CENTRE_WAYS; w++) {
		centre_ent_t *e = &s_centre[set][w];
		if (e->used && !memcmp(e->pos, pos, sizeof(e->pos)))
			return e;
	}
	return NULL;
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
			t->glyph[1] = 0;
			t->fg[1] = 0;
		}
		return;
	}
	if (!e) {
		for (int w = 0; w < CENTRE_WAYS && !e; w++)
			if (!s_centre[set][w].used)
				e = &s_centre[set][w];
		if (!e) {
			e = &s_centre[set][s_centre_next[set]];
			s_centre_next[set] = (uint8_t)((s_centre_next[set] + 1) % CENTRE_WAYS);
		}
		memcpy(e->pos, pos, sizeof(e->pos));
	}
	e->glyph = t->glyph[1];
	e->fg = t->fg[1];
	e->used = 1;
}

void nd_seven_forget(void)
{
	memset(s_centre, 0, sizeof(s_centre));
	memset(s_centre_next, 0, sizeof(s_centre_next));
	nd_seven_objects_forget();
	nd_seven_ground_cache_forget();
}

int nd_seven_tile_at(const nd_client_state_t *st, int ti, int tj, ndw_tile_t *t, int16_t pos[4])
{
	const nd_map_data_t *md;
	int col, row, w, h, known;

	if (!st)
		return 0;
	md = &st->map_data;
	w = md->width;
	h = md->height;
	if (w == 0 || h == 0)
		return 0;

	col = md->player_x + ti;
	row = md->player_y + tj;

	pos[0] = (int16_t)(md->pos[0] + ti);
	pos[1] = (int16_t)(md->pos[1] - tj);
	pos[2] = md->pos[2];
	pos[3] = md->pos[3];

	known = (col >= 0 && col < w && row >= 0 && row < h);

	if (known) {
		*t = nd_seven_convert_tile(&md->tiles[row * w + col]);
		centre_remember(pos, t);
		return 1;
	} else {
		if (col < 0) col = 0;
		if (row < 0) row = 0;
		if (col >= w) col = w - 1;
		if (row >= h) row = h - 1;
		*t = nd_seven_convert_tile(&md->tiles[row * md->width + col]);
		t->flags &= (uint8_t)~(NDW_F_ROOM | NDW_F_PLAYER);
		t->exits = NDW_EXIT_W | NDW_EXIT_N | NDW_EXIT_E | NDW_EXIT_S;
		memset(t->glyph, 0, sizeof(t->glyph));
		memset(t->fg, 0, sizeof(t->fg));
		return 2;
	}
}

typedef struct { const nd_client_state_t *st; } world_t;

static int lookup(void *ctx, int ti, int tj, ndw_tile_t *t, int16_t pos[4])
{
	const world_t *wd = (const world_t *)ctx;
	if (ti < -1 || ti > 1 || tj < -1 || tj > 1)
		return 0;
	return nd_seven_tile_at(wd->st, ti, tj, t, pos) == 1 ? 1 : 0;
}

void nd_seven_lamb(int *fx, int *fy, int *dir, int *walking)
{
	if (fx) *fx = s_lx;
	if (fy) *fy = s_ly;
	if (dir) *dir = s_dir;
	if (walking) *walking = s_walking;
}

void nd_seven_set_lamb(int fx, int fy)
{
	s_lx = fx;
	s_ly = fy;
}

void nd_seven_lamb_world(const nd_client_state_t *st, int *fx, int *fy)
{
	nd_seven_sync_origin(st);
	if (fx) *fx = s_lx;
	if (fy) *fy = s_ly;
}

void nd_seven_debug(int *pending, int *pos_x, int *pos_y, int *have_room)
{
	if (pending) *pending = s_pending_key;
	if (pos_x) *pos_x = (int)s_pos[0];
	if (pos_y) *pos_y = (int)s_pos[1];
	if (have_room) *have_room = s_have;
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
		s_last = bit;
	} else {
		s_held &= ~bit;
		if (s_last == bit)
			s_last = 0;
	}
	return 1;
}

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
	nd_seven_sync_origin(st);
	if (dt > 0.1f)
		dt = 0.1f;

	if (s_pending_key) {
		s_pending_t -= dt;
		if (s_pending_t <= 0) {
			recentre(0);
		}
	}

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
