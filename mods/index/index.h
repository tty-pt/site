#ifndef INDEX_MOD_H
#define INDEX_MOD_H

/*
 * mods/index — Generic CRUD routing, list views, and picker endpoints.
 *
 * Index wires dataset schemas into complete HTTP applications:
 * - Registers standard routes: GET /{mod}, GET /{mod}/add, POST /{mod}/add,
 *   GET /{mod}/:id, GET /{mod}/:id/edit, POST /{mod}/:id/edit, POST /{mod}/:id/delete.
 * - Serves picker option endpoints for omni-dropdowns (GET /_options/{dataset}).
 * - Serializes list views into framework-neutral list_state_t for SSR + WASM rendering.
 */

#include <ttypt/xy-mod.h>
#include <hyle-bud/hyle-bud.h>
#include <hyle-source/hyle_source.h>
#include "../common/common.h"
#include "../source/source.h"
#include "ux/list_state.h"
#include "ux/site_ui.h"

#include <hyle/schema.h>
typedef hyle_schema_desc_t source_desc_t;

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

#ifndef INDEX_IMPL
/* Open and register standard CRUD routes for a dataset */
XY_DECL(unsigned, index_open,
	const char *, name,
	const char *, dataset_name,
	index_cleanup_fn, cleanup,
	index_detail_handler_fn, detail_handler,
	index_handler_fn, add_handler,
	index_handler_fn, edit_get_handler,
	index_handler_fn, edit_post_handler,
	const char *, url_slug);

/* Generic handler for creating an item from POST data */
XY_DECL(int, index_add_item,
	int, fd,
	char *, body,
	char *, id_out,
	size_t, id_len);

/* Root GET handler */
XY_DECL(int, core_get, int, fd, char *, body);

/* Render plain text list of dataset items */
XY_DECL(int, index_render_list,
	int, fd,
	unsigned, hd,
	index_format_fn, fmt);

/* Validate access permissions and extract item id/path from request */
XY_DECL(int, check_item_access,
	int, fd,
	const char *, module,
	char *, id, size_t, id_sz,
	const char **, user,
	char *, item_path, size_t, path_sz);

/* Populate list_state_t from dataset query and URL query string */
XY_DECL(int, list_fill_state,
	list_state_t *, state,
	const char *, dataset_id,
	const char *, raw_qs,
	int, allow_fields);

/* Free resources allocated inside list_state_t */
XY_DECL(int, list_fill_free, list_state_t *, state);

/* One-line declarative module setup (schema, dataset, list view, handlers) */
XY_DECL(uint32_t, index_module_init, const index_module_def_t *, def);
#endif /* INDEX_IMPL */

#endif

