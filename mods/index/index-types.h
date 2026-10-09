#ifndef INDEX_TYPES_H
#define INDEX_TYPES_H

/*
 * mods/index/index-types.h — Shared types, structs, and callbacks
 * for mods/index. Contains zero XY_DECLs.
 */

#include <stddef.h>
#include <stdint.h>
#include <ttypt/xy-mod.h>
#include <hyle-bud/hyle-bud.h>
#include <hyle-source/hyle_source.h>
#include "../common/common.h"
#include "../source/source.h"
#include "ux/list_state.h"
#include "ux/site_ui.h"
#include <hyle/schema.h>

/*
 * Declarative module initialization definition.
 * Pass to index_module_init() to register dataset, schema, list-view,
 * and standard CRUD handlers in a single call.
 */
typedef struct {
	const char *name;                    /* module slug (e.g. "item") */
	const char *display_name;            /* display label (e.g. "Item") */
	const source_desc_t *schema;         /* hyle_schema_desc_t array */
	int field_count;                     /* count of fields in schema */
	size_t record_size;                  /* sizeof module cache struct */
	const char *key_field;               /* primary key field (usually "id") */
	const char *items_path;              /* filesystem storage dir (e.g. "var/item") */
	unsigned flags;                      /* SOURCE_FLAG_* */
	const source_list_view_t *list_view; /* optional list view columns & search presentation */
	standard_item_handlers_t handlers;   /* custom handler overrides (NULL = generic default) */
	const char *media_exts;              /* allowlisted media file extensions, e.g. "jpeg,jpg,png" */
	const char *body_file;               /* allowlisted public body file, e.g. "pt_PT.html" */
} index_module_def_t;

typedef void (*index_cleanup_fn)(const char *id);

/* Optional serializer for index_render_list.
 * Writes one line for (id, val) into out (up to out_sz bytes).
 * Returns the number of bytes written (like snprintf, without NUL).
 * NULL → default "id val\r\n" format. */
typedef size_t (*index_format_fn)(
        const char *id, const char *val, char *out, size_t out_sz);

typedef int (*index_handler_fn)(int fd, char *body);
typedef int (*index_detail_handler_fn)(int fd, char *body);

#endif /* INDEX_TYPES_H */
