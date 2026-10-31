#include "nd_seven_objects.h"
#include "nd_seven_motion.h"
#include <stdio.h>
#include <string.h>

#define SCALE 2
#define LAMB_AY 23

static const int8_t s_spots[16][2] = {
	{ -16, 26 }, { 16, 26 }, { -28, 12 }, { 28, 12 }, { -38, -6 }, { 38, -6 }, { -22, -18 }, { 22, -18 },
	{ -52, 0 }, { 52, 0 }, { -46, 24 }, { 46, 24 }, { -30, 34 }, { 30, 34 }, { -46, -22 }, { 46, -22 }
};

static room_tracker_t s_tracker = { 0 };

static const nd_scenery_trait_t s_scenery_traits[] = {
	{ NDW_K_DECID,       ND_SCENERY_TRAIT_FOLIAGE | ND_SCENERY_TRAIT_CONTAINER, "get",  "tree" },
	{ NDW_K_TALL,        ND_SCENERY_TRAIT_FOLIAGE | ND_SCENERY_TRAIT_CONTAINER, "get",  "tall tree" },
	{ NDW_K_FIR,         ND_SCENERY_TRAIT_FOLIAGE | ND_SCENERY_TRAIT_CONTAINER, "get",  "fir tree" },
	{ NDW_K_CACTUS,      ND_SCENERY_TRAIT_FOLIAGE | ND_SCENERY_TRAIT_BLOCKING,  "get",  "cactus" },
	{ NDW_K_STONE,       ND_SCENERY_TRAIT_MINERAL,                               "get",  "stone" },
	{ NDW_K_ROCK,        ND_SCENERY_TRAIT_MINERAL | ND_SCENERY_TRAIT_BLOCKING,  "get",  "rock" },
	{ NDW_K_BOULDER,     ND_SCENERY_TRAIT_MINERAL | ND_SCENERY_TRAIT_BLOCKING,  "get",  "boulder" },
	{ NDW_K_POND,        ND_SCENERY_TRAIT_WATER,                                 "fill", "pond" },
	{ NDW_K_SHOP,        ND_SCENERY_TRAIT_CONTAINER,                             "look", "shop" },
	{ NDW_K_STAIRS_DOWN, 0,                                                      "down", "stairs down" },
	{ NDW_K_STAIRS_UP,   0,                                                      "up",   "stairs up" },
	{ 0, 0, NULL, NULL }
};

const nd_scenery_trait_t *nd_seven_get_scenery_trait(uint8_t kind)
{
	for (size_t i = 0; s_scenery_traits[i].default_name != NULL; i++) {
		if (s_scenery_traits[i].kind == kind)
			return &s_scenery_traits[i];
	}
	return NULL;
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
	nd_entity_desc_t entities[ND_MAX_ENTITIES];
} room_entity_cache_entry_t;

static room_entity_cache_entry_t s_room_cache[MULTI_ROOM_CACHE_SIZE];
static unsigned s_room_cache_next = 0;

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
	}
	entry->room_ref = room_ref;
	memcpy(entry->pos, pos, sizeof(entry->pos));
	entry->entity_count = (count > ND_MAX_ENTITIES) ? ND_MAX_ENTITIES : count;
	if (entities && count > 0)
		memcpy(entry->entities, entities, entry->entity_count * sizeof(nd_entity_desc_t));
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
	size_t count = 0;
	uint32_t room_ref = 0;
	const nd_entity_desc_t *ents = nd_seven_get_cached_entities(pos, &count, &room_ref);
	if (!ents || count == 0)
		return 0;
	for (size_t i = 0; i < count; i++) {
		if (ents[i].location == room_ref && ents[i].side == slot_id &&
		    nd_seven_is_picked(ents[i].ref))
			return 1;
	}
	return 0;
}

void nd_seven_objects_forget(void)
{
	memset(&s_tracker, 0, sizeof(s_tracker));
	nd_seven_forget_picked();
	memset(s_room_cache, 0, sizeof(s_room_cache));
	s_room_cache_next = 0;
}

