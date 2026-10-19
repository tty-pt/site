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

#include <hyle/schema.h>
#include <stddef.h>

typedef struct {
	char id[128];
	char title[256];
	char owner[32];
} poem_cache_t;

/* ── Poem record fields ──────────────────────────────────────── */

static const hyle_schema_desc_t poem_fields[] = {
	FIELD_TEXT(id, poem_cache_t),
	FIELD_TEXT(title, poem_cache_t, .required = 1, .min_length = 1, .in_meta = 1),
	FIELD_EXCL(owner, poem_cache_t, .in_meta = 1),
	FIELD_FILE(body_content, "pt_PT.html"),
	FIELD_END
};

#define POEM_FIELD_COUNT (sizeof(poem_fields) / sizeof(poem_fields[0]) - 1)

#ifndef __wasm__
static const source_list_field_t poem_list_fields[] = {
	{ "title", "Title" },
	{ "owner", "Owner" },
};

static const source_list_view_t poem_list_view = {
	"poem",
	poem_list_fields,
	sizeof(poem_list_fields) / sizeof(poem_list_fields[0]),
	NULL,
	NULL,
	NULL,
	NULL,
};
#endif

#endif
