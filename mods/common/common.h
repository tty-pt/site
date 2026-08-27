#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ttypt/xy.h>
#include <ttypt/axil.h>
#include <json-c/json.h>
#include "bud/bud.h"

struct item_ctx_s;
typedef struct item_ctx_s item_ctx_t;

typedef struct bud_node bud_node;

typedef struct {
	axil_handler_t *detail;
	axil_handler_t *add_get;
	axil_handler_t *add_post;
	axil_handler_t *edit_get;
	axil_handler_t *edit_post;
} standard_item_handlers_t;

typedef struct {
	const char *name;
	char *buf;
	size_t sz;
} meta_field_t;

/* Convenience macros: declare a local `fields` array then call these. */
#define META_READ(item_path, fields)                                           \
	meta_fields_read(                                                      \
	        (item_path), (fields), sizeof(fields) / sizeof(fields[0]))
#define META_WRITE(item_path, fields)                                          \
	meta_fields_write(                                                     \
	        (item_path), (fields), sizeof(fields) / sizeof(fields[0]))

typedef int (*str_list_cb)(const char *token, void *user);

/* ---------------------------------------------------------------------------
 * XY declarations.
 *
 * common.c (the implementer) must NOT include this header, since XY_DEF +
 * XY_DECL on the same symbol clash. Other modules include this freely.
 * ------------------------------------------------------------------------- */
#ifndef COMMON_IMPL

XY_DECL(int, str_trim, char *, s);
XY_DECL(int, register_standard_item_handlers,
	const char *, module_name,
	const standard_item_handlers_t *, handlers);
XY_DECL(int, str_list_contains, const char *, list, const char *, token);
XY_DECL(int, str_list_append, char *, out, size_t, out_sz, const char *, token);
XY_DECL(int, str_list_normalize, const char *, input, char *, out, size_t, out_sz);
XY_DECL(int, str_list_for_each, const char *, list, str_list_cb, cb, void *, user);
XY_DECL(int, respond_html, int, fd, const char *, html);
XY_DECL(const char *, require_user, int, fd);
XY_DECL(int, respond_json, int, fd, int, status, const char *, msg);
XY_DECL(int, respond_error, int, fd, int, status, const char *, msg);
XY_DECL(int, bad_request, int, fd, const char *, msg);  /* 400; NULL -> "Bad
request" */
XY_DECL(int, server_error, int, fd, const char *, msg); /* 500; NULL -> "Internal
server error" */
XY_DECL(int, not_found, int, fd, const char *, msg);    /* 404; NULL -> "Not found"
                                                  */
XY_DECL(int, redirect_to_item,
	int, fd,
	const char *, module,
	const char *, id);

XY_DECL(int, build_owner_path,
	const char *, ip,
	char *, out,
	size_t, len);

XY_DECL(int, read_meta_file,
	const char *, item_path,
	const char *, name,
	char *, buf,
	size_t, sz);
XY_DECL(int, write_meta_file,
	const char *, item_path,
	const char *, name,
	const char *, buf,
	size_t, sz);
XY_DECL(int, meta_fields_read,
	const char *, item_path,
	meta_field_t *, fields,
	size_t, count);
XY_DECL(int, meta_fields_write,
	const char *, item_path,
	const meta_field_t *, fields,
	size_t, count);
XY_DECL(int, write_item_child_file,
	const char *, item_path,
	const char *, name,
	const char *, buf,
	size_t, sz);
XY_DECL(int, write_file_path,
	const char *, path,
	const char *, buf,
	size_t, sz);
XY_DECL(char *, slurp_file, const char *, path);
XY_DECL(int, get_doc_root, int, fd, char *, buf, size_t, len);
XY_DECL(const char *, resolve_doc_root, int, fd, char *, buf, size_t, len);
XY_DECL(int, ensure_dir_path, const char *, path);
XY_DECL(int, user_path_build,
	const char *, username,
	const char *, suffix,
	char *, out,
	size_t, outlen);
XY_DECL(int, item_child_path,
	const char *, item_path,
	const char *, name,
	char *, out,
	size_t, outlen);
XY_DECL(int, user_pref_read,
	const char *, username,
	const char *, name,
	char *, out,
	size_t, out_sz);
XY_DECL(int, user_pref_write,
	const char *, username,
	const char *, name,
	const char *, val);

XY_DECL(int, item_remove_path_recursive, const char *, item_path);

XY_DECL(int, is_safe_id, const char *, id);

/* Phase A helpers */
XY_DECL(int, module_path_build,
	const char *, doc_root,
	const char *, module,
	char *, out,
	size_t, outlen);
XY_DECL(int, module_items_path_build,
	const char *, doc_root,
	const char *, module,
	char *, out,
	size_t, outlen);
XY_DECL(int, item_path_build_root,
	const char *, doc_root,
	const char *, module,
	const char *, id,
	char *, out,
	size_t, outlen);
XY_DECL(int, item_path_build,
	int, fd,
	const char *, module,
	const char *, id,
	char *, out,
	size_t, outlen);

XY_DECL(int, datalist_extract_id,
	const char *, in,
	char *, id_out,
	size_t, outlen);

XY_DECL(int, respond_item_file,
	int, fd,
	const char *, item_path,
	const char *, filename,
	const char *, allowed_exts);

XY_DECL(int, site_ui_respond_item_detail,
	int, fd,
	const item_ctx_t *, ctx,
	const char *, module,
	const char *, title,
	bud_node *, body);

XY_DECL(int, site_ui_respond_page,
	int, fd,
	const char *, title,
	const char *, path,
	const char *, icon,
	const char *, user,
	const char *, extra_head,
	const char *, module,
	bud_node *, body);
XY_DECL(int, site_ui_respond_form_page,
	int, fd,
	const char *, user,
	const char *, title,
	const char *, action,
	const char *, icon,
	const char *, module,
	bud_node *, form);

XY_DECL(int, csrf_check_mpfd, int, fd);
XY_DECL(int, csrf_check_query, int, fd, char *, body);
XY_DECL(const char *, csrf_setup, int, fd);

XY_DECL(int, site_ui_respond_add_page,
	int, fd,
	const char *, user,
	const char *, module,
	const char *, icon,
	bud_node *, form);

XY_DECL(int, site_ui_respond_edit_page,
	int, fd,
	const char *, user,
	const char *, module,
	const char *, icon,
	const char *, title,
	const char *, id,
	bud_node *, form);

XY_DECL(int, parse_transpose_qs,
	const char *, qs,
	int *, transpose,
	int *, flags,
	int *, show_media);

XY_DECL(int, bud_adapter_overlay_from_desc,
	json_object *, jo,
	const void *, state,
	const bud_field_desc_t *, fields,
	int, int_kind,
	int, str_kind);
XY_DECL(json_object *, bud_adapter_overlay_array,
	const void *, items,
	int, count,
	size_t, elem_size,
	const bud_field_desc_t *, fields,
	int, int_kind,
	int, str_kind);

#endif /* COMMON_IMPL — end of XY_DECL section */

/* Transpose viewer query-string parser — outside COMMON_IMPL guard so the
 * implementation (common) can see it too. Callers also see it by inclusion. */
#define TPARAM_BEMOL 1
#define TPARAM_LATIN 2
#define TPARAM_HTML 4

#include "viewer_zoom.h"

#endif /* COMMON_H */
