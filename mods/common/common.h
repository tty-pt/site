#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ttypt/ndx-mod.h>

/* ---------------------------------------------------------------------------
 * Dynamic JSON array / form-body builder types.
 * Exposed as concrete structs so other modules may inspect fields if needed;
 * lifecycle is managed via the NDX ops below.
 * ------------------------------------------------------------------------- */

typedef struct {
	char *buf;  /* heap-allocated; NUL-terminated */
	size_t len; /* bytes used, not counting trailing NUL */
	size_t cap; /* total bytes allocated */
	int first;  /* 1 until the first element/object is appended */
} json_array_t;

typedef struct {
	char *buf;
	size_t len;
	size_t cap;
	int first;
} json_object_t;

typedef struct {
	char *buf;
	size_t len;
	size_t cap;
	int first;
} form_body_t;

typedef struct {
	const char *name;
	char *buf;
	size_t sz;
} meta_field_t;

/* Re-use item_ctx_t and flags from auth.h */
#include "../auth/auth.h"

typedef int (*form_body_builder_cb)(int fd, form_body_t *fb, void *user);

/* ---------------------------------------------------------------------------
 * NDX declarations.
 *
 * common.c (the implementer) must NOT include this header, since NDX_DEF +
 * NDX_HOOK_DECL on the same symbol clash. Other modules include this freely.
 * ------------------------------------------------------------------------- */
#ifndef COMMON_IMPL

NDX_HOOK_DECL(int, json_escape, const char *, in, char *, out, size_t, outlen);
NDX_HOOK_DECL(int, url_encode, const char *, in, char *, out, size_t, outlen);
NDX_HOOK_DECL(int, b64_encode, const char *, in, char *, out, size_t, outlen);
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
NDX_HOOK_DECL(char *, slurp_item_child_file,
	const char *, item_path,
	const char *, name);
NDX_HOOK_DECL(int, get_doc_root, int, fd, char *, buf, size_t, len);
NDX_HOOK_DECL(int, ensure_dir_path, const char *, path);
NDX_HOOK_DECL(int, user_path_build,
	const char *, username,
	const char *, suffix,
	char *, out,
	size_t, outlen);
NDX_HOOK_DECL(int, item_dir_exists, const char *, item_path);
NDX_HOOK_DECL(int, item_child_path,
	const char *, item_path,
	const char *, name,
	char *, out,
	size_t, outlen);
NDX_HOOK_DECL(int, index_field_clean, char *, s);
NDX_HOOK_DECL(int, item_remove_path_recursive, const char *, item_path);
NDX_HOOK_DECL(int, core_post_form, int, fd, form_body_t *, fb);
NDX_HOOK_DECL(int, core_post_form_builder,
	int, fd,
	form_body_builder_cb, cb,
	void *, user);

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

/* --- JSON array lifecycle --- */
NDX_HOOK_DECL(json_array_t *, json_array_new, int, dummy);
NDX_HOOK_DECL(int, json_array_append_raw, json_array_t *, ja, const char *, s);
NDX_HOOK_DECL(int, json_array_begin_object, json_array_t *, ja);
NDX_HOOK_DECL(int, json_array_end_object, json_array_t *, ja);
NDX_HOOK_DECL(int, json_array_kv_str,
	json_array_t *, ja,
	const char *, key,
	const char *, value);
NDX_HOOK_DECL(int, json_array_kv_int,
	json_array_t *, ja,
	const char *, key,
	int, value);
NDX_HOOK_DECL(int, json_array_kv_bool,
	json_array_t *, ja,
	const char *, key,
	int, value);
NDX_HOOK_DECL(char *, json_array_finish, json_array_t *, ja);

/* --- JSON object lifecycle --- */
NDX_HOOK_DECL(json_object_t *, json_object_new, int, dummy);
NDX_HOOK_DECL(int, json_object_kv_str,
	json_object_t *, jo,
	const char *, key,
	const char *, value);
NDX_HOOK_DECL(int, json_object_kv_int,
	json_object_t *, jo,
	const char *, key,
	int, value);
NDX_HOOK_DECL(int, json_object_kv_bool,
	json_object_t *, jo,
	const char *, key,
	int, value);
NDX_HOOK_DECL(int, json_object_kv_raw,
	json_object_t *, jo,
	const char *, key,
	const char *, value);
NDX_HOOK_DECL(char *, json_object_finish, json_object_t *, jo);
NDX_HOOK_DECL(int, json_object_free, json_object_t *, jo);

/* --- Form body lifecycle --- */
NDX_HOOK_DECL(form_body_t *, form_body_new, int, dummy);
NDX_HOOK_DECL(int, form_body_add,
	form_body_t *, fb,
	const char *, name,
	const char *, value);
NDX_HOOK_DECL(int, form_body_free, form_body_t *, fb);

#endif /* COMMON_IMPL */

#endif
