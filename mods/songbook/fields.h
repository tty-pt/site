/* Unified field definition for songbook module.
 * One table serves both server (source generators) and WASM (bud_state_apply).
 *
 * Kinds:
 *   0 = SB_RECORD  — record field, include in state JSON
 *   1 = SB_EXCLUDE — record field, exclude from state JSON
 *   3 = SB_OVERLAY_INT   — computed int overlay
 *   4 = SB_OVERLAY_STR   — computed string overlay
 */

#ifndef SONGBOOK_FIELDS_H
#define SONGBOOK_FIELDS_H

#include "bud/bud.h"
#include <stddef.h>
#include "../common/field_macros.h"

/* ── Songbook item ──────────────────────────────────────── */

typedef struct {
	char id[64];
	char title[256];
	char choir[128];
	char song_source[16];
	char owner[32];
} songbook_cache_t;

static const bud_field_desc_t songbook_fields[] = {
	REC_FIELD(id, songbook_cache_t, id, 64, 1, 0, 0, 0),
	REC_FIELD(title, songbook_cache_t, title, 256, 1, 0, 0, 1),
	REF_FIELD(
	        choir,
	        songbook_cache_t,
	        choir,
	        128,
	        "choir.items",
	        "songbooks",
	        1),
	EXCL_FIELD(song_source, songbook_cache_t, song_source, 16, BUD_QM_STR, 0),
	EXCL_FIELD(owner, songbook_cache_t, owner, 32, BUD_QM_STR, 0),
	FIELD_END
};

#define SB_FIELD_COUNT                                                         \
	(sizeof(songbook_fields) / sizeof(songbook_fields[0]) - 1)

#endif
