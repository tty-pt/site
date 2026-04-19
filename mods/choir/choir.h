#ifndef CHOIR_H
#define CHOIR_H

#include <stddef.h>
#include <ttypt/ndx-mod.h>

/* ---------------------------------------------------------------------------
 * Repertoire-file rewrite callback signature.
 *
 * Choir and songbook both maintain flat text files of "<id>:<int>:<fmt>"
 * lines (see parse_item_line in song). These helpers stream-rewrite such
 * a file through a user callback that decides, per line, to keep, replace
 * or skip it. The write goes to "<path>.tmp" and is rename()d on success.
 *
 * The callback receives the pre-parsed fields and the zero-based line
 * index. If parse_item_line fails (malformed line), the fields will be
 * zero/empty and parsed=0; the callback may still emit SONGS_LINE_KEEP
 * to passthrough-preserve the raw line.
 *
 * songs_file_rewrite returns the number of lines where cb returned
 * REPLACE or SKIP, or -1 on I/O error (the temp file is unlinked and
 * the original is untouched). A missing source file is treated as
 * empty (returns 0).
 * ------------------------------------------------------------------------- */

#define SONGS_LINE_KEEP     0
#define SONGS_LINE_REPLACE  1
#define SONGS_LINE_SKIP    -1

typedef int (*songs_line_fn)(
	int         idx,
	const char *raw,
	int         parsed,
	const char *sid,
	int         ival,
	const char *fmt,
	void       *user,
	char       *out,
	size_t      out_sz);

#ifndef CHOIR_IMPL
NDX_DECL(int, songs_file_rewrite,
	const char *, path, songs_line_fn, cb, void *, user);
#endif

#endif
