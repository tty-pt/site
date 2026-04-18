#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <ttypt/ndx-mod.h>
#include <ttypt/ndc.h>

#include "../mpfd/mpfd.h"
#include "../proxy/proxy.h"
#include "../index/index.h"
#include "../auth/auth.h"
#include "../common/common.h"

#define POEM_ITEMS_PATH "items/poem/items"

static unsigned index_hd;

/* Return current session username, or NULL */
static const char *
poem_current_user(int fd)
{
	char cookie[256] = { 0 };
	char token[64] = { 0 };
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	return call_get_session_user(token);
}

/* 1 if user owns the poem, 0 otherwise */
static int
poem_is_owner(int fd, const char *id)
{
	const char *username = poem_current_user(fd);
	if (!username || !username[0])
		return 0;

	char path[512];
	snprintf(path, sizeof(path), "%s/%s", POEM_ITEMS_PATH, id);
	return call_item_check_ownership(path, username);
}

/* GET /poem/:id — read HTML content and proxy to Fresh for rendering */
static int
poem_detail_handler(int fd, char *body)
{
	(void)body;

	char id[128] = { 0 };
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	if (!id[0]) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 404);
		ndc_body(fd, "Not found");
		return 1;
	}

	/* Return 404 if poem directory does not exist */
	{
		char dir_path[512];
		struct stat st;
		snprintf(dir_path, sizeof(dir_path), "%s/%s", POEM_ITEMS_PATH, id);
		if (stat(dir_path, &st) != 0) {
			ndc_header(fd, "Content-Type", "text/plain");
			ndc_head(fd, 404);
			ndc_body(fd, "Not found");
			return 1;
		}
	}

	char title[256] = { 0 };
	char title_path[512];
	snprintf(title_path, sizeof(title_path), "%s/%s/title", POEM_ITEMS_PATH, id);
	FILE *tfp = fopen(title_path, "r");
	if (tfp) {
		if (fgets(title, sizeof(title) - 1, tfp))
			title[strcspn(title, "\n")] = '\0';
		fclose(tfp);
	}

	char html_path[512];
	snprintf(html_path, sizeof(html_path), "%s/%s/pt_PT.html", POEM_ITEMS_PATH, id);
	char *content = NULL;
	size_t content_len = 0;
	FILE *fp = fopen(html_path, "r");
	if (fp) {
		fseek(fp, 0, SEEK_END);
		content_len = (size_t)ftell(fp);
		fseek(fp, 0, SEEK_SET);
		content = malloc(content_len + 1);
		if (content) {
			size_t got = fread(content, 1, content_len, fp);
			content[got] = '\0';
			content_len = got;
		}
		fclose(fp);
	}

	/* Escape title and html for JSON */
	char title_esc[512] = { 0 };
	call_json_escape(title, title_esc, sizeof(title_esc));

	size_t json_len = 128 + sizeof(title_esc) + (content ? content_len * 2 : 4);
	char *json = malloc(json_len);
	if (!json) {
		free(content);
		ndc_head(fd, 500);
		return 1;
	}

	int owner = poem_is_owner(fd, id);

	if (content) {
		char *html_esc = malloc(content_len * 2 + 1);
		if (html_esc) {
			call_json_escape(content, html_esc, content_len * 2 + 1);
			snprintf(json, json_len,
				"{\"id\":\"%s\",\"title\":\"%s\",\"html\":\"%s\",\"owner\":%s}",
				id, title_esc, html_esc, owner ? "true" : "false");
			free(html_esc);
		} else {
			snprintf(json, json_len,
				"{\"id\":\"%s\",\"title\":\"%s\",\"html\":\"\",\"owner\":%s}",
				id, title_esc, owner ? "true" : "false");
		}
	} else {
		snprintf(json, json_len,
			"{\"id\":\"%s\",\"title\":\"%s\",\"html\":\"\",\"owner\":%s}",
			id, title_esc, owner ? "true" : "false");
	}
	free(content);

	call_proxy_header("Content-Type", "application/json");
	int ret = call_core_post(fd, json, strlen(json));
	free(json);
	return ret;
}

