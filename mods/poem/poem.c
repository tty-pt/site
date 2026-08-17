#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include <ttypt/xy-mod.h>
#include <ttypt/axil.h>
#include <ttypt/qmap.h>

#include "../index/index.h"
#include "../common/common.h"
#include "../source/source.h"
#include "../auth/auth.h"
#include "fields.h"

#include "ux/all.c"

/* ── Meta reader ──────────────────────────────────────────────── */

static void poem_meta_read(const char *path, poem_cache_t *m)
{
	source_meta_read(path, poem_fields, POEM_FIELD_COUNT, m, sizeof(*m));
}

/* ── HTTP handlers ────────────────────────────────────────────── */

static int poem_add_get_handler(int fd, char *body)
{
	(void)body;
	const char *user = require_user(fd);
	if (!user)
		return 1;

	const char *csrf_token = csrf_setup(fd);

	bud_node *form = poem_form_content(0, NULL, NULL, csrf_token);
	return site_ui_respond_add_page(
	        fd, user, "poem", "\xf0\x9f\x93\x9d", form);
}

static int poem_edit_auth(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;
	poem_cache_t meta;
	poem_meta_read(ctx->item_path, &meta);

	const char *csrf_token = csrf_setup(fd);

	bud_node *form = poem_form_content(1, ctx->id, &meta, csrf_token);
	return site_ui_respond_edit_page(
	        fd, ctx->username, "poem", "\xf0\x9f\x93\x9d", meta.title,
	        ctx->id, form);
}

static int poem_edit_get_handler(int fd, char *body)
{
	return with_item_access(
	        fd, body, "items/poem/items",
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP, NULL, NULL,
	        poem_edit_auth, NULL);
}

static int
poem_detail_auth(int fd, char *body, const item_ctx_t *ctx, void *user_data)
{
	(void)body;
	(void)user_data;
	char path[256];
	char page_title[512];

	poem_cache_t meta;
	poem_meta_read(ctx->item_path, &meta);

	if (!meta.title[0])
		return respond_error(fd, 404, "Poem not found");

	char content_path[PATH_MAX];
	item_child_path(
	        ctx->item_path, "pt_PT.html", content_path,
	        sizeof(content_path));
	char *body_content = slurp_file(content_path);
	if (!body_content)
		body_content = strdup("");

	int is_owner =
	        (ctx->username && ctx->username[0] &&
	         strcmp(ctx->username, meta.owner) == 0);
	snprintf(path, sizeof(path), "/poem/%s", ctx->id);
	snprintf(page_title, sizeof(page_title), "poem: %s", meta.title);

	bud_node *frag = poem_render_detail_body(body_content, meta.owner);
	free(body_content);

	if (!frag)
		return server_error(fd, "OOM");

	bud_node *layout = site_ui_layout(
	        page_title, path, "\xf0\x9f\x93\x9d", ctx->username,
	        site_ui_item_menu("poem", ctx->id, is_owner), frag);

	return site_ui_respond_page(fd, page_title, NULL, NULL, layout);
}

static int poem_detail_handler(int fd, char *body)
{
	return with_item_access(
	        fd, body, "items/poem/items", 0, NULL, NULL, poem_detail_auth,
	        NULL);
}

void xy_install(void)
{
	xy_load("./mods/index/index");

	source_setup(
	        "poem.items", NULL, sizeof(poem_cache_t), "items/poem/items",
	        poem_fields, POEM_FIELD_COUNT, 0);

	index_open("Poem", "poem.items", NULL, NULL, NULL, NULL, NULL);
	standard_item_handlers_t handlers = {
		.detail = poem_detail_handler,
		.add_get = poem_add_get_handler,
		.edit_get = poem_edit_get_handler,
	};
	register_standard_item_handlers("poem", &handlers);
}
