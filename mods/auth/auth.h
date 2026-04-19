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
	int         fd;
	const char *username;
	char        doc_root[256];
	char        id[128];
	char        song_id[128];
	char        item_path[PATH_MAX - 512];
} item_ctx_t;

#define ICTX_NEED_LOGIN      0x1  /* require logged-in user; else 401 */
#define ICTX_NEED_OWNERSHIP  0x2  /* require item ownership; else 403/404 */
#define ICTX_SONG_ID         0x4  /* also read PATTERN_PARAM_SONG_ID */

#ifndef AUTH_IMPL

/* Session helpers */
NDX_DECL(int, get_cookie, const char *, cookie, char *, token, size_t, len);
NDX_DECL(const char *, get_session_user, const char *, token);
NDX_DECL(int, get_user_uid, const char *, username);
NDX_DECL(const char *, get_request_user, int, fd);
NDX_DECL(int, require_login, int, fd, const char *, username);

/* Ownership helpers */

/* Record ownership of item_path for username.
 * When root: chown to uid. When non-root: write owner file. */
NDX_DECL(int, item_record_ownership, const char *, item_path, const char *, username);

/* Check if username owns item_path.
 * When root: compare stat uid. When non-root: read owner file. */
NDX_DECL(int, item_check_ownership, const char *, item_path, const char *, username);

/* Send 403 if username does not own item_path. Returns non-zero on failure.
 * msg may be NULL to use the default "Forbidden". */
NDX_DECL(int, require_ownership, int, fd, const char *, item_path, const char *, username, const char *, msg);

/* Read owner name into out (display use).
 * When root: getpwuid_r. When non-root: read owner file. */
NDX_DECL(int, item_read_owner, const char *, item_path, char *, out, size_t, outlen);

/* Unlink owner file (non-root only); call before rmdir. */
NDX_DECL(int, item_unlink_owner, const char *, item_path);

/* Populate an item_ctx_t from the request; enforces flags and responds
 * with an appropriate error on failure. Returns 0 on success, non-zero
 * if the handler should return (response already sent). */
NDX_DECL(int, item_ctx_load,
	item_ctx_t *, ctx, int, fd,
	const char *, items_path, unsigned, flags);

#endif /* AUTH_IMPL */

#endif
