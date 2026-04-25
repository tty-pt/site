#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#include <ttypt/ndx-mod.h>
#include <ttypt/ndc.h>
#include <ttypt/qmap.h>

#include "../mpfd/mpfd.h"
#include "../proxy/proxy.h"
#include "../index/index.h"
#include "../auth/auth.h"
#include "../common/common.h"

#define POEM_ITEMS_PATH "items/poem/items"

static unsigned index_hd;
static unsigned langs_hd; /* qmap: "{id}/{lang}" -> "1" */

static void langs_cache_put(const char *id, const char *lang);

typedef struct {
	char title[256];
} poem_meta_t;

static int
poem_item_path_build(int fd, const char *id, char *out, size_t out_sz)
{
	return item_path_build(fd, "poem", id, out, out_sz);
}

static void
poem_meta_read(const char *item_path, poem_meta_t *meta)
{
	meta_field_t fields[] = {
		{ "title", meta->title, sizeof(meta->title) },
	};

	memset(meta, 0, sizeof(*meta));
	meta_fields_read(item_path, fields, sizeof(fields) / sizeof(fields[0]));
}

static int
poem_meta_write(const char *item_path, const poem_meta_t *meta)
{
	meta_field_t fields[] = {
		{ "title", (char *)meta->title, sizeof(meta->title) },
	};

	return meta_fields_write(item_path, fields, sizeof(fields) / sizeof(fields[0]));
}

static int
poem_write_uploaded_html(const char *item_path, const char *id)
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
		if (write_item_child_file(item_path, "pt_PT.html",
				file_content, (size_t)got) != 0) {
			free(file_content);
			return -1;
		}
		langs_cache_put(id, "pt_PT");
	}

	free(file_content);
	return 0;
}

/* Register that a lang file exists for an item in the cache. */
static void
langs_cache_put(const char *id, const char *lang)
{
	char key[256];
	snprintf(key, sizeof(key), "%s/%s", id, lang);
	qmap_put(langs_hd, key, "1");
}

/* Pick best lang for item from Accept-Language header.
 * Probes cache keys "{id}/{lang}" for each token in accept_lang.
 * Falls back to "pt_PT". */
static void
pick_lang(const char *id, const char *accept_lang,
	char *chosen, size_t chosen_size)
{
	chosen[0] = '\0';

	if (accept_lang && accept_lang[0]) {
		char buf[512];
		snprintf(buf, sizeof(buf), "%s", accept_lang);
		char *tok = buf;
		while (*tok) {
			while (*tok == ' ') tok++;
			char *end = tok;
			while (*end && *end != ',' && *end != ';') end++;
			char save = *end;
			*end = '\0';

			/* normalize: replace '-' with '_' */
			char norm[64];
			size_t norm_len = strlen(tok);
			if (norm_len >= sizeof(norm))
				norm_len = sizeof(norm) - 1;
			memcpy(norm, tok, norm_len);
			norm[norm_len] = '\0';
			for (char *p = norm; *p; p++)
				if (*p == '-') *p = '_';

			/* probe exact key */
			char key[256];
			snprintf(key, sizeof(key), "%s/%s", id, norm);
			if (qmap_get(langs_hd, key)) {
				snprintf(chosen, chosen_size, "%s", norm);
				return;
			}

			/* probe prefix: try "{id}/{norm[0..n]}" patterns by
			 * checking if any stored key starts with "{id}/{norm}" */
			/* For simplicity: try truncating at '_' */
			char *underscore = strchr(norm, '_');
			if (underscore) {
				char prefix[64];
				size_t plen = (size_t)(underscore - norm);
				memcpy(prefix, norm, plen);
				prefix[plen] = '\0';
				snprintf(key, sizeof(key), "%s/%s", id, prefix);
				if (qmap_get(langs_hd, key)) {
					snprintf(chosen, chosen_size, "%s", prefix);
					return;
				}
			}

			*end = save;
			tok = (save == ',') ? end + 1 : end;
			if (!save) break;
		}
	}

	/* Fallback: pt_PT */
	char key[256];
	snprintf(key, sizeof(key), "%s/pt_PT", id);
	if (qmap_get(langs_hd, key)) {
		snprintf(chosen, chosen_size, "pt_PT");
		return;
	}

	snprintf(chosen, chosen_size, "pt_PT");
}

/* Scan item directory for *.html files and populate langs cache. */
static void
langs_cache_scan_item(const char *id)
{
	char item_path[512];
	if (item_path_build_root(".", "poem", id, item_path, sizeof(item_path)) != 0)
		return;

	DIR *dp = opendir(item_path);
	if (!dp) return;

	struct dirent *de;
	while ((de = readdir(dp)) != NULL) {
		const char *name = de->d_name;
		size_t len = strlen(name);
		if (len > 5 && strcmp(name + len - 5, ".html") == 0) {
			char lang[64];
			size_t stem_len = len - 5;
			if (stem_len >= sizeof(lang)) continue;
			memcpy(lang, name, stem_len);
			lang[stem_len] = '\0';
			langs_cache_put(id, lang);
		}
	}
	closedir(dp);
}

