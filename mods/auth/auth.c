#include <ttypt/xy-mod.h>
#include <ttypt/xy.h>
#include <ttypt/corm.h>

#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <ttypt/axil.h>
#include <ttypt/axil-xy.h>

#include "../common/common.h"
#include "../i18n/i18n.h"
#include "../mpfd/mpfd.h"
#include "../source/source.h"
#include "auth-types.h"
#include "auth-oauth-hook.h"
#include "dict.h"
#include "site_ui.h"
bud_node *auth_render_login(const char *user, const char *ret, const char *error);
bud_node *auth_render_register(const char *user);

/* ------------------------------------------------------------------ */
/* CSRF helpers                                                         */
/* ------------------------------------------------------------------ */

static int csrf_find_token(const char *cookie, char *out, size_t len)
{
	const char *p;

	out[0] = '\0';
	if (!cookie || !*cookie)
		return -1;
	p = cookie;
	while (*p) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (strncmp(p, "csrf_token=", 11) == 0) {
			const char *val = p + 11;
			const char *end = val;
			size_t vlen;

			while (*end && *end != ';')
				end++;
			vlen = (size_t)(end - val);
			if (vlen >= len)
				vlen = len - 1;
			memcpy(out, val, vlen);
			out[vlen] = '\0';
			return 0;
		}
		while (*p && *p != ';')
			p++;
		if (*p == ';')
			p++;
	}
	return -1;
}

static int
csrf_ct_compare(const char *a, size_t alen, const char *b, size_t blen)
{
	unsigned char diff = 0;
	size_t i;

	if (alen != blen)
		return -1;
	for (i = 0; i < alen; i++)
		diff |= (unsigned char)(a[i] ^ b[i]);
	return diff == 0 ? 0 : -1;
}

XY_IMPL(int, csrf_generate_token, char *, out, size_t, len)
{
	unsigned char buf[16];
	int urfd;
	size_t i;
	ssize_t n;

	if (!out || len < 33)
		return -1;

	urfd = open("/dev/urandom", O_RDONLY);
	if (urfd < 0) {
		out[0] = '\0';
		return -1;
	}
	n = read(urfd, buf, sizeof(buf));
	close(urfd);
	if (n != (ssize_t)sizeof(buf)) {
		out[0] = '\0';
		return -1;
	}
	for (i = 0; i < sizeof(buf); i++)
		snprintf(out + i * 2, 3, "%02x", buf[i]);
	out[32] = '\0';
	return 0;
}

XY_IMPL(int, csrf_set_cookie, int, fd, char *, out, size_t, len)
{
	char token[33];
	char header[96];
	char cookie_hdr[512] = { 0 };

	token[0] = '\0';
	axil_env_get(fd, cookie_hdr, sizeof(cookie_hdr), "HTTP_COOKIE");
	csrf_find_token(cookie_hdr, token, sizeof(token));

	if (!token[0])
		if (csrf_generate_token(token, sizeof(token)) != 0)
			return -1;

	snprintf(
	        header, sizeof(header),
	        "csrf_token=%s; Path=/; SameSite=Strict; HttpOnly", token);
	axil_header_set(fd, "Set-Cookie", header);
	if (out && len > 0)
		snprintf(out, len, "%s", token);
	return 0;
}

XY_IMPL(int, csrf_validate, int, fd, const char *, submitted)
{
	char cookie_hdr[512];
	char cookie_val[33];

	if (!submitted || !submitted[0])
		return -1;

	cookie_hdr[0] = '\0';
	axil_env_get(fd, cookie_hdr, sizeof(cookie_hdr), "HTTP_COOKIE");

	if (csrf_find_token(cookie_hdr, cookie_val, sizeof(cookie_val)) != 0)
		return -1;

	return csrf_ct_compare(
	        cookie_val, strlen(cookie_val), submitted, strlen(submitted));
}

/* ------------------------------------------------------------------ */
/* Ownership helpers                                                    */
/* ------------------------------------------------------------------ */

static int is_disk_permission_mode(void)
{
	const char *disk_perms = getenv("AUTH_DISK_PERMS");
	if (disk_perms)
		return (strcmp(disk_perms, "1") == 0 ||
		        strcmp(disk_perms, "true") == 0);

	if (geteuid() == 0)
		return 1;

	const char *env = getenv("AUTH_ENV");
	if (env && strcmp(env, "prod") == 0)
		return 1;

	return 0;
}

XY_IMPL(int, auth_get_username_by_uid,
	int, uid,
	char *, out,
	size_t, len)
{
	return auth_get_username(uid, out, len);
}

XY_IMPL(int, item_owner_record,
	const char *, item_path,
	const char *, username)
{
	char owner_path[PATH_MAX];
	int uid;

	if (!item_path || !item_path[0] || !username || !username[0])
		return -1;
	uid = auth_get_uid(username);
	if (uid < 0)
		return -1;

	if (is_disk_permission_mode()) {
		if (geteuid() == 0) {
			if (chown(item_path, (uid_t)uid,
			          (gid_t)auth_www_gid()) != 0 &&
			    chown(item_path, (uid_t)uid, (gid_t)-1) != 0)
				return -1;
		}
		if (build_owner_path(
		            item_path, owner_path, sizeof(owner_path)) == 0)
			unlink(owner_path);
		return 0;
	}

	if (build_owner_path(item_path, owner_path, sizeof(owner_path)) != 0)
		return -1;
	if (write_file_path(owner_path, username, strlen(username)) != 0)
		return -1;
	return 0;
}

