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
#include "dict.h"

#include "ux/all.c"

/* ── Meta reader ──────────────────────────────────────────────── */

static void poem_meta_read(const char *path, poem_cache_t *m)
{
	source_meta_read(path, poem_fields, POEM_FIELD_COUNT, m, sizeof(*m));
}

/* ── HTTP handlers ────────────────────────────────────────────── */

static int
poem_detail_auth(int fd, char *body, const item_ctx_t *ctx, void *user_data)
{
	(void)body;
	(void)user_data;
	char owner[64] = { 0 };

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

	item_owner_read(ctx->item_path, owner, sizeof(owner));

	bud_node *frag = poem_render_detail_body(body_content, owner, ctx->id);
	free(body_content);

	if (!frag)
		return server_error(fd, "OOM");

	return site_ui_respond_item_detail(fd, ctx, "poem", meta.title, frag);
}

static int poem_detail_handler(int fd, char *body)
{
	return with_module_item_access(
	        fd, body, "poem", 0, NULL, NULL, poem_detail_auth, NULL);
}

/* GET /poem/:id/pt_PT.html — allowlisted body file, public read.
 * Only this basename is ever served; owner/meta stay unreachable. */
static int
poem_body_auth(int fd, char *body, const item_ctx_t *ctx, void *user_data)
{
	(void)body;
	(void)user_data;
	return respond_item_file(fd, ctx->item_path, "pt_PT.html", "html");
}

static int poem_body_handler(int fd, char *body)
{
	return with_module_item_access(
	        fd, body, "poem", 0, NULL, NULL, poem_body_auth, NULL);
}

/* GET /poem/:id/:file — allowlisted image assets, public read.
 * The stem must be a safe id and the extension one of .jpeg/.jpg/.png;
 * anything else (including pt_PT.html, which has its own route) is 404. */
static int
poem_media_auth(int fd, char *body, const item_ctx_t *ctx, void *user_data)
{
	(void)body;
	(void)user_data;
	char file[256] = { 0 };
	axil_env_get(fd, file, sizeof(file), "PATTERN_PARAM_FILE");
	return respond_item_file(fd, ctx->item_path, file, "jpeg,jpg,png");
}

static int poem_media_handler(int fd, char *body)
{
	return with_module_item_access(
	        fd, body, "poem", 0, NULL, NULL, poem_media_auth, NULL);
}

void xy_install(void)
{
	xy_load("./mods/index/index");

	i18n_register_dict(poem_dict, POEM_DICT_COUNT);

	index_module_init(&(index_module_def_t){
	        .name = "poem",
	        .display_name = "Poem",
	        .schema = poem_fields,
	        .field_count = POEM_FIELD_COUNT,
	        .record_size = sizeof(poem_cache_t),
	        .items_path = "var/poem",
	        .list_view = &poem_list_view,
	        .handlers = { .detail = poem_detail_handler },
	});

	axil_register_handler("GET:/poem/:id/pt_PT.html", poem_body_handler);
	axil_register_handler("GET:/poem/:id/:file", poem_media_handler);
}