int str_has_ci(const char *hay, const char *needle)
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

int is_landscape_element(const nd_entity_desc_t *e)
{
	if (!e) return 0;
	if (e->ch == '*' || e->ch == 'o' || e->ch == 'O' ||
	    e->ch == 'x' || e->ch == 'X' || e->ch == 't' || e->ch == 'T' ||
	    e->ch == 'a' || e->ch == 'A' || e->ch == 'i' || e->ch == 'I')
		return 1;
	return str_has_ci(e->name, "stone") || str_has_ci(e->name, "rock") ||
	       str_has_ci(e->name, "boulder") || str_has_ci(e->name, "mineral") ||
	       str_has_ci(e->name, "quartz") || str_has_ci(e->name, "flint") ||
	       str_has_ci(e->name, "pebble") ||
	       str_has_ci(e->name, "tree") || str_has_ci(e->name, "pine") ||
	       str_has_ci(e->name, "fir") || str_has_ci(e->name, "birch") ||
	       str_has_ci(e->name, "cactus") || str_has_ci(e->name, "plant") ||
	       str_has_ci(e->name, "pinus") || str_has_ci(e->name, "pseudotsuga") ||
	       str_has_ci(e->name, "abies") || str_has_ci(e->name, "acacia") ||
	       str_has_ci(e->name, "betula") || str_has_ci(e->name, "linum") ||
	       str_has_ci(e->name, "arthrocereus");
}

int is_stone_or_rock(const nd_entity_desc_t *e)
{
	if (!e) return 0;
	if (e->ch == '*' || e->ch == 'o' || e->ch == 'O')
		return 1;
	return str_has_ci(e->name, "stone") || str_has_ci(e->name, "rock") ||
	       str_has_ci(e->name, "boulder") || str_has_ci(e->name, "mineral") ||
	       str_has_ci(e->name, "quartz") || str_has_ci(e->name, "flint") ||
	       str_has_ci(e->name, "pebble");
}

int is_contained_in_tree(const nd_client_state_t *st, const nd_entity_desc_t *e)
{
	if (!st || !e)
		return 0;

	if (str_has_ci(e->name, "stick")) {
		/* A stick not directly in the room is inside a container/tree */
		if (e->location != st->room.ref)
			return 1;
		/* If the stick is in the room and the room has trees, it belongs inside the tree */
		const room_tracker_t *tr = nd_seven_get_tracker();
		if (tr) {
			for (int k = 0; k < tr->count; k++) {
				uint8_t kind = tr->objs[k].kind;
				if (kind == NDW_K_DECID || kind == NDW_K_TALL || kind == NDW_K_FIR)
					return 1;
			}
		}
		for (size_t i = 0; i < st->entity_count; i++) {
			const nd_entity_desc_t *other = &st->entities[i];
			if (other->location == st->room.ref && is_landscape_element(other) && !is_stone_or_rock(other))
				return 1;
		}
	}

	for (size_t i = 0; i < st->entity_count; i++) {
		const nd_entity_desc_t *other = &st->entities[i];
		if (other->ref == e->location && is_landscape_element(other) && !is_stone_or_rock(other))
			return 1;
	}
	return 0;
}

