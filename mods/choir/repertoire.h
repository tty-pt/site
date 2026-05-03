#ifndef CHOIR_REPERTOIRE_H
#define CHOIR_REPERTOIRE_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <ttypt/ndx-mod.h>

/* ---------------------------------------------------------------------------
 * Choir-owned repertoire storage API.
 *
 * Choir and songbook both maintain flat text files of "<id>:<int>:<fmt>"
 * lines (see parse_item_line in song). These helpers stream-rewrite such
 * a file through a user callback that decides, per line, to keep, replace
 * or skip it. The write goes to "<path>.tmp" and is rename()d on success.
 *
 * The callback receives the pre-parsed fields and the zero-based line
 * index. If parse_item_line fails (malformed line), the fields will be
 * zero/empty and parsed=0; the callback may still emit REPERTOIRE_LINE_KEEP
 * to passthrough-preserve the raw line.
 *
 * repertoire_file_rewrite returns the number of lines where cb returned
 * REPLACE or SKIP, or -1 on I/O error (the temp file is unlinked and
 * the original is untouched). A missing source file is treated as
 * empty (returns 0).
 * ------------------------------------------------------------------------- */

#define REPERTOIRE_LINE_KEEP 0
#define REPERTOIRE_LINE_REPLACE 1
#define REPERTOIRE_LINE_SKIP -1

typedef int (*repertoire_line_fn)(
    int idx,
    const char *raw,
    int parsed,
    const char *sid,
    int ival,
    const char *fmt,
    void *user,
    char *out,
    size_t out_sz);

typedef struct {
	int parsed;
	char id[128];
	int value;
	char format[128];
} repertoire_row_t;

#ifndef CHOIR_REPERTOIRE_IMPL
NDX_HOOK_DECL(int, repertoire_file_rewrite,
              const char *, path, repertoire_line_fn, cb, void *, user);
NDX_HOOK_DECL(int, repertoire_parse_line,
              const char *, line, repertoire_row_t *, row);
NDX_HOOK_DECL(int, repertoire_rows_load,
              const char *, path, repertoire_row_t **, rows, size_t *, count);
NDX_HOOK_DECL(int, repertoire_file_append,
              const char *, path, const repertoire_row_t *, row);
NDX_HOOK_DECL(int, repertoire_rows_write,
              const char *, path, const repertoire_row_t *, rows, size_t, count);
#endif

/* Heap rows returned by repertoire_rows_load are freed by callers. */
static inline void
repertoire_rows_dispose(repertoire_row_t *rows) {
	free(rows);
}

#endif
