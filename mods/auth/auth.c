#include <ttypt/xy-mod.h>
#include <ttypt/xy.h>
#include <ttypt/qmap.h>

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <fcntl.h>

#include <ttypt/axil.h>
#include <ttypt/axil-xy.h>

#include "../index/index.h"

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

XY_IMPL(int, item_check_ownership,
	const char *, item_path,
	const char *, username)
{
	if (!username || !*username)
		return 0;

	if (geteuid() == 0) {
		struct stat st;
		if (stat(item_path, &st) != 0)
			return 0;
		int uid = auth_get_uid(username);
		return uid >= 0 && (uid_t)uid == st.st_uid;
	} else {
		char owner_path[1024];
		build_owner_path(item_path, owner_path, sizeof(owner_path));
		FILE *fp = fopen(owner_path, "r");
		if (!fp)
			return 0;
		char owner[64] = { 0 };
		if (fgets(owner, sizeof(owner) - 1, fp))
			owner[strcspn(owner, "\n")] = '\0';
		fclose(fp);
		if (owner[0] && strcmp(owner, username) == 0)
			return 1;
		return 0;
	}
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
	    !item_check_ownership(item_path, username))
		return ITEM_ACCESS_FORBIDDEN;

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

XY_IMPL(int, item_ctx_load,
	item_ctx_t *, ctx,
	int, fd,
	const char *, items_path,
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

	if (flags & ICTX_SONG_ID)
		axil_env_get(
		        fd, ctx->song_id, sizeof(ctx->song_id),
		        "PATTERN_PARAM_SONG_ID");

	if (!ctx->id[0] || ((flags & ICTX_SONG_ID) && !ctx->song_id[0])) {
		bad_request(fd, "Missing parameters");
		return 1;
	}

	if (!is_safe_id(ctx->id)) {
		bad_request(fd, "Invalid id");
		return 1;
	}

	snprintf(
	        ctx->item_path, sizeof(ctx->item_path), "%s/%s/%s",
	        ctx->doc_root, items_path, ctx->id);

	if (flags & ICTX_NEED_OWNERSHIP) {
		if (item_require_access(
		            fd, ctx->item_path, ctx->username, flags,
		            "Not found", "Forbidden"))
			return 1;
	}

	return 0;
}

XY_IMPL(int, with_item_access,
	int, fd,
	char *, body,
	const char *, items_path,
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

	if (item_ctx_load(&ctx, fd, items_path, load_flags))
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
	if (strstr(accept, "text/html")) {
		const char *user = get_request_user(fd);
		bud_node *layout = auth_render_login(user, redirect, msg);
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
	xy_load("./mods/index/index");
	xy_load("./mods/common/common");
	xy_load("libaxil-auth");
	axil_register_handler("GET:/api/csrf", csrf_endpoint_handler);
	axil_register_handler("GET:/auth/login", login_get_handler);
	axil_register_handler("GET:/auth/register", register_get_handler);
	auth_init();
}