const char *world_obj_name(const ndw_obj_t *o, const nd_client_state_t *st, int ti, int tj)
{
	if (ti == 0 && tj == 0 && st && st->room.ref) {
		if (o->kind == NDW_K_STONE || o->kind == NDW_K_ROCK || o->kind == NDW_K_BOULDER) {
			for (size_t i = 0; i < st->entity_count; i++) {
				if (st->entities[i].ref != st->me &&
				    st->entities[i].location == st->room.ref &&
				    !nd_seven_is_picked(st->entities[i].ref) &&
				    is_stone_or_rock(&st->entities[i])) {
					if (st->entities[i].name[0])
						return st->entities[i].name;
				}
			}
		}
		if (o->kind == NDW_K_DECID || o->kind == NDW_K_TALL || o->kind == NDW_K_FIR || o->kind == NDW_K_CACTUS) {
			for (size_t i = 0; i < st->entity_count; i++) {
				if (st->entities[i].ref != st->me &&
				    st->entities[i].location == st->room.ref &&
				    is_landscape_element(&st->entities[i]) &&
				    !is_stone_or_rock(&st->entities[i])) {
					if (st->entities[i].name[0])
						return st->entities[i].name;
				}
			}
		}
	}
	switch (o->kind) {
	case NDW_K_STONE:
	case NDW_K_ROCK:
	case NDW_K_BOULDER:
		return "stone";
	case NDW_K_FIR:
		return "abies alba";
	case NDW_K_CACTUS:
		return "arthrocereus rondonianus";
	case NDW_K_TALL:
		return (o->fg == 12) ? "acacia senegal" : "pseudotsuga menziesii";
	case NDW_K_DECID:
		return (o->fg == 14 || o->fg == 6) ? "betula pendula" :
		       (o->fg == 15 || o->fg == 7) ? "betula pubescens" : "pinus sylvestris";
	case NDW_K_SHOP:        return "shop";
	case NDW_K_STAIRS_DOWN: return "stairs";
	case NDW_K_STAIRS_UP:   return "stairs";
	case NDW_K_POND:        return "pond";
	default:                return NULL;
	}
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

	int need_sync = 0;
	if (s_tracker.room_ref != st->room.ref ||
	    memcmp(s_tracker.pos, pos, sizeof(pos)) != 0 ||
	    memcmp(s_tracker.glyphs, t.glyph, 3) != 0 ||
	    s_tracker.last_entity_count != st->entity_count)
		need_sync = 1;

	if (!need_sync)
		return;

	nd_seven_cache_room_entities(st->room.ref, pos, st->entities, st->entity_count);

	s_tracker.room_ref = st->room.ref;
	memcpy(s_tracker.pos, pos, sizeof(pos));
	memcpy(s_tracker.glyphs, t.glyph, 3);
	s_tracker.last_entity_count = st->entity_count;
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
		s_tracker.objs[k].stick_harvested = 0;
		s_tracker.objs[k].name[0] = '\0';

		int is_mineral = nd_seven_has_scenery_trait(objs[k].kind, ND_SCENERY_TRAIT_MINERAL);
		int is_foliage = nd_seven_has_scenery_trait(objs[k].kind, ND_SCENERY_TRAIT_FOLIAGE);
		uint8_t obj_slot = objs[k].slot_id;

		if (is_mineral) {
			for (size_t ei = 0; ei < st->entity_count; ei++) {
				if (used_entity[ei])
					continue;
				const nd_entity_desc_t *e = &st->entities[ei];
				if (e->location == st->room.ref && e->ref != st->me && is_stone_or_rock(e) &&
				    e->side == obj_slot) {
					s_tracker.objs[k].entity_ref = e->ref;
					snprintf(s_tracker.objs[k].name, sizeof(s_tracker.objs[k].name), "%s",
					         e->name[0] ? e->name : "stone");
					used_entity[ei] = 1;
					s_tracker.bound_entities++;
					if (nd_seven_is_picked(e->ref))
						s_tracker.objs[k].harvested = 1;
					break;
				}
			}
		} else if (is_foliage) {
			for (size_t ei = 0; ei < st->entity_count; ei++) {
				if (used_entity[ei])
					continue;
				const nd_entity_desc_t *e = &st->entities[ei];
				if (e->location == st->room.ref && e->ref != st->me &&
				    is_landscape_element(e) && !is_stone_or_rock(e) &&
				    e->side == obj_slot) {
					s_tracker.objs[k].entity_ref = e->ref;
					snprintf(s_tracker.objs[k].name, sizeof(s_tracker.objs[k].name), "%s",
					         e->name[0] ? e->name : world_obj_name(&objs[k], st, 0, 0));
					used_entity[ei] = 1;
					s_tracker.bound_entities++;
					break;
				}
			}
		}
	}

	/* Pass 2: fallback match for any remaining unbound objects */
	for (int k = 0; k < cnt; k++) {
		if (s_tracker.objs[k].entity_ref)
			continue;
		int is_mineral = nd_seven_has_scenery_trait(s_tracker.objs[k].kind, ND_SCENERY_TRAIT_MINERAL);
		int is_foliage = nd_seven_has_scenery_trait(s_tracker.objs[k].kind, ND_SCENERY_TRAIT_FOLIAGE);

		if (is_mineral) {
			for (size_t ei = 0; ei < st->entity_count; ei++) {
				if (used_entity[ei])
					continue;
				const nd_entity_desc_t *e = &st->entities[ei];
				if (e->location == st->room.ref && e->ref != st->me && is_stone_or_rock(e)) {
					s_tracker.objs[k].entity_ref = e->ref;
					snprintf(s_tracker.objs[k].name, sizeof(s_tracker.objs[k].name), "%s",
					         e->name[0] ? e->name : "stone");
					used_entity[ei] = 1;
					s_tracker.bound_entities++;
					if (nd_seven_is_picked(e->ref))
						s_tracker.objs[k].harvested = 1;
					break;
				}
			}
		} else if (is_foliage) {
			for (size_t ei = 0; ei < st->entity_count; ei++) {
				if (used_entity[ei])
					continue;
				const nd_entity_desc_t *e = &st->entities[ei];
				if (e->location == st->room.ref && e->ref != st->me &&
				    is_landscape_element(e) && !is_stone_or_rock(e)) {
					s_tracker.objs[k].entity_ref = e->ref;
					snprintf(s_tracker.objs[k].name, sizeof(s_tracker.objs[k].name), "%s",
					         e->name[0] ? e->name : world_obj_name(&objs[k], st, 0, 0));
					used_entity[ei] = 1;
					s_tracker.bound_entities++;
					break;
				}
			}
		}
	}

	/* Finalize names and stick harvest */
	for (int k = 0; k < cnt; k++) {
		const nd_scenery_trait_t *tr = nd_seven_get_scenery_trait(s_tracker.objs[k].kind);
		int is_mineral = tr && (tr->traits & ND_SCENERY_TRAIT_MINERAL);
		int is_foliage = tr && (tr->traits & ND_SCENERY_TRAIT_FOLIAGE);
		if (is_mineral) {
			if (!s_tracker.objs[k].name[0])
				snprintf(s_tracker.objs[k].name, sizeof(s_tracker.objs[k].name), "%s",
				         tr->default_name ? tr->default_name : "stone");
		} else if (is_foliage) {
			if (s_tracker.objs[k].entity_ref) {
				for (size_t si = 0; si < st->entity_count; si++) {
					if (st->entities[si].location == s_tracker.objs[k].entity_ref &&
					    str_has_ci(st->entities[si].name, "stick") &&
					    nd_seven_is_picked(st->entities[si].ref)) {
						s_tracker.objs[k].stick_harvested = 1;
						break;
					}
				}
			}
			if (!s_tracker.objs[k].name[0]) {
				const char *wn = world_obj_name(&objs[k], st, 0, 0);
				if (wn)
					snprintf(s_tracker.objs[k].name, sizeof(s_tracker.objs[k].name), "%s", wn);
			}
		} else {
			const char *wn = world_obj_name(&objs[k], st, 0, 0);
			if (wn)
				snprintf(s_tracker.objs[k].name, sizeof(s_tracker.objs[k].name), "%s", wn);
		}
	}
}

