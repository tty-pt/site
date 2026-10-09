#ifndef SOURCE_TYPES_H
#define SOURCE_TYPES_H

/*
 * mods/source/source-types.h — Shared types, structs, macros and callbacks
 * for mods/source. Contains zero XY_DECLs.
 */

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <json-c/json.h>
#include <ttypt/xy.h>
#include <hyle/schema.h>
#include <hyle-source/hyle_source.h>
#include <hyle-source/store.h>

typedef hyle_source_access_policy_t source_access_policy_t;
typedef hyle_source_access_result_t source_access_result_t;
#define SOURCE_ACCESS_PUBLIC HYLE_SOURCE_ACCESS_PUBLIC
#define DATASET_ACCESS_LOGIN HYLE_SOURCE_ACCESS_LOGIN
#define DATASET_ACCESS_RESULT_ALLOW HYLE_SOURCE_ACCESS_RESULT_ALLOW
#define DATASET_ACCESS_RESULT_UNAUTHORIZED                                     \
	HYLE_SOURCE_ACCESS_RESULT_UNAUTHORIZED
#define DATASET_ACCESS_RESULT_FORBIDDEN HYLE_SOURCE_ACCESS_RESULT_FORBIDDEN

typedef hyle_source_field_t source_field_t;
typedef hyle_source_list_field_t source_list_field_t;
typedef hyle_source_list_view_t source_list_view_t;
typedef hyle_source_store_ops_t source_store_ops_t;
typedef hyle_source_store_t source_store_t;
typedef hyle_source_def_t source_def_t;
typedef hyle_source_each_cb_t source_each_cb_t;

#define SOURCE_FLAG_VOLATILE HYLE_SOURCE_FLAG_VOLATILE
#define SOURCE_ERR_VALIDATION HYLE_SOURCE_ERR_VALIDATION

typedef hyle_source_state_kind_t source_state_kind_t;
#define SF_RECORD HYLE_SF_RECORD
#define SF_EXCLUDE HYLE_SF_EXCLUDE
#define SF_REF_DISPLAY HYLE_SF_REF_DISPLAY
typedef hyle_source_state_field_t source_state_field_t;
typedef hyle_source_state_kv_t source_state_kv_t;
typedef hyle_json_str_map_t json_str_map_t;
#define json_extract_strings hyle_json_extract_strings

typedef char source_opt_buf_t[128];

typedef struct {
	const char *form_field_prefix;
	const char *schema_field_name;
	const char *default_value;
	int is_primary_key;
} source_ordered_field_sync_t;

#include <hyle/field.h>

#define SOURCE_AUTO_RECORD 0x01

typedef int (*source_persist_load_fn)(
        const char *source_id, const char *partition_val, unsigned fields_hd,
        void *user);
typedef int (*source_persist_save_fn)(
        const char *source_id, const char *partition_val, unsigned fields_hd,
        void *user);
typedef const char *(*source_derive_fn_t)(
        const void *def, const char *row_id, const char *field_name,
        void *user);

typedef struct {
	const char *source_id;
	const hyle_field_t *fields;
	size_t field_count;
	const char *partition_field;
	uint32_t record_id;
	unsigned flags;
	source_persist_load_fn load_fn;
	source_persist_save_fn save_fn;
	void *persist_user;
} source_ordered_def_t;

typedef void (*source_ordered_each_fn)(
    int index, const char *key, unsigned fields_hd, void *user);

typedef void (*source_ref_cb_t)(const char *source_id, void *user);

struct item_ctx_s;
typedef struct item_ctx_s item_ctx_t;

#endif /* SOURCE_TYPES_H */
