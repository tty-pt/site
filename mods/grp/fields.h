/* Unified field definition for grp module.
 * One table serves both server (source generators).
 *
 * Kinds:
 *   0 = CH_RECORD   — record field, include in state JSON
 *   1 = CH_EXCLUDE  — record field, exclude from state JSON
 */

#ifndef GRP_FIELDS_H
#define GRP_FIELDS_H

#include <hyle/schema.h>
#include <stddef.h>

/* ── Grp item ──────────────────────────────────────────── */

typedef struct {
	char id[64];
	char title[256];
	char format[2048];
	char owner[32];
} grp_cache_t;

static const hyle_schema_desc_t grp_fields[] = {
	FIELD_TEXT(id, grp_cache_t),
	FIELD_TEXT(title, grp_cache_t, .in_meta = 1),
	FIELD_TEXT(format, grp_cache_t, .in_meta = 1),
	FIELD_EXCL(owner, grp_cache_t, .in_meta = 1),
	FIELD_INVERSE(gigs, "gig.items", "grp"),
	FIELD_INVERSE(repertoire, "grp.songs", "grp"),
	FIELD_END
};

#define GRP_FIELD_COUNT (sizeof(grp_fields) / sizeof(grp_fields[0]) - 1)

#endif
