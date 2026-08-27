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
int build_owner_path(const char *ip, char *out, size_t len);
int is_safe_id(const char *id);
int site_ui_respond_page(
        int fd, const char *title, const char *path, const char *icon,
        const char *user, const char *extra_head, const char *module,
        bud_node *body);

#endif
