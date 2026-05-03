#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#include <ttypt/ndx-mod.h>
#include <ttypt/ndc.h>
#include <ttypt/qmap.h>

#include "../mpfd/mpfd.h"
#include "../index/index.h"
#include "../auth/auth.h"
#include "../common/common.h"
#include "../ssr/ssr.h"

#define POEM_ITEMS_PATH "items/poem/items"

static unsigned index_hd;

typedef struct {
	char title[256];
} poem_meta_t;

static void poem_meta_read(const char *item_path, poem_meta_t *meta)
{
	meta_field_t fields[] = {
		{ "title", meta->title, sizeof(meta->title) },
	};
	memset(meta, 0, sizeof(*meta));
	meta_fields_read(item_path, fields, 1);
}

static int poem_meta_write(const char *item_path, const poem_meta_t *meta)
{
	meta_field_t fields[] = {
		{ "title", (char *)meta->title, sizeof(meta->title) },
	};
	return meta_fields_write(item_path, fields, 1);
}

static int poem_write_uploaded_html(const char *item_path)
{
	int file_len = mpfd_len("file");
	if (file_len <= 0)
		return 0;
	char *file_content = malloc((size_t)file_len + 1);
	if (!file_content)
		return -1;
	int got = mpfd_get("file", file_content, file_len);
	if (got > 0) {
		file_content[got] = '\0';
		write_item_child_file(
		        item_path, "pt_PT.html", file_content, (size_t)got);
	}
	free(file_content);
	return 0;
}

static char *html_tag_inner(const char *html, const char *tag)
{
	const char *start = strstr(html, tag);
	if (!start)
		return strdup("");
	const char *inner_start = strchr(start, '>');
	if (!inner_start)
		return NULL;
	inner_start++;
	char end_tag[32];
	snprintf(end_tag, sizeof(end_tag), "</%s>", tag);
	const char *inner_end = strstr(inner_start, end_tag);
	if (!inner_end)
		return strdup(inner_start);
	size_t out_len = (size_t)(inner_end - inner_start);
	char *out = malloc(out_len + 1);
	if (!out)
		return NULL;
	memcpy(out, inner_start, out_len);
	out[out_len] = '\0';
	return out;
}

static int
poem_detail_authorized(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;
	poem_meta_t meta;
	poem_meta_read(ctx->item_path, &meta);
	char lang_file[80];
	snprintf(lang_file, sizeof(lang_file), "pt_PT.html");
	char *html = slurp_item_child_file(ctx->item_path, lang_file);
	char *head = html ? html_tag_inner(html, "head") : strdup("");
	char *body_content = html ? html_tag_inner(html, "body") : strdup("");
	free(html);
	int owner =
	        (ctx->username && ctx->username[0])
	                ? item_check_ownership(ctx->item_path, ctx->username)
	                : 0;
	static __thread char s_query[512];
	struct ModuleEntryFfi modules_snap[64];
	size_t modules_len;
	ndc_env_get(fd, s_query, "QUERY_STRING");
	SSR_FILL_MODULES(modules_snap, modules_len);
	struct PoemRenderFfi req = {
		.title = meta.title,
		.head_content = head ? head : "",
		.body_content = body_content ? body_content : "",
		.owner = owner != 0,
		.id = ctx->id,
		.query = s_query,
		.remote_user = ctx->username ? ctx->username : "",
		.modules = modules_snap,
		.modules_len = modules_len,
	};
	int rc = ssr_render_poem_detail(fd, &req);
	free(head);
	free(body_content);
	return rc;
}

static int poem_detail_handler(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        POEM_ITEMS_PATH,
	        0,
	        NULL,
	        NULL,
	        poem_detail_authorized,
	        NULL);
}

static int poem_child_file_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;
	char name[512], child_path[1024];
	ndc_env_get(fd, name, "PATTERN_PARAM_FILE");
	if (name[0] == '/' || strstr(name, "..") ||
	    (strstr(name, ".html") && !strstr(name, ".html.")))
		return not_found(fd, "Not found");
	if (item_child_path(
	            ctx->item_path, name, child_path, sizeof(child_path)) != 0)
		return not_found(fd, "Not found");
	ndc_sendfile(fd, child_path);
	return 0;
}

static int poem_child_file_handler(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        POEM_ITEMS_PATH,
	        0,
	        NULL,
	        NULL,
	        poem_child_file_authorized,
	        NULL);
}

static int poem_add_post_handler(int fd, char *body)
{
	char id[256] = { 0 }, item_path[512], redirect_path[512];
	if (index_add_item(fd, body, id, sizeof(id)) != 0)
		return 1;
	item_path_build(fd, "poem", id, item_path, sizeof(item_path));
	poem_write_uploaded_html(item_path);
	snprintf(redirect_path, sizeof(redirect_path), "/poem/%s", id);
	return ndc_redirect(fd, redirect_path);
}

static int
poem_edit_get_authorized(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;
	poem_meta_t meta;
	poem_meta_read(ctx->item_path, &meta);
	int owner =
	        (ctx->username && ctx->username[0])
	                ? item_check_ownership(ctx->item_path, ctx->username)
	                : 0;
	static __thread char s_query[512];
	struct ModuleEntryFfi modules_snap[64];
	size_t modules_len;
	ndc_env_get(fd, s_query, "QUERY_STRING");
	SSR_FILL_MODULES(modules_snap, modules_len);
	struct PoemRenderFfi req = {
		.title = meta.title,
		.head_content = "",
		.body_content = "",
		.owner = owner != 0,
		.id = ctx->id,
		.query = s_query,
		.remote_user = ctx->username ? ctx->username : "",
		.modules = modules_snap,
		.modules_len = modules_len,
	};
	return ssr_render_poem_edit(fd, &req);
}

static int
poem_edit_post_authorized(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	if (mpfd_parse(fd, body) == -1)
		return respond_error(fd, 415, "Expected multipart/form-data");
	poem_meta_t meta = { 0 };
	int title_len = mpfd_get("title", meta.title, sizeof(meta.title) - 1);
	if (title_len > 0) {
		meta.title[title_len] = '\0';
		poem_meta_write(ctx->item_path, &meta);
		index_put(index_hd, (char *)ctx->id, meta.title);
	}
	poem_write_uploaded_html(ctx->item_path);
	char redirect_path[256];
	snprintf(redirect_path, sizeof(redirect_path), "/poem/%s", ctx->id);
	return ndc_redirect(fd, redirect_path);
}

static int poem_edit_get_handler(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        POEM_ITEMS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	        NULL,
	        NULL,
	        poem_edit_get_authorized,
	        NULL);
}

static int poem_edit_post_handler(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        POEM_ITEMS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	        NULL,
	        NULL,
	        poem_edit_post_authorized,
	        NULL);
}

void ndx_install(void)
{
	ndx_load("./mods/auth/auth");
	ndx_load("./mods/index/index");
	index_hd = index_open("Poem", 0, 1, NULL);
	ndc_register_handler("POST:/poem/add", poem_add_post_handler);
	ndc_register_handler("GET:/poem/:id/*", poem_child_file_handler);
	ndc_register_handler("GET:/poem/:id", poem_detail_handler);
	ndc_register_handler("GET:/poem/:id/edit", poem_edit_get_handler);
	ndc_register_handler("POST:/poem/:id/edit", poem_edit_post_handler);
}
