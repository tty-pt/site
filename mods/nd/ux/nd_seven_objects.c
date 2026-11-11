#include "nd_seven_objects.h"
#include "nd_seven_motion.h"
#include "nd_seven_atlas.h"
#include <stdio.h>
#include <string.h>

#define SCALE 2
#define LAMB_AY 23

static const int8_t s_spots[16][2] = {
	{ -16, 26 }, { 16, 26 }, { -28, 12 }, { 28, 12 }, { -38, -6 }, { 38, -6 }, { -22, -18 }, { 22, -18 },
	{ -52, 0 }, { 52, 0 }, { -46, 24 }, { 46, 24 }, { -30, 34 }, { 30, 34 }, { -46, -22 }, { 46, -22 }
};

static room_tracker_t s_tracker = { 0 };
static uint32_t s_last_action_anim = 0;

void nd_seven_action_cooldown_reset(void)
{
	s_last_action_anim = 0;
}

static int interact_mineral(const nd_client_state_t *st, const tracked_obj_t *obj,
                            char *cmd_out, size_t sz, uint32_t *out_picked_ref)
{
	(void)st;
	if (obj->entity_ref && !nd_seven_is_picked(obj->entity_ref)) {
		snprintf(cmd_out, sz, "get #%u\n", obj->entity_ref);
		if (out_picked_ref) *out_picked_ref = obj->entity_ref;
		return 1;
	}
	return 0;
}

static int interact_container(const nd_client_state_t *st, const tracked_obj_t *obj,
                              char *cmd_out, size_t sz, uint32_t *out_picked_ref)
{
	uint32_t parent_ref = obj->entity_ref;
	uint32_t child_ref = 0;
	if (!obj->drop_harvested && parent_ref) {
		for (size_t si = 0; si < st->entity_count; si++) {
			const nd_entity_desc_t *e = &st->entities[si];
			if (nd_seven_is_picked(e->ref))
				continue;
			if (e->location == parent_ref) {
				child_ref = e->ref;
				break;
			}
		}
	}
	if (child_ref && parent_ref) {
		snprintf(cmd_out, sz, "get #%u #%u\n", parent_ref, child_ref);
		if (out_picked_ref) *out_picked_ref = child_ref;
		return 1;
	}
	return 0;
}

static int interact_stairs_down(const nd_client_state_t *st, const tracked_obj_t *obj,
                                char *cmd_out, size_t sz, uint32_t *out_picked_ref)
{
	(void)st; (void)obj; (void)out_picked_ref;
	snprintf(cmd_out, sz, ">\n");
	return 1;
}

static int interact_stairs_up(const nd_client_state_t *st, const tracked_obj_t *obj,
                              char *cmd_out, size_t sz, uint32_t *out_picked_ref)
{
	(void)st; (void)obj; (void)out_picked_ref;
	snprintf(cmd_out, sz, "<\n");
	return 1;
}

static int interact_shop(const nd_client_state_t *st, const tracked_obj_t *obj,
                         char *cmd_out, size_t sz, uint32_t *out_picked_ref)
{
	(void)st; (void)out_picked_ref;
	if (obj->entity_ref) {
		snprintf(cmd_out, sz, "look #%u\n", obj->entity_ref);
		return 1;
	}
	return 0;
}

static const nd_scenery_def_t s_scenery_defs[] = {
	{ NDW_K_DECID,       ND_SCENERY_TRAIT_PERMANENT,                             1, "tree",        interact_container },
	{ NDW_K_TALL,        ND_SCENERY_TRAIT_PERMANENT,                             1, "tall tree",   interact_container },
	{ NDW_K_FIR,         ND_SCENERY_TRAIT_PERMANENT,                             1, "fir tree",    interact_container },
	{ NDW_K_CACTUS,      ND_SCENERY_TRAIT_PERMANENT | ND_SCENERY_TRAIT_BLOCKING,  1, "cactus",      interact_container },
	{ NDW_K_STONE,       ND_SCENERY_TRAIT_DEPLETABLE,                             0, "stone",       interact_mineral },
	{ NDW_K_ROCK,        ND_SCENERY_TRAIT_DEPLETABLE | ND_SCENERY_TRAIT_BLOCKING, 0, "rock",        interact_mineral },
	{ NDW_K_BOULDER,     ND_SCENERY_TRAIT_DEPLETABLE | ND_SCENERY_TRAIT_BLOCKING, 0, "boulder",     interact_mineral },
	{ NDW_K_POND,        ND_SCENERY_TRAIT_WATER,                                  1, "pond",        NULL },
	{ NDW_K_SHOP,        0,                                                       1, "shop",        interact_shop },
	{ NDW_K_STAIRS_DOWN, 0,                                                       1, "stairs down", interact_stairs_down },
	{ NDW_K_STAIRS_UP,   0,                                                       1, "stairs up",   interact_stairs_up },
	{ 0, 0, 0, NULL, NULL }
};

const nd_scenery_def_t *nd_seven_get_scenery_def(uint8_t kind)
{
	for (size_t i = 0; s_scenery_defs[i].default_name != NULL; i++) {
		if (s_scenery_defs[i].kind == kind)
			return &s_scenery_defs[i];
	}
	return NULL;
}

const nd_scenery_trait_t *nd_seven_get_scenery_trait(uint8_t kind)
{
	return nd_seven_get_scenery_def(kind);
}

int nd_seven_has_scenery_trait(uint8_t kind, uint8_t trait)
{
	const nd_scenery_trait_t *t = nd_seven_get_scenery_trait(kind);
	return (t && (t->traits & trait) != 0);
}

#define MAX_PICKED_REFS 128
static uint32_t s_picked_refs[MAX_PICKED_REFS] = { 0 };
static size_t s_picked_count = 0;
static size_t s_picked_head = 0;

