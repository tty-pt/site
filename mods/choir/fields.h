/* Unified field definition for choir module.
 * One table serves both server (source generators).
 *
 * Kinds:
 *   0 = CH_RECORD   — record field, include in state JSON
 *   1 = CH_EXCLUDE  — record field, exclude from state JSON
 */

#ifndef CHOIR_FIELDS_H
#define CHOIR_FIELDS_H

#include "bud/bud.h"
#include <stddef.h>
#include "../common/field_macros.h"

/* ── Choir item ──────────────────────────────────────────── */

typedef struct {
	char id[64];
	char title[256];
	char format[2048];
	char owner[32];
} choir_cache_t;

static const bud_field_desc_t choir_fields[] = {
	REC_FIELD(id, choir_cache_t, id, 64, 1, 0, 0, 0),
	REC_FIELD(title, choir_cache_t, title, 256, 1, 0, 0, 1),
	REC_FIELD(format, choir_cache_t, format, 2048, 1, 0, 0, 1),
	EXCL_FIELD(owner, choir_cache_t, owner, 32, BUD_QM_STR, 1),
	INVERSE_FIELD(songbooks, "songbook.items", "choir"),
	INVERSE_FIELD(repertoire, "choir.songs", "choir"),
	FIELD_END
};

#define CHOIR_FIELD_COUNT (sizeof(choir_fields) / sizeof(choir_fields[0]) - 1)

#endif
