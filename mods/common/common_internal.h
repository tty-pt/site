#ifndef COMMON_INTERNAL_H
#define COMMON_INTERNAL_H

#include <stddef.h>

#define COMMON_IMPL
#include "common.h"
#undef COMMON_IMPL

int respond_error(int fd, int status, const char *msg);
int bad_request(int fd, const char *msg);

int write_file_path(const char *path, const char *buf, size_t sz);
int write_item_child_file(
        const char *item_path, const char *name, const char *buf, size_t sz);
int item_child_path(
        const char *item_path, const char *name, char *out, size_t outlen);
int str_list_contains(const char *list, const char *token);
int str_list_append(char *out, size_t out_sz, const char *token);
int str_list_normalize(const char *input, char *out, size_t out_sz);
int str_list_for_each(const char *list, str_list_cb cb, void *user);
char *slurp_file(const char *path);
int server_error(int fd, const char *msg);
int respond_json(int fd, int status, const char *msg);
int get_doc_root(int fd, char *buf, size_t len);
int item_remove_path_recursive(const char *item_path);

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
#endif