void nd_seven_mark_picked(uint32_t entity_ref)
{
	if (!entity_ref)
		return;
	for (size_t i = 0; i < s_picked_count; i++) {
		if (s_picked_refs[i] == entity_ref)
			return;
	}
	if (s_picked_count < MAX_PICKED_REFS) {
		s_picked_refs[s_picked_count++] = entity_ref;
	} else {
		s_picked_refs[s_picked_head] = entity_ref;
		s_picked_head = (s_picked_head + 1) % MAX_PICKED_REFS;
	}

	for (int k = 0; k < s_tracker.count; k++) {
		if (s_tracker.objs[k].entity_ref == entity_ref) {
			s_tracker.objs[k].harvested = 1;
			if (nd_seven_has_scenery_trait(s_tracker.objs[k].kind, ND_SCENERY_TRAIT_MINERAL))
				nd_seven_mark_tile_slot_harvested(s_tracker.pos, s_tracker.objs[k].slot_id);
		}
	}
	nd_spatial_resolver_invalidate();
	nd_seven_collision_invalidate();
}

int nd_seven_is_picked(uint32_t entity_ref)
{
	if (!entity_ref)
		return 0;
	for (size_t i = 0; i < s_picked_count; i++) {
		if (s_picked_refs[i] == entity_ref)
			return 1;
	}
	return 0;
}

void nd_seven_forget_picked(void)
{
	s_picked_count = 0;
	s_picked_head = 0;
	memset(s_picked_refs, 0, sizeof(s_picked_refs));
	nd_spatial_resolver_invalidate();
	nd_seven_collision_invalidate();
}

const room_tracker_t *nd_seven_get_tracker(void)
{
	return &s_tracker;
}

#define MULTI_ROOM_CACHE_SIZE 9

typedef struct {
	uint32_t room_ref;
	int16_t pos[4];
	size_t entity_count;
	uint8_t valid;
	uint8_t slot_harvested[3];
	nd_entity_desc_t entities[ND_MAX_ENTITIES];
} room_entity_cache_entry_t;

static room_entity_cache_entry_t s_room_cache[MULTI_ROOM_CACHE_SIZE];
static unsigned s_room_cache_next = 0;

void nd_seven_mark_tile_slot_harvested(const int16_t pos[4], uint8_t slot_id)
{
	if (!pos || slot_id >= 3)
		return;

	room_entity_cache_entry_t *entry = NULL;
	for (size_t i = 0; i < MULTI_ROOM_CACHE_SIZE; i++) {
		if (s_room_cache[i].valid &&
		    memcmp(s_room_cache[i].pos, pos, sizeof(s_room_cache[i].pos)) == 0) {
			entry = &s_room_cache[i];
			break;
		}
	}
	if (!entry) {
		entry = &s_room_cache[s_room_cache_next++ % MULTI_ROOM_CACHE_SIZE];
		memset(entry, 0, sizeof(*entry));
		memcpy(entry->pos, pos, sizeof(entry->pos));
		entry->valid = 1;
	}
	entry->slot_harvested[slot_id] = 1;
	nd_spatial_resolver_invalidate();
	nd_seven_collision_invalidate();
}

void nd_seven_cache_room_entities(uint32_t room_ref, const int16_t pos[4],
                                  const nd_entity_desc_t *entities, size_t count)
{
	if (!room_ref || !pos)
		return;

	room_entity_cache_entry_t *entry = NULL;
	for (size_t i = 0; i < MULTI_ROOM_CACHE_SIZE; i++) {
		if (s_room_cache[i].valid &&
		    (s_room_cache[i].room_ref == room_ref ||
		     memcmp(s_room_cache[i].pos, pos, sizeof(s_room_cache[i].pos)) == 0)) {
			entry = &s_room_cache[i];
			break;
		}
	}
	if (!entry) {
		entry = &s_room_cache[s_room_cache_next++ % MULTI_ROOM_CACHE_SIZE];
		memset(entry->slot_harvested, 0, sizeof(entry->slot_harvested));
	}
	entry->room_ref = room_ref;
	memcpy(entry->pos, pos, sizeof(entry->pos));
	entry->entity_count = (count > ND_MAX_ENTITIES) ? ND_MAX_ENTITIES : count;
	if (entities && count > 0) {
		memcpy(entry->entities, entities, entry->entity_count * sizeof(nd_entity_desc_t));
		for (size_t i = 0; i < entry->entity_count; i++) {
			if (entry->entities[i].location == room_ref &&
			    entry->entities[i].side < 3 &&
			    nd_seven_is_picked(entry->entities[i].ref)) {
				entry->slot_harvested[entry->entities[i].side] = 1;
			}
		}
	}
	entry->valid = 1;
}

const nd_entity_desc_t *nd_seven_get_cached_entities(const int16_t pos[4], size_t *out_count, uint32_t *out_room_ref)
{
	if (!pos)
		return NULL;
	for (size_t i = 0; i < MULTI_ROOM_CACHE_SIZE; i++) {
		if (s_room_cache[i].valid &&
		    memcmp(s_room_cache[i].pos, pos, sizeof(s_room_cache[i].pos)) == 0) {
			if (out_count)
				*out_count = s_room_cache[i].entity_count;
			if (out_room_ref)
				*out_room_ref = s_room_cache[i].room_ref;
			return s_room_cache[i].entities;
		}
	}
	if (out_count)
		*out_count = 0;
	if (out_room_ref)
		*out_room_ref = 0;
	return NULL;
}

int nd_seven_is_tile_obj_picked(const int16_t pos[4], uint8_t slot_id)
{
	if (!pos)
		return 0;
	for (size_t i = 0; i < MULTI_ROOM_CACHE_SIZE; i++) {
		if (s_room_cache[i].valid &&
		    memcmp(s_room_cache[i].pos, pos, sizeof(s_room_cache[i].pos)) == 0) {
			if (slot_id < 3 && s_room_cache[i].slot_harvested[slot_id])
				return 1;
			for (size_t j = 0; j < s_room_cache[i].entity_count; j++) {
				if (s_room_cache[i].entities[j].side == slot_id &&
				    nd_seven_is_picked(s_room_cache[i].entities[j].ref))
					return 1;
			}
			return 0;
		}
	}
	return 0;
}

void nd_seven_objects_forget(void)
{
	memset(&s_tracker, 0, sizeof(s_tracker));
	nd_seven_forget_picked();
	nd_seven_action_cooldown_reset();
	memset(s_room_cache, 0, sizeof(s_room_cache));
	s_room_cache_next = 0;
	nd_spatial_resolver_invalidate();
	nd_seven_collision_invalidate();
}

