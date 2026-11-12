#include "nd_seven_motion.h"
#include "nd_seven_objects.h"
#include "nd_seven_ground.h"
#include <nd/field.h>
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
static float s_anim_sec;

static nd_grass_wake_step_t s_wake_ring[ND_SEVEN_WAKE_MAX];
static int s_wake_head = 0;
static int s_wake_count = 0;

void nd_seven_clear_wake(void)
{
	memset(s_wake_ring, 0, sizeof(s_wake_ring));
	s_wake_head = 0;
	s_wake_count = 0;
}

void nd_seven_record_wake_step(float wx, float wy, float vx, float vy, float t_sec)
{
	if (s_wake_count > 0) {
		int prev_idx = (s_wake_head + ND_SEVEN_WAKE_MAX - 1) % ND_SEVEN_WAKE_MAX;
		const nd_grass_wake_step_t *prev = &s_wake_ring[prev_idx];
		if (prev->valid) {
			float dx = wx - prev->wx;
			float dy = wy - prev->wy;
			float dt = t_sec - prev->t_sec;
			if (dx * dx + dy * dy < 9.0f && dt >= 0.0f && dt < 0.12f)
				return;
		}
	}
	s_wake_ring[s_wake_head].wx = wx;
	s_wake_ring[s_wake_head].wy = wy;
	s_wake_ring[s_wake_head].vx = vx;
	s_wake_ring[s_wake_head].vy = vy;
	s_wake_ring[s_wake_head].t_sec = t_sec;
	s_wake_ring[s_wake_head].valid = 1;
	s_wake_head = (s_wake_head + 1) % ND_SEVEN_WAKE_MAX;
	if (s_wake_count < ND_SEVEN_WAKE_MAX)
		s_wake_count++;
}

const nd_grass_wake_step_t *nd_seven_get_wake_steps(int *out_count)
{
	if (out_count)
		*out_count = s_wake_count;
	return s_wake_ring;
}

int nd_seven_get_lx(void) { return s_lx; }
int nd_seven_get_ly(void) { return s_ly; }
int nd_seven_get_dir(void) { return s_dir; }
int nd_seven_get_walking(void) { return s_walking; }
uint32_t nd_seven_get_anim(void) { return s_anim; }
float nd_seven_get_anim_time(void) { return s_anim_sec; }
void nd_seven_step_anim(uint32_t delta) { s_anim += delta; s_anim_sec += (float)delta * (1.0f / 60.0f); }

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

