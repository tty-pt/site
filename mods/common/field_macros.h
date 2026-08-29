/* Shared field descriptor macros.
 * Consolidates bud_field_desc_t initializers that are identical
 * across song, grp, gig, and poem modules.
 *
 * WASM-safe: pure C, no XY hooks.
 */
#ifndef FIELD_MACROS_H
#define FIELD_MACROS_H

#include <stddef.h>
#include <hyle/schema.h>
/* bud constants for WASM binder kind values — keep independent of bud.h */
#ifndef BUD_RECORD
#define BUD_RECORD 0
#define BUD_EXCLUDE 1
#define BUD_REF_DISPLAY 2
#define BUD_OVERLAY_INT 3
#define BUD_OVERLAY_STR 4
#define BUD_INVERSE 5
#endif
#ifndef BUD_QM_STR
#define BUD_QM_STR 2
#define BUD_QM_VSTR 8
#define BUD_QM_MULTI_REF 7
#endif

/* QM type constants — BUD_QM_REFERENCE from qmap.h */
#ifndef BUD_QM_REFERENCE
#define BUD_QM_REFERENCE 6
#endif

/* Source type constants (from source_field_type_t) */
#ifndef SOURCE_FIELD_STRING
#define SOURCE_FIELD_STRING 0
#define SOURCE_FIELD_INT 1
#define SOURCE_FIELD_BOOL 2
#define SOURCE_FIELD_REFERENCE 4
#define SOURCE_FIELD_MULTI_REFERENCE 5
#define SOURCE_FIELD_DERIVED 99
#endif

/* ── Record field (string, backed by struct member) ───────────── */
#define REC_FIELD(name, st, mb, sz, wr, rq, ml, im)                            \
	{                                                                      \
		#name, offsetof(st, mb), sz, 0, BUD_RECORD, BUD_QM_STR,        \
		        SOURCE_FIELD_STRING, wr, rq, ml, NULL, NULL, im, NULL, \
		        NULL, NULL, NULL, 0                                    \
	}

/* Single reference field */
#define REF_FIELD(name, st, mb, sz, src, inv, im)                              \
	{                                                                      \
		#name, offsetof(st, mb), sz, 0, BUD_REF_DISPLAY,              \
		        BUD_QM_REFERENCE, SOURCE_FIELD_REFERENCE, 1, 0, 0,     \
		        src, inv, im, #name, NULL, NULL, NULL, 0               \
	}

/* Single reference field with a filter style hint (e.g. "dropdown") */
#define REF_FIELD_S(name, st, mb, sz, src, inv, im, style)                     \
	{                                                                      \
		#name, offsetof(st, mb), sz, 0, BUD_REF_DISPLAY,              \
		        BUD_QM_REFERENCE, SOURCE_FIELD_REFERENCE, 1, 0, 0,     \
		        src, inv, im, #name, style, NULL, NULL, 0              \
	}

/* Single reference field with style and allow_add flag */
#define REF_FIELD_SA(name, st, mb, sz, src, inv, im, style, add)              \
	{                                                                      \
		#name, offsetof(st, mb), sz, 0, BUD_REF_DISPLAY,              \
		        BUD_QM_REFERENCE, SOURCE_FIELD_REFERENCE, 1, 0, 0,     \
		        src, inv, im, #name, style, NULL, NULL, add            \
	}

/* Multi-reference field (resolves to display names) */
#define MULTI_REF_FIELD(name, st, mb, sz, src, inv, im)                        \
	{                                                                      \
		#name, offsetof(st, mb), sz, 0, BUD_REF_DISPLAY,               \
		        BUD_QM_MULTI_REF, SOURCE_FIELD_MULTI_REFERENCE, 1, 0,  \
		        0, src, inv, im, #name, NULL, NULL, NULL, 0            \
	}

/* Multi-reference field with an explicit filter style hint */
#define MULTI_REF_FIELD_S(name, st, mb, sz, src, inv, im, style)               \
	{                                                                      \
		#name, offsetof(st, mb), sz, 0, BUD_REF_DISPLAY,               \
		        BUD_QM_MULTI_REF, SOURCE_FIELD_MULTI_REFERENCE, 1, 0,  \
		        0, src, inv, im, #name, style, NULL, NULL, 0           \
	}

/* Multi-reference field with style + combine-mode hint ("and"/"or"/NULL) */
#define MULTI_REF_FIELD_SM(name, st, mb, sz, src, inv, im, style, mode)        \
	{                                                                      \
		#name, offsetof(st, mb), sz, 0, BUD_REF_DISPLAY,               \
		        BUD_QM_MULTI_REF, SOURCE_FIELD_MULTI_REFERENCE, 1, 0,  \
		        0, src, inv, im, #name, style, mode, NULL, 0           \
	}

