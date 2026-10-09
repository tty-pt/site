#ifndef INDEX_MOD_H
#define INDEX_MOD_H

/*
 * mods/index — Generic CRUD routing, list views, and picker endpoints.
 *
 * Caller-facing XY hook declarations.
 * Types and definitions live in index-types.h.
 * Implementers must include index-types.h, not this header.
 */

#include "index-types.h"

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

XY_DECL(int, list_respond_page,
	int, fd,
	list_state_t *, state,
	const char *, module,
	const char *, username);

#endif /* INDEX_MOD_H */