XY_IMPL(int, item_owner_read,
	const char *, item_path,
	char *, out,
	size_t, out_sz)
{
	char owner_path[PATH_MAX];
	char *owner;
	size_t len;
	size_t full_len;
	size_t i;
	struct stat st;

	if (out && out_sz > 0)
		out[0] = '\0';
	if (!item_path || !item_path[0] || !out || out_sz == 0)
		return -1;

	if (is_disk_permission_mode()) {
		if (stat(item_path, &st) != 0)
			return -1;
		return auth_get_username((int)st.st_uid, out, out_sz);
	}

	if (build_owner_path(item_path, owner_path, sizeof(owner_path)) != 0)
		return -1;
	owner = slurp_file(owner_path);
	if (!owner)
		return -1;
	full_len = strlen(owner);
	len = strcspn(owner, "\r\n");
	if (len == 0 || len >= out_sz) {
		free(owner);
		return -1;
	}
	for (i = len; i < full_len; i++) {
		if (owner[i] != '\r' && owner[i] != '\n') {
			free(owner);
			return -1;
		}
	}
	memcpy(out, owner, len);
	out[len] = '\0';
	free(owner);
	return 0;
}

XY_IMPL(int, item_owner_check,
	const char *, item_path,
	const char *, username)
{
	struct stat st;
	int uid;
	char owner[128];

	if (!username || !username[0] || !item_path || !item_path[0])
		return 0;

	if (is_disk_permission_mode()) {
		if (stat(item_path, &st) != 0)
			return 0;
		uid = auth_get_uid(username);
		if (uid < 0)
			return 0;
		return ((uid_t)uid == st.st_uid) ? 1 : 0;
	}

	if (item_owner_read(item_path, owner, sizeof(owner)) != 0)
		return 0;
	return (strcmp(owner, username) == 0) ? 1 : 0;
}

XY_IMPL(int, module_item_owner_record,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, username)
{
	char item_path[PATH_MAX];

	if (item_path_build(fd, module, id, item_path, sizeof(item_path)) != 0)
		return -1;
	return item_owner_record(item_path, username);
}

XY_IMPL(int, module_item_owner_check,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, username)
{
	char item_path[PATH_MAX];

	if (item_path_build(fd, module, id, item_path, sizeof(item_path)) != 0)
		return 0;
	return item_owner_check(item_path, username);
}

/* ------------------------------------------------------------------ */
/* Group helpers                                                      */
/* ------------------------------------------------------------------ */

XY_IMPL(int, item_group_record,
	const char *, item_path,
	const char *, grp_name)
{
	char group_path[PATH_MAX];
	int gid;

	if (!item_path || !item_path[0] || !grp_name || !grp_name[0])
		return -1;

	gid = auth_get_gid(grp_name);
	if (gid < 0)
		gid = auth_create_group(grp_name);

	if (is_disk_permission_mode()) {
		if (gid > 0 && geteuid() == 0) {
			if (chown(item_path, (uid_t)-1, (gid_t)gid) != 0)
				return -1;
		}
		snprintf(group_path, sizeof(group_path), "%s/group", item_path);
		unlink(group_path);
		return 0;
	}

	snprintf(group_path, sizeof(group_path), "%s/group", item_path);
	if (write_file_path(group_path, grp_name, strlen(grp_name)) != 0)
		return -1;
	return 0;
}

XY_IMPL(int, item_group_read,
	const char *, item_path,
	char *, out,
	size_t, out_sz)
{
	char group_path[PATH_MAX];
	char *grp;
	size_t len;
	struct stat st;

	if (out && out_sz > 0)
		out[0] = '\0';
	if (!item_path || !item_path[0] || !out || out_sz == 0)
		return -1;

	if (is_disk_permission_mode()) {
		if (stat(item_path, &st) != 0)
			return -1;
		return auth_get_grpname((int)st.st_gid, out, out_sz);
	}

	snprintf(group_path, sizeof(group_path), "%s/group", item_path);
	grp = slurp_file(group_path);
	if (!grp)
		return -1;
	len = strcspn(grp, "\r\n");
	if (len == 0 || len >= out_sz) {
		free(grp);
		return -1;
	}
	memcpy(out, grp, len);
	out[len] = '\0';
	free(grp);
	return 0;
}

XY_IMPL(int, item_group_check,
	const char *, item_path,
	const char *, username)
{
	char grp_name[128];
	if (!item_path || !item_path[0] || !username || !username[0])
		return 0;
	if (item_group_read(item_path, grp_name, sizeof(grp_name)) != 0)
		return 0;
	return auth_user_in_group(username, grp_name);
}

XY_IMPL(int, item_is_private,
	const char *, item_path)
{
	char priv_path[PATH_MAX];
	struct stat st;

	if (!item_path || !item_path[0])
		return 0;

	if (is_disk_permission_mode()) {
		if (stat(item_path, &st) != 0)
			return 0;
		return ((st.st_mode & 0007) == 0) ? 1 : 0;
	}

	snprintf(priv_path, sizeof(priv_path), "%s/private", item_path);
	if (access(priv_path, F_OK) == 0)
		return 1;

	if (stat(item_path, &st) == 0 && (st.st_mode & 0007) == 0)
		return 1;

	return 0;
}

