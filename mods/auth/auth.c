#include <ttypt/xy-mod.h>
#include <ttypt/xy.h>
#include <ttypt/qmap.h>

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <ttypt/axil.h>
#include <ttypt/axil-xy.h>

#include "../common/common.h"
#include "../mpfd/mpfd.h"
#include "../source/source.h"

#define ITEM_IMPL
#include "auth.h"
#undef ITEM_IMPL

#include "../common/common.h"
#include "ux/all.c"

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
			          (gid_t)auth_config.www_gid) != 0 &&
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
	char *html = site_ui_page(title, path, icon, user, NULL, NULL, layout);

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

int on_auth_login_error(
        int fd, int status, const char *msg, const char *redirect)
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

static int login_get_handler(int fd, char *body)
{
	char qs[512] = { 0 };
	char ret[256] = { 0 };
	const char *user;
	char *p;
	char *end;

	(void)body;
	user = get_request_user(fd);
	axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");
	p = strstr(qs, "ret=");
	if (p) {
		p += 4;
		end = strchr(p, '&');
		if (end) {
			size_t n = (size_t)(end - p);

			if (n >= sizeof(ret))
				n = sizeof(ret) - 1;
			memcpy(ret, p, n);
			ret[n] = '\0';
		} else {
			strncpy(ret, p, sizeof(ret) - 1);
		}
	}
	{
		bud_node *layout = auth_render_login(user, ret, NULL);
		auth_send_html(
		        fd, 200, "Login", "/auth/login", "🔑", user, layout);
	}
	return 0;
}

static int register_get_handler(int fd, char *body)
{
	(void)body;
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
	xy_load("libaxil-auth"); /* external — not in ./mods */
	axil_register_handler("GET:/api/csrf", csrf_endpoint_handler);
	axil_register_handler("GET:/auth/login", login_get_handler);
	axil_register_handler("GET:/auth/register", register_get_handler);
	auth_init();
}