int is_tracked_obj_present(const nd_client_state_t *st, int k)
{
	if (!st || !st->room.ref || k < 0 || k >= s_tracker.count)
		return 1;

	if (s_tracker.objs[k].harvested)
		return 0;

	uint8_t kind = s_tracker.objs[k].kind;
	int is_scenery = (kind == NDW_K_DECID || kind == NDW_K_TALL || kind == NDW_K_FIR || kind == NDW_K_CACTUS ||
	                  kind == NDW_K_POND || kind == NDW_K_SHOP || kind == NDW_K_STAIRS_DOWN || kind == NDW_K_STAIRS_UP);

	/* Permanent landscape and world features from the map tile must never be suppressed by entity counts */
	if (is_scenery)
		return 1;

	if (s_tracker.objs[k].entity_ref != 0) {
		if (nd_seven_is_picked(s_tracker.objs[k].entity_ref))
			return 0;
		for (size_t ei = 0; ei < st->entity_count; ei++) {
			if (st->entities[ei].ref == s_tracker.objs[k].entity_ref &&
			    st->entities[ei].location == st->room.ref)
				return 1;
		}
		return 0;
	}

	/* Unbound mineral: if room entities are loaded in an instantiated room,
	 * any mineral that does not exist in Corm DB has been harvested/cleared */
	int is_mineral = (kind == NDW_K_STONE || kind == NDW_K_ROCK || kind == NDW_K_BOULDER);
	if (is_mineral && st->room.ref != 0 && st->entity_count > 0)
		return 0;

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
				uint8_t kind = s_tracker.objs[k].kind;
				if (nd_seven_has_scenery_trait(kind, ND_SCENERY_TRAIT_MINERAL)) {
					if (s_tracker.objs[k].entity_ref && !nd_seven_is_picked(s_tracker.objs[k].entity_ref)) {
						snprintf(best_cmd, sizeof(best_cmd), "get #%u\n", s_tracker.objs[k].entity_ref);
						best_dist = d2;
						best_tracker_k = k;
						best_picked_ref = s_tracker.objs[k].entity_ref;
					}
				} else if (nd_seven_has_scenery_trait(kind, ND_SCENERY_TRAIT_FOLIAGE)) {
					uint32_t tree_ref = s_tracker.objs[k].entity_ref;
					uint32_t stick_ref = 0;
					if (!s_tracker.objs[k].stick_harvested) {
						for (size_t si = 0; si < st->entity_count; si++) {
							if ((st->entities[si].location == tree_ref || (st->entities[si].location == st->room.ref && !tree_ref)) &&
							    str_has_ci(st->entities[si].name, "stick") &&
							    !nd_seven_is_picked(st->entities[si].ref)) {
								stick_ref = st->entities[si].ref;
								break;
							}
						}
					}
					if (stick_ref) {
						if (tree_ref)
							snprintf(best_cmd, sizeof(best_cmd), "get #%u #%u\n", tree_ref, stick_ref);
						else
							snprintf(best_cmd, sizeof(best_cmd), "get #%u\n", stick_ref);
						best_dist = d2;
						best_tracker_k = k;
						best_picked_ref = stick_ref;
					}
				} else if (kind == NDW_K_STAIRS_DOWN) {
					snprintf(best_cmd, sizeof(best_cmd), ">\n");
					best_dist = d2;
				} else if (kind == NDW_K_STAIRS_UP) {
					snprintf(best_cmd, sizeof(best_cmd), "<\n");
					best_dist = d2;
				} else if (kind == NDW_K_SHOP) {
					snprintf(best_cmd, sizeof(best_cmd), "shop\n");
					best_dist = d2;
				}
			}
		}
	}

	uint32_t room = st->room.ref;
	if (room) {
		for (size_t i = 0; i < st->entity_count; i++) {
			const nd_entity_desc_t *e = &st->entities[i];
			if (st->me && e->ref == st->me)
				continue;
			if (e->location != room || is_contained_in_tree(st, e))
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
			if (is_landscape_element(e) && !is_stone_or_rock(e))
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
					if ((e->actions & 4) || (e->actions == 0 && e->type != 1) || is_stone_or_rock(e) || str_has_ci(e->name, "stick")) {
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
				if (kind == NDW_K_STONE || kind == NDW_K_ROCK || kind == NDW_K_BOULDER)
					s_tracker.objs[best_tracker_k].harvested = 1;
				else if (kind == NDW_K_DECID || kind == NDW_K_TALL || kind == NDW_K_FIR)
					s_tracker.objs[best_tracker_k].stick_harvested = 1;
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
	return seven_action_impl(st, out, sz, 1);
}
