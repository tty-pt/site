/* Unified field definition for software module.
 * Serves both server (source generators) and client list views.
 */

#ifndef SOFTWARE_FIELDS_H
#define SOFTWARE_FIELDS_H

#include <hyle/schema.h>
#include <stddef.h>

typedef struct {
	char id[128];
	char title[256];
	char repo_url[512];
	char summary[512];
	char version[64];
	char license[64];
	char category[64];
	char owner[32];
} software_cache_t;

/* ── Software record fields ────────────────────────────────────── */

static const hyle_schema_desc_t software_fields[] = {
	FIELD_TEXT(id, software_cache_t),
	FIELD_TEXT(title, software_cache_t, .required = 1, .min_length = 1, .in_meta = 1),
	FIELD_TEXT(repo_url, software_cache_t, .in_meta = 1),
	FIELD_TEXT(summary, software_cache_t, .in_meta = 1),
	FIELD_TEXT(version, software_cache_t, .in_meta = 1),
	FIELD_TEXT(license, software_cache_t, .in_meta = 1),
	FIELD_TEXT(category, software_cache_t, .in_meta = 1),
	FIELD_EXCL(owner, software_cache_t, .in_meta = 1),
	FIELD_FILE(body_content, "README.md"),
	FIELD_END
};

#define SOFTWARE_FIELD_COUNT (sizeof(software_fields) / sizeof(software_fields[0]) - 1)

#ifndef __wasm__
static const source_list_field_t software_list_fields[] = {
	{ "title", "Software" },
	{ "category", "Category" },
	{ "version", "Version" },
	{ "summary", "Summary" },
	{ "owner", "Maintainer" },
};

static const source_list_view_t software_list_view = {
	"software",
	software_list_fields,
	sizeof(software_list_fields) / sizeof(software_list_fields[0]),
	NULL,
	NULL,
	NULL,
	NULL,
};
#endif

#endif /* SOFTWARE_FIELDS_H */
