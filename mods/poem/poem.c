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
			snprintf(norm, sizeof(norm), "%s", tok);
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
	snprintf(item_path, sizeof(item_path), "%s/%s", POEM_ITEMS_PATH, id);

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
		return call_respond_plain(fd, 404, "Not found");

	char item_path[512];
	snprintf(item_path, sizeof(item_path), "%s/%s", POEM_ITEMS_PATH, id);

	{
		struct stat st;
		if (stat(item_path, &st) != 0)
			return call_respond_plain(fd, 404, "Not found");
	}

	char title[256] = { 0 };
	call_read_meta_file(item_path, "title", title, sizeof(title));

	char title_esc[512] = { 0 };
	call_json_escape(title, title_esc, sizeof(title_esc));

	char accept_lang[512] = { 0 };
	ndc_env_get(fd, accept_lang, "HTTP_ACCEPT_LANGUAGE");

	char lang[64] = { 0 };
	pick_lang(id, accept_lang, lang, sizeof(lang));

	char lang_esc[128] = { 0 };
	call_json_escape(lang, lang_esc, sizeof(lang_esc));

	const char *username = call_get_request_user(fd);
	int owner = username && *username
		? call_item_check_ownership(item_path, username) : 0;

	char id_esc[256] = { 0 };
	call_json_escape(id, id_esc, sizeof(id_esc));

	char json[1024];
	snprintf(json, sizeof(json),
		"{\"id\":\"%s\",\"title\":\"%s\",\"lang\":\"%s\",\"owner\":%s}",
		id_esc, title_esc, lang_esc, owner ? "true" : "false");

	call_proxy_header("Content-Type", "application/json");
	return call_core_post(fd, json, strlen(json));
}

/* POST /poem/add — handle multipart upload, create item */
static int
poem_add_post_handler(int fd, char *body)
{
	const char *username = call_get_request_user(fd);
	if (!username || !username[0])
		return call_redirect(fd, "/auth/login");

	int parse_result = call_mpfd_parse(fd, body);
	if (parse_result == -1)
		return call_respond_plain(fd, 415, "Expected multipart/form-data");

	char title[256] = { 0 };
	int title_len = call_mpfd_get("title", title, sizeof(title) - 1);
	if (title_len <= 0)
		return call_respond_plain(fd, 400, "Missing title");
	title[title_len] = '\0';

	char id[256] = { 0 };
	call_index_id(id, sizeof(id) - 1, title, (size_t)title_len);

	char item_path[512];
	snprintf(item_path, sizeof(item_path), "%s/%s", POEM_ITEMS_PATH, id);

	if (mkdir(item_path, 0755) == -1)
		return call_respond_plain(fd, 409, "Poem already exists");

	call_item_record_ownership(item_path, username);

	char title_path[768];
	snprintf(title_path, sizeof(title_path), "%s/title", item_path);
	FILE *tfp = fopen(title_path, "w");
	if (tfp) {
		fwrite(title, 1, (size_t)title_len, tfp);
		fclose(tfp);
	}

	int file_len = call_mpfd_len("file");
	if (file_len > 0) {
		char *file_content = malloc((size_t)file_len + 1);
		if (file_content) {
			int got = call_mpfd_get("file", file_content, file_len);
			if (got > 0) {
				file_content[got] = '\0';
				char html_path[768];
				snprintf(html_path, sizeof(html_path), "%s/pt_PT.html", item_path);
				FILE *fp = fopen(html_path, "w");
				if (fp) {
					fwrite(file_content, 1, (size_t)got, fp);
					fclose(fp);
				}
				langs_cache_put(id, "pt_PT");
			}
			free(file_content);
		}
	}

	call_index_put(index_hd, id, title);

	char redirect[512];
	snprintf(redirect, sizeof(redirect), "/poem/%s", id);
	return call_redirect(fd, redirect);
}

/* GET /poem/:id/edit — read title and proxy to Fresh for edit form */
static int
poem_edit_get_handler(int fd, char *body)
{
	(void)body;

	char id[128] = { 0 };
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	if (!id[0])
		return call_respond_plain(fd, 400, "Missing poem ID");

	char item_path[512];
	snprintf(item_path, sizeof(item_path), "%s/%s", POEM_ITEMS_PATH, id);
	const char *username = call_get_request_user(fd);
	if (call_require_ownership(fd, item_path, username, "Forbidden"))
		return 1;

	char title[256] = { 0 };
	call_read_meta_file(item_path, "title", title, sizeof(title));

	char title_esc[512] = { 0 };
	call_json_escape(title, title_esc, sizeof(title_esc));

	char json[768];
	snprintf(json, sizeof(json),
		"{\"id\":\"%s\",\"title\":\"%s\"}", id, title_esc);

	call_proxy_header("Content-Type", "application/json");
	return call_core_post(fd, json, strlen(json));
}

/* POST /poem/:id/edit — save title (optional) + file upload */
static int
poem_edit_post_handler(int fd, char *body)
{
	char id[128] = { 0 };
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	if (!id[0])
		return call_respond_plain(fd, 400, "Missing poem ID");

	int parse_result = call_mpfd_parse(fd, body);
	if (parse_result == -1)
		return call_respond_plain(fd, 415, "Expected multipart/form-data");

	char item_path[512];
	snprintf(item_path, sizeof(item_path), "%s/%s", POEM_ITEMS_PATH, id);
	const char *username = call_get_request_user(fd);
	if (call_require_ownership(fd, item_path, username, "Forbidden"))
		return 1;

	char title[256] = { 0 };
	int title_len = call_mpfd_get("title", title, sizeof(title) - 1);
	if (title_len > 0) {
		title[title_len] = '\0';
		char title_path[768];
		snprintf(title_path, sizeof(title_path), "%s/title", item_path);
		FILE *tfp = fopen(title_path, "w");
		if (tfp) {
			fwrite(title, 1, (size_t)title_len, tfp);
			fclose(tfp);
		}
		call_index_put(index_hd, id, title);
	}

	int file_len = call_mpfd_len("file");
	if (file_len > 0) {
		char *file_content = malloc((size_t)file_len + 1);
		if (!file_content)
			return call_respond_plain(fd, 500, "Memory error");
		int got = call_mpfd_get("file", file_content, file_len);
		if (got > 0) {
			file_content[got] = '\0';
			char dst_path[768];
			snprintf(dst_path, sizeof(dst_path), "%s/pt_PT.html", item_path);
			FILE *dfp = fopen(dst_path, "w");
			if (!dfp) {
				free(file_content);
				return call_respond_plain(fd, 500, "Failed to write poem file");
			}
			fwrite(file_content, 1, (size_t)got, dfp);
			fclose(dfp);
			langs_cache_put(id, "pt_PT");
		}
		free(file_content);
	}

	char redirect[256];
	snprintf(redirect, sizeof(redirect), "/poem/%s", id);
	return call_redirect(fd, redirect);
}

void ndx_install(void) {
	ndx_load("./mods/auth/auth");
	ndx_load("./mods/index/index");

	index_hd = call_index_open("Poem", 0, 1, NULL);

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

