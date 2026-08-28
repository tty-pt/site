#ifndef INDEX_MOD_H
#define INDEX_MOD_H

#include <ttypt/xy-mod.h>
#include <hyle-bud/hyle-bud.h>
#include <hyle-source/hyle_source.h>
#include "../common/common.h"
#include "../source/source.h"
#include "ux/list_state.h"
#include "ux/site_ui.h"

#include <hyle/schema.h>
typedef hyle_schema_desc_t source_desc_t;

typedef struct {
	const char *name;
	const char *display_name;
	const source_desc_t *schema;
	int field_count;
	size_t record_size;
	const char *key_field;
	const char *items_path;
	unsigned flags;
	const source_list_view_t *list_view;
	standard_item_handlers_t handlers;
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
XY_DECL(unsigned, index_open,
	const char *, name,
	const char *, dataset_name,
	index_cleanup_fn, cleanup,
	index_detail_handler_fn, detail_handler,
	index_handler_fn, add_handler,
	index_handler_fn, edit_get_handler,
	index_handler_fn, edit_post_handler,
	const char *, url_slug);

XY_DECL(int, index_add_item,
	int, fd,
	char *, body,
	char *, id_out,
	size_t, id_len);

XY_DECL(int, core_get, int, fd, char *, body);
XY_DECL(int, index_render_list,
	int, fd,
	unsigned, hd,
	index_format_fn, fmt);
XY_DECL(int, check_item_access,
	int, fd,
	const char *, module,
	char *, id, size_t, id_sz,
	const char **, user,
	char *, item_path, size_t, path_sz);
XY_DECL(int, list_fill_state,
	list_state_t *, state,
	const char *, dataset_id,
	const char *, raw_qs,
	int, allow_fields);
XY_DECL(int, list_fill_free, list_state_t *, state);

XY_DECL(uint32_t, index_module_init, const index_module_def_t *, def);
#endif /* INDEX_IMPL */

#endif
