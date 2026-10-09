#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

/*
 * mods/common/common-types.h — Shared types, structs, and macros
 * for mods/common. Contains zero XY_DECLs.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ttypt/xy.h>
#include <ttypt/axil.h>
#include <json-c/json.h>
#include "bud/bud.h"
#include <hyle/schema.h>
#include "hyle-bud/hyle-bud.h"

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

#include "viewer_zoom.h"

typedef hyle_bud_picker_view_t pick_view_t;

typedef struct site_entity_def_s {
	const char *name;
	const char *display_name;
	const hyle_schema_desc_t *schema;
	size_t field_count;
	size_t record_size;
	const char *items_path;
	const char *file_attachment;
	int (*detail_auth)(
	        int fd, char *body, const item_ctx_t *ctx, void *user);
	bud_node *(*form_render)(
	        int is_edit, const char *id, const void *meta,
	        const char *file_val, const char *csrf_token,
	        const pick_view_t *pv);
} site_entity_def_t;

/* Shared detail page state */
typedef struct {
	char module[64];
	char id[64];
	char username[64];
	char path[256];
	char title[256];
	char lang[32];
	int is_owner;
	const char *csrf_token;
	const char *wasm_module;
	char *state_json;
} detail_state_t;

typedef struct {
	const char *module;
	const char *id;
	const char *username;
	const char *item_path;
	int fd;
	unsigned flags;
	const char *wasm_module;
} detail_state_build_spec_t;

#define DETAIL_BUILD_OWNERSHIP 1
#define DETAIL_BUILD_CSRF      2
#define DETAIL_BUILD_LOCALE    4

#endif /* COMMON_TYPES_H */
