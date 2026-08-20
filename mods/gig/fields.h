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

#include "bud/bud.h"
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

static const bud_field_desc_t gig_fields[] = {
	REC_FIELD(id, gig_cache_t, id, 64, 1, 0, 0, 0),
	REC_FIELD(title, gig_cache_t, title, 256, 1, 0, 0, 1),
	REF_FIELD_S(
	        grp, gig_cache_t, grp, 128, "grp.items", "gigs",
	        1, "dropdown"),
	EXCL_FIELD_W(
	        song_source, gig_cache_t, song_source, 16, BUD_QM_STR, 1),
	EXCL_FIELD(owner, gig_cache_t, owner, 32, BUD_QM_STR, 0),
	FIELD_END
};

#define SB_FIELD_COUNT                                                         \
	(sizeof(gig_fields) / sizeof(gig_fields[0]) - 1)

#endif
