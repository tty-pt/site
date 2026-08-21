#ifndef BUD_ADAPTER_H
#define BUD_ADAPTER_H

#include "bud/bud.h"
#include "../source/source.h"

/* Framework-neutral bud→source converters (L02).
 * Owned by common, not source. Source's public XY APIs now take
 * source_desc_t (neutral, binary-compatible with bud_field_desc_t).
 * This header owns the bud include and provides cast-safe forwarding
 * for legacy bud tables (song_fields etc) — static_assert guards drift.
 */
int bud_adapter_def_to_qmap(
        const bud_field_desc_t *defs, int count, void *out);
int bud_adapter_def_to_source_fields(
        const bud_field_desc_t *defs, int count, void *out);
int bud_adapter_def_to_meta_fields(
        const bud_field_desc_t *defs, int count,
        const void *record, void *out);
int bud_adapter_build_state_specs(
        const bud_field_desc_t *fields,
        void *specs_out, int max_specs);
int bud_adapter_meta_read(
        const char *path, const bud_field_desc_t *fields,
        int count, void *record, size_t record_size);
int bud_adapter_meta_write(
        const char *path, const bud_field_desc_t *fields,
        int count, const void *record);
int bud_adapter_resolve_meta_display(
        const char *dataset_id, const char *item_id,
        const bud_field_desc_t *fields, int count, void *state);
uint32_t bud_adapter_source_setup(
        const char *source_id, const char *key_field,
        size_t record_size, const char *items_path,
        const bud_field_desc_t *defs, int field_count,
        unsigned flags, const source_list_view_t *list_view);

#endif
