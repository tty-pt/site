#ifndef ND_SEVEN_OBJECTS_H
#define ND_SEVEN_OBJECTS_H

#include <stdint.h>
#include <stddef.h>
#include "nd/bcp.h"
#include <nd/seven-world.h>

enum {
	ND_SCENERY_TRAIT_PERMANENT = (1 << 0), /* Static scenery never cleared when room entities absent */
	ND_SCENERY_TRAIT_DEPLETABLE= (1 << 1), /* Harvestable/gatherable node cleared if absent from room DB */
	ND_SCENERY_TRAIT_WATER     = (1 << 2),
	ND_SCENERY_TRAIT_BLOCKING  = (1 << 3),

	/* Compatibility aliases */
	ND_SCENERY_TRAIT_FOLIAGE   = ND_SCENERY_TRAIT_PERMANENT,
	ND_SCENERY_TRAIT_MINERAL   = ND_SCENERY_TRAIT_DEPLETABLE,
};

typedef struct tracked_obj tracked_obj_t;

typedef struct nd_scenery_def {
	uint8_t kind;            /* NDW_K_* */
	uint8_t traits;          /* ND_SCENERY_TRAIT_* */
	uint8_t default_slot;    /* 0=side 0, 1=side 1, 2=side 2 */
	const char *default_name;/* "stone", "tree", "rock", "pond" */

	/* Declarative interaction handler */
	int (*interact)(const nd_client_state_t *st, const tracked_obj_t *obj,
	                char *cmd_out, size_t sz, uint32_t *out_picked_ref);
} nd_scenery_def_t;

typedef nd_scenery_def_t nd_scenery_trait_t;

struct tracked_obj {
	int16_t x, y;
	uint8_t kind;
	uint8_t slot_id;
	uint32_t entity_ref;
	uint8_t harvested;
	uint8_t drop_harvested;  /* Generic: child drops/contents have been taken */
	char name[32];
};

typedef struct {
	uint32_t room_ref;
	int16_t pos[4];
	uint8_t glyphs[3];
	int count;
	int bound_entities;
	size_t last_entity_count;
	uint32_t last_entity_hash;
	tracked_obj_t objs[NDW_MAX_OBJ];
} room_tracker_t;

/* Shared spatial entity resolver structures */
typedef struct {
	int16_t x, y;             /* 0..160, 0..128 tile local px */
	uint8_t kind;             /* NDW_K_* */
	uint8_t slot_id;          /* 0, 1, 2 */
	uint8_t variant;          /* variant index */
	uint8_t glyph;            /* visual symbol or plant glyph */
	uint8_t fg;               /* ansi color */
	uint8_t big;              /* 1 if large foliage */
	uint8_t is_entity;        /* 1 if server entity, 0 if procedural */
	uint8_t flip;             /* flip sprite horizontal */
	uint8_t sprite_id;        /* mineral sprite id (1..8) */
	uint8_t harvested;        /* 1 if picked/harvested */
	uint8_t plant_id;         /* packed flora species/stage/variant */
	uint8_t col_mask;         /* programmer-defined collision footprint */
	uint8_t hw, hh;           /* collision half-extents */
	uint8_t anchor_y;         /* downward screen-Y anchor offset from bottom of sprite */
	uint32_t entity_ref;      /* Corm DB entity ref if known */
	char name[32];            /* display name */
	char art[64];             /* staged art path if known */
} nd_spatial_obj_t;

#define ND_SPATIAL_MAX_OBJS (NDW_MAX_OBJ + 16)

typedef struct {
	uint8_t valid;
	int16_t pos[4];
	uint32_t room_ref;
	size_t entity_count;
	uint8_t biome;
	uint8_t exits;
	uint8_t flags;
	uint8_t obj_count;
	nd_spatial_obj_t objs[ND_SPATIAL_MAX_OBJS];
} nd_spatial_tile_t;

const nd_scenery_def_t *nd_seven_get_scenery_def(uint8_t kind);
const nd_scenery_trait_t *nd_seven_get_scenery_trait(uint8_t kind);
int nd_seven_has_scenery_trait(uint8_t kind, uint8_t trait);

void nd_spatial_resolver_invalidate(void);
int nd_spatial_tile_resolve(const nd_client_state_t *st, int ti, int tj, nd_spatial_tile_t *out);
int nd_spatial_tile_resolve_map(const nd_map_data_t *map, int col, int row, nd_spatial_tile_t *out);

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

int is_entity_nested(const nd_client_state_t *st, const nd_entity_desc_t *e);
int is_permanent_scenery_entity(const nd_entity_desc_t *e);
int nd_scenery_matches_entity(const nd_scenery_def_t *def, const nd_entity_desc_t *e);
uint8_t nd_seven_resolve_entity_sprite(const nd_entity_desc_t *e);
const char *world_obj_name(const ndw_obj_t *o, const nd_client_state_t *st, int ti, int tj);

int nd_seven_action(const nd_client_state_t *st, char *out, size_t sz);
int nd_seven_action_execute(const nd_client_state_t *st, char *out, size_t sz);
void nd_seven_action_cooldown_reset(void);

#endif /* ND_SEVEN_OBJECTS_H */
