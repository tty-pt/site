#include <ttypt/xy-mod.h>
#include "bud_adapter.h"
#include "bud/bud.h"
#include <string.h>
#include <json-c/json.h>
#include <hyle-bud/hyle-bud.h>

XY_IMPL(int, bud_adapter_overlay_from_desc,
        json_object *, jo,
        const void *, state,
        const bud_field_desc_t *, fields,
        int, int_kind,
        int, str_kind)
{
	return hyle_bud_state_overlay_from_desc(
	        jo, state, fields, int_kind, str_kind);
}

XY_IMPL(json_object *, bud_adapter_overlay_array,
        const void *, items,
        int, count,
        size_t, elem_size,
        const bud_field_desc_t *, fields,
        int, int_kind,
        int, str_kind)
{
	return hyle_bud_state_overlay_array(
	        items, count, elem_size, fields, int_kind, str_kind);
}
