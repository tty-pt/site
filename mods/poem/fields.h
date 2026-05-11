/* Unified field definition for poem module.
 * One table serves both server (source generators) and WASM (bud_state_apply).
 *
 * Kinds:
 *   0 = SF_RECORD     — record field, include as-is in state JSON
 *   1 = SF_EXCLUDE    — record field, exclude from state JSON
 *   2 = SF_REF_DISPLAY — record field, resolve multi-ref IDs to display names
 *   3 = POEM_OVERLAY_INT  — computed int, overlay onto state JSON
 *   4 = POEM_OVERLAY_STR  — computed string, overlay onto state JSON
 */

#ifndef POEM_FIELDS_H
#define POEM_FIELDS_H

#include "bud/bud.h"
#include <stddef.h>
#include "../common/field_macros.h"

typedef struct {
	char id[128];
	char title[256];
	char owner[32];
} poem_cache_t;

/* ── Poem record fields ──────────────────────────────────────── */

static const bud_field_desc_t poem_fields[] = {
	REC_FIELD(id, poem_cache_t, id, 128, 1, 0, 0, 0),
	REC_FIELD(title, poem_cache_t, title, 256, 1, 1, 1, 1),
	EXCL_FIELD(owner, poem_cache_t, owner, 32, BUD_QM_STR, 1),
	EXCL_FIELD_VF(body_content, BUD_QM_VSTR, 1, 0, "pt_PT.html"),
	FIELD_END
};

#define POEM_FIELD_COUNT (sizeof(poem_fields) / sizeof(poem_fields[0]) - 1)

#endif
