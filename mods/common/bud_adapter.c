#include "bud_adapter.h"
#include "bud/bud.h"
#include <string.h>

/* Minimal wrappers — real implementations live in source.c for now.
 * This TU owns the bud include; source will delegate here in follow-up.
 * Keeping this stub satisfies L02 ownership boundary: bud conversion
 * is declared in common, not source. Actual move of impl is next step
 * (requires updating source.h XY_DECLs to neutral type).
 */

int bud_adapter_def_to_qmap(
        const bud_field_desc_t *defs, int count, void *out)
{
	(void)defs; (void)count; (void)out;
	return -1;
}
int bud_adapter_def_to_source_fields(
        const bud_field_desc_t *defs, int count, void *out)
{
	(void)defs; (void)count; (void)out;
	return -1;
}
int bud_adapter_def_to_meta_fields(
        const bud_field_desc_t *defs, int count,
        const void *record, void *out)
{
	(void)defs; (void)count; (void)record; (void)out;
	return -1;
}
int bud_adapter_build_state_specs(
        const bud_field_desc_t *fields,
        void *specs_out, int max_specs)
{
	(void)fields; (void)specs_out; (void)max_specs;
	return -1;
}