XY_IMPL(int, item_can_write,
	const char *, item_path,
	const char *, username)
{
	if (!username || !username[0])
		return 0;
	return item_owner_check(item_path, username);
}

XY_IMPL(int, item_can_read,
	const char *, item_path,
	const char *, username)
{
	if (!item_path || !item_path[0])
		return 0;

	if (!item_is_private(item_path))
		return 1;

	if (!username || !username[0])
		return 0;

	if (item_owner_check(item_path, username))
		return 1;

	if (item_group_check(item_path, username))
		return 1;

	return 0;
}

XY_IMPL(int, module_item_group_record,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, grp_name)
{
	char item_path[PATH_MAX];

	if (item_path_build(fd, module, id, item_path, sizeof(item_path)) != 0)
		return -1;
	return item_group_record(item_path, grp_name);
}

XY_IMPL(int, module_item_group_check,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, username)
{
	char item_path[PATH_MAX];

	if (item_path_build(fd, module, id, item_path, sizeof(item_path)) != 0)
		return 0;
	return item_group_check(item_path, username);
}

XY_IMPL(int, module_item_can_read,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, username)
{
	char item_path[PATH_MAX];

	if (item_path_build(fd, module, id, item_path, sizeof(item_path)) != 0)
		return 0;
	return item_can_read(item_path, username);
}

XY_IMPL(int, module_item_can_write,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, username)
{
	char item_path[PATH_MAX];

	if (item_path_build(fd, module, id, item_path, sizeof(item_path)) != 0)
		return 0;
	return item_can_write(item_path, username);
}

XY_IMPL(item_access_t, item_access_status,
	const char *, item_path,
	const char *, username,
	unsigned, flags)
{
	if ((flags & ICTX_NEED_LOGIN) && (!username || !*username))
		return ITEM_ACCESS_UNAUTHENTICATED;

	struct stat st;
	if (stat(item_path, &st) != 0 || !S_ISDIR(st.st_mode))
		return ITEM_ACCESS_MISSING;

	if ((flags & ICTX_NEED_OWNERSHIP) &&
	    !item_can_write(item_path, username))
		return ITEM_ACCESS_FORBIDDEN;

	if (!item_can_read(item_path, username))
		return (username && *username) ? ITEM_ACCESS_FORBIDDEN : ITEM_ACCESS_UNAUTHENTICATED;

	return ITEM_ACCESS_OK;
}

XY_IMPL(int, item_require_access,
	int, fd,
	const char *, item_path,
	const char *, username,
	unsigned, flags,
	const char *, not_found_msg,
	const char *, forbidden_msg)
{
	item_access_t status = item_access_status(item_path, username, flags);
	switch (status) {
	case ITEM_ACCESS_OK:
		return 0;
	case ITEM_ACCESS_UNAUTHENTICATED:
		return require_login(fd, username);
	case ITEM_ACCESS_MISSING:
		return respond_error(
		        fd, 404, not_found_msg ? not_found_msg : "Not found");
	case ITEM_ACCESS_FORBIDDEN:
		return respond_error(
		        fd, 403, forbidden_msg ? forbidden_msg : "Forbidden");
	}
	return respond_error(fd, 500, "Invalid item access status");
}

/* --- Item context --- */

XY_IMPL(int, module_item_ctx_load,
	item_ctx_t *, ctx,
	int, fd,
	const char *, module,
	unsigned, flags)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->fd = fd;

	if (flags & ICTX_NEED_LOGIN) {
		ctx->username = get_request_user(fd);
		if (require_login(fd, ctx->username))
			return 1;
	} else {
		ctx->username = get_request_user(fd);
	}

	resolve_doc_root(fd, ctx->doc_root, sizeof(ctx->doc_root));
	axil_env_get(fd, ctx->id, sizeof(ctx->id), "PATTERN_PARAM_ID");

	if (flags & ICTX_SUB_ID) {
		axil_env_get(
		        fd, ctx->sub_id, sizeof(ctx->sub_id),
		        "PATTERN_PARAM_SUB_ID");
		if (!ctx->sub_id[0])
			axil_env_get(
			        fd, ctx->sub_id, sizeof(ctx->sub_id),
			        "PATTERN_PARAM_CHILD_ID");
		if (!ctx->sub_id[0])
			axil_env_get(
			        fd, ctx->sub_id, sizeof(ctx->sub_id),
			        "PATTERN_PARAM_SONG_ID");
	}

	if (!ctx->id[0] || ((flags & ICTX_SUB_ID) && !ctx->sub_id[0])) {
		bad_request(fd, "Missing parameters");
		return 1;
	}

	if (!is_safe_id(module) || !is_safe_id(ctx->id) ||
	    ((flags & ICTX_SUB_ID) && !is_safe_id(ctx->sub_id)))
	{
		bad_request(fd, "Invalid parameters");
		return 1;
	}

	if (item_path_build_root(
	            ctx->doc_root, module, ctx->id, ctx->item_path,
	            sizeof(ctx->item_path)) != 0)
	{
		server_error(fd, "Failed to resolve item path");
		return 1;
	}

	if (flags & ICTX_NEED_OWNERSHIP) {
		if (item_require_access(
		            fd, ctx->item_path, ctx->username, flags,
		            "Not found", "Forbidden"))
			return 1;
	}

	return 0;
}

