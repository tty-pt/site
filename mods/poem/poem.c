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

static int
poem_detail_auth(int fd, char *body, const item_ctx_t *ctx, void *user_data)
{
	(void)body;
	(void)user_data;
	char path[256];
	char page_title[512];
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
	int is_owner = item_owner_check(ctx->item_path, ctx->username);
	snprintf(path, sizeof(path), "/poem/%s", ctx->id);
	snprintf(page_title, sizeof(page_title), "poem: %s", meta.title);

	bud_node *frag = poem_render_detail_body(body_content, owner, ctx->id);
	free(body_content);

	if (!frag)
		return server_error(fd, "OOM");

	bud_node *layout = site_ui_layout(
	        page_title, path, site_ui_module_icon("poem"), ctx->username,
	        site_ui_item_menu("poem", ctx->id, is_owner), frag);

	return site_ui_respond_page(
	        fd, page_title, path, site_ui_module_icon("poem"),
	        ctx->username, NULL, NULL, layout);
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

	char content_path[PATH_MAX];
	item_child_path(
	        ctx->item_path, "pt_PT.html", content_path,
	        sizeof(content_path));
	char *content = slurp_file(content_path);
	if (!content)
		return respond_error(fd, 404, "Not found");

	axil_header_set(fd, "Content-Type", "text/html; charset=utf-8");
	axil_respond(fd, 200, content);
	free(content);
	return 0;
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

	static const struct {
		const char *ext;
		const char *mime;
	} allowed[] = {
		{ ".jpeg", "image/jpeg" },
		{ ".jpg", "image/jpeg" },
		{ ".png", "image/png" },
	};

	char file[256] = { 0 };
	char stem[256];
	char path[PATH_MAX];
	char len_buf[32];
	size_t flen;
	size_t slen = 0;
	const char *mime = NULL;
	FILE *fp;
	struct stat st;
	char *buf;

	axil_env_get(fd, file, sizeof(file), "PATTERN_PARAM_FILE");
	flen = strlen(file);

	for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
		size_t elen = strlen(allowed[i].ext);
		if (flen > elen &&
		    strcasecmp(file + flen - elen, allowed[i].ext) == 0)
		{
			mime = allowed[i].mime;
			slen = flen - elen;
			break;
		}
	}
	if (!mime || slen == 0 || slen >= sizeof(stem))
		return respond_error(fd, 404, "Not found");

	memcpy(stem, file, slen);
	stem[slen] = '\0';
	if (!is_safe_id(stem))
		return respond_error(fd, 404, "Not found");

	item_child_path(ctx->item_path, file, path, sizeof(path));
	fp = fopen(path, "rb");
	if (!fp)
		return respond_error(fd, 404, "Not found");
	if (fstat(fileno(fp), &st) != 0 || !S_ISREG(st.st_mode) ||
	    st.st_size <= 0)
	{
		fclose(fp);
		return respond_error(fd, 404, "Not found");
	}
	buf = malloc((size_t)st.st_size);
	if (!buf) {
		fclose(fp);
		return server_error(fd, "OOM");
	}
	if (fread(buf, 1, (size_t)st.st_size, fp) != (size_t)st.st_size) {
		fclose(fp);
		free(buf);
		return server_error(fd, "short read");
	}
	fclose(fp);

	snprintf(len_buf, sizeof(len_buf), "%ld", (long)st.st_size);
	axil_header_set(fd, "Content-Type", mime);
	axil_header_set(fd, "Content-Length", len_buf);
	axil_respond(fd, 200, NULL);
	axil_write(fd, buf, (size_t)st.st_size);
	free(buf);
	return 0;
}

static int poem_media_handler(int fd, char *body)
{
	return with_module_item_access(
	        fd, body, "poem", 0, NULL, NULL, poem_media_auth, NULL);
}

void xy_install(void)
{
	xy_load("./mods/index/index");

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
