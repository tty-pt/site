#ifndef SOURCE_H
#define SOURCE_H

#include <stddef.h>
#include <stdint.h>
#include <json-c/json.h>
#include <ttypt/ndx.h>

typedef enum {
	SOURCE_ACCESS_PUBLIC = 0,
	DATASET_ACCESS_LOGIN,
} source_access_policy_t;

typedef enum {
	DATASET_ACCESS_RESULT_ALLOW = 0,
	DATASET_ACCESS_RESULT_UNAUTHORIZED,
	DATASET_ACCESS_RESULT_FORBIDDEN,
} source_access_result_t;

#define SOURCE_FIELD_KIND_INVERSE 5

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

#define SOURCE_FLAG_VOLATILE 64u
#define SOURCE_ERR_VALIDATION -2

/* ── State JSON builder ─────────────────────────────────────────── */

typedef enum {
	SF_RECORD,
	SF_EXCLUDE,
	SF_REF_DISPLAY,
} source_state_kind_t;

typedef struct {
	const char *name;
	source_state_kind_t kind;
} source_state_field_t;

typedef struct {
	const char *key;
	int is_int;
	int int_val;
	const char *str_val;
} source_state_kv_t;

typedef struct {
	const char *key;
	char *dest;
	size_t dest_size;
} json_str_map_t;

static inline void
json_extract_strings(json_object *jo, const json_str_map_t *map)
{
	if (!jo || !map)
		return;
	json_object *jval;
	for (const json_str_map_t *m = map; m->key; m++) {
		if (json_object_object_get_ex(jo, m->key, &jval))
			snprintf(
			        m->dest,
			        m->dest_size,
			        "%s",
			        json_object_get_string(jval));
	}
}

/* ── Unified field schema generators ───────────────────────────── */

struct bud_field_desc;

#ifndef SOURCE_IMPL
NDX_HOOK_DECL(int, source_def_to_qmap,
    const struct bud_field_desc *, defs, int, count, void *, out);
NDX_HOOK_DECL(int, source_def_to_source_fields,
    const struct bud_field_desc *, defs, int, count, void *, out);
NDX_HOOK_DECL(int, source_def_to_meta_fields,
    const struct bud_field_desc *, defs, int, count,
    const void *, record, void *, out);
NDX_HOOK_DECL(int, source_build_state_specs,
    const struct bud_field_desc *, fields,
    source_state_field_t *, specs,
    int, max_specs);
NDX_HOOK_DECL(source_def_t *, source_find, const char *, dataset_id);
NDX_HOOK_DECL(int, source_register, const source_def_t *, def);
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

NDX_HOOK_DECL(unsigned, source_get_data_hd, const char *, dataset_id);
NDX_HOOK_DECL(unsigned, source_get_fields_hd, const char *, dataset_id);
NDX_HOOK_DECL(unsigned, source_get_schema_hd, const char *, dataset_id);
NDX_HOOK_DECL(int, source_build_state_json,
    const char *, dataset_id,
    const char *, item_id,
    const source_state_field_t *, specs,
    json_object **, out);
NDX_HOOK_DECL(int, source_state_overlay,
    json_object *, jo,
    const source_state_kv_t *, kvs);
NDX_HOOK_DECL(int, source_overlay_from_desc,
    json_object *, jo,
    const void *, state,
    const struct bud_field_desc *, fields,
    int, int_kind,
    int, str_kind);
NDX_HOOK_DECL(int, source_resolve_ref_display_str,
    const char *, dataset_id,
    const char *, item_id,
    const char *, field_name,
    char *, out, size_t, out_sz);
NDX_HOOK_DECL(int, source_resolve_meta_display,
    const char *, dataset_id,
    const char *, item_id,
    const struct bud_field_desc *, fields,
    int, count,
    void *, state);
NDX_HOOK_DECL(int, source_meta_read,
    const char *, path,
    const struct bud_field_desc *, fields,
    int, count,
    void *, record,
    size_t, record_size);
NDX_HOOK_DECL(int, source_meta_write,
    const char *, path,
    const struct bud_field_desc *, fields,
    int, count,
    const void *, record);
NDX_HOOK_DECL(uint32_t, source_setup,
    const char *, source_id,
    const char *, key_field,
    size_t, record_size,
    const char *, items_path,
    const struct bud_field_desc *, defs,
    int, field_count,
    unsigned, flags);

NDX_HOOK_DECL(size_t, source_inv_keys,
    const char *, dataset_id,
    const char *, field,
    uint32_t, target_pos,
    const char **, keys,
    size_t, max);

NDX_HOOK_DECL(const char *, source_inv_key_at,
    const char *, dataset_id,
    const char *, field,
    uint32_t, target_pos,
    size_t, index);

NDX_HOOK_DECL(const char *, qmap_get_field_str,
    unsigned, hd,
    const char *, id,
    const char *, field);
#endif /* SOURCE_IMPL */
#endif
