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

#include <hyle/schema.h>
#include <stddef.h>
#include "../common/state_macros.h"

typedef struct {
	char id[128];
	char title[256];
	char type[2048];
	char author[256];
	char yt[512];
	char audio[512];
	char pdf[512];
	char owner[32];
} song_cache_t;

typedef struct {
	song_cache_t cache;
	int transpose;
	int use_latin;
	int show_media;
	char chord_html[65536];
	int original_key;
	char save_url[512];
	int is_owner;
	char page_user[64];
	char path[256];
	int zoom;
} app_state_t;

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

/* ── Song record fields ──────────────────────────────────────── */

static const hyle_schema_desc_t song_fields[] = {
	FIELD_TEXT(id, song_cache_t),
	FIELD_TEXT(title, song_cache_t, .required = 1, .min_length = 1, .in_meta = 1),
	FIELD_ARRAY(
	        FIELD_REF, type, song_cache_t, "song.types",
	        .ref_inverse = "songs", .filter_style = "dropdown",
	        .filter_mode = "and", .allow_add = 1, .in_meta = 1),
	FIELD_REF(
	        author, song_cache_t, "song.authors",
	        .ref_inverse = "songs", .filter_style = "dropdown",
	        .allow_add = 1, .in_meta = 1),
	FIELD_TEXT(yt, song_cache_t, .in_meta = 1),
	FIELD_TEXT(audio, song_cache_t, .in_meta = 1),
	FIELD_TEXT(pdf, song_cache_t, .in_meta = 1),
	FIELD_FILE(data, "data.txt"),
	FIELD_EXCL(owner, song_cache_t),
	FIELD_DERIVED(lyrics, "song.lyrics_from_data"),
	FIELD_END
};

#define SONG_FIELD_COUNT (sizeof(song_fields) / sizeof(song_fields[0]) - 1)

#define SONG_APP_SCHEMA(F_STR, F_INT, st)                                      \
	F_STR(st, save_url, 512)                                               \
	F_STR(st, page_user, 64)                                               \
	F_STR(st, path, 256)                                                   \
	F_INT(st, transpose)                                                   \
	F_INT(st, use_latin)                                                   \
	F_INT(st, show_media)                                                  \
	F_INT(st, zoom)                                                        \
	F_INT(st, original_key)                                                \
	F_INT(st, is_owner)

BUD_STATE_FIELDS(app_state_t, song_app_fields, SONG_APP_SCHEMA)

#ifndef __wasm__
static const source_list_field_t song_list_fields[] = {
	{ "title", "Title" },
	{ "type", "Type" },
	{ "author", "Author" },
};

static const source_list_view_t song_list_view = {
	"song",
	song_list_fields,
	sizeof(song_list_fields) / sizeof(song_list_fields[0]),
	NULL,
	"lyrics",
	"Lyrics",
	"e.g. \"a quiet place\"",
};
#endif

#endif