XY_IMPL(int, with_module_item_access,
	int, fd,
	char *, body,
	const char *, module,
	unsigned, flags,
	const char *, not_found_msg,
	const char *, forbidden_msg,
	item_handler_cb, cb,
	void *, user)
{
	item_ctx_t ctx;
	unsigned load_flags = flags & ~(ICTX_NEED_OWNERSHIP | ICTX_CSRF_MPFD |
	                                ICTX_CSRF_QUERY);

	if (!cb)
		return respond_error(fd, 500, "Missing item handler");

	site_ui_set_locale(i18n_resolve_locale(fd));

	if (module_item_ctx_load(&ctx, fd, module, load_flags))
		return 1;

	if (item_require_access(
	            fd, ctx.item_path, ctx.username, flags, not_found_msg,
	            forbidden_msg))
		return 1;

	if (flags & ICTX_CSRF_MPFD) {
		mpfd_parse(fd, body);
		if (csrf_check_mpfd(fd))
			return 1;
	} else if (flags & ICTX_CSRF_QUERY) {
		if (csrf_check_query(fd, body))
			return 1;
	}

	return cb(fd, body, &ctx, user);
}

/* ── HTTP response helper (moved from auth_fe.c) ───────────── */

static void auth_send_html(
        int fd, uint16_t status, const char *title, const char *path,
        const char *icon, const char *user, bud_node *layout)
{
	const char *lang = i18n_resolve_locale(fd);
	site_ui_set_locale(lang);
	char *html = site_ui_page_lang(title, path, icon, user, NULL, NULL, layout, lang);

	if (html) {
		axil_header_set(fd, "Content-Type", "text/html; charset=utf-8");
		axil_respond(fd, status, html);
		bud_free_string(html);
	} else {
		axil_respond(fd, 500, "Internal Server Error");
	}
}

/* ------------------------------------------------------------------ */
/* SSR outcome hooks                                                    */
/* ------------------------------------------------------------------ */

XY_IMPL(int, on_auth_login_error,
	int, fd, int, status, const char *, msg, const char *, redirect)
{
	char accept[256] = { 0 };
	axil_header_get(fd, "Accept", accept, sizeof(accept));
	if (strstr(accept, "text/html") || strstr(accept, "*/*") || accept[0] == '\0') {
		char full_target[768] = { 0 };
		const char *target = redirect;

		if (!target || !*target) {
			char uri[512] = { 0 };
			char qs[256] = { 0 };
			axil_env_get(fd, uri, sizeof(uri), "DOCUMENT_URI");
			if (uri[0] == '/' &&
			    strncmp(uri, "/auth/login", 11) != 0 &&
			    strncmp(uri, "/auth/logout", 12) != 0 &&
			    strncmp(uri, "/auth/register", 14) != 0)
			{
				axil_env_get(
				        fd, qs, sizeof(qs), "QUERY_STRING");
				if (qs[0]) {
					snprintf(
					        full_target,
					        sizeof(full_target), "%s?%s",
					        uri, qs);
				} else {
					strncpy(full_target, uri,
					        sizeof(full_target) - 1);
				}
				target = full_target;
			}
		}

		const char *user = get_request_user(fd);
		site_ui_set_locale(i18n_resolve_locale(fd));
		bud_node *layout = auth_render_login(user, target, msg);
		auth_send_html(
		        fd, (uint16_t)status, "Login", "/auth/login", "🔑",
		        user, layout);
		return 1;
	}
	axil_header_set(fd, "Content-Type", "text/plain");
	axil_respond(fd, status, msg ? msg : "");
	return 1;
}

/* ------------------------------------------------------------------ */
/* Module init                                                          */
/* ------------------------------------------------------------------ */

