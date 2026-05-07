#ifndef COMMON_INTERNAL_H
#define COMMON_INTERNAL_H

#include <stddef.h>

#define COMMON_IMPL
#include "common.h"
#undef COMMON_IMPL

int json_escape(const char *in, char *out, size_t outlen);
int url_encode(const char *in, char *out, size_t outlen);

int respond_error(int fd, int status, const char *msg);

int write_file_path(const char *path, const char *buf, size_t sz);
int item_child_path(
        const char *item_path, const char *name, char *out, size_t outlen);
int index_field_clean(char *s);
int server_error(int fd, const char *msg);
int respond_json(int fd, int status, const char *msg);

form_body_t *form_body_new(int dummy);
int form_body_free(form_body_t *fb);
char *form_body_finish(form_body_t *fb, size_t *out_len);

json_array_t *json_array_new(int dummy);
int json_array_append_raw(json_array_t *ja, const char *s);
int json_array_begin_object(json_array_t *ja);
int json_array_end_object(json_array_t *ja);
int json_array_kv_str(json_array_t *ja, const char *key, const char *value);
int json_array_kv_int(json_array_t *ja, const char *key, int value);
int json_array_kv_bool(json_array_t *ja, const char *key, int value);
char *json_array_finish(json_array_t *ja);

json_object_t *json_object_new(int dummy);
int json_object_kv_str(json_object_t *jo, const char *key, const char *value);
int json_object_kv_int(json_object_t *jo, const char *key, int value);
int json_object_kv_bool(json_object_t *jo, const char *key, int value);
int json_object_kv_raw(json_object_t *jo, const char *key, const char *value);
char *json_object_finish(json_object_t *jo);
int json_object_free(json_object_t *jo);
int json_object_append_fragment(
        json_object_t *jo, const char *fragment, size_t len);

#endif
