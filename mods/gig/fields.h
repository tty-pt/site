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
#include "../common/field_macros.h"

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
	char format[64];
} gig_song_item_t;

static const hyle_schema_desc_t gig_song_fields[] = {
	REF_FIELD_S(
	        song, gig_song_item_t, song, 64, "song.items", "songs", 1,
	        "dropdown"),
	REC_FIELD(
	        transpose, gig_song_item_t, transpose, sizeof(int), 1, 0, 0, 1),
	REF_FIELD_S(
	        fmt, gig_song_item_t, format, 64, "song.types", "types", 1,
	        "dropdown"),
	FIELD_END
};

static const hyle_schema_desc_t gig_fields[] = {
	REC_FIELD(id, gig_cache_t, id, 64, 1, 0, 0, 0),
	REC_FIELD(title, gig_cache_t, title, 256, 1, 0, 0, 1),
	REF_FIELD_S(
	        grp, gig_cache_t, grp, 128, "grp.items", "gigs", 1, "dropdown"),
	EXCL_FIELD_W(song_source, gig_cache_t, song_source, 16, BUD_QM_STR, 1),
	EXCL_FIELD(owner, gig_cache_t, owner, 32, BUD_QM_STR, 0),
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
