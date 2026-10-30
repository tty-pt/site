#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include <ttypt/xy-mod.h>
#include <ttypt/axil.h>
#include <ttypt/corm.h>

#include "../index/index.h"
#include "../common/common.h"
#include "../i18n/i18n.h"
#include "../source/source.h"
#include "../auth/auth.h"
#include "fields.h"
#include "dict.h"
#include "markdown.h"

#include "ux/all.c"

/* ── Meta reader ──────────────────────────────────────────────── */

static void software_meta_read(const char *path, software_cache_t *m)
{
	source_meta_read(path, software_fields, SOFTWARE_FIELD_COUNT, m, sizeof(*m));
}

/* ── HTTP handlers ────────────────────────────────────────────── */

static int
software_detail_auth(int fd, char *body, const item_ctx_t *ctx, void *user_data)
{
	(void)body;
	(void)user_data;
	char owner[64] = { 0 };

	software_cache_t meta;
	memset(&meta, 0, sizeof(meta));
	software_meta_read(ctx->item_path, &meta);

	if (!meta.title[0])
		return respond_error(fd, 404, "Software not found");

	char content_path[PATH_MAX];
	item_child_path(
	        ctx->item_path, "README.md", content_path,
	        sizeof(content_path));
	char *readme_md = slurp_file(content_path);
	char *rendered_html = markdown_to_html(readme_md ? readme_md : "");
	free(readme_md);

	item_owner_read(ctx->item_path, owner, sizeof(owner));

	bud_node *frag = software_render_detail_body(&meta, rendered_html, owner, ctx->id);
	free(rendered_html);

	if (!frag)
		return server_error(fd, "OOM");

	return site_ui_respond_item_detail(fd, ctx, "software", meta.title, frag);
}

static int software_detail_handler(int fd, char *body)
{
	return with_module_item_access(
	        fd, body, "software", 0, NULL, NULL, software_detail_auth, NULL);
}

void xy_install(void)
{
	xy_load("./mods/index/index");

	i18n_register_dict(software_dict, SOFTWARE_DICT_COUNT);

	index_module_init(&(index_module_def_t){
	        .name = "software",
	        .display_name = "Software",
	        .schema = software_fields,
	        .field_count = SOFTWARE_FIELD_COUNT,
	        .record_size = sizeof(software_cache_t),
	        .items_path = "var/software",
	        .list_view = &software_list_view,
	        .handlers = { .detail = software_detail_handler },
	        .body_file = "README.md",
	        .media_exts = "png,jpg,jpeg,svg,webp",
	});
}