int nd_scenery_matches_entity(const nd_scenery_def_t *def, const nd_entity_desc_t *e)
{
	if (!def || !e || (e->flags & (ND_EF_PLAYER | ND_EF_CREATURE)) || e->type == 1)
		return 0;

	/* Mineral family matching: any mineral entity matches any mineral scenery node */
	if (def->traits & ND_SCENERY_TRAIT_DEPLETABLE) {
		if ((e->flags & ND_EF_MINERAL) || e->ch == '*' || e->ch == 'o' || e->ch == 'O')
			return 1;
	}

	/* Permanent flora family matching: any plant entity matches any plant scenery node */
	if (def->traits & ND_SCENERY_TRAIT_PERMANENT) {
		if (e->flags & ND_EF_FLORA) {
			const ndw_content_entry_t *ce = ndw_find_content(e->ch);
			if (ce)
				return (ce->kind == def->kind);
			return 1;
		}
		const ndw_content_entry_t *ce = ndw_find_content(e->ch);
		if (ce && ce->kind == def->kind)
			return 1;
	}

	return 0;
}

int is_permanent_scenery_entity(const nd_entity_desc_t *e)
{
	if (!e || (e->flags & (ND_EF_PLAYER | ND_EF_CREATURE)) || e->type == 1) return 0;
	if (e->flags & ND_EF_FLORA) return 1;
	for (size_t i = 0; s_scenery_defs[i].default_name != NULL; i++) {
		if ((s_scenery_defs[i].traits & ND_SCENERY_TRAIT_PERMANENT) &&
		    nd_scenery_matches_entity(&s_scenery_defs[i], e))
			return 1;
	}
	return 0;
}

int is_entity_nested(const nd_client_state_t *st, const nd_entity_desc_t *e)
{
	if (!st || !e)
		return 0;
	return (e->location != st->room.ref && e->location != 0);
}

uint8_t nd_seven_resolve_entity_sprite(const nd_entity_desc_t *e)
{
	if (!e || (e->flags & (ND_EF_PLAYER | ND_EF_CREATURE)) || e->type == 1)
		return 0;

	if ((e->flags & ND_EF_MINERAL) || e->ch == '*' || e->ch == 'o' || e->ch == 'O') {
		static const uint8_t mineral_sprites[] = {
			NDS_STONE_GREY, NDS_STONE_BLUE, NDS_STONE_BLUE_SM, NDS_STONE_FLAT,
			NDS_STONE_TINY, NDS_ROCK_TAN_SM, NDS_ROCK_TAN_TINY, NDS_ROCK_GREY
		};
		return mineral_sprites[ndw_mix(e->ref) % 8];
	}

	if (e->flags & ND_EF_FLORA) {
		const ndw_content_entry_t *ce = ndw_find_content(e->ch);
		if (ce && ce->kind == NDW_K_FIR)
			return (uint8_t)((ndw_mix(e->ref) & 1) ? NDS_BUSH_TEAL : NDS_BUSH_DARK);
		return NDS_TREE_ROUND;
	}

	return 0;
}

const char *world_obj_name(const ndw_obj_t *o, const nd_client_state_t *st, int ti, int tj)
{
	if (!o)
		return NULL;
	if (ti == 0 && tj == 0 && st && st->room.ref) {
		const nd_scenery_def_t *def = nd_seven_get_scenery_def(o->kind);
		if (def) {
			size_t ec = st->entity_count > ND_MAX_ENTITIES ? ND_MAX_ENTITIES : st->entity_count;
			for (size_t i = 0; i < ec; i++) {
				const nd_entity_desc_t *e = &st->entities[i];
				if (e->ref != st->me && e->location == st->room.ref &&
				    !nd_seven_is_picked(e->ref) &&
				    nd_scenery_matches_entity(def, e)) {
					if (e->name[0])
						return e->name;
				}
			}
		}
	}
	const nd_scenery_def_t *def = nd_seven_get_scenery_def(o->kind);
	if (def && def->default_name)
		return def->default_name;
	return NULL;
}