static int csrf_endpoint_handler(int fd, char *body)
{
	char token[33] = { 0 };
	(void)body;

	const char *user = get_request_user(fd);
	if (!user || !user[0]) {
		axil_header_set(fd, "Content-Type", "text/plain");
		axil_respond(fd, 401, "");
		return 0;
	}

	csrf_set_cookie(fd, token, sizeof(token));
	axil_header_set(fd, "Content-Type", "text/plain");
	axil_header_set(fd, "Cache-Control", "no-store");
	axil_respond(fd, 200, token);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Generic RFC 7636 + RFC 8252 OAuth2 PKCE authorization server        */
/* ------------------------------------------------------------------ */

#define OAUTH_MAX_PENDING 128u
#define OAUTH_MAX_CODES   128u
#define OAUTH_PENDING_TTL 300
#define OAUTH_CODE_TTL    120

typedef struct {
	char   id[33];
	char   client_id[64];
	char   app_id[64];
	char   redirect_uri[256];
	char   code_challenge[128];
	char   state[128];
	time_t expires_at;
} oauth_pending_rec_t;

typedef struct {
	char   code[65];
	char   username[64];
	char   client_id[64];
	char   app_id[64];
	char   redirect_uri[256];
	char   code_challenge[128];
	time_t expires_at;
} oauth_code_rec_t;

static oauth_pending_rec_t g_oauth_pending[OAUTH_MAX_PENDING];
static oauth_code_rec_t    g_oauth_codes[OAUTH_MAX_CODES];

static void
oauth_wipe(void *p, size_t n)
{
	volatile unsigned char *b = (volatile unsigned char *)p;
	while (n--)
		*b++ = 0;
}

static int
oauth_rand_hex(char *out, size_t nbytes)
{
	unsigned char buf[32];
	int urfd;
	ssize_t r;
	size_t i;

	if (!out || nbytes == 0 || nbytes > sizeof(buf))
		return -1;
	urfd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (urfd < 0)
		return -1;
	r = read(urfd, buf, nbytes);
	close(urfd);
	if (r != (ssize_t)nbytes) {
		oauth_wipe(buf, sizeof(buf));
		return -1;
	}
	for (i = 0; i < nbytes; i++)
		snprintf(out + i * 2, 3, "%02x", buf[i]);
	out[nbytes * 2] = '\0';
	oauth_wipe(buf, sizeof(buf));
	return 0;
}

static inline uint32_t
oauth_rotr32(uint32_t x, unsigned n)
{
	return (x >> n) | (x << (32u - n));
}

static void
oauth_sha256_compress(uint32_t st[8], const uint8_t blk[64])
{
	static const uint32_t k[64] = {
		0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
		0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
		0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
		0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
		0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
		0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
		0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
		0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
		0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
		0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
		0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
		0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
		0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
		0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
		0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
		0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
	};
	uint32_t w[64];
	size_t i;

	for (i = 0; i < 16u; i++) {
		w[i] = ((uint32_t)blk[i * 4u + 0u] << 24) |
		       ((uint32_t)blk[i * 4u + 1u] << 16) |
		       ((uint32_t)blk[i * 4u + 2u] <<  8) |
		       ((uint32_t)blk[i * 4u + 3u]);
	}
	for (i = 16u; i < 64u; i++) {
		uint32_t s0 = oauth_rotr32(w[i - 15u], 7u) ^ oauth_rotr32(w[i - 15u], 18u) ^ (w[i - 15u] >> 3);
		uint32_t s1 = oauth_rotr32(w[i -  2u], 17u) ^ oauth_rotr32(w[i -  2u], 19u) ^ (w[i -  2u] >> 10);
		w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
	}

	uint32_t a = st[0], b = st[1], c = st[2], d = st[3];
	uint32_t e = st[4], f = st[5], g = st[6], h = st[7];
	for (i = 0; i < 64u; i++) {
		uint32_t S1  = oauth_rotr32(e, 6u) ^ oauth_rotr32(e, 11u) ^ oauth_rotr32(e, 25u);
		uint32_t ch  = (e & f) ^ ((~e) & g);
		uint32_t t1  = h + S1 + ch + k[i] + w[i];
		uint32_t S0  = oauth_rotr32(a, 2u) ^ oauth_rotr32(a, 13u) ^ oauth_rotr32(a, 22u);
		uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
		uint32_t t2  = S0 + maj;

		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}
	st[0] += a; st[1] += b; st[2] += c; st[3] += d;
	st[4] += e; st[5] += f; st[6] += g; st[7] += h;
	oauth_wipe(w, sizeof(w));
}

static void
oauth_sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
	uint32_t st[8] = {
		0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
		0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
	};
	size_t off = 0, rem, i;
	uint8_t tail[64];

	while (len - off >= 64u) {
		oauth_sha256_compress(st, data + off);
		off += 64u;
	}
	memset(tail, 0, sizeof(tail));
	rem = len - off;
	if (rem > 0u)
		memcpy(tail, data + off, rem);
	tail[rem] = 0x80u;
	if (rem >= 56u) {
		oauth_sha256_compress(st, tail);
		memset(tail, 0, sizeof(tail));
	}
	uint64_t bitlen = (uint64_t)len * 8ULL;
	for (i = 0; i < 8u; i++)
		tail[63u - i] = (uint8_t)((bitlen >> (i * 8u)) & 0xffu);
	oauth_sha256_compress(st, tail);

	for (i = 0; i < 8u; i++) {
		out[i * 4u + 0u] = (uint8_t)(st[i] >> 24);
		out[i * 4u + 1u] = (uint8_t)(st[i] >> 16);
		out[i * 4u + 2u] = (uint8_t)(st[i] >>  8);
		out[i * 4u + 3u] = (uint8_t)(st[i]);
	}
	oauth_wipe(tail, sizeof(tail));
	oauth_wipe(st, sizeof(st));
}

static void
oauth_b64url_32(const uint8_t in[32], char out[44])
{
	static const char tbl[65] =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	size_t i = 0, o = 0;

	while (i + 3u <= 32u) {
		uint32_t v = ((uint32_t)in[i] << 16) |
		             ((uint32_t)in[i + 1u] << 8) |
		             (uint32_t)in[i + 2u];
		out[o++] = tbl[(v >> 18) & 0x3fu];
		out[o++] = tbl[(v >> 12) & 0x3fu];
		out[o++] = tbl[(v >>  6) & 0x3fu];
		out[o++] = tbl[ v        & 0x3fu];
		i += 3u;
	}
	/* 32 % 3 == 2 remaining bytes */
	{
		uint32_t v = ((uint32_t)in[30] << 16) | ((uint32_t)in[31] << 8);
		out[o++] = tbl[(v >> 18) & 0x3fu];
		out[o++] = tbl[(v >> 12) & 0x3fu];
		out[o++] = tbl[(v >>  6) & 0x3fu];
	}
	out[o] = '\0';
}

