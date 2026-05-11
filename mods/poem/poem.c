#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include <ttypt/ndx-mod.h>
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

static int poem_edit_get_handler(int fd, char *body)
{
	(void)body;
	char id[128] = { 0 };
	const char *user;
	char item_path_buf[512];

	if (check_item_access(
	            fd,
	            "poem",
	            id,
	            sizeof(id),
	            &user,
	            item_path_buf,
	            sizeof(item_path_buf)))
		return 1;

	poem_cache_t meta;
	poem_meta_read(item_path_buf, &meta);

	const char *csrf_token = csrf_setup(fd);

	bud_node *form = poem_form_content(1, id, &meta, csrf_token);
	return site_ui_respond_edit_page(
	        fd, user, "poem", "\xf0\x9f\x93\x9d", meta.title, id, form);
}

static int poem_detail_handler(int fd, char *body)
{
	(void)body;
	char id[128] = { 0 };
	const char *user = get_request_user(fd);
	char path[256];
	char page_title[512];

	axil_env_get(fd, id, "PATTERN_PARAM_ID");
	if (!id[0])
		return bad_request(fd, "Missing ID");

	char item_path_buf[512];
	if (item_path_build(
	            fd, "poem", id, item_path_buf, sizeof(item_path_buf)) != 0)
		return server_error(fd, "Failed to resolve path");

	poem_cache_t meta;
	poem_meta_read(item_path_buf, &meta);

	if (!meta.title[0])
		return respond_error(fd, 404, "Poem not found");

	char content_path[PATH_MAX];
	item_child_path(
	        item_path_buf,
	        "pt_PT.html",
	        content_path,
	        sizeof(content_path));
	char *body_content = slurp_file(content_path);
	if (!body_content)
		body_content = strdup("");

	int is_owner = (user && user[0] && strcmp(user, meta.owner) == 0);
	snprintf(path, sizeof(path), "/poem/%s", id);
	snprintf(page_title, sizeof(page_title), "poem: %s", meta.title);

	bud_node *frag = poem_render_detail_body(body_content, meta.owner);
	free(body_content);

	if (!frag)
		return server_error(fd, "OOM");

	bud_node *layout = site_ui_layout(
	        page_title,
	        path,
	        "\xf0\x9f\x93\x9d",
	        user,
	        site_ui_item_menu("poem", id, is_owner),
	        frag);

	return site_ui_respond_page(fd, page_title, NULL, "poem", layout);
}

void ndx_install(void)
{
	ndx_load("./mods/index/index");

	source_setup(
	        "poem.items",
	        NULL,
	        sizeof(poem_cache_t),
	        "items/poem/items",
	        poem_fields,
	        POEM_FIELD_COUNT,
	        0);

	index_open(
	        "Poem",
	        "poem.items",
	        NULL,
	        poem_detail_handler,
	        NULL,
	        poem_edit_get_handler,
	        NULL);
	axil_register_handler("GET:/poem/add", poem_add_get_handler);
}
