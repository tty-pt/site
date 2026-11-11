/* Unified field definition for song module.
 * One table serves both server (source generators) and WASM (bud_state_apply).
 *
 * Kinds:
 *   0 = SF_RECORD     — record field, include as-is in state JSON
 *   1 = SF_EXCLUDE    — record field, exclude from state JSON
 *   2 = SF_REF_DISPLAY — record field, resolve multi-ref IDs to display names
 *   3 = SONG_OVERLAY_INT  — computed int, overlay onto state JSON
 *   4 = SONG_OVERLAY_STR  — computed string, overlay onto state JSON
 */

#ifndef SONG_FIELDS_H
#define SONG_FIELDS_H

#include "schema.h"

#define song_app_fields song_bud_fields

/* ── Song type entity ────────────────────────────────────────── */

typedef struct {
	char id[64];
	char name[256];
} song_type_cache_t;

static const hyle_schema_desc_t song_type_fields[] = {
	FIELD_TEXT(id, song_type_cache_t),
	FIELD_TEXT(name, song_type_cache_t, .in_meta = 1),
	FIELD_INVERSE(songs, "song.items", "type"),
	FIELD_END
};

#define SONG_TYPE_FIELD_COUNT                                                  \
	(sizeof(song_type_fields) / sizeof(song_type_fields[0]) - 1)

/* ── Song author entity ──────────────────────────────────────── */

typedef struct {
	char id[64];
	char name[256];
} song_author_cache_t;

static const hyle_schema_desc_t song_author_fields[] = {
	FIELD_TEXT(id, song_author_cache_t),
	FIELD_TEXT(name, song_author_cache_t, .in_meta = 1),
	FIELD_INVERSE(songs, "song.items", "author"),
	FIELD_END
};

#define SONG_AUTHOR_FIELD_COUNT                                                \
	(sizeof(song_author_fields) / sizeof(song_author_fields[0]) - 1)

#define SONG_FIELD_COUNT (sizeof(song_hyle_schema) / sizeof(song_hyle_schema[0]) - 1)

typedef song_state_t app_state_t;

#endif
