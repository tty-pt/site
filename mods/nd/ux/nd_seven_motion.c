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

void nd_seven_reset_input(void)
{
	s_held = 0;
	s_last = 0;
	s_walking = 0;
	s_pending_key = 0;
	s_pending_t = 0;
}

static void recentre(int drop_keys)
{
	s_lx = NDW_CENTER_X;
	s_ly = NDW_CENTER_Y;
	s_pending_key = 0;
	s_pending_t = 0;
	if (drop_keys) {
		s_held = 0;
		s_last = 0;
		s_walking = 0;
	}
}

void nd_seven_sync_origin(const nd_client_state_t *st)
{
	const int16_t *pos;

	if (!st || st->map_data.width == 0) {
		if (!s_have) {
			recentre(1);
			s_have = 1;
			nd_seven_collision_invalidate();
			nd_spatial_resolver_invalidate();
		}
		return;
	}
	pos = st->map_data.pos;
	if (!s_have) {
		memcpy(s_pos, pos, sizeof(s_pos));
		recentre(1);
		s_have = 1;
		nd_seven_collision_invalidate();
		nd_spatial_resolver_invalidate();
		return;
	}
	if (!memcmp(pos, s_pos, sizeof(s_pos)))
		return;
	{
		int dx = pos[0] - s_pos[0], dj = -(pos[1] - s_pos[1]);
		int is_adjacent = (pos[2] == s_pos[2] && pos[3] == s_pos[3] &&
		                   dx >= -1 && dx <= 1 && dj >= -1 && dj <= 1);

		if (is_adjacent && (dx != 0 || dj != 0)) {
			/* Check if lamb was physically stepping across the boundary seam */
			if (dj == -1) { /* Stepping North into new tile */
				if (s_pending_key == 'k' || s_ly <= NDW_FOOT_H) {
					s_ly -= dj * NDW_TILE_H; /* +128 */
				} else {
					/* Entered from South doorway threshold */
					s_lx = NDW_CENTER_X;
					s_ly = NDW_TILE_H - NDW_FOOT_H - 4;
					s_dir = DIR_UP;
				}
			} else if (dj == 1) { /* Stepping South into new tile */
				if (s_pending_key == 'j' || s_ly >= NDW_TILE_H - NDW_FOOT_H) {
					s_ly -= dj * NDW_TILE_H; /* -128 */
				} else {
					/* Entered from North doorway threshold */
					s_lx = NDW_CENTER_X;
					s_ly = NDW_FOOT_H + 4;
					s_dir = DIR_DOWN;
				}
			}

			if (dx == 1) { /* Stepping East into new tile */
				if (s_pending_key == 'l' || s_lx >= NDW_TILE_W - NDW_FOOT_HW) {
					s_lx -= dx * NDW_TILE_W; /* -160 */
				} else {
					/* Entered from West doorway threshold */
					s_lx = NDW_FOOT_HW + 4;
					if (!dj) s_ly = NDW_CENTER_Y;
					s_dir = DIR_RIGHT;
				}
			} else if (dx == -1) { /* Stepping West into new tile */
				if (s_pending_key == 'h' || s_lx <= NDW_FOOT_HW) {
					s_lx -= dx * NDW_TILE_W; /* +160 */
				} else {
					/* Entered from East doorway threshold */
					s_lx = NDW_TILE_W - NDW_FOOT_HW - 4;
					if (!dj) s_ly = NDW_CENTER_Y;
					s_dir = DIR_LEFT;
				}
			}

			/* In-tile clamping: must strictly lie within [0, TILE-1] so ndw_fdiv is always 0 */
			if (s_lx < 0) s_lx = 0;
			else if (s_lx >= NDW_TILE_W) s_lx = NDW_TILE_W - 1;

			if (s_ly < 0) s_ly = 0;
			else if (s_ly >= NDW_TILE_H) s_ly = NDW_TILE_H - 1;

			s_pending_key = 0;
			s_pending_t = 0;
		} else {
			recentre(0);
		}
		memcpy(s_pos, pos, sizeof(s_pos));
		nd_seven_collision_invalidate();
		nd_spatial_resolver_invalidate();
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

static void centre_remember(const nd_client_state_t *st, const int16_t pos[4], ndw_tile_t *t)
{
	unsigned set = centre_set(pos);
	centre_ent_t *e = centre_find(set, pos);

	if (t->glyph[1] == '!') {
		if (e) {
			t->glyph[1] = e->glyph;
			t->fg[1] = e->fg;
		} else {
			/* First time seeing player on this tile: inspect active or cached entities */
			t->glyph[1] = 0;
			t->fg[1] = 0;
			if (st && st->room.ref && memcmp(st->map_data.pos, pos, sizeof(int16_t) * 4) == 0) {
				for (size_t i = 0; i < st->entity_count; i++) {
					const nd_entity_desc_t *ent = &st->entities[i];
					if (ent->location == st->room.ref && ent->ref != st->me && ent->side == 1) {
						if (is_stone_or_rock(ent)) {
							t->glyph[1] = ent->ch ? (uint8_t)ent->ch : (uint8_t)'o';
							t->fg[1] = ent->fg ? (uint8_t)(ent->fg & 15) : 10;
							break;
						} else if (is_landscape_element(ent)) {
							t->glyph[1] = ent->ch ? (uint8_t)ent->ch : (uint8_t)'t';
							t->fg[1] = ent->fg ? (uint8_t)(ent->fg & 15) : 10;
							break;
						}
					}
				}
			}
			for (int w = 0; w < CENTRE_WAYS && !e; w++)
				if (!s_centre[set][w].used)
					e = &s_centre[set][w];
			if (!e) {
				e = &s_centre[set][s_centre_next[set]];
				s_centre_next[set] = (uint8_t)((s_centre_next[set] + 1) % CENTRE_WAYS);
			}
			memcpy(e->pos, pos, sizeof(e->pos));
			e->glyph = t->glyph[1];
			e->fg = t->fg[1];
			e->used = 1;
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
	nd_seven_collision_invalidate();
	nd_spatial_resolver_invalidate();
	nd_seven_reset_input();
	s_have = 0;
	recentre(1);
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
		centre_remember(st, pos, t);
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

void nd_seven_world_pos(const nd_client_state_t *st, int64_t *wx, int64_t *wy)
{
	nd_seven_sync_origin(st);
	if (wx) *wx = (int64_t)s_pos[0] * NDW_TILE_W + s_lx;
	if (wy) *wy = (int64_t)(-s_pos[1]) * NDW_TILE_H + s_ly;
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

/* Pre-computed 3x3 Tile Collision Hull Cache */
typedef struct {
	uint8_t valid;
	int16_t pos[4];
	uint8_t wall_count;
	uint8_t obj_count;
	ndw_rect_t walls[NDW_MAX_WALL];
	struct {
		ndw_rect_t rect;
		uint8_t slot_id;
		uint8_t kind;
		uint8_t picked;
	} objs[NDW_MAX_OBJ];
} nd_tile_collision_t;

static nd_tile_collision_t s_col_cache[3][3];

void nd_seven_collision_invalidate(void)
{
	memset(s_col_cache, 0, sizeof(s_col_cache));
}

static void update_tile_collision(const nd_client_state_t *st, int ti, int tj,
                                  nd_tile_collision_t *tc, const ndw_tile_t *t,
                                  const int16_t pos[4], int kind)
{
	tc->valid = 1;
	memcpy(tc->pos, pos, sizeof(tc->pos));
	tc->wall_count = (uint8_t)ndw_walls(t, tc->walls);

	ndw_obj_t raw_objs[NDW_MAX_OBJ];
	int n = ndw_tile_objects(t, pos, kind == 2, raw_objs);
	tc->obj_count = 0;

	for (int i = 0; i < n; i++) {
		if (!raw_objs[i].hw)
			continue;
		tc->objs[tc->obj_count].rect = ndw_obj_rect(&raw_objs[i]);
		tc->objs[tc->obj_count].slot_id = raw_objs[i].slot_id;
		tc->objs[tc->obj_count].kind = raw_objs[i].kind;

		int picked = 0;
		if (ti == 0 && tj == 0) {
			const room_tracker_t *tr = nd_seven_get_tracker();
			if (tr && tr->count > 0) {
				for (int k = 0; k < tr->count; k++) {
					if (tr->objs[k].slot_id == raw_objs[i].slot_id &&
					    tr->objs[k].kind == raw_objs[i].kind) {
						if (!is_tracked_obj_present(st, k))
							picked = 1;
						break;
					}
				}
			}
			if (!picked && nd_seven_is_tile_obj_picked(pos, raw_objs[i].slot_id))
				picked = 1;
		} else {
			if (nd_seven_is_tile_obj_picked(pos, raw_objs[i].slot_id))
				picked = 1;
		}
		tc->objs[tc->obj_count].picked = (uint8_t)picked;
		tc->obj_count++;
	}
}

static int is_motion_blocked(const nd_client_state_t *st, int fx, int fy)
{
	int x0 = fx - NDW_FOOT_HW, x1 = fx + NDW_FOOT_HW;
	int y0 = fy - NDW_FOOT_H,  y1 = fy;
	int ti0 = ndw_fdiv(x0, NDW_TILE_W), ti1 = ndw_fdiv(x1 - 1, NDW_TILE_W);
	int tj0 = ndw_fdiv(y0, NDW_TILE_H), tj1 = ndw_fdiv(y1 - 1, NDW_TILE_H);

	for (int tj = tj0; tj <= tj1; tj++) {
		for (int ti = ti0; ti <= ti1; ti++) {
			if (ti < -1 || ti > 1 || tj < -1 || tj > 1)
				return 1;
			int ci = ti + 1, cj = tj + 1;
			nd_tile_collision_t *tc = &s_col_cache[cj][ci];
			int16_t cur_pos[4];
			ndw_tile_t t;
			int kind = nd_seven_tile_at(st, ti, tj, &t, cur_pos);
			if (!kind)
				return 1;
			if (!tc->valid || memcmp(tc->pos, cur_pos, sizeof(cur_pos)) != 0) {
				update_tile_collision(st, ti, tj, tc, &t, cur_pos, kind);
			}

			int ox = ti * NDW_TILE_W, oy = tj * NDW_TILE_H;
			ndw_rect_t foot = { (int16_t)(x0 - ox), (int16_t)(y0 - oy),
			                    (int16_t)(x1 - ox), (int16_t)(y1 - oy) };

			for (int w = 0; w < tc->wall_count; w++) {
				if (ndw_rect_hit(&foot, &tc->walls[w]))
					return 1;
			}

			for (int i = 0; i < tc->obj_count; i++) {
				if (tc->objs[i].picked)
					continue;
				if (ti == 0 && tj == 0) {
					const room_tracker_t *tr = nd_seven_get_tracker();
					if (tr && tr->count > 0) {
						for (int k = 0; k < tr->count; k++) {
							if (tr->objs[k].slot_id == tc->objs[i].slot_id &&
							    tr->objs[k].kind == tc->objs[i].kind) {
								if (!is_tracked_obj_present(st, k))
									tc->objs[i].picked = 1;
								break;
							}
						}
					}
					if (!tc->objs[i].picked && nd_seven_is_tile_obj_picked(cur_pos, tc->objs[i].slot_id))
						tc->objs[i].picked = 1;
				} else {
					if (nd_seven_is_tile_obj_picked(cur_pos, tc->objs[i].slot_id))
						tc->objs[i].picked = 1;
				}
				if (tc->objs[i].picked)
					continue;
				if (ndw_rect_hit(&foot, &tc->objs[i].rect))
					return 1;
			}
		}
	}
	return 0;
}

static void move_axis(const nd_client_state_t *st, int dx, int dy)
{
	int nx = s_lx + dx, ny = s_ly + dy;
	if (is_motion_blocked(st, nx, ny))
		return;
	s_lx = nx;
	s_ly = ny;
}

int nd_seven_collision(const nd_client_state_t *st, int x, int y)
{
	return is_motion_blocked(st, x, y);
}

int nd_seven_tick(const nd_client_state_t *st, float dt)
{
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
		move_axis(st, dx, dy);
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
