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
static unsigned poem_index_hd = 0;
static unsigned poem_meta_qtype = 0;

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

static int poem_index_write_file(const char *root)
{
	char path[PATH_MAX], tmp[PATH_MAX];
	snprintf(path, sizeof(path), "%s/items/poem/index.tsv", root);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE *fp = fopen(tmp, "w");
	if (!fp)
		return -1;
	unsigned c = qmap_iter(poem_index_hd, NULL, 0);
	const void *k, *v;
	while (qmap_next(&k, &v, c)) {
		const poem_meta_t *m = (const poem_meta_t *)v;
		char title[256];
		snprintf(title, sizeof(title), "%s", m->title);
		index_field_clean(title);
		fprintf(fp, "%s\t%s\n", (const char *)k, title);
	}
	if (fclose(fp) != 0) {
		unlink(tmp);
		return -1;
	}
	return rename(tmp, path);
}

static void poem_index_put_meta(const char *id, const poem_meta_t *meta)
{
	qmap_put(poem_index_hd, id, meta);
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

/*
 * Remove all <script>...</script> blocks from html in-place (case-insensitive
 * tag match). Protects other users from stored XSS via uploaded poem HTML.
 */
static void strip_script_tags(char *html)
{
	char *p = html;

	while (*p) {
		/* Case-insensitive search for <script */
		if (*p == '<' && strncasecmp(p + 1, "script", 6) == 0 &&
		    (p[7] == '>' || p[7] == ' ' || p[7] == '\t' ||
		     p[7] == '\n' || p[7] == '\r' || p[7] == '/'))
		{
			/* Find closing </script> — case-insensitive */
			char *end = p + 7;
			while (*end) {
				if (*end == '<' &&
				    strncasecmp(end + 1, "/script", 7) == 0)
				{
					char *close = strchr(end + 7, '>');
					if (close) {
						/* Remove from p to close+1 */
						size_t remove_len =
						        (size_t)(close + 1 - p);
						memmove(p,
						        close + 1,
						        strlen(close + 1) + 1);
						(void)remove_len;
						goto next;
					}
				}
				end++;
			}
			/* No closing tag found — truncate from here */
			*p = '\0';
			return;
		}
		p++;
	next:;
	}
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
	if (head)
		strip_script_tags(head);
	if (body_content)
		strip_script_tags(body_content);
	int owner =
	        (ctx->username && ctx->username[0])
	                ? item_check_ownership(ctx->item_path, ctx->username)
	                : 0;
	static __thread char s_query[512];
	static __thread char s_csrf[33];
	struct ModuleEntryFfi modules_snap[64];
	size_t modules_len;
	ndc_env_get(fd, s_query, "QUERY_STRING");
	csrf_set_cookie(fd, s_csrf, sizeof(s_csrf));
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
		.csrf_token = s_csrf,
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
	static __thread char s_csrf[33];
	struct ModuleEntryFfi modules_snap[64];
	size_t modules_len;
	ndc_env_get(fd, s_query, "QUERY_STRING");
	csrf_set_cookie(fd, s_csrf, sizeof(s_csrf));
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
		.csrf_token = s_csrf,
	};
	return ssr_render_poem_edit(fd, &req);
}

static int
poem_edit_post_authorized(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	if (mpfd_parse(fd, body) == -1)
		return respond_error(fd, 415, "Expected multipart/form-data");
	{
		char csrf_submitted[33] = { 0 };
		mpfd_get(
		        "csrf_token",
		        csrf_submitted,
		        sizeof(csrf_submitted) - 1);
		if (csrf_validate(fd, csrf_submitted))
			return respond_error(fd, 403, "Forbidden");
	}
	poem_meta_t meta = { 0 };
	int title_len = mpfd_get("title", meta.title, sizeof(meta.title) - 1);
	if (title_len > 0) {
		meta.title[title_len] = '\0';
		poem_meta_write(ctx->item_path, &meta);
		index_put(index_hd, (char *)ctx->id, meta.title);
		poem_index_put_meta(ctx->id, &meta);
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

static int poem_detail_handler(int fd, char *body);

void ndx_install(void)
{
	char doc_root[256] = { 0 };
	get_doc_root(0, doc_root, sizeof(doc_root));
	ndx_load("./mods/auth/auth");
	ndx_load("./mods/index/index");
	ndx_load("./mods/common/common");
	index_hd = index_open(
	        "Poem",
	        0,
	        1,
	        NULL,
	        poem_detail_handler,
	        poem_add_post_handler,
	        poem_edit_get_handler,
	        poem_edit_post_handler);
	poem_meta_qtype = qmap_reg(sizeof(poem_meta_t));
	poem_index_hd = qmap_open(
	        NULL, "poem_idx", QM_STR, poem_meta_qtype, 0x3FF, QM_SORTED);
	{
		const char *root = doc_root[0] ? doc_root : ".";
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/items/poem/index.tsv", root);
		FILE *fp = fopen(path, "r");
		if (fp) {
			char line[512];
			while (fgets(line, sizeof(line), fp)) {
				char *id = line;
				char *nl = strpbrk(line, "\r\n");
				if (nl)
					*nl = '\0';
				char *val = strchr(id, '\t');
				if (!val)
					continue;
				*val++ = '\0';
				poem_meta_t meta;
				memset(&meta, 0, sizeof(meta));
				snprintf(
				        meta.title,
				        sizeof(meta.title),
				        "%s",
				        val);
				qmap_put(poem_index_hd, id, &meta);
			}
			fclose(fp);
		} else {
			/* Rebuild from filesystem */
			char p[PATH_MAX];
			if (module_items_path_build(
			            root, "poem", p, sizeof(p)) == 0)
			{
				DIR *d = opendir(p);
				if (d) {
					struct dirent *e;
					while ((e = readdir(d))) {
						char ip[PATH_MAX];
						poem_meta_t meta;
						if (e->d_name[0] == '.')
							continue;
						if (item_path_build_root(
						            root,
						            "poem",
						            e->d_name,
						            ip,
						            sizeof(ip)) != 0)
							continue;
						poem_meta_read(ip, &meta);
						poem_index_put_meta(
						        e->d_name, &meta);
					}
					closedir(d);
					poem_index_write_file(root);
				}
			}
		}
	}
	ndc_register_handler("GET:/poem/:id/*", poem_child_file_handler);
}
