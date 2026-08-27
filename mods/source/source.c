#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "../auth/auth.h"
#include "../common/common.h"
#include "../mpfd/mpfd.h"
#include <ttypt/axil.h>
#include <ttypt/xy-mod.h>
#include <ttypt/qmap.h>
#include <hyle/hyle.h>
#include <hyle/source.h>

#define SOURCE_IMPL
#include "source.h"

void source_install_routes(void);

XY_DECL(int, source_after_update,
	int, fd, const char *, dataset_id,
	const char *, id, unsigned, data_handle);

XY_MODULE_API void xy_install(void)
{
	source_install_routes();
}

XY_IMPL(source_def_t *, source_find, const char *, dataset_id)
{
	return hyle_source_find(dataset_id);
}

XY_IMPL(int, source_item_exists,
	const char *, dataset_id,
	const char *, item_id)
{
	return hyle_source_item_exists(dataset_id, item_id);
}

XY_IMPL(int, source_resolve_ref_display_str,
	const char *, dataset_id,
	const char *, item_id,
	const char *, field_name,
	char *, out, size_t, out_sz)
{
	return hyle_source_resolve_ref_display_str(
	        dataset_id, item_id, field_name, out, out_sz);
}

XY_IMPL(int, source_resolve_meta_display,
	const char *, dataset_id,
	const char *, item_id,
	const source_desc_t *, fields,
	int, count,
	void *, state)
{
	return hyle_source_resolve_meta_display(
	        dataset_id, item_id, fields, count, state);
}

XY_IMPL(int, ref_field_register,
	const char *, dataset_id, const char *, field_name)
{
	return hyle_ref_field_register(dataset_id, field_name);
}

XY_IMPL(int, source_delete_item,
	int, fd,
	const source_def_t *, def,
	const char *, item_id)
{
	return hyle_source_delete_item(fd, def, item_id);
}

XY_IMPL(int, source_clear_inverse_refs,
	int, fd,
	const char *, dataset_id,
	const char *, item_id)
{
	return hyle_source_clear_inverse_refs(fd, dataset_id, item_id);
}

XY_IMPL(int, source_refresh_row,
	int, fd, const char *, dataset_id, const char *, id)
{
	return hyle_source_refresh_row(fd, dataset_id, id);
}

static int get_single_field_cb(const char *name, char *buf, size_t sz, void *user)
{
	(void)user;
	int fld_len = mpfd_len(name);
	if (fld_len < 0) {
		if (!buf || sz == 0) {
			char tmp[1024];
			int rl = axil_query_param(name, tmp, sizeof(tmp));
			return (rl > 0 && rl < (int)(sizeof(tmp) - 1)) ? rl : -1;
		}
		return axil_query_param(name, buf, sz);
	}
	if (!buf || sz == 0)
		return fld_len;
	return mpfd_get(name, buf, sz);
}

static int get_multi_field_cb(const char *name, char *buf, size_t sz, void *user)
{
	(void)user;
	return mpfd_get_all(name, buf, sz);
}

static unsigned source_parse_row_data(const source_def_t *def)
{
	return hyle_source_parse_row_data_custom(
	        def, get_single_field_cb, get_multi_field_cb, NULL);
}

XY_IMPL(unsigned, source_parse_form, const char *, dataset_id)
{
	const source_def_t *def = source_find(dataset_id);
	if (!def)
		return 0;
	return source_parse_row_data(def);
}

static int respond_422_close(int fd, const char *json_str)
{
	axil_header_set(fd, "Connection", "close");
	return respond_json(fd, 422, json_str);
}

XY_IMPL(int, source_update_item,
	int, fd,
	const char *, dataset_id,
	const char *, id,
	unsigned, data_handle)
{
	const source_def_t *def = source_find(dataset_id);
	if (!def)
		return -1;

	char *err_json = NULL;
	if (hyle_source_validate_row(def, data_handle, &err_json)) {
		if (fd > 0 && err_json) {
			respond_422_close(fd, err_json);
		}
		free(err_json);
		return SOURCE_ERR_VALIDATION;
	}

	int rc = hyle_source_update_item(fd, dataset_id, id, data_handle);
	if (rc == 0) {
		source_after_update(fd, dataset_id, id, data_handle);
	}
	return rc;
}

XY_IMPL(int, source_register, const source_def_t *, def)
{
	return hyle_source_register_def(def);
}

XY_IMPL(int, source_for_each, source_each_cb_t, cb, void *, user)
{
	return hyle_source_for_each(cb, user);
}

XY_IMPL(unsigned, source_query,
	const char *, dataset_id,
	const char *, query_str)
{
	return hyle_source_query_dataset(dataset_id, query_str);
}

XY_IMPL(unsigned, source_get_data_hd, const char *, dataset_id)
{
	return hyle_source_get_data_hd(dataset_id);
}

XY_IMPL(unsigned, source_get_fields_hd, const char *, dataset_id)
{
	return hyle_source_get_fields_hd(dataset_id);
}

XY_IMPL(unsigned, source_get_schema_hd, const char *, dataset_id)
{
	return hyle_source_get_schema_hd(dataset_id);
}

XY_IMPL(const source_list_view_t *, source_get_list_view,
	const char *, dataset_id)
{
	return hyle_source_get_list_view(dataset_id);
}