/* GET /poem/:id — look up lang from cache, proxy to Fresh */
static int
poem_detail_handler(int fd, char *body)
{
	(void)body;

	char id[128] = { 0 };
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	if (!id[0])
		return respond_error(fd, 404, "Not found");

	char item_path[512];
	if (poem_item_path_build(fd, id, item_path, sizeof(item_path)) != 0)
		return server_error(fd, "Failed to resolve poem path");

	if (!item_dir_exists(item_path))
		return respond_error(fd, 404, "Poem not found");

	poem_meta_t meta;
	poem_meta_read(item_path, &meta);

	char accept_lang[512] = { 0 };
	ndc_env_get(fd, accept_lang, "HTTP_ACCEPT_LANGUAGE");

	char lang[64] = { 0 };
	pick_lang(id, accept_lang, lang, sizeof(lang));

	const char *username = get_request_user(fd);
	int owner = username && *username
		? item_check_ownership(item_path, username) : 0;

	json_object_t *jo = json_object_new(0);
	if (!jo)
		return respond_error(fd, 500, "OOM");
	if (json_object_kv_str(jo, "id", id) != 0 ||
			json_object_kv_str(jo, "title", meta.title) != 0 ||
			json_object_kv_str(jo, "lang", lang) != 0 ||
			json_object_kv_bool(jo, "owner", owner) != 0) {
		json_object_free(jo);
		return respond_error(fd, 500, "OOM");
	}
	char *json = json_object_finish(jo);
	if (!json)
		return respond_error(fd, 500, "OOM");
	int rc = core_post_json(fd, json);
	free(json);
	return rc;
}

/* POST /poem/add — handle multipart upload, create item */
static int
poem_add_post_handler(int fd, char *body)
{
	char id[256] = { 0 };
	if (index_add_item(fd, body, id, sizeof(id)) != 0)
		return 1;

	char item_path[512];
	if (item_path_build(fd, "poem", id, item_path, sizeof(item_path)) != 0)
		return server_error(fd, "Failed to resolve poem path");
	if (poem_write_uploaded_html(item_path, id) != 0)
		return server_error(fd, "Failed to write poem file");

	char redirect_path[512];
	snprintf(redirect_path, sizeof(redirect_path), "/poem/%s", id);
	return redirect(fd, redirect_path);
}

/* GET /poem/:id/edit — read title and proxy to Fresh for edit form */
static int
poem_edit_get_handler(int fd, char *body)
{
	(void)body;

	item_ctx_t ctx;
	if (item_ctx_load(&ctx, fd, POEM_ITEMS_PATH,
			0))
		return 1;

	if (item_require_access(fd, ctx.item_path, ctx.username,
			ICTX_NEED_OWNERSHIP,
			"Poem not found", "Forbidden"))
		return 1;

	poem_meta_t meta;
	poem_meta_read(ctx.item_path, &meta);

	json_object_t *jo = json_object_new(0);
	if (!jo)
		return respond_error(fd, 500, "OOM");
	if (json_object_kv_str(jo, "id", ctx.id) != 0 ||
			json_object_kv_str(jo, "title", meta.title) != 0) {
		json_object_free(jo);
		return respond_error(fd, 500, "OOM");
	}
	char *json = json_object_finish(jo);
	if (!json)
		return respond_error(fd, 500, "OOM");
	int rc = core_post_json(fd, json);
	free(json);
	return rc;
}

/* POST /poem/:id/edit — save title (optional) + file upload */
static int
poem_edit_post_handler(int fd, char *body)
{
	item_ctx_t ctx;
	if (item_ctx_load(&ctx, fd, POEM_ITEMS_PATH,
			0))
		return 1;

	if (item_require_access(fd, ctx.item_path, ctx.username,
			ICTX_NEED_OWNERSHIP,
			"Poem not found", "Forbidden"))
		return 1;

	int parse_result = mpfd_parse(fd, body);
	if (parse_result == -1)
		return respond_error(fd, 415, "Expected multipart/form-data");

	poem_meta_t meta = {0};
	int title_len = mpfd_get("title", meta.title, sizeof(meta.title) - 1);
	if (title_len > 0) {
		meta.title[title_len] = '\0';
		if (poem_meta_write(ctx.item_path, &meta) != 0)
			return server_error(fd, "Failed to write poem title");
		index_put(index_hd, ctx.id, meta.title);
	}

	if (poem_write_uploaded_html(ctx.item_path, ctx.id) != 0)
		return server_error(fd, "Failed to write poem file");

	char redirect_path[256];
	snprintf(redirect_path, sizeof(redirect_path), "/poem/%s", ctx.id);
	return redirect(fd, redirect_path);
}

void ndx_install(void) {
	ndx_load("./mods/auth/auth");
	ndx_load("./mods/index/index");

	index_hd = index_open("Poem", 0, 1, NULL);

	/* Build langs cache from all existing items on startup */
	langs_hd = qmap_open(NULL, "langs", QM_STR, QM_STR, 0x3FF, 0);
	{
		DIR *dp = opendir(POEM_ITEMS_PATH);
		if (dp) {
			struct dirent *de;
			while ((de = readdir(dp)) != NULL) {
				if (de->d_name[0] == '.') continue;
				langs_cache_scan_item(de->d_name);
			}
			closedir(dp);
		}
	}

	ndc_register_handler("POST:/poem/add", poem_add_post_handler);
	ndc_register_handler("GET:/poem/:id", poem_detail_handler);
	ndc_register_handler("GET:/poem/:id/edit", poem_edit_get_handler);
	ndc_register_handler("POST:/poem/:id/edit", poem_edit_post_handler);
}
