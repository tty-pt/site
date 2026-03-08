#include <ttypt/ndx-mod.h>

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#include <ttypt/ndc.h>

#include "../mpfd/mpfd.h"
#include "../index/index.h"

#define POEM_ITEMS_PATH "items/poem/items"

static unsigned index_hd;

static void
handle_poem_add(int fd, char *doc_root)
{
	char id[64] = { 0 };
	char title[256] = { 0 };

	/* Check if id field exists */
	int id_len = call_mpfd_get("id", id, sizeof(id) - 1);
	if (id_len <= 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing id or file");
		return;
	}
	id[id_len] = '\0';

	/* Get title (optional) */
	int title_len = call_mpfd_get("title", title, sizeof(title) - 1);
	if (title_len > 0) {
		title[title_len] = '\0';
	}

	/* Check if file field exists */
	if (!call_mpfd_exists("file")) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing id or file");
		return;
	}

	/* Check if file has content */
	int file_len = call_mpfd_len("file");
	if (file_len <= 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing id or file");
		return;
	}

	char *file_content = malloc(file_len + 1);
	if (!file_content) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Memory error");
		return;
	}

	int got = call_mpfd_get("file", file_content, file_len);
	if (got <= 0) {
		free(file_content);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing id or file");
		return;
	}
	file_content[got] = '\0';

	char item_path[512];
	snprintf(item_path, sizeof(item_path), "%s/%s/%s", 
		doc_root[0] ? doc_root : ".", POEM_ITEMS_PATH, id);
	
	if (mkdir(item_path, 0755) == -1 && errno != EEXIST) {
		free(file_content);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to create poem directory");
		return;
	}

	char dst_path[1024];
	snprintf(dst_path, sizeof(dst_path), "%s/pt_PT.html", item_path);

	FILE *dfp = fopen(dst_path, "w");
	if (!dfp) {
		free(file_content);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to write poem file");
		return;
	}
	fwrite(file_content, 1, got, dfp);
	fclose(dfp);
	free(file_content);

	char comment_path[1024];
	snprintf(comment_path, sizeof(comment_path), "%s/comments.txt", item_path);
	FILE *cfp = fopen(comment_path, "w");
	if (cfp) fclose(cfp);

	/* Write title file */
	if (title[0]) {
		char title_path[1024];
		snprintf(title_path, sizeof(title_path), "%s/title", item_path);
		FILE *tfp = fopen(title_path, "w");
		if (tfp) {
			fwrite(title, 1, strlen(title), tfp);
			fclose(tfp);
		}
	}

	char redirect_path[256];
	snprintf(redirect_path, sizeof(redirect_path), "/poem/%s/", id);
	ndc_header(fd, "Location", redirect_path);
	ndc_head(fd, 303);
	ndc_close(fd);
}

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
	if (!fp) {
		ndc_head(fd, 404);
		ndc_body(fd, "Poem not found");
		return 1;
	}

	fseek(fp, 0, SEEK_END);
	content_len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	content = malloc(content_len + 1);
	if (!content) {
		fclose(fp);
		ndc_head(fd, 500);
		ndc_body(fd, "Memory error");
		return 1;
	}

	size_t got = fread(content, 1, content_len, fp);
	fclose(fp);
	content[got] = '\0';

	char fresh_path[256];
	snprintf(fresh_path, sizeof(fresh_path), "/poem/%s/", id);

	int ret = call_core_post(fd, content, got);
	free(content);

	return ret;
}

static int
poem_handler(int fd, char *body)
{
	char method[16] = { 0 };
	char uri[256] = { 0 };
	char doc_root[256] = { 0 };

	ndc_env_get(fd, method, "REQUEST_METHOD");
	ndc_env_get(fd, uri, "DOCUMENT_URI");
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");


	/* Parse multipart form data */
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

	/* Now process the upload */
	handle_poem_add(fd, doc_root);
	return 0;
}

void ndx_install(void) {
	ndx_load("./mods/index/index");

	ndc_register_handler("GET:/poem/:id", poem_detail_handler);
	ndc_register_handler("POST:/poem/add", poem_handler);
	
	index_hd = call_index_open("Poem", 0, 1);
}

void ndx_open(void) {}