void sync_room_objects(const nd_client_state_t *st)
{
	if (!st || !st->room.ref) {
		s_tracker.room_ref = 0;
		s_tracker.count = 0;
		s_tracker.bound_entities = 0;
		return;
	}

	ndw_tile_t t;
	int16_t pos[4];
	int kind = nd_seven_tile_at(st, 0, 0, &t, pos);
	if (!kind)
		return;

	uint32_t ent_hash = 0;
	size_t ec = st->entity_count;
	if (ec > ND_MAX_ENTITIES)
		ec = ND_MAX_ENTITIES;
	for (size_t i = 0; i < ec; i++) {
		ent_hash ^= (uint32_t)st->entities[i].ref * 31u + (uint32_t)st->entities[i].ch + (uint32_t)st->entities[i].location;
		for (const char *p = st->entities[i].name; *p; p++)
			ent_hash = ent_hash * 33u + (uint8_t)*p;
	}

	int need_sync = 0;
	if (s_tracker.room_ref != st->room.ref ||
	    memcmp(s_tracker.pos, pos, sizeof(pos)) != 0 ||
	    memcmp(s_tracker.glyphs, t.glyph, 3) != 0 ||
	    s_tracker.last_entity_count != st->entity_count ||
	    s_tracker.last_entity_hash != ent_hash)
		need_sync = 1;

	if (!need_sync)
		return;

	nd_seven_cache_room_entities(st->room.ref, pos, st->entities, ec);

	s_tracker.room_ref = st->room.ref;
	memcpy(s_tracker.pos, pos, sizeof(pos));
	memcpy(s_tracker.glyphs, t.glyph, 3);
	s_tracker.last_entity_count = ec;
	s_tracker.last_entity_hash = ent_hash;
	s_tracker.count = 0;
	s_tracker.bound_entities = 0;

	ndw_obj_t objs[NDW_MAX_OBJ];
	int cnt = ndw_tile_objects(&t, pos, 0, objs);
	if (cnt > NDW_MAX_OBJ)
		cnt = NDW_MAX_OBJ;
	s_tracker.count = cnt;

	uint8_t used_entity[ND_MAX_ENTITIES] = { 0 };

	/* Pass 1: exact match by slot / side for all objects */
	for (int k = 0; k < cnt; k++) {
		s_tracker.objs[k].x = objs[k].x;
		s_tracker.objs[k].y = objs[k].y;
		s_tracker.objs[k].kind = objs[k].kind;
		s_tracker.objs[k].slot_id = objs[k].slot_id;
		s_tracker.objs[k].entity_ref = 0;
		s_tracker.objs[k].harvested = 0;
		s_tracker.objs[k].drop_harvested = 0;
		s_tracker.objs[k].name[0] = '\0';

		int is_depletable = nd_seven_has_scenery_trait(objs[k].kind, ND_SCENERY_TRAIT_DEPLETABLE);
		const nd_scenery_def_t *def = nd_seven_get_scenery_def(objs[k].kind);
		(void)def;
		uint8_t obj_slot = objs[k].slot_id;

		if (is_depletable && nd_seven_is_tile_obj_picked(pos, obj_slot))
			s_tracker.objs[k].harvested = 1;

		for (size_t ei = 0; ei < ec; ei++) {
			if (used_entity[ei])
				continue;
			const nd_entity_desc_t *e = &st->entities[ei];
			if (e->location == st->room.ref && e->ref != st->me && e->side == obj_slot &&
			    (!def || nd_scenery_matches_entity(def, e))) {
				s_tracker.objs[k].entity_ref = e->ref;
				snprintf(s_tracker.objs[k].name, sizeof(s_tracker.objs[k].name), "%s",
				         e->name[0] ? e->name : world_obj_name(&objs[k], st, 0, 0));
				used_entity[ei] = 1;
				s_tracker.bound_entities++;
				if (is_depletable && nd_seven_is_picked(e->ref))
					s_tracker.objs[k].harvested = 1;
				break;
			}
		}
	}

	/* Pass 2: fallback match for any remaining unbound objects */
	for (int k = 0; k < cnt; k++) {
		if (s_tracker.objs[k].entity_ref)
			continue;
		int is_depletable = nd_seven_has_scenery_trait(s_tracker.objs[k].kind, ND_SCENERY_TRAIT_DEPLETABLE);
		const nd_scenery_def_t *def = nd_seven_get_scenery_def(s_tracker.objs[k].kind);

		for (size_t ei = 0; ei < ec; ei++) {
			if (used_entity[ei])
				continue;
			const nd_entity_desc_t *e = &st->entities[ei];
			if (e->location == st->room.ref && e->ref != st->me &&
			    (!def || nd_scenery_matches_entity(def, e))) {
				s_tracker.objs[k].entity_ref = e->ref;
				snprintf(s_tracker.objs[k].name, sizeof(s_tracker.objs[k].name), "%s",
				         e->name[0] ? e->name : world_obj_name(&objs[k], st, 0, 0));
				used_entity[ei] = 1;
				s_tracker.bound_entities++;
				if (is_depletable && nd_seven_is_picked(e->ref))
					s_tracker.objs[k].harvested = 1;
				break;
			}
		}
	}

	/* Finalize names and child drop harvest */
	for (int k = 0; k < cnt; k++) {
		if (s_tracker.objs[k].entity_ref) {
			for (size_t si = 0; si < ec; si++) {
				if (st->entities[si].location == s_tracker.objs[k].entity_ref &&
				    nd_seven_is_picked(st->entities[si].ref)) {
					s_tracker.objs[k].drop_harvested = 1;
					break;
				}
			}
		}
		if (!s_tracker.objs[k].name[0]) {
			const char *wn = world_obj_name(&objs[k], st, 0, 0);
			if (wn)
				snprintf(s_tracker.objs[k].name, sizeof(s_tracker.objs[k].name), "%s", wn);
			else {
				const nd_scenery_def_t *def = nd_seven_get_scenery_def(s_tracker.objs[k].kind);
				if (def && def->default_name)
					snprintf(s_tracker.objs[k].name, sizeof(s_tracker.objs[k].name), "%s", def->default_name);
			}
		}
	}
}

int is_tracked_obj_present(const nd_client_state_t *st, int k)
{
	if (!st || !st->room.ref || k < 0 || k >= s_tracker.count)
		return 1;

	uint8_t kind = s_tracker.objs[k].kind;
	int is_permanent = nd_seven_has_scenery_trait(kind, ND_SCENERY_TRAIT_PERMANENT);

	/* Permanent landscape and world features from the map tile must never be suppressed by entity counts */
	if (is_permanent)
		return 1;

	if (s_tracker.objs[k].harvested)
		return 0;

	uint8_t slot_id = s_tracker.objs[k].slot_id;
	if (slot_id < 3 && nd_seven_is_tile_obj_picked(st->map_data.pos, slot_id))
		return 0;

	if (s_tracker.objs[k].entity_ref != 0) {
		if (nd_seven_is_picked(s_tracker.objs[k].entity_ref))
			return 0;
		size_t ec = st->entity_count > ND_MAX_ENTITIES ? ND_MAX_ENTITIES : st->entity_count;
		for (size_t ei = 0; ei < ec; ei++) {
			if (st->entities[ei].ref == s_tracker.objs[k].entity_ref &&
			    st->entities[ei].location == st->room.ref)
				return 1;
		}
		return 1;
	}

	return 1;
}

static inline void put_px(uint32_t *fb, int w, int h, int x, int y, uint32_t c)
{
	if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h)
		fb[y * w + x] = c;
}

static inline void fill_px(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, uint32_t c)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > w) x1 = w;
	if (y1 > h) y1 = h;
	for (int y = y0; y < y1; y++) {
		uint32_t *row = &fb[y * w + x0];
		int n = x1 - x0;
		while (n-- > 0) *row++ = c;
	}
}

