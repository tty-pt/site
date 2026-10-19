/* Public API of the grp module (cross-.so calls via XY dispatch).
 *
 * Implementing modules must not include this header (XY_DECL and XY_IMPL
 * of the same name may not share a translation unit); grp.c defines these
 * with XY_IMPL. Callers (e.g. gig.c) include it after xy-mod.h.
 */

#ifndef GRP_MOD_H
#define GRP_MOD_H

#include <ttypt/xy.h>

typedef void (*rep_entry_cb)(
        const char *song_id, int transpose, const char *format, int pinned,
        void *user);

/* Iterate over a group's complete merged repertoire:
 * Pinned rows (pinned=1 from grp.songs) are yielded first, followed by
 * derived rows tallied from the group's gigs (majority transpose, first-seen
 * tie-breaker) that are not already pinned. Returns 0 on success.
 */
XY_DECL(int, rep_for_each_merged,
        const char *, grp_id,
        rep_entry_cb, cb,
        void *, user);

/* Prune unpinned rows from a group's stored partition (grp.songs).
 *
 * Pinned rows (pinned=1) are preserved; any legacy pinned=0 rows are stripped.
 * Idempotent: compares against stored partition and only writes if changed.
 *
 * Returns 0 on success, -1 on error.
 */
XY_DECL(int, rep_rebuild, const char *, grp_id);

#endif /* GRP_MOD_H */
