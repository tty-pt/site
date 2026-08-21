#include <ttypt/xy-mod.h>
#include "bud_adapter.h"
#include "bud/bud.h"
#include <string.h>
#include <assert.h>

/* L02: bud adapter owns bud include and forwards to neutral source_desc_t.
 * Binary-compatible — static_assert guards drift. */
_Static_assert(
        sizeof(bud_field_desc_t) == sizeof(source_desc_t),
        "bud_field_desc_t / source_desc_t size mismatch");
_Static_assert(
        offsetof(bud_field_desc_t, key) == offsetof(source_desc_t, key),
        "layout mismatch");
_Static_assert(
        offsetof(bud_field_desc_t, filter_mode) ==
        offsetof(source_desc_t, filter_mode),
        "layout mismatch");

int bud_adapter_def_to_qmap(
        const bud_field_desc_t *defs, int count, void *out)
{
	return source_def_to_qmap(
	        (const source_desc_t *)defs, count, out);
}
int bud_adapter_def_to_source_fields(
        const bud_field_desc_t *defs, int count, void *out)
{
	return source_def_to_source_fields(
	        (const source_desc_t *)defs, count, out);
}
int bud_adapter_def_to_meta_fields(
        const bud_field_desc_t *defs, int count,
        const void *record, void *out)
{
	return source_def_to_meta_fields(
	        (const source_desc_t *)defs, count, record, out);
}
int bud_adapter_build_state_specs(
        const bud_field_desc_t *fields,
        void *specs_out, int max_specs)
{
	return source_build_state_specs(
	        (const source_desc_t *)fields,
	        (source_state_field_t *)specs_out, max_specs);
}
int bud_adapter_meta_read(
        const char *path, const bud_field_desc_t *fields,
        int count, void *record, size_t record_size)
{
	return source_meta_read(
	        path, (const source_desc_t *)fields, count, record,
	        record_size);
}
int bud_adapter_meta_write(
        const char *path, const bud_field_desc_t *fields,
        int count, const void *record)
{
	return source_meta_write(
	        path, (const source_desc_t *)fields, count, record);
}
int bud_adapter_resolve_meta_display(
        const char *dataset_id, const char *item_id,
        const bud_field_desc_t *fields, int count, void *state)
{
	return source_resolve_meta_display(
	        dataset_id, item_id, (const source_desc_t *)fields,
	        count, state);
}
uint32_t bud_adapter_source_setup(
        const char *source_id, const char *key_field,
        size_t record_size, const char *items_path,
        const bud_field_desc_t *defs, int field_count,
        unsigned flags, const source_list_view_t *list_view)
{
	return source_setup(
	        source_id, key_field, record_size, items_path,
	        (const source_desc_t *)defs, field_count, flags,
	        list_view);
}
XY_IMPL(int, bud_adapter_overlay_from_desc,
        json_object *, jo,
        const void *, state,
        const bud_field_desc_t *, fields,
        int, int_kind,
        int, str_kind)
{
	return source_overlay_from_desc(
	        jo, state, (const source_desc_t *)fields, int_kind,
	        str_kind);
}
XY_IMPL(json_object *, bud_adapter_overlay_array,
        const void *, items,
        int, count,
        size_t, elem_size,
        const bud_field_desc_t *, fields,
        int, int_kind,
        int, str_kind)
{
	return source_overlay_array(
	        items, count, elem_size, (const source_desc_t *)fields,
	        int_kind, str_kind);
}
