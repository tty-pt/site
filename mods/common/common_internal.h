#ifndef COMMON_INTERNAL_H
#define COMMON_INTERNAL_H

#include <stddef.h>

#define COMMON_IMPL
#include "common.h"
#undef COMMON_IMPL

NDX_HOOK_DECL(int, core_post, int, fd, char *, body, size_t, len);

int json_escape(const char *in, char *out, size_t outlen);
int url_encode(const char *in, char *out, size_t outlen);

int respond_plain(int fd, int status, const char *msg);
int respond_error(int fd, int status, const char *msg);

int write_file_path(const char *path, const char *buf, size_t sz);
int item_child_path(const char *item_path, const char *name,
	char *out, size_t outlen);

form_body_t *form_body_new(int dummy);
int form_body_free(form_body_t *fb);
char *form_body_finish(form_body_t *fb, size_t *out_len);

int core_post_json(int fd, const char *json);

json_object_t *json_object_new(int dummy);
int json_object_kv_str(json_object_t *jo, const char *key,
	const char *value);
int json_object_kv_bool(json_object_t *jo, const char *key, int value);
char *json_object_finish(json_object_t *jo);
int json_object_append_fragment(json_object_t *jo, const char *fragment,
	size_t len);

#endif
