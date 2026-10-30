#ifndef ND_SEVEN_OBJECTS_H
#define ND_SEVEN_OBJECTS_H

#include <stdint.h>
#include <stddef.h>
#include "nd/bcp.h"
#include <nd/seven-world.h>

typedef struct {
	int16_t x, y;
	uint8_t kind;
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
	tracked_obj_t objs[NDW_MAX_OBJ];
} room_tracker_t;

void sync_room_objects(const nd_client_state_t *st);
int is_tracked_obj_present(const nd_client_state_t *st, int k);
const room_tracker_t *nd_seven_get_tracker(void);
void nd_seven_objects_forget(void);

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
