#include <ttypt/ndx-mod.h>

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <fcntl.h>

#include <ttypt/axil.h>
#include <ttypt/axil-ndx.h>

#define ITEM_IMPL
#include "auth.h"
#undef ITEM_IMPL

#include "../common/common.h"
#include "ux/all.c"

/* ------------------------------------------------------------------ */
/* CSRF helpers                                                         */
/* ------------------------------------------------------------------ */

NDX_LISTENER(int, csrf_generate_token, char *, out, size_t, len)
{
	unsigned char buf[16];
	int urfd;
	size_t i;
	ssize_t n;

	if (!out || len < 33)
		return 0;

	urfd = open("/dev/urandom", O_RDONLY);
	if (urfd < 0) {
		out[0] = '\0';
		return 0;
	}
	n = read(urfd, buf, sizeof(buf));
	close(urfd);
	if (n != (ssize_t)sizeof(buf)) {
		out[0] = '\0';
		return 0;
	}
	for (i = 0; i < sizeof(buf); i++)
		snprintf(out + i * 2, 3, "%02x", buf[i]);
	out[32] = '\0';
	return 0;
}

NDX_LISTENER(int, csrf_set_cookie, int, fd, char *, out, size_t, len)
{
	char token[33];
	char header[80];
	char cookie_hdr[512] = { 0 };
	char *p;
	char *eq;
	char *end;
	size_t vlen;

	token[0] = '\0';
	axil_env_get(fd, cookie_hdr, "HTTP_COOKIE");
	p = strstr(cookie_hdr, "csrf_token=");
	if (p) {
		eq = p + strlen("csrf_token=");
		end = strchr(eq, ';');
		vlen = end ? (size_t)(end - eq) : strlen(eq);
		if (vlen == 32) {
			memcpy(token, eq, 32);
			token[32] = '\0';
		}
	}

	if (!token[0])
		csrf_generate_token(token, sizeof(token));

	snprintf(
	        header,
	        sizeof(header),
	        "csrf_token=%s; Path=/; SameSite=Strict",
	        token);
	axil_header_set(fd, "Set-Cookie", header);
	if (out && len > 0)
		snprintf(out, len, "%s", token);
	return 0;
}

NDX_LISTENER(int, csrf_validate, int, fd, const char *, submitted)
{
	char cookie_hdr[512];
	char *p;
	char *eq;
	char *end;
	size_t vlen;
	size_t slen;

	if (!submitted || !submitted[0])
		return -1;

	cookie_hdr[0] = '\0';
	axil_env_get(fd, cookie_hdr, "HTTP_COOKIE");

	p = strstr(cookie_hdr, "csrf_token=");
	if (!p)
		return -1;
	eq = p + strlen("csrf_token=");
	end = strchr(eq, ';');
	vlen = end ? (size_t)(end - eq) : strlen(eq);
	slen = strlen(submitted);
	if (vlen != slen)
		return -1;
	return memcmp(eq, submitted, vlen) == 0 ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Ownership helpers                                                    */
/* ------------------------------------------------------------------ */

static void build_owner_path(const char *ip, char *out, size_t len)
{
	snprintf(out, len, "%s/owner", ip);
}

NDX_LISTENER(int, item_check_ownership,
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
		if (fp) {
			char owner[64] = { 0 };
			if (fgets(owner, sizeof(owner) - 1, fp))
				owner[strcspn(owner, "\n")] = '\0';
			fclose(fp);
			if (owner[0] && strcmp(owner, username) == 0)
				return 1;
		}

		struct stat st;
		if (stat(item_path, &st) != 0)
			return 0;
		if (geteuid() != st.st_uid)
			return 0;
		int uid = auth_get_uid(username);
		return uid >= 0 && (uid_t)uid == st.st_uid;
	}
}

NDX_LISTENER(item_access_t, item_access_status,
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

NDX_LISTENER(int, item_require_access,
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

NDX_LISTENER(int, item_ctx_load,
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
	axil_env_get(fd, ctx->id, "PATTERN_PARAM_ID");

	if (flags & ICTX_SONG_ID)
		axil_env_get(fd, ctx->song_id, "PATTERN_PARAM_SONG_ID");

	if (!ctx->id[0] || ((flags & ICTX_SONG_ID) && !ctx->song_id[0])) {
		bad_request(fd, "Missing parameters");
		return 1;
	}

	snprintf(
	        ctx->item_path,
	        sizeof(ctx->item_path),
	        "%s/%s/%s",
	        ctx->doc_root,
	        items_path,
	        ctx->id);

	if (flags & ICTX_NEED_OWNERSHIP) {
		if (item_require_access(
		            fd,
		            ctx->item_path,
		            ctx->username,
		            flags,
		            "Not found",
		            "Forbidden"))
			return 1;
	}

	return 0;
}

NDX_LISTENER(int, with_item_access,
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
	unsigned load_flags = flags & ~ICTX_NEED_OWNERSHIP;

	if (!cb)
		return respond_error(fd, 500, "Missing item handler");

	if (item_ctx_load(&ctx, fd, items_path, load_flags))
		return 1;

	if (item_require_access(
	            fd,
	            ctx.item_path,
	            ctx.username,
	            flags,
	            not_found_msg,
	            forbidden_msg))
		return 1;

	return cb(fd, body, &ctx, user);
}

/* ── HTTP response helper (moved from auth_fe.c) ───────────── */

static void
auth_send_html(int fd, uint16_t status, const char *title, bud_node *layout)
{
	char *html = site_ui_page(title, NULL, NULL, layout);

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
		auth_send_html(fd, (uint16_t)status, "Login", layout);
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
	csrf_set_cookie(fd, token, sizeof(token));
	axil_header_set(fd, "Content-Type", "text/plain");
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
	axil_env_get(fd, qs, "QUERY_STRING");
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
		auth_send_html(fd, 200, "Login", layout);
	}
	return 0;
}

static int register_get_handler(int fd, char *body)
{
	(void)body;
	bud_node *layout = auth_render_register(get_request_user(fd));
	auth_send_html(fd, 200, "Register", layout);
	return 0;
}

void ndx_install(void)
{
	ndx_load("./mods/index/index");
	ndx_load("./mods/common/common");
	ndx_load("axil-auth");
	axil_register_handler("GET:/api/csrf", csrf_endpoint_handler);
	axil_register_handler("GET:/auth/login", login_get_handler);
	axil_register_handler("GET:/auth/register", register_get_handler);
	auth_init();
}
