#ifndef STATE_MACROS_H
#define STATE_MACROS_H

#include <hyle/schema.h>

/*
 * X-Macro Generators for bud app state.
 *
 * Your schema macro should be defined like this:
 *
 * #define MY_SCHEMA(F_STR, F_INT, st) \
 *     F_STR(st, title, 256) \
 *     F_INT(st, count)
 *
 * Then simply call:
 * BUD_STATE_STRUCT(my_struct_t, MY_SCHEMA)
 * BUD_STATE_FIELDS(my_struct_t, my_fields_array, MY_SCHEMA)
 */

#define BUD_GEN_STRUCT_STR(st, name, sz) char name[sz];
#define BUD_GEN_STRUCT_INT(st, name) int name;

#define BUD_GEN_FIELD_STR(st, name, sz) OVERLAY_STR(name, st, name, sz),
#define BUD_GEN_FIELD_INT(st, name) OVERLAY_INT(name, st, name),

#define BUD_STATE_STRUCT(struct_name, SCHEMA)                                  \
	typedef struct {                                                       \
		SCHEMA(BUD_GEN_STRUCT_STR, BUD_GEN_STRUCT_INT, struct_name)    \
	} struct_name;

#define BUD_STATE_STRUCT_EXT(struct_name, SCHEMA, EXT)                         \
	typedef struct {                                                       \
		SCHEMA(BUD_GEN_STRUCT_STR, BUD_GEN_STRUCT_INT, struct_name)    \
		EXT                                                            \
	} struct_name;

#define BUD_STATE_FIELDS(struct_name, array_name, SCHEMA)                      \
	static const bud_field_desc_t array_name[] = { SCHEMA(                 \
		BUD_GEN_FIELD_STR, BUD_GEN_FIELD_INT, struct_name)             \
		                                               FIELD_END };

#endif
