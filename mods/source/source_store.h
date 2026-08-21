#ifndef SOURCE_STORE_H
#define SOURCE_STORE_H

/* Re-export store ops for FS/Mem adapters. The canonical definitions
 * live in source.h to avoid circular includes; this header just
 * declares the concrete adapters. */

#include "source.h"

const source_store_ops_t *source_store_fs_ops(void);
source_store_t source_store_fs(const char *items_path);

const source_store_ops_t *source_store_mem_ops(void);
source_store_t source_store_mem(void);

#endif