static int
oauth_is_safe_token(const char *s, size_t min_len, size_t max_len)
{
	size_t n;
	size_t i;

	if (!s)
		return 0;
	n = strlen(s);
	if (n < min_len || n > max_len)
		return 0;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		         (c >= '0' && c <= '9') || c == '-' || c == '_' ||
		         c == '.' || c == '~';
		if (!ok)
			return 0;
	}
	return 1;
}

/* Strictly allow RFC 8252 loopback callback URIs only:
 *   http://127.0.0.1:<1..65535>/callback
 *   http://[::1]:<1..65535>/callback */
static int
oauth_is_valid_loopback_redirect(const char *uri)
{
	const char *p;
	unsigned long port = 0;
	int digits = 0;

	if (!uri)
		return 0;
	if (strncmp(uri, "http://127.0.0.1:", 17) == 0)
		p = uri + 17;
	else if (strncmp(uri, "http://[::1]:", 13) == 0)
		p = uri + 13;
	else
		return 0;

	while (*p >= '0' && *p <= '9') {
		port = port * 10UL + (unsigned long)(*p - '0');
		if (port > 65535UL)
			return 0;
		digits++;
		p++;
	}
	if (digits == 0 || port == 0UL || port > 65535UL)
		return 0;
	return strcmp(p, "/callback") == 0;
}

static int
oauth_send_json(int fd, uint16_t status, const char *json)
{
	axil_header_set(fd, "Content-Type", "application/json");
	axil_header_set(fd, "Cache-Control", "no-store");
	axil_header_set(fd, "Pragma", "no-cache");
	axil_respond(fd, status, json);
	return 0;
}

static int
oauth_issue_code_redirect(int fd,
                          const char *username,
                          const char *client_id,
                          const char *app_id,
                          const char *redirect_uri,
                          const char *code_challenge,
                          const char *state)
{
	time_t now = time(NULL);
	oauth_code_rec_t *slot = NULL;
	oauth_code_rec_t *oldest = &g_oauth_codes[0];
	size_t i;

	for (i = 0; i < OAUTH_MAX_CODES; i++) {
		if (g_oauth_codes[i].expires_at == 0 || g_oauth_codes[i].expires_at <= now) {
			oauth_wipe(&g_oauth_codes[i], sizeof(g_oauth_codes[i]));
			if (!slot)
				slot = &g_oauth_codes[i];
		} else if (g_oauth_codes[i].expires_at < oldest->expires_at) {
			oldest = &g_oauth_codes[i];
		}
	}
	if (!slot) {
		slot = oldest;
		oauth_wipe(slot, sizeof(*slot));
	}

	if (oauth_rand_hex(slot->code, 32) != 0)
		return oauth_send_json(fd, 500, "{\"error\":\"server_error\"}");

	snprintf(slot->username, sizeof(slot->username), "%s", username);
	snprintf(slot->client_id, sizeof(slot->client_id), "%s", client_id);
	snprintf(slot->app_id, sizeof(slot->app_id), "%s", app_id);
	snprintf(slot->redirect_uri, sizeof(slot->redirect_uri), "%s", redirect_uri);
	snprintf(slot->code_challenge, sizeof(slot->code_challenge), "%s", code_challenge);
	slot->expires_at = now + OAUTH_CODE_TTL;

	char loc[640];
	int ln = snprintf(loc, sizeof(loc), "%s?code=%s&state=%s",
	                  redirect_uri, slot->code, state);
	if (ln <= 0 || (size_t)ln >= sizeof(loc)) {
		oauth_wipe(slot, sizeof(*slot));
		return oauth_send_json(fd, 500, "{\"error\":\"server_error\"}");
	}
	axil_header_set(fd, "Cache-Control", "no-store");
	axil_header_set(fd, "Location", loc);
	axil_respond(fd, 303, "");
	return 0;
}