/* Multi-reference field with style, combine-mode hint, and allow_add flag */
#define MULTI_REF_FIELD_SMA(name, st, mb, sz, src, inv, im, style, mode, add)  \
	{                                                                      \
		#name, offsetof(st, mb), sz, 0, BUD_REF_DISPLAY,               \
		        BUD_QM_MULTI_REF, SOURCE_FIELD_MULTI_REFERENCE, 1, 0,  \
		        0, src, inv, im, #name, style, mode, NULL, add         \
	}

/* Inverse field (virtual, computed from reference) */
#define INVERSE_FIELD(name, src, inv)                                          \
	{                                                                      \
		#name, 0, 0, 0, BUD_INVERSE, 0, 0, 0, 0, 0, src, inv, 0, NULL, \
		        NULL, NULL, NULL, 0                                    \
	}

/* Excluded field (backed by struct member) */
#define EXCL_FIELD(name, st, mb, sz, qt, im)                                   \
	{                                                                      \
		#name, offsetof(st, mb), sz, 0, BUD_EXCLUDE, qt, 0, 0, 0, 0,   \
		        NULL, NULL, im, NULL, NULL, NULL, NULL, 0              \
	}

/* Excluded field, writable (backed by struct member) */
#define EXCL_FIELD_W(name, st, mb, sz, qt, im)                                 \
	{                                                                      \
		#name, offsetof(st, mb), sz, 0, BUD_EXCLUDE, qt,               \
		        SOURCE_FIELD_STRING, 1, 0, 0, NULL, NULL, im, NULL,    \
		        NULL, NULL, NULL, 0                                    \
	}

/* Excluded virtual field (no struct backing) */
#define EXCL_FIELD_V(name, qt, wr, im)                                         \
	{                                                                      \
		#name, 0, 0, 0, BUD_EXCLUDE, qt, 0, wr, 0, 0, NULL, NULL, im,  \
		        NULL, NULL, NULL, NULL, 0                              \
	}

/* Excluded virtual field with file path */
#define EXCL_FIELD_VF(name, qt, wr, im, fl)                                    \
	{                                                                      \
		#name, 0, 0, 0, BUD_EXCLUDE, qt, 0, wr, 0, 0, NULL, NULL, im,  \
		        fl, NULL, NULL, NULL, 0                                \
	}

/* Derived virtual field (computed in-memory for FTS/indexing) */
#define DERIVED_FIELD(name, dkey)                                              \
	{                                                                      \
		#name, 0, 0, 0, BUD_EXCLUDE, BUD_QM_STR, SOURCE_FIELD_DERIVED, \
		        0, 0, 0, NULL, NULL, 0, NULL, NULL, NULL, dkey, 0      \
	}

/* Typed integer field */
#define INT_FIELD(name, st, mb, wr)                                            \
	{                                                                      \
		#name, offsetof(st, mb), sizeof(int), 1, BUD_RECORD, 0,        \
		        SOURCE_FIELD_INT, wr, 0, 0, NULL, NULL, 0, NULL, NULL, \
		        NULL, NULL, 0                                          \
	}

/* Typed boolean field */
#define BOOL_FIELD(name, st, mb, wr)                                           \
	{                                                                      \
		#name, offsetof(st, mb), sizeof(int), 0, BUD_RECORD, 0,        \
		        SOURCE_FIELD_BOOL, wr, 0, 0, NULL, NULL, 0, NULL,      \
		        NULL, NULL, NULL, 0                                    \
	}

/* Required string record field */
#define REQ_FIELD(name, st, mb, sz)                                            \
	{                                                                      \
		#name, offsetof(st, mb), sz, 0, BUD_RECORD, BUD_QM_STR,        \
		        SOURCE_FIELD_STRING, 1, 1, 0, NULL, NULL, 0, NULL,     \
		        NULL, NULL, NULL, 0                                    \
	}

/* Required string record field with min_length */
#define REQ_FIELD_MIN(name, st, mb, sz, ml)                                    \
	{                                                                      \
		#name, offsetof(st, mb), sz, 0, BUD_RECORD, BUD_QM_STR,        \
		        SOURCE_FIELD_STRING, 1, 1, ml, NULL, NULL, 0, NULL,    \
		        NULL, NULL, NULL, 0                                    \
	}

/* Virtual multi-line file / data field */
#define VSTR_FIELD(name, fl)                                                   \
	{                                                                      \
		#name, 0, 0, 0, BUD_EXCLUDE, BUD_QM_VSTR, SOURCE_FIELD_STRING, \
		        1, 0, 0, NULL, NULL, 0, fl, NULL, NULL, NULL, 0        \
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
		0                                                              \
	}

#endif
