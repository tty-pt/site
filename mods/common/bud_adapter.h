#ifndef BUD_ADAPTER_H
#define BUD_ADAPTER_H

#include "bud/bud.h"

/* Framework-neutral bud→source converters (L02).
 * Owned by common, not source. Source's public XY APIs
 * source_def_to_* remain as thin wrappers for compat,
 * but new code should use these or the neutral source_field_t path.
 * Goal: source/data layer never includes bud/bud.h directly.
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

#endif
