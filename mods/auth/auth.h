#ifndef MOD_AUTH_H
#define MOD_AUTH_H

#include <stddef.h>
#include <ttypt/ndx-mod.h>

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

#endif
