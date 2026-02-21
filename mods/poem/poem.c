#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx.h>

#include "papi.h"

#define POEM_ITEMS_PATH "items/poem/items"


static void
handle_poem_add(int fd, char *body, char *doc_root)
{
	char id[64] = { 0 };
	char content_type[256] = { 0 };

	ndc_env_get(fd, content_type, "CONTENT_TYPE");

	if (!strstr(content_type, "multipart/form-data")) {
		ndc_writef(fd,
			"HTTP/1.1 400 Bad Request\r\n"
			"Content-Type: text/plain\r\n\r\n"
			"Expected multipart/form-data");
		ndc_close(fd);
		return;
	}

	char *body_start = body;
	char *part = strstr(body_start, "name=\"id\"");
	if (part) {
		char *value = strstr(part, "\r\n\r\n");
		if (value) {
			value += 4;
			char *end = strstr(value, "\r\n--");
			size_t len = end ? (size_t)(end - value) : strlen(value);
			if (len > sizeof(id) - 1) len = sizeof(id) - 1;
			strncpy(id, value, len);
			id[len] = 0;
		}
	}

	char *file_part = strstr(body_start, "name=\"file\"");
	if (!file_part) {
		ndc_writef(fd,
			"HTTP/1.1 400 Bad Request\r\n"
			"Content-Type: text/plain\r\n\r\n"
			"No file field");
		ndc_close(fd);
		return;
	}

	char *file_content = strstr(file_part, "\r\n\r\n");
	if (!file_content) {
		ndc_writef(fd,
			"HTTP/1.1 400 Bad Request\r\n"
			"Content-Type: text/plain\r\n\r\n"
			"No file content");
		ndc_close(fd);
		return;
	}
	file_content += 4;

	char *file_end = strstr(file_content, "\r\n--");
	if (file_end) *file_end = 0;

	if (!*id || !*file_content) {
		ndc_writef(fd,
			"HTTP/1.1 400 Bad Request\r\n"
			"Content-Type: text/plain\r\n\r\n"
			"Missing id or file");
		ndc_close(fd);
		return;
	}

	char item_path[512];
	snprintf(item_path, sizeof(item_path), "%s/%s/%s", 
		doc_root[0] ? doc_root : ".", POEM_ITEMS_PATH, id);
	mkdir(item_path, 0755);

	char dst_path[1024];
	snprintf(dst_path, sizeof(dst_path), "%s/pt_PT.html", item_path);

	FILE *dfp = fopen(dst_path, "w");
	if (dfp) {
		fprintf(dfp, "%s", file_content);
		fclose(dfp);
	}

	char comment_path[1024];
	snprintf(comment_path, sizeof(comment_path), "%s/comments.txt", item_path);
	FILE *cfp = fopen(comment_path, "w");
	if (cfp) fclose(cfp);

	ndc_writef(fd, "HTTP/1.1 303 See Other\r\n"
		"Location: /poem/\r\n\r\n");
	ndc_close(fd);
}

static void
poem_handler(int fd, char *body)
{
	char method[16] = { 0 };
	char uri[256] = { 0 };
	char doc_root[256] = { 0 };

	ndc_env_get(fd, method, "REQUEST_METHOD");
	ndc_env_get(fd, uri, "DOCUMENT_URI");
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");

	if (strcmp(method, "POST") == 0) {
		if (strncmp(uri, "/poem/add", 9) == 0) {
			handle_poem_add(fd, body, doc_root);
			return;
		}
	}

	if (strcmp(method, "GET") == 0) {
		ndc_writef(fd,
			"HTTP/1.1 405 Method Not Allowed\r\n"
			"Content-Type: text/plain\r\n\r\n"
			"Method not allowed");
		ndc_close(fd);
		return;
	}

	ndc_writef(fd,
		"HTTP/1.1 404 Not Found\r\n"
		"Content-Type: text/plain\r\n\r\n"
		"Not found");
	ndc_close(fd);
}

MODULE_API void
ndx_install(void)
{
	ndc_register_handler("POST:/poem/add", poem_handler);
}

MODULE_API void
ndx_open(void)
{
}

MODULE_API ndx_t *
get_ndx_ptr(void)
{
	static ndx_t ndx;
	return &ndx;
}
