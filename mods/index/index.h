#ifndef INDEX_MOD_H
#define INDEX_MOD_H

#include <ttypt/xy-mod.h>
#include <hyle-bud/hyle-bud.h>
#include "ux/list_state.h"
#include "ux/site_ui.h"

#include <hyle/schema.h>
typedef hyle_schema_desc_t source_desc_t;

typedef void (*index_cleanup_fn)(const char *id);

/* Optional serializer for index_render_list.
 * Writes one line for (id, val) into out (up to out_sz bytes).
 * Returns the number of bytes written (like snprintf, without NUL).
 * NULL → default "id val\r\n" format. */
typedef size_t (*index_format_fn)(
        const char *id, const char *val, char *out, size_t out_sz);

typedef int (*index_handler_fn)(int fd, char *body);
typedef int (*index_detail_handler_fn)(int fd, char *body);

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

/* ── Omnisearch pickers (OMNI-DROPDOWN) ──────────────────────────── */

typedef struct {
	char q[256];
	int page;      /* 0-based window index */
	int per_page;
} pick_ctx_t;

/* Read pick_q_<key> / pick_page_<key> from the request query string.
 * Returns 0 always (int, because XY_DECL cannot declare void). */
XY_DECL(int, pick_ctx_load,
	char *, qs,
	const char *, key,
	pick_ctx_t *, out);

/* Paged id/label options via source_query; label = first non-id display
 * field (same convention as idx_resolve_filter_options). Sets *total.
 * Option strings are thread-local — valid until the next call. */
XY_DECL(int, pick_options_fill,
	const char *, dataset_id,
	pick_ctx_t *, ctx,
	hyle_bud_option_t *, opts, int, max,
	int *, total);

/* Resolve stored slugs -> {id,label} pairs for pinned rows. Option
 * strings are thread-local — valid until the next call. */
XY_DECL(int, pick_selected_fill,
	const char *, dataset_id,
	const char *, comma_slugs,
	hyle_bud_option_t *, out, int, max);

/* Per-form collector (native only): overlay draft mirrors from the
 * query string onto vals_out (aliases vals_in unless overlaid) and
 * fill pv entries for every ref field in the descriptor — options,
 * pinned selections, ctx. WASM callers pass pv == NULL. */
XY_DECL(int, pick_view_collect,
	char *, body,
	const form_field_t *, fields,
	const char **, vals_in,
	const char **, vals_out,
	pick_view_t *, pv);

XY_DECL(int, pick_view_collect_fd,
	int, fd,
	const form_field_t *, fields,
	const char **, vals_in,
	const char **, vals_out,
	pick_view_t *, pv);

/* Scoped variant: namespaces this collector's pick_q_/pick_page_
 * params as `<key>__<scope>` so several picker instances for the same
 * field can coexist on one page (docs/current/multi-omni-dropdown.md). */
XY_DECL(int, pick_view_collect_scoped,
	char *, body,
	const form_field_t *, fields,
	const char **, vals_in,
	const char **, vals_out,
	pick_view_t *, pv,
	const char *, scope);

/* Auto-resolves active scope from query string (e.g. ?replace=N or
 * pick_q_<key>__N=...) and collects options for that scope. */
XY_DECL(int, pick_view_collect_auto,
	char *, body,
	const form_field_t *, fields,
	const char **, vals_in,
	const char **, vals_out,
	pick_view_t *, pv,
	int *, active_scope_out);

XY_DECL(int, pick_view_collect_auto_fd,
	int, fd,
	const form_field_t *, fields,
	const char **, vals_in,
	const char **, vals_out,
	pick_view_t *, pv,
	int *, active_scope_out);

XY_DECL(int, pick_view_collect_desc,
	const char *, qs,
	const source_desc_t *, defs,
	pick_view_t *, pv,
	int *, active_scope_out);

XY_DECL(int, pick_view_collect_desc_fd,
	int, fd,
	const source_desc_t *, defs,
	pick_view_t *, pv,
	int *, active_scope_out);


#endif
