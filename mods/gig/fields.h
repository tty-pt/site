/* Unified field definition for gig module.
 * One table serves both server (source generators) and WASM (bud_state_apply).
 *
 * Kinds:
 *   0 = SB_RECORD  — record field, include in state JSON
 *   1 = SB_EXCLUDE — record field, exclude from state JSON
 *   3 = SB_OVERLAY_INT   — computed int overlay
 *   4 = SB_OVERLAY_STR   — computed string overlay
 */

#ifndef GIG_FIELDS_H
#define GIG_FIELDS_H

#include <hyle/schema.h>
#include <stddef.h>

/* ── Gig item ──────────────────────────────────────── */

typedef struct {
	char id[64];
	char title[256];
	char grp[128];
	char song_source[16];
	char owner[32];
} gig_cache_t;

/* Ordered list song item schema (for row-level pickers) */
typedef struct {
	char song[64];
	int transpose;
	char fmt[64];
} gig_song_item_t;

static const hyle_schema_desc_t gig_song_fields[] = {
	FIELD_REF(
	        song, gig_song_item_t, "song.items",
	        .ref_inverse = "songs", .filter_style = "dropdown",
	        .in_meta = 1),
	FIELD_INT(transpose, gig_song_item_t, .in_meta = 1),
	FIELD_REF(
	        fmt, gig_song_item_t, "song.types",
	        .ref_inverse = "types", .filter_style = "dropdown",
	        .in_meta = 1),
	FIELD_END
};

static const hyle_schema_desc_t gig_fields[] = {
	FIELD_TEXT(id, gig_cache_t),
	FIELD_TEXT(title, gig_cache_t, .in_meta = 1),
	FIELD_REF(
	        grp, gig_cache_t, "grp.items",
	        .ref_inverse = "gigs", .filter_style = "dropdown",
	        .in_meta = 1),
	FIELD_EXCL(song_source, gig_cache_t, .writable = 1, .in_meta = 1),
	FIELD_EXCL(owner, gig_cache_t),
	FIELD_END
};

#define SB_FIELD_COUNT (sizeof(gig_fields) / sizeof(gig_fields[0]) - 1)

#ifndef __wasm__
static const source_list_field_t gig_list_fields[] = {
	{ "title", "Title" },
	{ "grp", "Group" },
};

static const source_list_view_t gig_list_view = {
	"gig",
	gig_list_fields,
	sizeof(gig_list_fields) / sizeof(gig_list_fields[0]),
	NULL,
	NULL,
	NULL,
	NULL,
};
#endif

#endif
