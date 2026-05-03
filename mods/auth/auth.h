#ifndef MOD_AUTH_H
#define MOD_AUTH_H

#include <stddef.h>
#include <ttypt/ndx-mod.h>

#ifndef PATH_MAX
#include <limits.h>
#endif

/* ---------------------------------------------------------------------------
 * Item handler context.
 *
 * Standard preamble for handlers operating on items under
 * "<doc_root>/<items_path>/<PATTERN_PARAM_ID>". Populated by item_ctx_load()
 * which also enforces login/ownership and writes error responses on failure.
 * ------------------------------------------------------------------------- */

typedef struct {
	int fd;
	const char *username;
	char doc_root[256];
	char id[128];
	char song_id[128];
	char item_path[PATH_MAX - 512];
} item_ctx_t;

typedef enum {
	ITEM_ACCESS_OK = 0,
	ITEM_ACCESS_UNAUTHENTICATED,
	ITEM_ACCESS_MISSING,
	ITEM_ACCESS_FORBIDDEN,
} item_access_t;

typedef int (*item_handler_cb)(
        int fd, char *body, const item_ctx_t *ctx, void *user);

#define ICTX_NEED_LOGIN 0x1     /* require logged-in user; else 401 */
#define ICTX_NEED_OWNERSHIP 0x2 /* require item ownership; else 403/404 */
#define ICTX_SONG_ID 0x4        /* also read PATTERN_PARAM_SONG_ID */

#include <ttypt/auth.h>

#ifndef ITEM_IMPL

/* Ownership helpers */

/* Record ownership of item_path for username.
 * When root: chown to uid. When non-root: write owner file. */
NDX_HOOK_DECL(int, item_record_ownership,
	const char *, item_path,
	const char *, username);

/* Check if username owns item_path.
 * When root: compare stat uid. When non-root: read owner file. */
NDX_HOOK_DECL(int, item_check_ownership,
	const char *, item_path,
	const char *, username);

NDX_HOOK_DECL(item_access_t, item_access_status,
	const char *, item_path,
	const char *, username,
	unsigned, flags);
NDX_HOOK_DECL(int, item_require_access,
	int, fd,
	const char *, item_path,
	const char *, username,
	unsigned, flags,
	const char *, not_found_msg,
	const char *, forbidden_msg);

/* Read owner name into out (display use).
 * When root: getpwuid_r. When non-root: read owner file. */
NDX_HOOK_DECL(int, item_read_owner,
	const char *, item_path,
	char *, out,
	size_t, outlen);

/* Unlink owner file (non-root only); call before rmdir. */
NDX_HOOK_DECL(int, item_unlink_owner, const char *, item_path);

/* Populate an item_ctx_t from the request; enforces flags and responds
 * with an appropriate error on failure. Returns 0 on success, non-zero
 * if the handler should return (response already sent). */
NDX_HOOK_DECL(int, item_ctx_load,
	item_ctx_t *, ctx,
	int, fd,
	const char *, items_path,
	unsigned, flags);

NDX_HOOK_DECL(int, with_item_access,
	int, fd,
	char *, body,
	const char *, items_path,
	unsigned, flags,
	const char *, not_found_msg,
	const char *, forbidden_msg,
	item_handler_cb, cb,
	void *, user);

#endif /* ITEM_IMPL */

#endif