static int
oauth_authorize_handler(int fd, char *body)
{
	char qs[1024] = { 0 };
	char response_type[32] = { 0 };
	char client_id[64] = { 0 };
	char app_id[64] = { 0 };
	char redirect_uri[256] = { 0 };
	char code_challenge[128] = { 0 };
	char method[16] = { 0 };
	char state[128] = { 0 };
	const char *user;

	(void)body;
	axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");
	axil_query_parse(qs);
	axil_query_param("response_type", response_type, sizeof(response_type));
	axil_query_param("client_id", client_id, sizeof(client_id));
	axil_query_param("app_id", app_id, sizeof(app_id));
	axil_query_param("redirect_uri", redirect_uri, sizeof(redirect_uri));
	axil_query_param("code_challenge", code_challenge, sizeof(code_challenge));
	axil_query_param("code_challenge_method", method, sizeof(method));
	axil_query_param("state", state, sizeof(state));

	if (strcmp(response_type, "code") != 0 ||
	    strcmp(method, "S256") != 0 ||
	    !oauth_is_valid_loopback_redirect(redirect_uri) ||
	    !oauth_is_safe_token(code_challenge, 43, 128) ||
	    !oauth_is_safe_token(state, 16, 120))
	{
		return oauth_send_json(fd, 400, "{\"error\":\"invalid_request\"}");
	}
	if (client_id[0] && !oauth_is_safe_token(client_id, 1, 63))
		return oauth_send_json(fd, 400, "{\"error\":\"invalid_request\"}");
	if (app_id[0] && !oauth_is_safe_token(app_id, 1, 63))
		return oauth_send_json(fd, 400, "{\"error\":\"invalid_request\"}");

	user = get_request_user(fd);
	if (user && user[0]) {
		return oauth_issue_code_redirect(
		        fd, user, client_id, app_id, redirect_uri,
		        code_challenge, state);
	}

	/* Unauthenticated user: save pending authorization request and bounce
	 * through standard /auth/login with a short same-origin return path. */
	time_t now = time(NULL);
	oauth_pending_rec_t *slot = NULL;
	oauth_pending_rec_t *oldest = &g_oauth_pending[0];
	size_t i;

	for (i = 0; i < OAUTH_MAX_PENDING; i++) {
		if (g_oauth_pending[i].expires_at == 0 ||
		    g_oauth_pending[i].expires_at <= now)
		{
			oauth_wipe(&g_oauth_pending[i], sizeof(g_oauth_pending[i]));
			if (!slot)
				slot = &g_oauth_pending[i];
		} else if (g_oauth_pending[i].expires_at < oldest->expires_at) {
			oldest = &g_oauth_pending[i];
		}
	}
	if (!slot) {
		slot = oldest;
		oauth_wipe(slot, sizeof(*slot));
	}

	if (oauth_rand_hex(slot->id, 16) != 0)
		return oauth_send_json(fd, 500, "{\"error\":\"server_error\"}");

	snprintf(slot->client_id, sizeof(slot->client_id), "%s", client_id);
	snprintf(slot->app_id, sizeof(slot->app_id), "%s", app_id);
	snprintf(slot->redirect_uri, sizeof(slot->redirect_uri), "%s", redirect_uri);
	snprintf(slot->code_challenge, sizeof(slot->code_challenge), "%s", code_challenge);
	snprintf(slot->state, sizeof(slot->state), "%s", state);
	slot->expires_at = now + OAUTH_PENDING_TTL;

	char login_loc[160];
	snprintf(login_loc, sizeof(login_loc),
	         "/auth/login?ret=%%2Fauth%%2Foauth%%2Fcontinue%%3Fid%%3D%s",
	         slot->id);
	axil_header_set(fd, "Cache-Control", "no-store");
	axil_header_set(fd, "Location", login_loc);
	axil_respond(fd, 303, "");
	return 0;
}

static int
oauth_continue_handler(int fd, char *body)
{
	char qs[256] = { 0 };
	char req_id[64] = { 0 };
	const char *user;
	time_t now = time(NULL);
	oauth_pending_rec_t copy;
	int found = 0;
	size_t i;

	(void)body;
	user = get_request_user(fd);
	if (!user || !user[0]) {
		axil_header_set(fd, "Location", "/auth/login");
		axil_respond(fd, 303, "");
		return 0;
	}

	axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");
	axil_query_parse(qs);
	axil_query_param("id", req_id, sizeof(req_id));
	if (!oauth_is_safe_token(req_id, 32, 32))
		return oauth_send_json(fd, 400, "{\"error\":\"invalid_request\"}");

	memset(&copy, 0, sizeof(copy));
	for (i = 0; i < OAUTH_MAX_PENDING; i++) {
		if (g_oauth_pending[i].expires_at == 0)
			continue;
		if (g_oauth_pending[i].expires_at <= now) {
			oauth_wipe(&g_oauth_pending[i], sizeof(g_oauth_pending[i]));
			continue;
		}
		if (csrf_ct_compare(g_oauth_pending[i].id,
		                    strlen(g_oauth_pending[i].id),
		                    req_id, strlen(req_id)) == 0)
		{
			copy = g_oauth_pending[i];
			oauth_wipe(&g_oauth_pending[i], sizeof(g_oauth_pending[i]));
			found = 1;
			break;
		}
	}
	if (!found)
		return oauth_send_json(fd, 400, "{\"error\":\"expired_request\"}");

	int rc = oauth_issue_code_redirect(
	        fd, user, copy.client_id, copy.app_id, copy.redirect_uri,
	        copy.code_challenge, copy.state);
	oauth_wipe(&copy, sizeof(copy));
	return rc;
}

