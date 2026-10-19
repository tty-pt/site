#ifndef SOURCE_H
#define SOURCE_H

/*
 * mods/source — Caller-facing XY hook declarations over libhyle-source.
 * Types and structs live in source-types.h.
 * Implementers must include source-types.h, not this header.
 */

#include "source-types.h"

XY_DECL(int, source_clear_inverse_refs,
    int, fd,
    const char *, dataset_id,
    const char *, item_id);
XY_DECL(int, source_def_to_corm,
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

XY_DECL(const char *, corm_get_field_str,
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
XY_DECL(int, source_dataset_collect_options,
    const char *, dataset_id,
    const char *, label_field,
    const char *, default_opt,
    source_opt_buf_t *, buf,
    const char **, opts,
    int, max);
XY_DECL(int, source_resolve_partition_key,
    const char *, primary_dataset,
    const char *, partition_dataset,
    const char *, target_field,
    char *, id_inout,
    size_t, id_sz);
XY_DECL(int, source_ordered_sync_form,
    const char *, source_id,
    const char *, partition_id,
    const char *, amount_param,
    const char *, remove_param_prefix,
    const source_ordered_field_sync_t *, fields,
    size_t, n_fields);

XY_DECL(unsigned, source_register_ordered,
    const source_ordered_def_t *, def);

XY_DECL(int, source_ordered_count,
    const char *, source_id,
    const char *, partition_val);

XY_DECL(const char *, source_ordered_key_at,
    const char *, source_id,
    const char *, partition_val,
    int, pos);

XY_DECL(int, source_ordered_append,
    const char *, source_id,
    const char *, partition_val,
    const char **, names,
    const char **, values,
    size_t, count);

XY_DECL(int, source_ordered_insert_at,
    const char *, source_id,
    const char *, partition_val,
    int, pos,
    const char **, names,
    const char **, values,
    size_t, count);

XY_DECL(int, source_ordered_remove_at,
    const char *, source_id,
    const char *, partition_val,
    int, pos);

XY_DECL(int, source_ordered_clear,
    const char *, source_id,
    const char *, partition_val);

XY_DECL(int, source_ordered_save,
    const char *, source_id,
    const char *, partition_val);

XY_DECL(int, source_put_row,
    const char *, source_id,
    const char *, row_id,
    const char **, names,
    const char **, values,
    size_t, count);

XY_DECL(const char *, source_ordered_get_field,
    const char *, source_id,
    const char *, partition_val,
    int, index,
    const char *, field);

XY_DECL(int, source_ordered_set_field,
    const char *, source_id,
    const char *, partition_val,
    int, index,
    const char *, field,
    const char *, value);

XY_DECL(int, source_ordered_remove_and_save,
    const char *, source_id,
    const char *, partition_val,
    int, index);

XY_DECL(int, source_ordered_append_and_save,
    const char *, source_id,
    const char *, partition_val,
    const char **, names,
    const char **, vals,
    size_t, count);

XY_DECL(int, source_ordered_for_each,
    const char *, source_id,
    const char *, partition_val,
    source_ordered_each_fn, fn,
    void *, user);

XY_DECL(size_t, source_find_referencing,
    const char *, source_dataset,
    const char *, ref_field,
    const char *, target_id,
    const char **, ids_out,
    size_t, max);

XY_DECL(size_t, source_for_each_referencing,
    const char *, source_dataset,
    const char *, ref_field,
    const char *, target_id,
    source_ref_cb_t, cb,
    void *, user);

XY_DECL(int, source_ordered_find,
    const char *, source_id,
    const char *, partition_val,
    const char *, field,
    const char *, val);

XY_DECL(int, source_ordered_remove_matching,
    const char *, source_id,
    const char *, partition_val,
    const char *, field,
    const char *, val);

XY_DECL(int, source_ordered_replace_row,
    const char *, source_id,
    const char *, partition_val,
    int, index,
    const char **, names,
    const char **, vals,
    size_t, count);

XY_DECL(const char *, source_get_field,
    const char *, dataset_id,
    const char *, item_id,
    const char *, field);

XY_DECL(int, source_set_field,
    int, fd,
    const char *, dataset_id,
    const char *, item_id,
    const char *, field,
    const char *, value);

XY_DECL(int, source_get_field_int,
    const char *, dataset_id,
    const char *, item_id,
    const char *, field,
    int, def_val);

XY_DECL(int, source_set_field_int,
    int, fd,
    const char *, dataset_id,
    const char *, item_id,
    const char *, field,
    int, val);

XY_DECL(int, source_register_derive,
    const char *, derive_key,
    source_derive_fn_t, fn,
    void *, user);

#endif /* SOURCE_H */
