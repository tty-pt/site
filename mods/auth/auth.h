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
 * Standard preamble for handlers operating on a module item. Populated by
 * module_item_ctx_load(), which resolves the storage path and also enforces
 * login/ownership and writes error responses on failure.
 * ------------------------------------------------------------------------- */

typedef struct item_ctx_s {
	int fd;
	const char *username;
	char doc_root[256];
	char id[128];
	char sub_id[128];
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
#define ICTX_SUB_ID                                                            \
	0x4                /* also read secondary pattern param                \
	                      (PATTERN_PARAM_SUB_ID/CHILD_ID/SONG_ID) */
#define ICTX_CSRF_MPFD 0x8 /* validate CSRF token from multipart form data */
#define ICTX_CSRF_QUERY                                                        \
	0x10 /* validate CSRF token from query string / url-encoded body */
#define ICTX_NEED_READ_ACCESS 0x20 /* explicitly require read access (owner or group member if private) */

#include <ttypt/auth.h>

#ifndef ITEM_IMPL

/* Canonical ownership operations. In production (chrooted / root /
 * AUTH_ENV=prod), ownership is determined strictly by POSIX disk permissions
 * (stat st_uid / chown). In dev mode (unprivileged non-root), owner files are
 * used as a fallback. */
XY_DECL(int, auth_get_username_by_uid,
	int, uid,
	char *, out,
	size_t, len);
XY_DECL(int, item_owner_record,
	const char *, item_path,
	const char *, username);
XY_DECL(int, item_owner_read,
	const char *, item_path,
	char *, out,
	size_t, out_sz);
XY_DECL(int, item_owner_check,
	const char *, item_path,
	const char *, username);
XY_DECL(int, module_item_owner_record,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, username);
XY_DECL(int, module_item_owner_check,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, username);

/* Canonical group operations. In production, groups map to POSIX GIDs (/etc/group). */
XY_DECL(int, item_group_record,
	const char *, item_path,
	const char *, grp_name);
XY_DECL(int, item_group_read,
	const char *, item_path,
	char *, out,
	size_t, out_sz);
XY_DECL(int, item_group_check,
	const char *, item_path,
	const char *, username);
XY_DECL(int, item_is_private,
	const char *, item_path);
XY_DECL(int, item_can_read,
	const char *, item_path,
	const char *, username);
XY_DECL(int, item_can_write,
	const char *, item_path,
	const char *, username);

XY_DECL(int, module_item_group_record,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, grp_name);
XY_DECL(int, module_item_group_check,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, username);
XY_DECL(int, module_item_can_read,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, username);
XY_DECL(int, module_item_can_write,
	int, fd,
	const char *, module,
	const char *, id,
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
XY_DECL(int, module_item_ctx_load,
	item_ctx_t *, ctx,
	int, fd,
	const char *, module,
	unsigned, flags);

XY_DECL(int, with_module_item_access,
	int, fd,
	char *, body,
	const char *, module,
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
