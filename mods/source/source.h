#ifndef SOURCE_H
#define SOURCE_H

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <json-c/json.h>
#include <ttypt/xy.h>
#include <hyle-source/hyle_source.h>
#include <hyle-source/store.h>

typedef hyle_source_access_policy_t source_access_policy_t;
typedef hyle_source_access_result_t source_access_result_t;
#define SOURCE_ACCESS_PUBLIC HYLE_SOURCE_ACCESS_PUBLIC
#define DATASET_ACCESS_LOGIN HYLE_SOURCE_ACCESS_LOGIN
#define DATASET_ACCESS_RESULT_ALLOW HYLE_SOURCE_ACCESS_RESULT_ALLOW
#define DATASET_ACCESS_RESULT_UNAUTHORIZED HYLE_SOURCE_ACCESS_RESULT_UNAUTHORIZED
#define DATASET_ACCESS_RESULT_FORBIDDEN HYLE_SOURCE_ACCESS_RESULT_FORBIDDEN

#define SOURCE_FIELD_KIND_INVERSE HYLE_SOURCE_FIELD_KIND_INVERSE
typedef hyle_source_field_type_t source_field_type_t;
#define SOURCE_FIELD_STRING HYLE_SOURCE_FIELD_STRING
#define DATASET_FIELD_INT HYLE_SOURCE_FIELD_INT
#define DATASET_FIELD_BOOL HYLE_SOURCE_FIELD_BOOL
#define DATASET_FIELD_NULLABLE_STRING HYLE_SOURCE_FIELD_NULLABLE_STRING
#define SOURCE_FIELD_REFERENCE HYLE_SOURCE_FIELD_REFERENCE
#define SOURCE_FIELD_MULTI_REFERENCE HYLE_SOURCE_FIELD_MULTI_REFERENCE
#define SOURCE_FIELD_INVERSE HYLE_SOURCE_FIELD_INVERSE
#define SOURCE_FIELD_DERIVED HYLE_SOURCE_FIELD_DERIVED

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
XY_DECL(const source_desc_t *, source_get_desc,
    const char *, dataset_id,
    int *, count_out);
XY_DECL(size_t, source_get_record_size,
    const char *, dataset_id);
#endif /* SOURCE_IMPL */
#endif
