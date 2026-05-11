#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ttypt/ndx.h>

typedef struct bud_node bud_node;

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
 * NDX declarations.
 *
 * common.c (the implementer) must NOT include this header, since NDX_DEF +
 * NDX_HOOK_DECL on the same symbol clash. Other modules include this freely.
 * ------------------------------------------------------------------------- */
#ifndef COMMON_IMPL

NDX_HOOK_DECL(int, str_trim, char *, s);
NDX_HOOK_DECL(int, str_list_contains, const char *, list, const char *, token);
NDX_HOOK_DECL(int, str_list_append, char *, out, size_t, out_sz, const char *, token);
NDX_HOOK_DECL(int, str_list_normalize, const char *, input, char *, out, size_t, out_sz);
NDX_HOOK_DECL(int, str_list_for_each, const char *, list, str_list_cb, cb, void *, user);
NDX_HOOK_DECL(int, respond_html, int, fd, const char *, html);
NDX_HOOK_DECL(const char *, require_user, int, fd);
NDX_HOOK_DECL(int, respond_json, int, fd, int, status, const char *, msg);
NDX_HOOK_DECL(int, respond_error, int, fd, int, status, const char *, msg);
NDX_HOOK_DECL(int, bad_request, int, fd, const char *, msg);  /* 400; NULL -> "Bad
request" */
NDX_HOOK_DECL(int, server_error, int, fd, const char *, msg); /* 500; NULL -> "Internal
server error" */
NDX_HOOK_DECL(int, not_found, int, fd, const char *, msg); /* 404; NULL -> "Not found"
                                                     */
NDX_HOOK_DECL(int, redirect_to_item,
	int, fd,
	const char *, module,
	const char *, id);

NDX_HOOK_DECL(int, read_meta_file,
	const char *, item_path,
	const char *, name,
	char *, buf,
	size_t, sz);
NDX_HOOK_DECL(int, write_meta_file,
	const char *, item_path,
	const char *, name,
	const char *, buf,
	size_t, sz);
NDX_HOOK_DECL(int, meta_fields_read,
	const char *, item_path,
	meta_field_t *, fields,
	size_t, count);
NDX_HOOK_DECL(int, meta_fields_write,
	const char *, item_path,
	const meta_field_t *, fields,
	size_t, count);
NDX_HOOK_DECL(int, write_item_child_file,
	const char *, item_path,
	const char *, name,
	const char *, buf,
	size_t, sz);
NDX_HOOK_DECL(int, write_file_path,
	const char *, path,
	const char *, buf,
	size_t, sz);
NDX_HOOK_DECL(char *, slurp_file, const char *, path);
NDX_HOOK_DECL(int, get_doc_root, int, fd, char *, buf, size_t, len);
NDX_HOOK_DECL(const char *, resolve_doc_root, int, fd, char *, buf, size_t, len);
NDX_HOOK_DECL(int, ensure_dir_path, const char *, path);
NDX_HOOK_DECL(int, user_path_build,
	const char *, username,
	const char *, suffix,
	char *, out,
	size_t, outlen);
NDX_HOOK_DECL(int, item_child_path,
	const char *, item_path,
	const char *, name,
	char *, out,
	size_t, outlen);
NDX_HOOK_DECL(int, item_remove_path_recursive, const char *, item_path);

/* Phase A helpers */
NDX_HOOK_DECL(int, module_path_build,
	const char *, doc_root,
	const char *, module,
	char *, out,
	size_t, outlen);
NDX_HOOK_DECL(int, module_items_path_build,
	const char *, doc_root,
	const char *, module,
	char *, out,
	size_t, outlen);
NDX_HOOK_DECL(int, item_path_build_root,
	const char *, doc_root,
	const char *, module,
	const char *, id,
	char *, out,
	size_t, outlen);
NDX_HOOK_DECL(int, item_path_build,
	int, fd,
	const char *, module,
	const char *, id,
	char *, out,
	size_t, outlen);

NDX_HOOK_DECL(int, datalist_extract_id,
	const char *, in,
	char *, id_out,
	size_t, outlen);

NDX_HOOK_DECL(int, site_ui_respond_page,
	int, fd,
	const char *, title,
	const char *, extra_head,
	const char *, module,
	bud_node *, body);
NDX_HOOK_DECL(int, site_ui_respond_form_page,
	int, fd,
	const char *, user,
	const char *, title,
	const char *, action,
	const char *, icon,
	const char *, module,
	bud_node *, form);

NDX_HOOK_DECL(int, csrf_check_mpfd, int, fd);
NDX_HOOK_DECL(int, csrf_check_query, int, fd, char *, body);
NDX_HOOK_DECL(const char *, csrf_setup, int, fd);

NDX_HOOK_DECL(int, site_ui_respond_add_page,
	int, fd,
	const char *, user,
	const char *, module,
	const char *, icon,
	bud_node *, form);

NDX_HOOK_DECL(int, site_ui_respond_edit_page,
	int, fd,
	const char *, user,
	const char *, module,
	const char *, icon,
	const char *, title,
	const char *, id,
	bud_node *, form);

#endif /* COMMON_H */

#endif