static inline const uint8_t *font_rows(char ch)
{
	static const uint8_t f[][5] = {
		[' '] = { 0, 0, 0, 0, 0 },
		['*'] = { 0, 5, 2, 5, 0 },
		['!'] = { 2, 2, 2, 0, 2 },
		['$'] = { 2, 7, 2, 7, 2 },
		['<'] = { 1, 2, 4, 2, 1 },
		['>'] = { 4, 2, 1, 2, 4 },
		['~'] = { 0, 5, 7, 2, 0 },
		['a'] = { 2, 5, 7, 5, 5 },
		['b'] = { 6, 5, 6, 5, 6 },
		['c'] = { 3, 4, 4, 4, 3 },
		['d'] = { 6, 5, 5, 5, 6 },
		['e'] = { 7, 4, 6, 4, 7 },
		['f'] = { 7, 4, 6, 4, 4 },
		['g'] = { 3, 4, 5, 5, 3 },
		['h'] = { 5, 5, 7, 5, 5 },
		['i'] = { 7, 2, 2, 2, 7 },
		['j'] = { 1, 1, 1, 5, 2 },
		['k'] = { 5, 6, 4, 6, 5 },
		['l'] = { 4, 4, 4, 4, 7 },
		['m'] = { 5, 7, 5, 5, 5 },
		['n'] = { 6, 5, 5, 5, 5 },
		['o'] = { 2, 5, 5, 5, 2 },
		['p'] = { 6, 5, 6, 4, 4 },
		['q'] = { 2, 5, 5, 6, 3 },
		['r'] = { 6, 5, 6, 5, 5 },
		['s'] = { 3, 4, 2, 1, 6 },
		['t'] = { 7, 2, 2, 2, 2 },
		['u'] = { 5, 5, 5, 5, 2 },
		['v'] = { 5, 5, 5, 5, 2 },
		['w'] = { 5, 5, 5, 7, 5 },
		['x'] = { 5, 5, 2, 5, 5 },
		['y'] = { 5, 5, 2, 2, 2 },
		['z'] = { 7, 1, 2, 4, 7 },
		['O'] = { 7, 5, 5, 5, 7 },
		['#'] = { 5, 7, 5, 7, 5 },
	};
	unsigned char uc = (unsigned char)ch;
	if (uc >= 'A' && uc <= 'Z' && uc != 'O')
		uc = (unsigned char)(uc + 32);
	if (uc < sizeof(f)/sizeof(f[0]))
		return f[uc];
	return NULL;
}

void draw_object_name(uint32_t *fb, int w, int h, int cx, int top_y, const char *name)
{
	if (!name || !*name || !fb || w <= 0 || h <= 0)
		return;

	int len = (int)strlen(name);
	int tw = len > 0 ? len * 8 - 2 : 0;
	int x0 = cx - tw / 2;
	int y0 = top_y - 15;

	if (x0 < 3) x0 = 3;
	if (x0 + tw >= w - 3) x0 = w - tw - 4;
	if (y0 < 3) y0 = 3;
	if (y0 + 10 >= h - 3) y0 = h - 14;

	fill_px(fb, w, h, x0 - 3, y0 - 2, x0 + tw + 3, y0 + 12, 0xEE0E1018u);
	put_px(fb, w, h, x0 - 3, y0 - 2, 0x55FFFFFFu);
	put_px(fb, w, h, x0 + tw + 2, y0 - 2, 0x55FFFFFFu);

	int cur_x = x0;
	for (const char *p = name; *p; p++) {
		const uint8_t *rows = font_rows(*p);
		if (rows) {
			for (int r = 0; r < 5; r++)
				for (int k = 0; k < 3; k++)
					if (rows[r] & (4 >> k))
						fill_px(fb, w, h, cur_x + k * 2, y0 + r * 2,
						        cur_x + k * 2 + 2, y0 + r * 2 + 2, 0xFFFAF0E6u);
		}
		cur_x += 8;
	}
}

void draw_thinking_balloon(uint32_t *fb, int w, int h, int hx, int hy, const char *action)
{
	(void)action;
	int hover = (int)((nd_seven_get_anim() / 20) & 1);
	int bw = 28, bh = 15;
	int bx0 = hx - bw / 2;
	int by0 = hy - bh - 10 - hover;

	if (by0 < 4) by0 = 4;
	if (by0 + bh >= h - 4) by0 = h - bh - 5;
	if (bx0 < 4) bx0 = 4;
	if (bx0 + bw >= w - 4) bx0 = w - bw - 5;

	int bx1 = bx0 + bw, by1 = by0 + bh;

	uint32_t c_border = 0xFF24262E;
	uint32_t c_bg = 0xFFFDFBF7;
	uint32_t c_shadow = 0xFFE2DDD3;

	int tx = hx;
	int ty0 = by1;
	put_px(fb, w, h, tx - 2, ty0,     c_border);
	put_px(fb, w, h, tx - 1, ty0,     c_bg);
	put_px(fb, w, h, tx,     ty0,     c_bg);
	put_px(fb, w, h, tx + 1, ty0,     c_border);
	put_px(fb, w, h, tx - 1, ty0 + 1, c_border);
	put_px(fb, w, h, tx,     ty0 + 1, c_bg);
	put_px(fb, w, h, tx + 1, ty0 + 1, c_border);
	put_px(fb, w, h, tx,     ty0 + 2, c_border);

	fill_px(fb, w, h, bx0 + 2, by0, bx1 - 2, by1, c_border);
	fill_px(fb, w, h, bx0, by0 + 2, bx1, by1 - 2, c_border);
	put_px(fb, w, h, bx0 + 1, by0 + 1, c_border);
	put_px(fb, w, h, bx1 - 2, by0 + 1, c_border);
	put_px(fb, w, h, bx0 + 1, by1 - 2, c_border);
	put_px(fb, w, h, bx1 - 2, by1 - 2, c_border);

	put_px(fb, w, h, bx1 - 1, by0 + 3, c_shadow);
	fill_px(fb, w, h, bx0 + 3, by1 - 2, bx1 - 3, by1 - 1, c_shadow);

	fill_px(fb, w, h, bx0 + 2, by0 + 1, bx1 - 2, by1 - 2, c_bg);
	fill_px(fb, w, h, bx0 + 1, by0 + 2, bx1 - 1, by1 - 2, c_bg);

	int cx = (bx0 + bx1) / 2;
	int cy = (by0 + by1) / 2 - 1;

	uint32_t c_gold = 0xFFFFC820;
	uint32_t c_spark = 0xFFFFF6A0;
	put_px(fb, w, h, cx - 6, cy - 2, c_gold);
	put_px(fb, w, h, cx - 7, cy - 1, c_gold);
	put_px(fb, w, h, cx - 6, cy - 1, c_spark);
	put_px(fb, w, h, cx - 5, cy - 1, c_gold);
	put_px(fb, w, h, cx - 6, cy,     c_gold);

	uint32_t c_key = 0xFF4A4E5C;
	put_px(fb, w, h, cx - 1, cy - 1, c_key);
	put_px(fb, w, h, cx - 1, cy,     c_key);
	put_px(fb, w, h, cx - 1, cy + 1, c_key);
	fill_px(fb, w, h, cx,    cy + 1, cx + 6, cy + 2, c_key);
	put_px(fb, w, h, cx + 6, cy - 1, c_key);
	put_px(fb, w, h, cx + 6, cy,     c_key);
	put_px(fb, w, h, cx + 6, cy + 1, c_key);
}

