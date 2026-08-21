/* Public API of the grp module (cross-.so calls via XY dispatch).
 *
 * Implementing modules must not include this header (XY_DECL and XY_IMPL
 * of the same name may not share a translation unit); grp.c defines these
 * with XY_IMPL. Callers (e.g. gig.c) include it after xy-mod.h.
 */

#ifndef GRP_MOD_H
#define GRP_MOD_H

#include <ttypt/xy.h>

/* Recompute a group's repertoire from its gigs + pinned rows.
 *
 * Derived rule: a song appears in the repertoire iff it is on at least one
 * gig of the group; its key is the majority transpose across those gigs
 * (ties keep the first-seen value in stable iteration order). Rows marked
 * pinned=1 are user-owned and survive rebuilds untouched.
 *
 * Idempotent: compares against the stored partition and only writes through
 * hyle when something changed, so redundant calls are free.
 *
 * Returns 0 on success (including "no change"), -1 on error or when the
 * tally exceeds fixed caps (refusing to write beats truncating rows).
 */
XY_DECL(int, rep_rebuild, const char *, grp_id);

#endif /* GRP_MOD_H */
