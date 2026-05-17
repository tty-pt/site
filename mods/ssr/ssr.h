#ifndef SSR_MOD_H
#define SSR_MOD_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <ttypt/ndx-mod.h>

/* ── Hook declarations (used by song.c, poem.c, choir.c, songbook.c, etc.) ──
 */

NDX_HOOK_DECL(int, ssr_render,
	int, fd,
	const char *, method,
	const char *, path,
	const char *, query,
	const char *, body,
	size_t, body_len,
	const char *, remote_user);

NDX_HOOK_DECL(int, ssr_render_delete,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, title);

#endif