static int seven_action_impl(const nd_client_state_t *st, char *out, size_t sz, int execute)
{
	int best_dist = 999999;
	int best_tracker_k = -1;
	uint32_t best_picked_ref = 0;
	char best_cmd[64] = { 0 };

	if (!st)
		return 0;
	nd_seven_sync_origin(st);
	sync_room_objects(st);

	int lx = nd_seven_get_lx();
	int ly = nd_seven_get_ly();

	/* Seam boundary check: only target current room's objects if lamb is inside the tile */
	int in_bounds = (lx >= -NDW_FOOT_HW && lx <= NDW_TILE_W + NDW_FOOT_HW &&
	                 ly >= -NDW_FOOT_H && ly <= NDW_TILE_H + NDW_FOOT_H);
	if (!in_bounds)
		return 0;

	for (int k = 0; k < s_tracker.count; k++) {
		if (!is_tracked_obj_present(st, k))
			continue;

		int obj_x = s_tracker.objs[k].x;
		int obj_y = s_tracker.objs[k].y;
		int dx = lx - obj_x;
		int dy = ly - obj_y;
		if (dx < 0) dx = -dx;
		if (dy < 0) dy = -dy;

		if (dx <= 40 && dy <= 32) {
			int d2 = dx * dx + dy * dy;
			if (d2 < best_dist) {
				const nd_scenery_def_t *def = nd_seven_get_scenery_def(s_tracker.objs[k].kind);
				if (def && def->interact) {
					char cmd[64];
					uint32_t picked_ref = 0;
					if (def->interact(st, &s_tracker.objs[k], cmd, sizeof(cmd), &picked_ref)) {
						snprintf(best_cmd, sizeof(best_cmd), "%s", cmd);
						best_dist = d2;
						best_tracker_k = k;
						best_picked_ref = picked_ref;
					}
				}
			}
		}
	}

	uint32_t room = st->room.ref;
	if (room) {
		size_t ec = st->entity_count > ND_MAX_ENTITIES ? ND_MAX_ENTITIES : st->entity_count;
		for (size_t i = 0; i < ec; i++) {
			const nd_entity_desc_t *e = &st->entities[i];
			if (st->me && e->ref == st->me)
				continue;
			if (e->location != room || is_entity_nested(st, e))
				continue;
			if (nd_seven_is_picked(e->ref))
				continue;

			int already_tracked = 0;
			for (int k = 0; k < s_tracker.count; k++) {
				if (s_tracker.objs[k].entity_ref == e->ref) {
					already_tracked = 1;
					break;
				}
			}
			if (already_tracked)
				continue;
			if (is_permanent_scenery_entity(e))
				continue;

			uint32_t spot = ndw_mix(e->ref) % 16;
			int ex = NDW_CENTER_X + s_spots[spot][0];
			int ey = NDW_CENTER_Y + s_spots[spot][1];
			int dx = lx - ex;
			int dy = ly - ey;
			if (dx < 0) dx = -dx;
			if (dy < 0) dy = -dy;
			if (dx <= 40 && dy <= 32) {
				int d2 = dx * dx + dy * dy;
				if (d2 < best_dist) {
					char c[64];
					if ((e->actions & 4) || (e->actions == 0 && !(e->flags & (ND_EF_PLAYER | ND_EF_CREATURE)) && e->type != 1) || nd_seven_resolve_entity_sprite(e)) {
						snprintf(c, sizeof(c), "get #%u\n", e->ref);
						best_picked_ref = e->ref;
					} else {
						snprintf(c, sizeof(c), "look #%u\n", e->ref);
						best_picked_ref = 0;
					}
					best_dist = d2;
					best_tracker_k = -1;
					snprintf(best_cmd, sizeof(best_cmd), "%s", c);
				}
			}
		}
	}

	if (best_dist < 999999 && best_cmd[0]) {
		if (execute) {
			if (best_tracker_k >= 0 && best_tracker_k < s_tracker.count) {
				uint8_t kind = s_tracker.objs[best_tracker_k].kind;
				if (nd_seven_has_scenery_trait(kind, ND_SCENERY_TRAIT_DEPLETABLE)) {
					s_tracker.objs[best_tracker_k].harvested = 1;
					nd_seven_mark_tile_slot_harvested(st->map_data.pos, s_tracker.objs[best_tracker_k].slot_id);
				}
				if (best_picked_ref && best_picked_ref != s_tracker.objs[best_tracker_k].entity_ref) {
					s_tracker.objs[best_tracker_k].drop_harvested = 1;
				}
			}
			if (best_picked_ref && strncmp(best_cmd, "get ", 4) == 0)
				nd_seven_mark_picked(best_picked_ref);
		}
		if (out && sz > 0)
			snprintf(out, sz, "%s", best_cmd);
		return (int)strlen(best_cmd);
	}
	return 0;
}

int nd_seven_action(const nd_client_state_t *st, char *out, size_t sz)
{
	return seven_action_impl(st, out, sz, 0);
}

int nd_seven_action_execute(const nd_client_state_t *st, char *out, size_t sz)
{
	uint32_t anim = nd_seven_get_anim();
	if (s_last_action_anim && anim && anim - s_last_action_anim < 18)
		return 0;

	int res = seven_action_impl(st, out, sz, 1);
	if (res > 0 && anim)
		s_last_action_anim = anim;
	return res;
}

/* --- Shared Spatial Entity Resolver -------------------------------------- */

static nd_spatial_tile_t s_spatial_cache[3][3];

void nd_spatial_resolver_invalidate(void)
{
	memset(s_spatial_cache, 0, sizeof(s_spatial_cache));
}