static int
oauth_token_handler(int fd, char *body)
{
	char grant_type[64] = { 0 };
	char code[128] = { 0 };
	char verifier[160] = { 0 };
	char redirect_uri[256] = { 0 };
	char client_id[64] = { 0 };
	char app_id[64] = { 0 };
	oauth_code_rec_t rec;
	time_t now = time(NULL);
	int found = 0;
	size_t i;

	if (!body || !body[0])
		return oauth_send_json(fd, 400, "{\"error\":\"invalid_request\"}");

	axil_query_parse(body);
	axil_query_param("grant_type", grant_type, sizeof(grant_type));
	axil_query_param("code", code, sizeof(code));
	axil_query_param("code_verifier", verifier, sizeof(verifier));
	axil_query_param("redirect_uri", redirect_uri, sizeof(redirect_uri));
	axil_query_param("client_id", client_id, sizeof(client_id));
	axil_query_param("app_id", app_id, sizeof(app_id));

	if (strcmp(grant_type, "authorization_code") != 0 ||
	    !oauth_is_safe_token(code, 32, 127) ||
	    !oauth_is_safe_token(verifier, 43, 128) ||
	    !oauth_is_valid_loopback_redirect(redirect_uri))
	{
		oauth_wipe(verifier, sizeof(verifier));
		oauth_wipe(code, sizeof(code));
		return oauth_send_json(fd, 400, "{\"error\":\"invalid_request\"}");
	}

	memset(&rec, 0, sizeof(rec));
	for (i = 0; i < OAUTH_MAX_CODES; i++) {
		if (g_oauth_codes[i].expires_at == 0)
			continue;
		if (g_oauth_codes[i].expires_at <= now) {
			oauth_wipe(&g_oauth_codes[i], sizeof(g_oauth_codes[i]));
			continue;
		}
		if (csrf_ct_compare(g_oauth_codes[i].code,
		                    strlen(g_oauth_codes[i].code),
		                    code, strlen(code)) == 0)
		{
			rec = g_oauth_codes[i];
			/* Single-use: consume immediately even if verification fails. */
			oauth_wipe(&g_oauth_codes[i], sizeof(g_oauth_codes[i]));
			found = 1;
			break;
		}
	}
	oauth_wipe(code, sizeof(code));

	if (!found || strcmp(rec.redirect_uri, redirect_uri) != 0 ||
	    (client_id[0] && rec.client_id[0] && strcmp(rec.client_id, client_id) != 0) ||
	    (app_id[0] && rec.app_id[0] && strcmp(rec.app_id, app_id) != 0))
	{
		oauth_wipe(verifier, sizeof(verifier));
		oauth_wipe(&rec, sizeof(rec));
		return oauth_send_json(fd, 400, "{\"error\":\"invalid_grant\"}");
	}

	uint8_t digest[32];
	char computed[44];
	oauth_sha256((const uint8_t *)verifier, strlen(verifier), digest);
	oauth_wipe(verifier, sizeof(verifier));
	oauth_b64url_32(digest, computed);
	oauth_wipe(digest, sizeof(digest));

	if (csrf_ct_compare(computed, strlen(computed),
	                    rec.code_challenge, strlen(rec.code_challenge)) != 0)
	{
		oauth_wipe(computed, sizeof(computed));
		oauth_wipe(&rec, sizeof(rec));
		return oauth_send_json(fd, 400, "{\"error\":\"invalid_grant\"}");
	}
	oauth_wipe(computed, sizeof(computed));

	char minted[256] = { 0 };
	uint64_t expires_in = 0;
	int mrc = on_oauth_mint_token(
	        rec.username, rec.client_id, rec.app_id,
	        minted, sizeof(minted), &expires_in);
	int hook_status = xy.last(NULL);
	oauth_wipe(&rec, sizeof(rec));

	if (hook_status != XY_OK) {
		oauth_wipe(minted, sizeof(minted));
		return oauth_send_json(fd, 501, "{\"error\":\"unsupported_grant_type\"}");
	}
	if (mrc != 0 || !minted[0] || !oauth_is_safe_token(minted, 1, 255)) {
		oauth_wipe(minted, sizeof(minted));
		return oauth_send_json(fd, 403, "{\"error\":\"access_denied\"}");
	}

	char resp[512];
	int rn = snprintf(resp, sizeof(resp),
	                  "{\"access_token\":\"%s\",\"token_type\":\"Bearer\",\"expires_in\":%" PRIu64 "}",
	                  minted, expires_in);
	oauth_wipe(minted, sizeof(minted));
	if (rn <= 0 || (size_t)rn >= sizeof(resp)) {
		oauth_wipe(resp, sizeof(resp));
		return oauth_send_json(fd, 500, "{\"error\":\"server_error\"}");
	}
	int ret_rc = oauth_send_json(fd, 200, resp);
	oauth_wipe(resp, sizeof(resp));
	return ret_rc;
}

static int login_get_handler(int fd, char *body)
{
	char qs[512] = { 0 };
	char ret[256] = { 0 };
	const char *user;

	(void)body;
	user = get_request_user(fd);
	axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");
	if (qs[0]) {
		axil_query_parse(qs);
		axil_query_param("ret", ret, sizeof(ret));
		if (ret[0] != '/' || ret[1] == '/')
			ret[0] = '\0';
	}
	{
		site_ui_set_locale(i18n_resolve_locale(fd));
		bud_node *layout = auth_render_login(user, ret, NULL);
		auth_send_html(
		        fd, 200, "Login", "/auth/login", "🔑", user, layout);
	}
	return 0;
}

static int register_get_handler(int fd, char *body)
{
	(void)body;
	site_ui_set_locale(i18n_resolve_locale(fd));
	bud_node *layout = auth_render_register(get_request_user(fd));
	auth_send_html(
	        fd, 200, "Register", "/auth/register", "📝",
	        get_request_user(fd), layout);
	return 0;
}

void xy_install(void)
{
	/* removed xy_load("./mods/index/index") to break auth↔index cycle; auth
	 * is independent but depends on common */
	xy_load("./mods/common/common");
	if (xy_load("libaxil-auth") < 0) { /* external — not in ./mods */
		fprintf(stderr, "auth: xy_load(\"libaxil-auth\") failed\n");
		abort();
	}
	i18n_register_dict(auth_dict, AUTH_DICT_COUNT);
	axil_register_handler("GET:/api/csrf", csrf_endpoint_handler);
	axil_register_handler("GET:/auth/login", login_get_handler);
	axil_register_handler("GET:/auth/register", register_get_handler);
	axil_register_handler("GET:/auth/oauth/authorize", oauth_authorize_handler);
	axil_register_handler("GET:/auth/oauth/continue", oauth_continue_handler);
	axil_register_handler("POST:/auth/oauth/token", oauth_token_handler);
}
