#include <ttypt/ndx-mod.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#include <ttypt/ndc.h>

#include "../mpfd/mpfd.h"
#include "../index/index.h"
#include "../common/common.h"

#define POEM_ITEMS_PATH "items/poem/items"

static unsigned index_hd;

/* GET /poem/:id — read HTML content and proxy to Fresh for rendering */
static int
poem_detail_handler(int fd, char *body)
{
	(void)body;

	char uri[256], doc_root[256], path[512];
	char *content = NULL;
	size_t content_len = 0;

	ndc_env_get(fd, uri, "DOCUMENT_URI");
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");

	char *id = strchr(uri + 1, '/');
	if (!id) {
		ndc_head(fd, 404);
		ndc_body(fd, "Not found");
		return 1;
	}
	id++;

	snprintf(path, sizeof(path), "%s/items/poem/items/%s/pt_PT.html",
		doc_root[0] ? doc_root : ".", id);

	FILE *fp = fopen(path, "r");
	if (fp) {
		fseek(fp, 0, SEEK_END);
		content_len = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		content = malloc(content_len + 1);
		if (content) {
			size_t got = fread(content, 1, content_len, fp);
			content[got] = '\0';
			content_len = got;
		}
		fclose(fp);
	}

	int ret = call_core_post(fd, content ? content : "", content_len);
	free(content);
	return ret;
}

/* GET /poem/:id/edit — read title and proxy to Fresh for edit form */
static int
poem_edit_get_handler(int fd, char *body)
{
	(void)body;

	char doc_root[256] = { 0 };
	char id[128] = { 0 };

	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	if (!id[0]) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing poem ID");
		return 1;
	}

	char title[256] = { 0 };
	char title_path[512];
	snprintf(title_path, sizeof(title_path), "%s/%s/%s/title",
		doc_root[0] ? doc_root : ".", POEM_ITEMS_PATH, id);
	FILE *tfp = fopen(title_path, "r");
	if (tfp) {
		if (fgets(title, sizeof(title) - 1, tfp)) {
			size_t len = strlen(title);
			if (len > 0 && title[len - 1] == '\n')
				title[len - 1] = '\0';
		}
		fclose(tfp);
	}

	char post_body[4096] = { 0 };
	size_t pos = 0;
	pos += snprintf(post_body + pos, sizeof(post_body) - pos, "title=");
	pos += call_url_encode(title, post_body + pos, sizeof(post_body) - pos);

	return call_core_post(fd, post_body, strlen(post_body));
}

/* POST /poem/:id/edit — save title (optional) + file upload */
static int
poem_edit_post_handler(int fd, char *body)
{
	char doc_root[256] = { 0 };
	char id[128] = { 0 };

	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
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
	if (parse_result < 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Parse error");
		return 1;
	}

	char item_path[512];
	snprintf(item_path, sizeof(item_path), "%s/%s/%s",
		doc_root[0] ? doc_root : ".", POEM_ITEMS_PATH, id);

	/* Optional: update title */
	char title[256] = { 0 };
	int title_len = call_mpfd_get("title", title, sizeof(title) - 1);
	if (title_len > 0) {
		title[title_len] = '\0';
		char title_path[1024];
		snprintf(title_path, sizeof(title_path), "%s/title", item_path);
		FILE *tfp = fopen(title_path, "w");
		if (tfp) {
			fwrite(title, 1, title_len, tfp);
			fclose(tfp);
		}
	}

	/* Optional: update file content */
	int file_len = call_mpfd_len("file");
	if (file_len > 0) {
		char *file_content = malloc(file_len + 1);
		if (!file_content) {
			ndc_header(fd, "Content-Type", "text/plain");
			ndc_head(fd, 500);
			ndc_body(fd, "Memory error");
			return 1;
		}
		int got = call_mpfd_get("file", file_content, file_len);
		if (got > 0) {
			file_content[got] = '\0';
			char dst_path[1024];
			snprintf(dst_path, sizeof(dst_path), "%s/pt_PT.html", item_path);
			FILE *dfp = fopen(dst_path, "w");
			if (!dfp) {
				free(file_content);
				ndc_header(fd, "Content-Type", "text/plain");
				ndc_head(fd, 500);
				ndc_body(fd, "Failed to write poem file");
				return 1;
			}
			fwrite(file_content, 1, got, dfp);
			fclose(dfp);
		}
		free(file_content);
	}

	char redirect_path[256];
	snprintf(redirect_path, sizeof(redirect_path), "/poem/%s", id);
	ndc_header(fd, "Location", redirect_path);
	ndc_header(fd, "Connection", "close");
	ndc_set_flags(fd, DF_TO_CLOSE);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

void ndx_install(void) {
	ndx_load("./mods/index/index");

	index_hd = call_index_open("Poem", 0, 1);

	ndc_register_handler("GET:/poem/:id", poem_detail_handler);
	ndc_register_handler("GET:/poem/:id/edit", poem_edit_get_handler);
	ndc_register_handler("POST:/poem/:id/edit", poem_edit_post_handler);
}

void ndx_open(void) {}
