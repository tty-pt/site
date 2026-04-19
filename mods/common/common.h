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
	char  *buf;     /* heap-allocated; NUL-terminated */
	size_t len;     /* bytes used, not counting trailing NUL */
	size_t cap;     /* total bytes allocated */
	int    first;   /* 1 until the first element/object is appended */
} json_array_t;

typedef struct {
	char  *buf;
	size_t len;
	size_t cap;
	int    first;
} form_body_t;

/* ---------------------------------------------------------------------------
 * NDX declarations.
 *
 * common.c (the implementer) must NOT include this header, since NDX_DEF +
 * NDX_DECL on the same symbol clash. Other modules include this freely.
 * ------------------------------------------------------------------------- */
#ifndef COMMON_IMPL

NDX_DECL(int, json_escape, const char *, in, char *, out, size_t, outlen);
NDX_DECL(int, url_encode, const char *, in, char *, out, size_t, outlen);
NDX_DECL(int, b64_encode, const char *, in, char *, out, size_t, outlen);
NDX_DECL(int, respond_plain, int, fd, int, status, const char *, msg);
NDX_DECL(int, respond_json, int, fd, int, status, const char *, msg);
NDX_DECL(int, respond_error, int, fd, int, status, const char *, msg);
NDX_DECL(int, redirect, int, fd, const char *, location);
NDX_DECL(int, read_meta_file, const char *, item_path, const char *, name, char *, buf, size_t, sz);
NDX_DECL(int, write_meta_file, const char *, item_path, const char *, name, const char *, buf, size_t, sz);
NDX_DECL(char *, slurp_file, const char *, path);
NDX_DECL(int, get_doc_root, int, fd, char *, buf, size_t, len);

/* Phase A helpers */
NDX_DECL(int, item_path_build,
	int, fd, const char *, module, const char *, id,
	char *, out, size_t, outlen);

NDX_DECL(int, datalist_extract_id,
	const char *, in, char *, id_out, size_t, outlen);

NDX_DECL(int, proxy_add_standard_headers,
	int, fd, const char *, modules_header);

/* --- JSON array lifecycle --- */
NDX_DECL(json_array_t *, json_array_new, int, dummy);
NDX_DECL(int, json_array_append_raw,
	json_array_t *, ja, const char *, s);
NDX_DECL(int, json_array_begin_object, json_array_t *, ja);
NDX_DECL(int, json_array_end_object, json_array_t *, ja);
NDX_DECL(int, json_array_kv_str,
	json_array_t *, ja, const char *, key, const char *, value);
NDX_DECL(int, json_array_kv_int,
	json_array_t *, ja, const char *, key, int, value);
NDX_DECL(int, json_array_kv_bool,
	json_array_t *, ja, const char *, key, int, value);
NDX_DECL(char *, json_array_finish, json_array_t *, ja);
NDX_DECL(int, json_array_free, json_array_t *, ja);

/* --- Form body lifecycle --- */
NDX_DECL(form_body_t *, form_body_new, int, dummy);
NDX_DECL(int, form_body_add,
	form_body_t *, fb, const char *, name, const char *, value);
NDX_DECL(char *, form_body_finish,
	form_body_t *, fb, size_t *, out_len);
NDX_DECL(int, form_body_free, form_body_t *, fb);

#endif /* COMMON_IMPL */

#endif
