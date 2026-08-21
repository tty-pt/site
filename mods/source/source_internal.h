#ifndef SOURCE_INTERNAL_H
#define SOURCE_INTERNAL_H

/* Private to mods/source/*.c — not part of public XY API.
 * Shares multi-ref ensure+normalize ownership between source.c
 * (where ref_regs + source_ensure_entity live) and the FS adapter.
 * Do not include from outside mods/source. */

#include "source.h"

/* Ensure referenced entities exist and normalize display names to slugs.
 * dataset_id is the owning dataset (e.g. "song.items"), f is the
 * MULTI_REFERENCE field, *data is malloc'd line-separated display names
 * and is replaced with newline-separated slugs when applicable.
 * Returns 0 on success. */
int source_internal_process_multi_ref(
        const source_field_t *f, const char *dataset_id, char **data);

#endif
