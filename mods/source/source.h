#ifndef SOURCE_H
#define SOURCE_H

#include <stddef.h>
#include <stdint.h>
#include <ttypt/ndx.h>

typedef enum {
	SOURCE_ACCESS_PUBLIC = 0,
	DATASET_ACCESS_LOGIN,
	DATASET_ACCESS_CALLBACK,
} source_access_policy_t;

typedef enum {
	DATASET_ACCESS_RESULT_ALLOW = 0,
	DATASET_ACCESS_RESULT_UNAUTHORIZED,
	DATASET_ACCESS_RESULT_FORBIDDEN,
} source_access_result_t;

typedef enum {
	SOURCE_FIELD_STRING = 0,
	DATASET_FIELD_INT,
	DATASET_FIELD_BOOL,
	DATASET_FIELD_NULLABLE_STRING,
	SOURCE_FIELD_REFERENCE,
	SOURCE_FIELD_MULTI_REFERENCE,
	SOURCE_FIELD_INVERSE,
} source_field_type_t;

typedef struct {
	const char *name;
	const char *file;
	source_field_type_t type;
	int writable;
	const char *target_source;
	const char *inverse_name;
	int required;
	int64_t min;
	int64_t max;
	size_t min_length;
	size_t max_length;
	const char *pattern;
} source_field_t;

typedef struct {
	const char *id;
	const char *key_field;
	const char *items_path;
	source_access_policy_t access_policy;
	const source_field_t *fields;
	size_t field_count;
	unsigned source_hd;
	unsigned fields_hd;
	uint32_t record_id;
	unsigned flags;
	void *user;
} source_def_t;

typedef int (*source_each_cb_t)(const source_def_t *, void *);

#define SOURCE_ERR_VALIDATION -2

#ifndef SOURCE_IMPL
NDX_HOOK_DECL(source_def_t *, source_find, const char *, dataset_id);
NDX_HOOK_DECL(int, source_register, const source_def_t *, def);
NDX_HOOK_DECL(unsigned, source_row_qtype, uint32_t, record_id);
NDX_HOOK_DECL(int, source_refresh_row,
    int, fd, const char *, dataset_id, const char *, id);
NDX_HOOK_DECL(int, source_update_item,
    int, fd, const char *, dataset_id,
    const char *, id, unsigned, data_handle);
NDX_HOOK_DECL(unsigned, source_parse_form, const char *, dataset_id);
NDX_HOOK_DECL(int, source_delete_item,
    int, fd, const source_def_t *, def, const char *, item_id);
NDX_HOOK_DECL(int, ref_field_register,
    const char *, dataset_id, const char *, field_name);
NDX_HOOK_DECL(int, source_for_each, source_each_cb_t, cb, void *, user);

NDX_HOOK_DECL(unsigned, source_query,
    const char *, dataset_id,
    const char *, query_str);

NDX_HOOK_DECL(int, source_get_json,
    int, fd,
    const char *, dataset_id,
    const char *, include,
    char **, out_json);

NDX_HOOK_DECL(int, source_get_item_json,
    int, fd,
    const char *, dataset_id,
    const char *, id,
    char **, out_json);

NDX_HOOK_DECL(unsigned, source_get_data_hd, const char *, dataset_id);
NDX_HOOK_DECL(unsigned, source_get_fields_hd, const char *, dataset_id);
NDX_HOOK_DECL(unsigned, source_get_schema_hd, const char *, dataset_id);
#endif /* SOURCE_IMPL */
#endif