int nd_spatial_tile_resolve(const nd_client_state_t *st, int ti, int tj, nd_spatial_tile_t *out)
{
	if (!st || !out)
		return 0;

	if (ti == 0 && tj == 0) {
		sync_room_objects(st);
	}

	ndw_tile_t t;
	int16_t pos[4];
	int kind = nd_seven_tile_at(st, ti, tj, &t, pos);
	if (!kind)
		return 0;

	int ci = ti + 1, cj = tj + 1;
	if (ci >= 0 && ci < 3 && cj >= 0 && cj < 3 && (ti != 0 || tj != 0)) {
		if (s_spatial_cache[cj][ci].valid &&
		    memcmp(s_spatial_cache[cj][ci].pos, pos, sizeof(pos)) == 0) {
			*out = s_spatial_cache[cj][ci];
			return 1;
		}
	}

	memset(out, 0, sizeof(*out));
	out->valid = 1;
	memcpy(out->pos, pos, sizeof(pos));
	out->room_ref = (ti == 0 && tj == 0) ? st->room.ref : 0;
	out->entity_count = (ti == 0 && tj == 0) ? st->entity_count : 0;
	out->biome = t.biome;
	out->exits = t.exits;
	out->flags = t.flags;
	out->obj_count = 0;

	ndw_obj_t raw_objs[NDW_MAX_OBJ];
	int n = ndw_tile_objects(&t, pos, kind == 2, raw_objs);

	/* Unbound room entities */
	const nd_entity_desc_t *ents = NULL;
	size_t ent_count = 0;
	uint32_t room_ref = 0;
	if (ti == 0 && tj == 0) {
		ents = st->entities;
		ent_count = st->entity_count > ND_MAX_ENTITIES ? ND_MAX_ENTITIES : st->entity_count;
		room_ref = st->room.ref;
	} else {
		ents = nd_seven_get_cached_entities(pos, &ent_count, &room_ref);
	}

	uint8_t used_entity[ND_MAX_ENTITIES] = { 0 };

	for (int i = 0; i < n && out->obj_count < ND_SPATIAL_MAX_OBJS; i++) {
		nd_spatial_obj_t *so = &out->objs[out->obj_count];
		so->x = raw_objs[i].x;
		so->y = raw_objs[i].y;
		so->kind = raw_objs[i].kind;
		so->slot_id = raw_objs[i].slot_id;
		so->variant = raw_objs[i].variant;
		so->glyph = raw_objs[i].glyph;
		so->fg = raw_objs[i].fg;
		so->big = raw_objs[i].big;
		so->is_entity = 0;
		so->harvested = 0;
		so->entity_ref = 0;
		so->flip = 0;
		so->sprite_id = 0;
		so->name[0] = '\0';

		if (ti == 0 && tj == 0) {
			const room_tracker_t *tr = nd_seven_get_tracker();
			if (tr && i < tr->count) {
				so->entity_ref = tr->objs[i].entity_ref;
				if (!is_tracked_obj_present(st, i))
					so->harvested = 1;
				if (tr->objs[i].name[0])
					snprintf(so->name, sizeof(so->name), "%s", tr->objs[i].name);
			} else {
				if (!is_tracked_obj_present(st, i))
					so->harvested = 1;
			}
		} else {
			if (nd_seven_is_tile_obj_picked(pos, raw_objs[i].slot_id))
				so->harvested = 1;

			int is_depletable = nd_seven_has_scenery_trait(raw_objs[i].kind, ND_SCENERY_TRAIT_DEPLETABLE);
			const nd_scenery_def_t *def = nd_seven_get_scenery_def(raw_objs[i].kind);
			if (ents && room_ref && is_depletable) {
				/* If room entities are known, check if depletable entity exists */
				for (size_t ei = 0; ei < ent_count; ei++) {
					if (used_entity[ei])
						continue;
					const nd_entity_desc_t *e = &ents[ei];
					if (e->location == room_ref && e->ref != (st ? st->me : 0) &&
					    (!def || nd_scenery_matches_entity(def, e)) && e->side == raw_objs[i].slot_id) {
						so->entity_ref = e->ref;
						if (nd_seven_is_picked(e->ref))
							so->harvested = 1;
						snprintf(so->name, sizeof(so->name), "%s",
						         e->name[0] ? e->name : world_obj_name(&raw_objs[i], st, ti, tj));
						used_entity[ei] = 1;
						break;
					}
				}
				if (!so->entity_ref) {
					/* Fallback match for depletable */
					for (size_t ei = 0; ei < ent_count; ei++) {
						if (used_entity[ei])
							continue;
						const nd_entity_desc_t *e = &ents[ei];
						if (e->location == room_ref && e->ref != (st ? st->me : 0) &&
						    (!def || nd_scenery_matches_entity(def, e))) {
							so->entity_ref = e->ref;
							if (nd_seven_is_picked(e->ref))
								so->harvested = 1;
							snprintf(so->name, sizeof(so->name), "%s",
							         e->name[0] ? e->name : world_obj_name(&raw_objs[i], st, ti, tj));
							used_entity[ei] = 1;
							break;
						}
					}
				}
			}

			if (!so->name[0]) {
				const char *wn = world_obj_name(&raw_objs[i], st, ti, tj);
				if (wn)
					snprintf(so->name, sizeof(so->name), "%s", wn);
			}
		}
		out->obj_count++;
	}

	if (ents && room_ref && ent_count > 0) {
		uint16_t used = 0;
		for (size_t ei = 0; ei < ent_count && out->obj_count < ND_SPATIAL_MAX_OBJS; ei++) {
			const nd_entity_desc_t *e = &ents[ei];
			if (st && st->me && e->ref == st->me)
				continue;
			if (e->location != room_ref)
				continue;
			if (is_entity_nested(st, e))
				continue;
			if (nd_seven_is_picked(e->ref))
				continue;

			/* Check if already bound to one of out->objs across ALL tiles (ti == 0 or ti != 0) */
			int already_bound = 0;
			for (int k = 0; k < out->obj_count; k++) {
				if (out->objs[k].entity_ref == e->ref) {
					already_bound = 1;
					break;
				}
			}
			if (already_bound)
				continue;

			if (is_permanent_scenery_entity(e))
				continue;

			uint32_t spot = ndw_mix(e->ref) % 16;
			int tries = 0;
			while ((used >> spot) & 1) {
				spot = (spot + 1) % 16;
				if (++tries >= 16) break;
			}
			if (tries >= 16) break;
			used |= (uint16_t)(1u << spot);

			nd_spatial_obj_t *so = &out->objs[out->obj_count];
			so->x = (int16_t)(NDW_CENTER_X + s_spots[spot][0]);
			so->y = (int16_t)(NDW_CENTER_Y + s_spots[spot][1]);
			so->kind = 0;
			so->slot_id = 0;
			so->variant = 0;
			so->glyph = e->ch ? (uint8_t)e->ch : (uint8_t)'?';
			so->fg = (uint8_t)(e->fg < 8 ? e->fg + 8 : e->fg);
			so->big = 0;
			so->is_entity = 1;
			so->harvested = 0;
			so->entity_ref = e->ref;
			so->flip = (uint8_t)((ndw_mix(e->ref) >> 8) & 1);
			so->sprite_id = nd_seven_resolve_entity_sprite(e);
			snprintf(so->name, sizeof(so->name), "%s", e->name);
			out->obj_count++;
		}
	}

	if (ci >= 0 && ci < 3 && cj >= 0 && cj < 3 && (ti != 0 || tj != 0)) {
		s_spatial_cache[cj][ci] = *out;
	}
	return 1;
}