/* POST /poem/add — handle multipart upload, create item */
static int
poem_add_post_handler(int fd, char *body)
{
	const char *username = poem_current_user(fd);
	if (!username || !username[0]) {
		ndc_header(fd, "Location", "/auth/login");
		ndc_header(fd, "Connection", "close");
		ndc_set_flags(fd, DF_TO_CLOSE);
		ndc_head(fd, 303);
		ndc_close(fd);
		return 0;
	}

	int parse_result = call_mpfd_parse(fd, body);
	if (parse_result == -1) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 415);
		ndc_body(fd, "Expected multipart/form-data");
		return 1;
	}

	char title[256] = { 0 };
	int title_len = call_mpfd_get("title", title, sizeof(title) - 1);
	if (title_len <= 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing title");
		return 1;
	}
	title[title_len] = '\0';

	char id[256] = { 0 };
	call_index_id(id, sizeof(id) - 1, title, (size_t)title_len);

	char item_path[512];
	snprintf(item_path, sizeof(item_path), "%s/%s", POEM_ITEMS_PATH, id);

	if (mkdir(item_path, 0755) == -1) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 409);
		ndc_body(fd, "Poem already exists");
		return 1;
	}

	/* Record ownership */
	call_item_record_ownership(item_path, username);

	/* Write title file */
	char title_path[768];
	snprintf(title_path, sizeof(title_path), "%s/title", item_path);
	FILE *tfp = fopen(title_path, "w");
	if (tfp) {
		fwrite(title, 1, (size_t)title_len, tfp);
		fclose(tfp);
	}

	/* Write HTML file */
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
			}
			free(file_content);
		}
	}

	call_index_put(index_hd, id, title);

	char redirect[512];
	snprintf(redirect, sizeof(redirect), "/poem/%s", id);
	ndc_header(fd, "Location", redirect);
	ndc_header(fd, "Connection", "close");
	ndc_set_flags(fd, DF_TO_CLOSE);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* GET /poem/:id/edit — read title and proxy to Fresh for edit form */
static int
poem_edit_get_handler(int fd, char *body)
{
	(void)body;

	char id[128] = { 0 };
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	if (!id[0]) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing poem ID");
		return 1;
	}

	if (!poem_is_owner(fd, id)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "Forbidden");
		return 1;
	}

	char title[256] = { 0 };
	char title_path[512];
	snprintf(title_path, sizeof(title_path), "%s/%s/title", POEM_ITEMS_PATH, id);
	FILE *tfp = fopen(title_path, "r");
	if (tfp) {
		if (fgets(title, sizeof(title) - 1, tfp))
			title[strcspn(title, "\n")] = '\0';
		fclose(tfp);
	}

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

	if (!id[0]) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing poem ID");
		return 1;
	}

	int parse_result = call_mpfd_parse(fd, body);
	if (parse_result == -1) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 415);
		ndc_body(fd, "Expected multipart/form-data");
		return 1;
	}

	if (!poem_is_owner(fd, id)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "Forbidden");
		return 1;
	}

	char item_path[512];
	snprintf(item_path, sizeof(item_path), "%s/%s", POEM_ITEMS_PATH, id);

	/* Optional: update title */
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

	/* Optional: update file content */
	int file_len = call_mpfd_len("file");
	if (file_len > 0) {
		char *file_content = malloc((size_t)file_len + 1);
		if (!file_content) {
			ndc_header(fd, "Content-Type", "text/plain");
			ndc_head(fd, 500);
			ndc_body(fd, "Memory error");
			return 1;
		}
		int got = call_mpfd_get("file", file_content, file_len);
		if (got > 0) {
			file_content[got] = '\0';
			char dst_path[768];
			snprintf(dst_path, sizeof(dst_path), "%s/pt_PT.html", item_path);
			FILE *dfp = fopen(dst_path, "w");
			if (!dfp) {
				free(file_content);
				ndc_header(fd, "Content-Type", "text/plain");
				ndc_head(fd, 500);
				ndc_body(fd, "Failed to write poem file");
				return 1;
			}
			fwrite(file_content, 1, (size_t)got, dfp);
			fclose(dfp);
		}
		free(file_content);
	}

	char redirect[256];
	snprintf(redirect, sizeof(redirect), "/poem/%s", id);
	ndc_header(fd, "Location", redirect);
	ndc_header(fd, "Connection", "close");
	ndc_set_flags(fd, DF_TO_CLOSE);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

void ndx_install(void) {
	ndx_load("./mods/auth/auth");
	ndx_load("./mods/index/index");

	index_hd = call_index_open("Poem", 0, 1, NULL);

	ndc_register_handler("POST:/poem/add", poem_add_post_handler);
	ndc_register_handler("GET:/poem/:id", poem_detail_handler);
	ndc_register_handler("GET:/poem/:id/edit", poem_edit_get_handler);
	ndc_register_handler("POST:/poem/:id/edit", poem_edit_post_handler);
}

void ndx_open(void) {}
