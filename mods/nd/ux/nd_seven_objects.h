#ifndef ND_SEVEN_OBJECTS_H
#define ND_SEVEN_OBJECTS_H

#include <stdint.h>
#include <stddef.h>
#include "nd/bcp.h"
#include <nd/seven-world.h>

enum {
	ND_SCENERY_TRAIT_FOLIAGE   = (1 << 0),
	ND_SCENERY_TRAIT_MINERAL   = (1 << 1),
	ND_SCENERY_TRAIT_WATER     = (1 << 2),
	ND_SCENERY_TRAIT_CONTAINER = (1 << 3),
	ND_SCENERY_TRAIT_BLOCKING  = (1 << 4),
};

typedef struct {
	uint8_t kind;            /* NDW_K_* */
	uint8_t traits;          /* ND_SCENERY_TRAIT_* */
	uint8_t default_slot;    /* 0=side 0, 1=side 1, 2=side 2 */
	const char *harvest_cmd; /* "get", "forage", "fill", etc. */
	const char *alias_stem;  /* "stone", "acacia", "stick", "pond", etc. */
	const char *default_name;/* "stone", "tree", "rock", "pond" */
} nd_scenery_trait_t;

typedef struct {
	int16_t x, y;
	uint8_t kind;
	uint8_t slot_id;
	uint32_t entity_ref;
	uint8_t harvested;
	uint8_t stick_harvested;
	char name[32];
} tracked_obj_t;

typedef struct {
	uint32_t room_ref;
	int16_t pos[4];
	uint8_t glyphs[3];
	int count;
	int bound_entities;
	size_t last_entity_count;
	tracked_obj_t objs[NDW_MAX_OBJ];
} room_tracker_t;

const nd_scenery_trait_t *nd_seven_get_scenery_trait(uint8_t kind);
int nd_seven_has_scenery_trait(uint8_t kind, uint8_t trait);

void sync_room_objects(const nd_client_state_t *st);
int is_tracked_obj_present(const nd_client_state_t *st, int k);
const room_tracker_t *nd_seven_get_tracker(void);
void nd_seven_objects_forget(void);

void nd_seven_cache_room_entities(uint32_t room_ref, const int16_t pos[4],
                                  const nd_entity_desc_t *entities, size_t count);
const nd_entity_desc_t *nd_seven_get_cached_entities(const int16_t pos[4], size_t *out_count, uint32_t *out_room_ref);
int nd_seven_is_tile_obj_picked(const int16_t pos[4], uint8_t slot_id);
void nd_seven_mark_tile_slot_harvested(const int16_t pos[4], uint8_t slot_id);

void nd_seven_mark_picked(uint32_t entity_ref);
int nd_seven_is_picked(uint32_t entity_ref);
void nd_seven_forget_picked(void);

int str_has_ci(const char *hay, const char *needle);
int is_landscape_element(const nd_entity_desc_t *e);
int is_stone_or_rock(const nd_entity_desc_t *e);
int is_contained_in_tree(const nd_client_state_t *st, const nd_entity_desc_t *e);
const char *world_obj_name(const ndw_obj_t *o, const nd_client_state_t *st, int ti, int tj);

int nd_seven_action(const nd_client_state_t *st, char *out, size_t sz);
int nd_seven_action_execute(const nd_client_state_t *st, char *out, size_t sz);

void draw_object_name(uint32_t *fb, int w, int h, int cx, int top_y, const char *name);
void draw_thinking_balloon(uint32_t *fb, int w, int h, int hx, int hy, const char *action);

#endif /* ND_SEVEN_OBJECTS_H */
