#ifndef SOURCE_H
#define SOURCE_H

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <json-c/json.h>
#include <ttypt/xy.h>

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
	const char *filter_style;
	const char *filter_mode;
} source_field_t;

typedef struct {
	const char *name;
	const char *label;
} source_list_field_t;

typedef struct {
	const char *display_name;
	const source_list_field_t *fields;
	size_t field_count;
	const char *default_sort;
	const char *content_field;
	const char *content_label;
	const char *content_placeholder;
} source_list_view_t;

/* Source borrows list-view strings and fields for the registered source's
 * lifetime. Module declarations must therefore have static storage. */

/* Persistence adapter — COMPLY.md §9.3 (M05). Generic source does not
 * assume filesystem; each dataset declares a store. Stage 1: interface
 * declared here, FS adapter in source_store_fs.c. */
struct source_def_s;
typedef struct source_store_s source_store_t;
typedef struct {
	int (*scan)(source_store_t *store, const struct source_def_s *def);
	int (*load)(source_store_t *store, const struct source_def_s *def,
	            const char *id, unsigned *row_out);
	int (*put)(source_store_t *store, const struct source_def_s *def,
	           const char *id, unsigned data_handle);
	int (*put_field)(source_store_t *store,
	                 const struct source_def_s *def, const char *id,
	                 const char *field, const char *value);
	int (*del)(source_store_t *store, const struct source_def_s *def,
	           const char *id);
} source_store_ops_t;
struct source_store_s {
	const source_store_ops_t *ops;
	void *user;
};

typedef struct source_def_s {
	const char *id;
	const char *key_field;
	const char *items_path;
	source_access_policy_t access_policy;
	const source_field_t *fields;
	size_t field_count;
	unsigned source_hd;
	unsigned fields_hd;
	unsigned schema_hd; /* lazily filled by source_get_schema_hd */
	uint32_t record_id;
	unsigned flags;
	const source_list_view_t *list_view;
	void *user;
	source_store_t store;
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
			        m->dest, m->dest_size, "%s",
			        json_object_get_string(jval));
	}
}

/* ── Unified field schema generators ───────────────────────────── */
/* Framework-neutral descriptor — binary compatible with bud_field_desc_t
 * (bud/bud.h:159). Site data layer owns this; WASM/client uses bud.h.
 * L02: source never includes bud/bud.h; cast is safe (static_assert in
 * bud_adapter.c). */
typedef struct source_desc {
	const char *key;
	size_t offset;
	size_t size;
	int is_int;
	int kind;
	int qm_type;
	int source_type;
	int writable;
	int required;
	size_t min_length;
	const char *ref_source;
	const char *ref_inverse;
	int in_meta;
	const char *file;
	const char *filter_style;
	const char *filter_mode;
} source_desc_t;

#ifndef SOURCE_IMPL
XY_DECL(int, source_clear_inverse_refs,
    int, fd,
    const char *, dataset_id,
    const char *, item_id);
XY_DECL(int, source_def_to_qmap,
    const source_desc_t *, defs, int, count, void *, out);
XY_DECL(int, source_def_to_source_fields,
    const source_desc_t *, defs, int, count, void *, out);
XY_DECL(int, source_def_to_meta_fields,
    const source_desc_t *, defs, int, count,
    const void *, record, void *, out);
XY_DECL(int, source_build_state_specs,
    const source_desc_t *, fields,
    source_state_field_t *, specs,
    int, max_specs);
XY_DECL(source_def_t *, source_find, const char *, dataset_id);
XY_DECL(int, source_item_exists,
    const char *, dataset_id,
    const char *, item_id);
XY_DECL(int, source_register, const source_def_t *, def);
XY_DECL(int, source_refresh_row,
    int, fd, const char *, dataset_id, const char *, id);
XY_DECL(int, source_update_item,
    int, fd, const char *, dataset_id,
    const char *, id, unsigned, data_handle);
XY_DECL(unsigned, source_parse_form, const char *, dataset_id);
XY_DECL(int, source_delete_item,
    int, fd, const source_def_t *, def, const char *, item_id);
XY_DECL(int, ref_field_register,
    const char *, dataset_id, const char *, field_name);
XY_DECL(int, source_for_each, source_each_cb_t, cb, void *, user);

XY_DECL(unsigned, source_query,
	const char *, dataset_id,
	const char *, query_str);

XY_DECL(unsigned, source_get_data_hd, const char *, dataset_id);
XY_DECL(unsigned, source_get_fields_hd, const char *, dataset_id);
XY_DECL(unsigned, source_get_schema_hd, const char *, dataset_id);
XY_DECL(const source_list_view_t *, source_get_list_view,
	const char *, dataset_id);
XY_DECL(int, source_build_state_json,
    const char *, dataset_id,
    const char *, item_id,
    const source_state_field_t *, specs,
    json_object **, out);
XY_DECL(int, source_state_overlay,
    json_object *, jo,
    const source_state_kv_t *, kvs);
XY_DECL(int, source_overlay_from_desc,
    json_object *, jo,
    const void *, state,
    const source_desc_t *, fields,
    int, int_kind,
    int, str_kind);
XY_DECL(int, source_respond_page_state,
    int, fd,
    const char *, dataset_id,
    const char *, item_id,
    const source_state_field_t *, specs,
    const void *, state_struct,
    const source_desc_t *, overlay_fields,
    void *, custom_overlay_fn,
    void *, user_data);
XY_DECL(json_object *, source_overlay_array,
    const void *, items, int, count, size_t, elem_size,
    const source_desc_t *, fields,
    int, int_kind, int, str_kind);
XY_DECL(int, source_resolve_ref_display_str,
    const char *, dataset_id,
    const char *, item_id,
    const char *, field_name,
    char *, out, size_t, out_sz);
XY_DECL(int, source_resolve_meta_display,
    const char *, dataset_id,
    const char *, item_id,
    const source_desc_t *, fields,
    int, count,
    void *, state);
XY_DECL(int, source_meta_read,
    const char *, path,
    const source_desc_t *, fields,
    int, count,
    void *, record,
    size_t, record_size);
XY_DECL(int, source_meta_write,
    const char *, path,
    const source_desc_t *, fields,
    int, count,
    const void *, record);
XY_DECL(uint32_t, source_setup,
    const char *, source_id,
    const char *, key_field,
    size_t, record_size,
    const char *, items_path,
    const source_desc_t *, defs,
    int, field_count,
    unsigned, flags,
    const source_list_view_t *, list_view);

XY_DECL(size_t, source_inv_keys,
    const char *, dataset_id,
    const char *, field,
    uint32_t, target_pos,
    const char **, keys,
    size_t, max);

XY_DECL(const char *, source_inv_key_at,
    const char *, dataset_id,
    const char *, field,
    uint32_t, target_pos,
    size_t, index);

XY_DECL(const char *, qmap_get_field_str,
    unsigned, hd,
    const char *, id,
    const char *, field);

XY_DECL(int, source_dsv_load,
    const char *, source_id,
    const char *, pval,
    unsigned, fhd,
    void *, user);
XY_DECL(int, source_dsv_save,
    const char *, source_id,
    const char *, pval,
    unsigned, fhd,
    void *, user);
#endif /* SOURCE_IMPL */
#endif
