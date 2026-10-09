#ifndef AUTH_TYPES_H
#define AUTH_TYPES_H

/*
 * mods/auth/auth-types.h — Shared types, structs, and context definitions
 * for mods/auth. Contains zero XY_DECLs.
 */

#include <stddef.h>
#include <ttypt/xy.h>

#ifndef PATH_MAX
#include <limits.h>
#endif

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

#endif /* AUTH_TYPES_H */
