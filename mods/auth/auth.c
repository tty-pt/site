#include <ttypt/ndx-mod.h>

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>

#include <ttypt/ndc.h>
#include <ttypt/ndc-ndx.h>

#define ITEM_IMPL
#include "auth.h"
#undef ITEM_IMPL

#include "../common/common.h"
#include "../ssr/ssr.h"

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static void
build_owner_path(const char *ip, char *out, size_t len) {
	snprintf(out, len, "%s/owner", ip);
}

/* ------------------------------------------------------------------ */
/* Ownership helpers                                                    */
/* ------------------------------------------------------------------ */

NDX_LISTENER(int, item_record_ownership,
             const char *, item_path, const char *, username) {
	if (geteuid() == 0) {
		int uid = auth_get_uid(username);
		if (uid >= 0)
			chown(item_path, (uid_t)uid, (gid_t)-1);
	} else {
		char owner_path[1024];
		build_owner_path(item_path, owner_path, sizeof(owner_path));
		FILE *fp = fopen(owner_path, "w");
		if (fp) {
			fwrite(username, 1, strlen(username), fp);
			fclose(fp);
		}
	}
	return 0;
}

NDX_LISTENER(int, item_check_ownership,
             const char *, item_path, const char *, username) {
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
		char owner[64] = {0};
		if (fgets(owner, sizeof(owner) - 1, fp))
			owner[strcspn(owner, "\n")] = '\0';
		fclose(fp);
		return owner[0] && strcmp(owner, username) == 0;
	}
}

NDX_LISTENER(int, item_read_owner,
             const char *, item_path, char *, out, size_t, outlen) {
	if (!out || outlen == 0)
		return -1;
	out[0] = '\0';

	if (geteuid() == 0) {
		struct stat st;
		if (stat(item_path, &st) != 0)
			return -1;
		char buf[4096];
		struct passwd pw, *result = NULL;
		if (getpwuid_r(st.st_uid, &pw, buf, sizeof(buf), &result) == 0 && result) {
			strncpy(out, result->pw_name, outlen - 1);
			out[outlen - 1] = '\0';
			return 0;
		}
		return -1;
	} else {
		char owner_path[1024];
		build_owner_path(item_path, owner_path, sizeof(owner_path));
		FILE *fp = fopen(owner_path, "r");
		if (!fp)
			return -1;
		if (fgets(out, (int)outlen, fp))
			out[strcspn(out, "\n")] = '\0';
		else
			out[0] = '\0';
		fclose(fp);
		return out[0] ? 0 : -1;
	}
}

NDX_LISTENER(int, item_unlink_owner, const char *, item_path) {
	if (geteuid() != 0) {
		char owner_path[1024];
		build_owner_path(item_path, owner_path, sizeof(owner_path));
		unlink(owner_path);
	}
	return 0;
}

NDX_LISTENER(item_access_t, item_access_status,
             const char *, item_path, const char *, username, unsigned, flags) {
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
             int, fd, const char *, item_path, const char *, username, unsigned, flags,
             const char *, not_found_msg, const char *, forbidden_msg) {
	item_access_t status = item_access_status(item_path, username, flags);
	switch (status) {
	case ITEM_ACCESS_OK:
		return 0;
	case ITEM_ACCESS_UNAUTHENTICATED:
		return require_login(fd, username);
	case ITEM_ACCESS_MISSING:
		return respond_error(fd, 404,
		                     not_found_msg ? not_found_msg : "Not found");
	case ITEM_ACCESS_FORBIDDEN:
		return respond_error(fd, 403,
		                     forbidden_msg ? forbidden_msg : "Forbidden");
	}
	return respond_error(fd, 500, "Invalid item access status");
}

/* --- Item context --- */

NDX_LISTENER(int, item_ctx_load,
             item_ctx_t *, ctx, int, fd,
             const char *, items_path, unsigned, flags) {
	memset(ctx, 0, sizeof(*ctx));
	ctx->fd = fd;

	if (flags & ICTX_NEED_LOGIN) {
		ctx->username = get_request_user(fd);
		if (require_login(fd, ctx->username))
			return 1;
	} else {
		ctx->username = get_request_user(fd);
	}

	get_doc_root(fd, ctx->doc_root, sizeof(ctx->doc_root));
	ndc_env_get(fd, ctx->id, "PATTERN_PARAM_ID");

	if (flags & ICTX_SONG_ID)
		ndc_env_get(fd, ctx->song_id, "PATTERN_PARAM_SONG_ID");

	if (!ctx->id[0] ||
	    ((flags & ICTX_SONG_ID) && !ctx->song_id[0])) {
		bad_request(fd, "Missing parameters");
		return 1;
	}

	snprintf(ctx->item_path, sizeof(ctx->item_path), "%s/%s/%s",
	         ctx->doc_root, items_path, ctx->id);

	if (flags & ICTX_NEED_OWNERSHIP) {
		if (item_require_access(fd, ctx->item_path, ctx->username, flags,
		                        "Not found", "Forbidden"))
			return 1;
	}

	return 0;
}

NDX_LISTENER(int, with_item_access,
             int, fd, char *, body,
             const char *, items_path, unsigned, flags,
             const char *, not_found_msg, const char *, forbidden_msg,
             item_handler_cb, cb, void *, user) {
	item_ctx_t ctx;
	unsigned load_flags = flags & ~ICTX_NEED_OWNERSHIP;

	if (!cb)
		return respond_error(fd, 500, "Missing item handler");

	if (item_ctx_load(&ctx, fd, items_path, load_flags))
		return 1;

	if (item_require_access(fd, ctx.item_path, ctx.username, flags,
	                        not_found_msg, forbidden_msg))
		return 1;

	return cb(fd, body, &ctx, user);
}

/* ------------------------------------------------------------------ */
/* SSR outcome hooks                                                    */
/* ------------------------------------------------------------------ */

int on_auth_login_error(int fd, int status, const char *msg, const char *redirect) {
	char accept[256] = {0};
	ndc_header_get(fd, "Accept", accept, sizeof(accept));
	if (strstr(accept, "text/html")) {
		char enc[128] = {0}, enc_ret[256] = {0}, pb[512];
		char ret[256] = {0};
		if (redirect)
			strncpy(ret, redirect, sizeof(ret) - 1);
		url_encode(msg, enc, sizeof(enc));
		url_encode(ret, enc_ret, sizeof(enc_ret));
		int plen = snprintf(pb, sizeof(pb),
		                    "status=%d&error=%s&ret=%s", status, enc, enc_ret);
		return ssr_render(fd, "POST", "/auth/login", "", pb, (size_t)plen,
		                  get_request_user(fd));
	}
	ndc_header_set(fd, "Content-Type", "text/plain");
	ndc_respond(fd, status, msg ? msg : "");
	return 1;
}

/* ------------------------------------------------------------------ */
/* Module init                                                          */
/* ------------------------------------------------------------------ */

void ndx_install(void) {
	ndx_load("./mods/index/index");
	ndx_load("./mods/common/common");
	ndx_load("ndc-auth");
	auth_init();
}
