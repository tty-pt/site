#ifndef MOD_AUTH_H
#define MOD_AUTH_H

#include <stddef.h>
#include <ttypt/xy.h>

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
#define ICTX_CSRF_MPFD 0x8 /* validate CSRF token from multipart form data */
#define ICTX_CSRF_QUERY                                                        \
	0x10 /* validate CSRF token from query string / url-encoded body */

#include <ttypt/auth.h>

#ifndef ITEM_IMPL

/* Ownership helper */

/* Check if username owns item_path.
 * When root: compare stat uid. When non-root: read owner file. */
XY_DECL(int, item_check_ownership,
	const char *, item_path,
	const char *, username);

XY_DECL(item_access_t, item_access_status,
	const char *, item_path,
	const char *, username,
	unsigned, flags);
XY_DECL(int, item_require_access,
	int, fd,
	const char *, item_path,
	const char *, username,
	unsigned, flags,
	const char *, not_found_msg,
	const char *, forbidden_msg);

/* Populate an item_ctx_t from the request; enforces flags and responds
 * with an appropriate error on failure. Returns 0 on success, non-zero
 * if the handler should return (response already sent). */
XY_DECL(int, item_ctx_load,
	item_ctx_t *, ctx,
	int, fd,
	const char *, items_path,
	unsigned, flags);

XY_DECL(int, with_item_access,
	int, fd,
	char *, body,
	const char *, items_path,
	unsigned, flags,
	const char *, not_found_msg,
	const char *, forbidden_msg,
	item_handler_cb, cb,
	void *, user);

/* CSRF helpers */
XY_DECL(int, csrf_generate_token, char *, out, size_t, len);
XY_DECL(int, csrf_set_cookie, int, fd, char *, out, size_t, len);
XY_DECL(int, csrf_validate, int, fd, const char *, submitted);

#endif /* ITEM_IMPL */

#endif