static void known_tile_ingest_map(const nd_client_state_t *st);

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
	known_tile_ingest_map(st);
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
	if ((m->flags & ND_MAP_FLAG_WATER) || m->bio_idx == 0) t.flags |= NDW_F_WATER;
	t.glyph[0] = m->side_l; t.glyph[1] = m->symbol; t.glyph[2] = m->side_r;
	t.fg[0] = m->fg_l & 15; t.fg[1] = m->fg & 15; t.fg[2] = m->fg_r & 15;
	for (int s = 0; s < 3; s++) {
		t.plant_id[s] = m->plant_id[s];
		t.col_mask[s] = m->col_mask[s];
	}
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
		if (ND_PLANT_UNPACK_PRESENT(t->plant_id[1])) {
			unsigned sp_idx = ND_PLANT_UNPACK_IDX(t->plant_id[1]);
			unsigned is_big = ND_PLANT_UNPACK_BIG(t->plant_id[1]);
			const nd_plant_spec_t *sp = &s_nd_plant_specs[sp_idx];
			t->glyph[1] = (uint8_t)(is_big ? sp->big : sp->small);
			t->fg[1] = sp->fg;
		} else if (e) {
			t->glyph[1] = e->glyph;
			t->fg[1] = e->fg;
		} else {
			/* First time seeing player on this tile: inspect active or cached entities */
			t->glyph[1] = 0;
			t->fg[1] = 0;
			if (st && st->room.ref && memcmp(st->map_data.pos, pos, sizeof(int16_t) * 4) == 0) {
				for (size_t i = 0; i < st->entity_count; i++) {
					const nd_entity_desc_t *ent = &st->entities[i];
					if (ent->location == st->room.ref && ent->ref != st->me && !(ent->flags & (ND_EF_PLAYER | ND_EF_CREATURE)) && ent->type != 1 && ent->side == 1) {
						t->glyph[1] = ent->ch ? (uint8_t)ent->ch : (uint8_t)'o';
						t->fg[1] = ent->fg ? (uint8_t)(ent->fg & 15) : 10;
						break;
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

int nd_seven_centre_restore(const int16_t pos[4], uint8_t *glyph, uint8_t *fg)
{
	if (!pos)
		return 0;
	unsigned set = centre_set(pos);
	centre_ent_t *e = centre_find(set, pos);
	if (e && e->used && e->glyph && e->glyph != '!') {
		if (glyph) *glyph = e->glyph;
		if (fg) *fg = e->fg;
		return 1;
	}
	return 0;
}

#define KNOWN_SETS 256
#define KNOWN_WAYS 4
typedef struct {
	int16_t    pos[4];
	ndw_tile_t tile;
	uint32_t   seq;
	uint8_t    valid;
} known_tile_ent_t;

static known_tile_ent_t s_known_tiles[KNOWN_SETS][KNOWN_WAYS];
static uint32_t s_known_seq = 0;
static int16_t s_known_min_x = 32767, s_known_max_x = -32768;
static int16_t s_known_min_y = 32767, s_known_max_y = -32768;

static unsigned known_tile_set(const int16_t pos[4])
{
	uint32_t h = (uint32_t)(uint16_t)pos[0] * 73856093u ^ (uint32_t)(uint16_t)pos[1] * 19349663u ^
	             (uint32_t)(uint16_t)pos[2] * 83492791u ^ (uint32_t)(uint16_t)pos[3] * 2654435761u;
	return (h ^ (h >> 15)) % KNOWN_SETS;
}

static void known_tile_apply_harvested(const int16_t pos[4], ndw_tile_t *t)
{
	for (uint8_t s = 0; s < 3; s++) {
		if (t->glyph[s] && nd_seven_is_tile_obj_picked(pos, s)) {
			t->glyph[s] = 0;
			t->fg[s] = 0;
		}
	}
}

static void known_tile_remember(const int16_t pos[4], const ndw_tile_t *t)
{
	unsigned set = known_tile_set(pos);
	known_tile_ent_t *target = NULL;
	uint32_t oldest_seq = 0xFFFFFFFFu;
	int oldest_w = 0;

	for (int w = 0; w < KNOWN_WAYS; w++) {
		known_tile_ent_t *e = &s_known_tiles[set][w];
		if (e->valid && !memcmp(e->pos, pos, sizeof(e->pos))) {
			target = e;
			break;
		}
		if (!e->valid && !target)
			target = e;
		if (e->valid && e->seq < oldest_seq) {
			oldest_seq = e->seq;
			oldest_w = w;
		}
	}
	if (!target)
		target = &s_known_tiles[set][oldest_w];

	memcpy(target->pos, pos, sizeof(target->pos));
	if (pos[0] < s_known_min_x) s_known_min_x = pos[0];
	if (pos[0] > s_known_max_x) s_known_max_x = pos[0];
	if (pos[1] < s_known_min_y) s_known_min_y = pos[1];
	if (pos[1] > s_known_max_y) s_known_max_y = pos[1];

	target->tile = *t;
	target->tile.flags &= (uint8_t)~(NDW_F_PLAYER | NDW_F_ENTITY);
	for (int s = 0; s < 3; s++) {
		if (target->tile.glyph[s] == ' ' || target->tile.glyph[s] == '!') {
			target->tile.glyph[s] = 0;
			target->tile.fg[s] = 0;
		}
	}
	known_tile_apply_harvested(pos, &target->tile);
	target->seq = ++s_known_seq;
	target->valid = 1;
}

static void known_tile_ingest_map(const nd_client_state_t *st)
{
	if (!st || st->map_data.width == 0 || st->map_data.height == 0)
		return;
	int w = (int)st->map_data.width;
	int h = (int)st->map_data.height;
	for (int row = 0; row < h; row++) {
		for (int col = 0; col < w; col++) {
			int ti = col - (int)st->map_data.player_x;
			int tj = row - (int)st->map_data.player_y;
			int16_t tpos[4] = {
				(int16_t)(st->map_data.pos[0] + ti),
				(int16_t)(st->map_data.pos[1] - tj),
				st->map_data.pos[2],
				st->map_data.pos[3],
			};
			ndw_tile_t t = nd_seven_convert_tile(&st->map_data.tiles[row * w + col]);
			centre_remember(st, tpos, &t);
			known_tile_remember(tpos, &t);
		}
	}
}

int nd_seven_lookup_known_tile(const int16_t pos[4], ndw_tile_t *out)
{
	if (!pos || s_known_seq == 0 ||
	    pos[0] < s_known_min_x || pos[0] > s_known_max_x ||
	    pos[1] < s_known_min_y || pos[1] > s_known_max_y)
		return 0;
	unsigned set = known_tile_set(pos);
	for (int w = 0; w < KNOWN_WAYS; w++) {
		known_tile_ent_t *e = &s_known_tiles[set][w];
		if (e->valid && !memcmp(e->pos, pos, sizeof(e->pos))) {
			if (out) {
				*out = e->tile;
				known_tile_apply_harvested(pos, out);
			}
			return 1;
		}
	}
	return 0;
}

#define VIRGIN_MEMO_SIZE   262144
#define VIRGIN_MEMO_MAX_OBJ 9
typedef struct {
	int16_t    pos[4];
	ndw_tile_t tile;
	uint8_t    valid;
	uint8_t    objs_valid;
	uint8_t    obj_count;
	ndw_obj_t  objs[VIRGIN_MEMO_MAX_OBJ];
} virgin_memo_ent_t;

static virgin_memo_ent_t s_virgin_memo[VIRGIN_MEMO_SIZE];
static uint32_t s_scenery_synth_budget = 0xFFFFFFFFu;

void nd_seven_set_scenery_synth_budget(uint32_t budget)
{
	s_scenery_synth_budget = budget;
}

static inline virgin_memo_ent_t *lookup_virgin_entry(const int16_t pos[4], unsigned *out_idx)
{
	/* Collision-free 512x512 2D spatial torus index across full-screen frustums */
	unsigned idx = (((unsigned)(uint16_t)pos[0] & 511u) |
	                (((unsigned)(uint16_t)pos[1] & 511u) << 9)) ^
	               (((unsigned)(uint16_t)pos[3] & 3u) << 16);
	if (out_idx)
		*out_idx = idx;
	virgin_memo_ent_t *m = &s_virgin_memo[idx];
	if (m->valid &&
	    m->pos[0] == pos[0] && m->pos[1] == pos[1] &&
	    m->pos[2] == pos[2] && m->pos[3] == pos[3])
		return m;
	return NULL;
}

static inline virgin_memo_ent_t *synth_virgin_entry(const int16_t pos[4])
{
	unsigned idx = 0;
	virgin_memo_ent_t *m = lookup_virgin_entry(pos, &idx);
	if (m)
		return m;
	m = &s_virgin_memo[idx];
	ndw_synth_virgin_tile(pos, &m->tile);
	memcpy(m->pos, pos, sizeof(m->pos));
	m->valid = 1;
	m->objs_valid = 0;
	m->obj_count = 0;
	return m;
}

static void synth_virgin_cached(const int16_t pos[4], ndw_tile_t *out)
{
	virgin_memo_ent_t *m = synth_virgin_entry(pos);
	*out = m->tile;
}

int nd_seven_resolve_tile_at_pos(const nd_client_state_t *st, const int16_t pos[4], ndw_tile_t *out)
{
	if (!pos || !out)
		return 0;

	if (st && st->map_data.width > 0 && st->map_data.height > 0 &&
	    pos[2] == st->map_data.pos[2] && pos[3] == st->map_data.pos[3]) {
		int ti = (int)pos[0] - (int)st->map_data.pos[0];
		int tj = (int)st->map_data.pos[1] - (int)pos[1];
		int col = (int)st->map_data.player_x + ti;
		int row = (int)st->map_data.player_y + tj;
		if (col >= 0 && col < (int)st->map_data.width &&
		    row >= 0 && row < (int)st->map_data.height) {
			*out = nd_seven_convert_tile(&st->map_data.tiles[row * st->map_data.width + col]);
			centre_remember(st, pos, out);
			known_tile_remember(pos, out);
			known_tile_apply_harvested(pos, out);
			return 1;
		}
	}

	if (nd_seven_lookup_known_tile(pos, out))
		return 1;

	synth_virgin_cached(pos, out);
	known_tile_apply_harvested(pos, out);
	return 2;
}

const ndw_obj_t *nd_seven_resolve_room_objects_fast(const nd_client_state_t *st, const int16_t pos[4],
                                                    ndw_obj_t scratch_objs[NDW_MAX_OBJ], int *out_count)
{
	if (!pos || !out_count) {
		if (out_count) *out_count = 0;
		return NULL;
	}

	ndw_tile_t t;
	if (st && st->map_data.width > 0 && st->map_data.height > 0 &&
	    pos[2] == st->map_data.pos[2] && pos[3] == st->map_data.pos[3]) {
		int ti = (int)pos[0] - (int)st->map_data.pos[0];
		int tj = (int)st->map_data.pos[1] - (int)pos[1];
		int col = (int)st->map_data.player_x + ti;
		int row = (int)st->map_data.player_y + tj;
		if (col >= 0 && col < (int)st->map_data.width &&
		    row >= 0 && row < (int)st->map_data.height) {
			t = nd_seven_convert_tile(&st->map_data.tiles[row * st->map_data.width + col]);
			centre_remember(st, pos, &t);
			known_tile_remember(pos, &t);
			known_tile_apply_harvested(pos, &t);
			if (!t.glyph[0] && !t.glyph[1] && !t.glyph[2]) {
				*out_count = 0;
				return NULL;
			}
			*out_count = ndw_tile_objects(&t, pos, 0, scratch_objs);
			return scratch_objs;
		}
	}

	if (nd_seven_lookup_known_tile(pos, &t)) {
		if (!t.glyph[0] && !t.glyph[1] && !t.glyph[2]) {
			*out_count = 0;
			return NULL;
		}
		*out_count = ndw_tile_objects(&t, pos, 0, scratch_objs);
		return scratch_objs;
	}

	unsigned idx = 0;
	virgin_memo_ent_t *m = lookup_virgin_entry(pos, &idx);
	if (!m) {
		if (s_scenery_synth_budget == 0) {
			*out_count = 0;
			return NULL;
		}
		if (s_scenery_synth_budget != 0xFFFFFFFFu)
			s_scenery_synth_budget--;
		m = &s_virgin_memo[idx];
		ndw_synth_virgin_tile(pos, &m->tile);
		memcpy(m->pos, pos, sizeof(m->pos));
		m->valid = 1;
		m->objs_valid = 0;
		m->obj_count = 0;
	}
	if (nd_seven_is_tile_obj_picked(pos, 0) ||
	    nd_seven_is_tile_obj_picked(pos, 1) ||
	    nd_seven_is_tile_obj_picked(pos, 2)) {
		t = m->tile;
		for (uint8_t s = 0; s < 3; s++) {
			if (t.glyph[s] && nd_seven_is_tile_obj_picked(pos, s)) {
				t.glyph[s] = 0;
				t.fg[s] = 0;
			}
		}
		if (!t.glyph[0] && !t.glyph[1] && !t.glyph[2]) {
			*out_count = 0;
			return NULL;
		}
		*out_count = ndw_tile_objects(&t, pos, 0, scratch_objs);
		return scratch_objs;
	}

	if (!m->objs_valid) {
		if (m->tile.glyph[0] || m->tile.glyph[1] || m->tile.glyph[2]) {
			ndw_obj_t tmp[NDW_MAX_OBJ];
			int n = ndw_tile_objects(&m->tile, pos, 0, tmp);
			if (n > VIRGIN_MEMO_MAX_OBJ) n = VIRGIN_MEMO_MAX_OBJ;
			if (n > 0) memcpy(m->objs, tmp, (size_t)n * sizeof(ndw_obj_t));
			m->obj_count = (uint8_t)n;
		} else {
			m->obj_count = 0;
		}
		m->objs_valid = 1;
	}
	*out_count = (int)m->obj_count;
	return m->objs;
}

int nd_seven_resolve_room_objects(const nd_client_state_t *st, const int16_t pos[4],
                                  ndw_tile_t *out_tile, ndw_obj_t out_objs[NDW_MAX_OBJ])
{
	if (!pos || !out_objs)
		return 0;
	if (out_tile)
		nd_seven_resolve_tile_at_pos(st, pos, out_tile);
	int count = 0;
	const ndw_obj_t *ptr = nd_seven_resolve_room_objects_fast(st, pos, out_objs, &count);
	if (ptr && ptr != out_objs && count > 0)
		memcpy(out_objs, ptr, (size_t)count * sizeof(ndw_obj_t));
	return count;
}

void nd_seven_forget(void)
{
	memset(s_centre, 0, sizeof(s_centre));
	memset(s_centre_next, 0, sizeof(s_centre_next));
	memset(s_known_tiles, 0, sizeof(s_known_tiles));
	s_known_seq = 0;
	s_known_min_x = 32767;
	s_known_max_x = -32768;
	s_known_min_y = 32767;
	s_known_max_y = -32768;
	nd_seven_objects_forget();
	nd_seven_ground_cache_forget();
	nd_seven_collision_invalidate();
	nd_spatial_resolver_invalidate();
	nd_seven_reset_input();
	nd_seven_clear_wake();
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
		known_tile_remember(pos, t);
		return 1;
	} else {
		if (nd_seven_lookup_known_tile(pos, t))
			return 1;
		synth_virgin_cached(pos, t);
		known_tile_apply_harvested(pos, t);
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

int nd_seven_get_world(void)
{
	return (int)s_pos[3];
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
	if (dt < 0.0f)
		dt = 0.0f;
	if (dt > 0.1f)
		dt = 0.1f;
	/* Advance continuous animation clock strictly by wall-clock dt (capped at 1/30s per frame
	 * so frame-rate changes from 15fps chunk baking to 60/120fps steady state never speed up wind/water) */
	float anim_dt = (dt > 0.033f) ? 0.033f : ((dt > 0.0f) ? dt : (1.0f / 60.0f));
	s_anim_sec += anim_dt * 1.4f;

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
	if (s_walking) {
		float wwx = (float)((int64_t)s_pos[0] * NDW_TILE_W + s_lx);
		float wwy = (float)((int64_t)(-s_pos[1]) * NDW_TILE_H + s_ly);
		nd_seven_record_wake_step(wwx, wwy, (float)dx * SPEED, (float)dy * SPEED, s_anim_sec);
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