XY_IMPL(int, source_def_to_qmap,
    const source_desc_t *, defs, int, count, void *, out)
{
	return hyle_source_def_to_qmap(defs, count, out);
}

XY_IMPL(int, source_def_to_source_fields,
    const source_desc_t *, defs, int, count, void *, out)
{
	return hyle_source_def_to_source_fields(defs, count, out);
}

XY_IMPL(int, source_def_to_meta_fields,
    const source_desc_t *, defs, int, count,
    const void *, record, void *, out)
{
	return hyle_source_def_to_meta_fields(defs, count, record, out);
}

XY_IMPL(int, source_build_state_specs,
	const source_desc_t *, fields,
	source_state_field_t *, specs,
	int, max_specs)
{
	return hyle_source_build_state_specs(fields, specs, max_specs);
}

XY_IMPL(int, source_meta_read,
	const char *, path,
	const source_desc_t *, fields,
	int, count,
	void *, record,
	size_t, record_size)
{
	return hyle_source_meta_read(path, fields, count, record, record_size);
}

XY_IMPL(int, source_meta_write,
	const char *, path,
	const source_desc_t *, fields,
	int, count,
	const void *, record)
{
	return hyle_source_meta_write(path, fields, count, record);
}

XY_IMPL(size_t, source_inv_keys,
	const char *, dataset_id,
	const char *, field,
	uint32_t, target_pos,
	const char **, keys,
	size_t, max)
{
	return hyle_source_inv_keys(dataset_id, field, target_pos, keys, max);
}

XY_IMPL(const char *, source_inv_key_at,
	const char *, dataset_id,
	const char *, field,
	uint32_t, target_pos,
	size_t, index)
{
	return hyle_source_inv_key_at(dataset_id, field, target_pos, index);
}

XY_IMPL(const char *, qmap_get_field_str,
	unsigned, hd,
	const char *, id,
	const char *, field)
{
	return hyle_qmap_get_field_str(hd, id, field);
}

XY_IMPL(uint32_t, source_setup,
	const char *, source_id,
	const char *, key_field,
	size_t, record_size,
	const char *, items_path,
	const source_desc_t *, defs,
	int, field_count,
	unsigned, flags,
	const source_list_view_t *, list_view)
{
	return hyle_source_setup(
	        source_id, key_field, record_size, items_path, defs,
	        field_count, flags, list_view);
}

XY_IMPL(int, source_build_state_json,
	const char *, dataset_id,
	const char *, item_id,
	const source_state_field_t *, specs,
	json_object **, out)
{
	return hyle_source_build_state_json(dataset_id, item_id, specs, out);
}

XY_IMPL(int, source_state_overlay,
	json_object *, jo,
	const source_state_kv_t *, kvs)
{
	return hyle_source_state_overlay(jo, kvs);
}

XY_IMPL(int, source_overlay_from_desc,
	json_object *, jo,
	const void *, state,
	const source_desc_t *, fields,
	int, int_kind,
	int, str_kind)
{
	return hyle_source_overlay_from_desc(
	        jo, state, fields, int_kind, str_kind);
}

XY_IMPL(json_object *, source_overlay_array,
    const void *, items, int, count, size_t, elem_size,
    const source_desc_t *, fields,
    int, int_kind, int, str_kind)
{
	return hyle_source_overlay_array(
	        items, count, elem_size, fields, int_kind, str_kind);
}

XY_IMPL(int, source_respond_page_state,
    int, fd,
    const char *, dataset_id,
    const char *, item_id,
    const source_state_field_t *, specs,
    const void *, state_struct,
    const source_desc_t *, overlay_fields,
    void *, custom_overlay_fn_ptr,
    void *, user_data)
{
	void (*custom_overlay_fn)(struct json_object *, void *) =
	        custom_overlay_fn_ptr;
	json_object *jo = NULL;
	int rc = source_build_state_json(dataset_id, item_id, specs, &jo);
	if (rc != 0 || !jo) {
		if (jo)
			json_object_put(jo);
		return respond_error(fd, 404, "Not found");
	}

	if (state_struct && overlay_fields) {
		source_overlay_from_desc(
		        jo, state_struct, overlay_fields, 3, 4);
	}

	if (custom_overlay_fn) {
		custom_overlay_fn(jo, user_data);
	}

	const char *json_str = json_object_to_json_string_ext(jo, 0);
	char *json = strdup(json_str ? json_str : "{}");
	json_object_put(jo);

	if (!json)
		return respond_error(fd, 500, "OOM");

	rc = respond_json(fd, 200, json);
	free(json);
	return rc;
}

XY_IMPL(int, source_dsv_load,
	const char *, source_id,
	const char *, pval,
	unsigned, fhd,
	void *, user)
{
	return hyle_source_dsv_load(source_id, pval, fhd, user);
}

XY_IMPL(int, source_dsv_save,
	const char *, source_id,
	const char *, pval,
	unsigned, fhd,
	void *, user)
{
	return hyle_source_dsv_save(source_id, pval, fhd, user);
}

XY_IMPL(const source_desc_t *, source_get_desc,
	const char *, dataset_id,
	int *, count_out)
{
	return hyle_source_get_desc(dataset_id, count_out);
}

XY_IMPL(size_t, source_get_record_size,
	const char *, dataset_id)
{
	return hyle_source_get_record_size(dataset_id);
}