int nd_spatial_tile_resolve_map(const nd_map_data_t *map, int col, int row, nd_spatial_tile_t *out)
{
	if (!map || !out || map->width == 0 || map->height == 0 ||
	    col < 0 || row < 0 || col >= (int)map->width || row >= (int)map->height)
		return 0;

	memset(out, 0, sizeof(*out));
	out->valid = 1;
	out->pos[0] = (int16_t)(map->pos[0] + col - (int)map->player_x);
	out->pos[1] = (int16_t)(map->pos[1] - (row - (int)map->player_y));
	out->pos[2] = map->pos[2];
	out->pos[3] = map->pos[3];

	ndw_tile_t t = nd_seven_convert_tile(&map->tiles[row * map->width + col]);
	int is_player_tile = (col == (int)map->player_x && row == (int)map->player_y);
	if (is_player_tile && t.glyph[1] == '!') {
		uint8_t cg = 0, cfg = 0;
		if (nd_seven_centre_restore(out->pos, &cg, &cfg)) {
			t.glyph[1] = cg;
			t.fg[1] = cfg;
		}
	}

	out->biome = t.biome;
	out->exits = t.exits;
	out->flags = t.flags;
	out->obj_count = 0;

	ndw_obj_t raw_objs[NDW_MAX_OBJ];
	int n = ndw_tile_objects(&t, out->pos, 0, raw_objs);

	const room_tracker_t *tr = is_player_tile ? nd_seven_get_tracker() : NULL;

	for (int i = 0; i < n && out->obj_count < ND_SPATIAL_MAX_OBJS; i++) {
		nd_spatial_obj_t *so = &out->objs[out->obj_count];
		so->x = raw_objs[i].x;
		so->y = raw_objs[i].y;
		so->kind = raw_objs[i].kind;
		so->slot_id = raw_objs[i].slot_id;
		so->variant = raw_objs[i].variant;
		so->glyph = raw_objs[i].glyph;
		so->fg = raw_objs[i].fg;
		so->big = raw_objs[i].big;
		so->is_entity = 0;
		so->harvested = 0;
		so->entity_ref = 0;
		so->flip = 0;
		so->sprite_id = 0;
		so->name[0] = '\0';

		if (is_player_tile && tr && i < tr->count) {
			so->entity_ref = tr->objs[i].entity_ref;
			if (tr->objs[i].harvested ||
			    (tr->objs[i].entity_ref && nd_seven_is_picked(tr->objs[i].entity_ref))) {
				so->harvested = 1;
			}
			if (tr->objs[i].name[0])
				snprintf(so->name, sizeof(so->name), "%s", tr->objs[i].name);
		}
		if (nd_seven_is_tile_obj_picked(out->pos, raw_objs[i].slot_id))
			so->harvested = 1;

		out->obj_count++;
	}

	/* Dynamic room entities */
	size_t ent_count = 0;
	uint32_t room_ref = 0;
	const nd_entity_desc_t *ents = nd_seven_get_cached_entities(out->pos, &ent_count, &room_ref);
	if (is_player_tile && (!ents || ent_count == 0) && tr && tr->room_ref) {
		ents = nd_seven_get_cached_entities(tr->pos, &ent_count, &room_ref);
	}

	if (ents && room_ref && ent_count > 0) {
		uint16_t used = 0;
		for (size_t ei = 0; ei < ent_count && out->obj_count < ND_SPATIAL_MAX_OBJS; ei++) {
			const nd_entity_desc_t *e = &ents[ei];
			if (e->flags & ND_EF_PLAYER)
				continue;
			if (e->location != room_ref)
				continue;
			if (nd_seven_is_picked(e->ref))
				continue;

			int already_bound = 0;
			for (int k = 0; k < out->obj_count; k++) {
				if (out->objs[k].entity_ref == e->ref) {
					already_bound = 1;
					break;
				}
			}
			if (already_bound)
				continue;

			if (is_permanent_scenery_entity(e))
				continue;

			uint32_t spot = ndw_mix(e->ref) % 16;
			int tries = 0;
			while ((used >> spot) & 1) {
				spot = (spot + 1) % 16;
				if (++tries >= 16) break;
			}
			if (tries >= 16) break;
			used |= (uint16_t)(1u << spot);

			nd_spatial_obj_t *so = &out->objs[out->obj_count];
			so->x = (int16_t)(NDW_CENTER_X + s_spots[spot][0]);
			so->y = (int16_t)(NDW_CENTER_Y + s_spots[spot][1]);
			so->kind = 0;
			so->slot_id = 0;
			so->variant = 0;
			so->glyph = e->ch ? (uint8_t)e->ch : (uint8_t)'?';
			so->fg = (uint8_t)(e->fg < 8 ? e->fg + 8 : e->fg);
			so->big = 0;
			so->is_entity = 1;
			so->harvested = 0;
			so->entity_ref = e->ref;
			so->flip = (uint8_t)((ndw_mix(e->ref) >> 8) & 1);
			so->sprite_id = nd_seven_resolve_entity_sprite(e);
			snprintf(so->name, sizeof(so->name), "%s", e->name);
			out->obj_count++;
		}
	}

	return 1;
}
