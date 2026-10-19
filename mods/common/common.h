#ifndef COMMON_H
#define COMMON_H

/*
 * mods/common — Core shared site utilities, HTML/JSON responses,
 * and entity registration.
 *
 * Caller-facing XY declarations.
 * Types and definitions live in common-types.h.
 * Implementers must include common-types.h (or common_internal.h), not this header.
 */

#include "common-types.h"

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
XY_DECL(int, bad_request, int, fd, const char *, msg);  /* 400; NULL -> "Bad request" */
XY_DECL(int, server_error, int, fd, const char *, msg); /* 500; NULL -> "Internal server error" */
XY_DECL(int, not_found, int, fd, const char *, msg);    /* 404; NULL -> "Not found" */
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

XY_DECL(int, site_ui_respond_isomorphic,
	int, fd,
	const item_ctx_t *, ctx,
	const char *, module,
	const char *, title,
	const char *, state_json,
	const char *, wasm_module,
	bud_node *, body);

XY_DECL(int, site_entity_register, const site_entity_def_t *, def);

XY_DECL(int, detail_state_build,
	detail_state_t *, state,
	const detail_state_build_spec_t *, spec,
	const char *, title,
	const char *, path);

XY_DECL(int, detail_respond_page,
	int, fd,
	const detail_state_t *, state,
	bud_node *, layout);

#endif /* COMMON_H */
