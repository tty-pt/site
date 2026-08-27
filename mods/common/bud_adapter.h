#ifndef BUD_ADAPTER_H
#define BUD_ADAPTER_H

#include "bud/bud.h"
#include <json-c/json.h>

int bud_adapter_overlay_from_desc(
        json_object *jo,
        const void *state,
        const bud_field_desc_t *fields,
        int int_kind,
        int str_kind);

json_object *bud_adapter_overlay_array(
        const void *items,
        int count,
        size_t elem_size,
        const bud_field_desc_t *fields,
        int int_kind,
        int str_kind);

#endif
