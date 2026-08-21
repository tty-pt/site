/* Unified field definition for grp module.
 * One table serves both server (source generators).
 *
 * Kinds:
 *   0 = CH_RECORD   — record field, include in state JSON
 *   1 = CH_EXCLUDE  — record field, exclude from state JSON
 */

#ifndef GRP_FIELDS_H
#define GRP_FIELDS_H

#include "bud/bud.h"
#include <stddef.h>
#include "../common/field_macros.h"

/* ── Grp item ──────────────────────────────────────────── */

typedef struct {
	char id[64];
	char title[256];
	char format[2048];
	char owner[32];
} grp_cache_t;

static const bud_field_desc_t grp_fields[] = {
	REC_FIELD(id, grp_cache_t, id, 64, 1, 0, 0, 0),
	REC_FIELD(title, grp_cache_t, title, 256, 1, 0, 0, 1),
	REC_FIELD(format, grp_cache_t, format, 2048, 1, 0, 0, 1),
	EXCL_FIELD(owner, grp_cache_t, owner, 32, BUD_QM_STR, 1),
	INVERSE_FIELD(gigs, "gig.items", "grp"),
	INVERSE_FIELD(repertoire, "grp.songs", "grp"),
	FIELD_END
};

#define GRP_FIELD_COUNT (sizeof(grp_fields) / sizeof(grp_fields[0]) - 1)

#ifndef __wasm__
static const source_list_field_t grp_list_fields[] = {
	{ "title", "Title" },
};

static const source_list_view_t grp_list_view = {
	"group",
	grp_list_fields,
	sizeof(grp_list_fields) / sizeof(grp_list_fields[0]),
	NULL,
	NULL,
	NULL,
	NULL,
};
#endif

#endif
