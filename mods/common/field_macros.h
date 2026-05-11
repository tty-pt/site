/* Shared field descriptor macros.
 * Consolidates bud_field_desc_t initializers that are identical
 * across song, choir, songbook, and poem modules.
 *
 * WASM-safe: pure C, no NDX hooks.
 */
#ifndef FIELD_MACROS_H
#define FIELD_MACROS_H

#include <stddef.h>
#include "bud/bud.h"

/* Field kind constants (numeric values consistent across all modules) */
#define BUD_RECORD 0
#define BUD_EXCLUDE 1
#define BUD_REF_DISPLAY 2
#define BUD_OVERLAY_INT 3
#define BUD_OVERLAY_STR 4
#define BUD_INVERSE 5

/* QM type constants (from qmap.h — not always visible in bud.h) */
#ifndef BUD_QM_REFERENCE
#define BUD_QM_REFERENCE 6
#endif
#ifndef BUD_QM_VSTR
#define BUD_QM_VSTR 7
#endif

/* Source type constants (from source_field_type_t) */
#ifndef SOURCE_FIELD_STRING
#define SOURCE_FIELD_STRING 0
#define SOURCE_FIELD_REFERENCE 4
#define SOURCE_FIELD_MULTI_REFERENCE 5
#endif

/* ── Record field (string, backed by struct member) ───────────── */
#define REC_FIELD(name, st, mb, sz, wr, rq, ml, im)                            \
	{                                                                      \
		#name, offsetof(st, mb), sz, 0, BUD_RECORD, BUD_QM_STR,        \
		        SOURCE_FIELD_STRING, wr, rq, ml, NULL, NULL, im, NULL  \
	}

/* Single reference field */
#define REF_FIELD(name, st, mb, sz, src, inv, im)                              \
	{                                                                      \
		#name, offsetof(st, mb), sz, 0, BUD_RECORD, BUD_QM_REFERENCE,  \
		        SOURCE_FIELD_REFERENCE, 1, 0, 0, src, inv, im, NULL    \
	}

/* Multi-reference field (resolves to display names) */
#define MULTI_REF_FIELD(name, st, mb, sz, src, inv, im)                        \
	{                                                                      \
		#name, offsetof(st, mb), sz, 0, BUD_REF_DISPLAY,               \
		        BUD_QM_MULTI_REF, SOURCE_FIELD_MULTI_REFERENCE, 1, 0,  \
		        0, src, inv, im, NULL                                  \
	}

/* Inverse field (virtual, computed from reference) */
#define INVERSE_FIELD(name, src, inv)                                          \
	{                                                                      \
		#name, 0, 0, 0, BUD_INVERSE, 0, 0, 0, 0, 0, src, inv, 0, NULL  \
	}

/* Excluded field (backed by struct member) */
#define EXCL_FIELD(name, st, mb, sz, qt, im)                                   \
	{                                                                      \
		#name, offsetof(st, mb), sz, 0, BUD_EXCLUDE, qt, 0, 0, 0, 0,   \
		        NULL, NULL, im, NULL                                   \
	}

/* Excluded virtual field (no struct backing) */
#define EXCL_FIELD_V(name, qt, wr, im)                                         \
	{                                                                      \
		#name, 0, 0, 0, BUD_EXCLUDE, qt, 0, wr, 0, 0, NULL, NULL, im,  \
		        NULL                                                   \
	}

/* Excluded virtual field with file path */
#define EXCL_FIELD_VF(name, qt, wr, im, fl)                                    \
	{                                                                      \
		#name, 0, 0, 0, BUD_EXCLUDE, qt, 0, wr, 0, 0, NULL, NULL, im,  \
		        fl                                                     \
	}

/* Integer overlay (computed, stored in app_state) */
#define OVERLAY_INT(name, st, mb)                                              \
	{                                                                      \
		#name, offsetof(st, mb), 0, 1, BUD_OVERLAY_INT                 \
	}

/* String overlay (computed, stored in app_state) */
#define OVERLAY_STR(name, st, mb, sz)                                          \
	{                                                                      \
		#name, offsetof(st, mb), sz, 0, BUD_OVERLAY_STR                \
	}

/* Sentinel — terminates the field array */
#define FIELD_END                                                              \
	{                                                                      \
		NULL, 0, 0, 0, 0                                               \
	}

#endif
