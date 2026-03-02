#include <ttypt/ndx-mod.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

#include <ttypt/ndc.h>
#include "../mpfd/mpfd.h"
#include "../../lib/transp/transp.h"
/* #include "chords_api.h" */

#define CHORDS_ITEMS_PATH "items/chords/items"

/* Global transpose context (initialized once in ndx_install) */
static transp_ctx_t *g_transp_ctx = NULL;

/* NDX declarations for ssr module functions */
NDX_DEF(int, ssr_proxy_get, int, fd, const char *, path);
NDX_DEF(int, ssr_proxy_post, int, fd, const char *, path, const char *, body_data, size_t, body_len);

/* 
 * NDX API: Transpose chord chart text
 */
NDX_DEF(int, chords_transpose, const char *, input, int, semitones, int, flags, char **, output)
{
	if (!g_transp_ctx || !input || !output) {
		return -1;
	}
	
	char *result = transp_buffer(g_transp_ctx, input, semitones, flags);
	if (!result) {
		return -1;
	}
	
	*output = result;
	return 0;
}

static void
handle_chords_add(int fd, char *doc_root)
{
	char id[64] = { 0 };
	char title[256] = { 0 };
	char type[64] = { 0 };

	/* Check if id field exists */
	int id_len = call_mpfd_get("id", id, sizeof(id) - 1);
	if (id_len <= 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing id");
		return;
	}
	id[id_len] = '\0';

	/* Get title (optional) */
	int title_len = call_mpfd_get("title", title, sizeof(title) - 1);
	if (title_len > 0) {
		title[title_len] = '\0';
	}

	/* Get type (optional) */
	int type_len = call_mpfd_get("type", type, sizeof(type) - 1);
	if (type_len > 0) {
		type[type_len] = '\0';
	}

	/* Check if data field exists */
	if (!call_mpfd_exists("data")) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing chord data");
		return;
	}

	/* Check if data has content */
	int data_len = call_mpfd_len("data");
	if (data_len <= 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing chord data");
		return;
	}

	char *data_content = malloc(data_len + 1);
	if (!data_content) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Memory error");
		return;
	}

	int got = call_mpfd_get("data", data_content, data_len);
	if (got <= 0) {
		free(data_content);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing chord data");
		return;
	}
	data_content[got] = '\0';

	char item_path[512];
	snprintf(item_path, sizeof(item_path), "%s/%s/%s", 
		doc_root[0] ? doc_root : ".", CHORDS_ITEMS_PATH, id);
	
	if (mkdir(item_path, 0755) == -1 && errno != EEXIST) {
		free(data_content);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to create chord directory");
		return;
	}

	/* Write data.txt */
	char data_path[1024];
	snprintf(data_path, sizeof(data_path), "%s/data.txt", item_path);

	FILE *dfp = fopen(data_path, "w");
	if (!dfp) {
		free(data_content);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to write chord data");
		return;
	}
	fwrite(data_content, 1, got, dfp);
	fclose(dfp);
	free(data_content);

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

	/* Write type file */
	if (type[0]) {
		char type_path[1024];
		snprintf(type_path, sizeof(type_path), "%s/type", item_path);
		FILE *yfp = fopen(type_path, "w");
		if (yfp) {
			fwrite(type, 1, strlen(type), yfp);
			fclose(yfp);
		}
	}

	ndc_header(fd, "Location", "/chords/");
	ndc_head(fd, 303);
	ndc_close(fd);
}

static int
chords_handler(int fd, char *body)
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
	handle_chords_add(fd, doc_root);
	return 0;
}

static int
chords_get_handler(int fd, char *body)
{
	char query[1024] = { 0 };
	char doc_root[256] = { 0 };
	char id[128] = { 0 };
	
	/* Get QUERY_STRING */
	int ret = ndc_env_get(fd, query, "QUERY_STRING");
	if (ret != 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to get QUERY_STRING");
		return 1;
	}
	
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	
	/* Parse query parameters: ?id=<id>&t=<semitones>&... */
	if (strlen(query) == 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing query parameters");
		return 1;
	}
	
	int transpose = 0;
	int flags = 0;
	int found_id = 0;
	
	char *saveptr;
	char *query_copy = strdup(query);
	char *param = strtok_r(query_copy, "&", &saveptr);
	
	while (param) {
		char *eq = strchr(param, '=');
		if (eq) {
			*eq = '\0';
			char *key = param;
			char *value = eq + 1;
			
			if (strcmp(key, "id") == 0) {
				strncpy(id, value, sizeof(id) - 1);
				found_id = 1;
			} else if (strcmp(key, "t") == 0) {
				transpose = atoi(value);
			} else if (strcmp(key, "b") == 0 && strcmp(value, "1") == 0) {
				flags |= TRANSP_BEMOL;
			} else if (strcmp(key, "l") == 0 && strcmp(value, "1") == 0) {
				flags |= TRANSP_LATIN;
			} else if (strcmp(key, "C") == 0 && strcmp(value, "1") == 0) {
				flags |= TRANSP_HIDE_CHORDS;
			} else if (strcmp(key, "L") == 0 && strcmp(value, "1") == 0) {
				flags |= TRANSP_HIDE_LYRICS;
			}
		}
		param = strtok_r(NULL, "&", &saveptr);
	}
	free(query_copy);
	
	if (!found_id) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing id parameter");
		return 1;
	}
	
	/* Read chord data from file */
	char filepath[512];
	snprintf(filepath, sizeof(filepath), "%s/%s/%s/data.txt",
			doc_root, CHORDS_ITEMS_PATH, id);
	
	FILE *fp = fopen(filepath, "r");
	if (!fp) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 404);
		ndc_body(fd, "Chord not found");
		return 1;
	}
	
	/* Read entire file */
	fseek(fp, 0, SEEK_END);
	long fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	char *content = malloc(fsize + 1);
	if (!content) {
		fclose(fp);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Memory allocation failed");
		return 1;
	}
	
	fread(content, 1, fsize, fp);
	fclose(fp);
	content[fsize] = '\0';
	
	/* Apply transposition if requested */
	char *result;
	if (transpose != 0 || flags != 0) {
		result = transp_buffer(g_transp_ctx, content, transpose, flags);
		if (!result) {
			free(content);
			ndc_header(fd, "Content-Type", "text/plain");
			ndc_head(fd, 500);
			ndc_body(fd, "Transposition failed");
			return 1;
		}
		free(content);
	} else {
		result = content;
	}
	
	/* Return result */
	ndc_header(fd, "Content-Type", "text/plain; charset=utf-8");
	ndc_head(fd, 200);
	ndc_body(fd, result);
	
	if (result != content) {
		free(result);
	}
	
	return 0;
}

/* SSR handler for /chords/* routes
 * Handles transposition when query params present, delegates to Deno
 */
static int
chords_ssr_handler(int fd, char *body)
{
	(void)body;  /* SSR is GET requests, body unused */
	
	char doc_root[256] = { 0 };
	char path[512] = { 0 };
	char query[1024] = { 0 };
	char id[128] = { 0 };
	
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	ndc_env_get(fd, path, "DOCUMENT_URI");
	ndc_env_get(fd, query, "QUERY_STRING");
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");
	
	/* If no ID pattern match, this is /chords/ list page - proxy GET */
	if (!id[0]) {
		/* Append query string if present */
		if (query[0]) {
			size_t path_len = strlen(path);
			snprintf(path + path_len, sizeof(path) - path_len, "?%s", query);
		}
		return call_ssr_proxy_get(fd, path);
	}
	
	/* Have ID - check if transposition requested */
	int transpose = 0;
	int flags = 0;
	int needs_transpose = 0;
	
	if (query[0]) {
		/* Parse query params */
		char *saveptr;
		char *query_copy = strdup(query);
		if (!query_copy) {
			return call_ssr_proxy_get(fd, path);
		}
		
		char *param = strtok_r(query_copy, "&", &saveptr);
		
		while (param) {
			char *eq = strchr(param, '=');
			if (eq) {
				*eq = '\0';
				char *key = param;
				char *value = eq + 1;
				
				if (strcmp(key, "t") == 0) {
					transpose = atoi(value);
					needs_transpose = 1;
				} else if (strcmp(key, "b") == 0 && strcmp(value, "1") == 0) {
					flags |= TRANSP_BEMOL;
					needs_transpose = 1;
				} else if (strcmp(key, "l") == 0 && strcmp(value, "1") == 0) {
					flags |= TRANSP_LATIN;
					needs_transpose = 1;
				} else if (strcmp(key, "C") == 0 && strcmp(value, "1") == 0) {
					flags |= TRANSP_HIDE_CHORDS;
					needs_transpose = 1;
				} else if (strcmp(key, "L") == 0 && strcmp(value, "1") == 0) {
					flags |= TRANSP_HIDE_LYRICS;
					needs_transpose = 1;
				}
			}
			param = strtok_r(NULL, "&", &saveptr);
		}
		free(query_copy);
	}
	
	/* No transposition needed - just proxy GET */
	if (!needs_transpose) {
		/* Append query string for Deno */
		if (query[0]) {
			size_t path_len = strlen(path);
			snprintf(path + path_len, sizeof(path) - path_len, "?%s", query);
		}
		return call_ssr_proxy_get(fd, path);
	}
	
	/* Read chord file */
	char filepath[512];
	snprintf(filepath, sizeof(filepath), "%s/%s/%s/data.txt",
			doc_root, CHORDS_ITEMS_PATH, id);
	
	FILE *fp = fopen(filepath, "r");
	if (!fp) {
		/* File not found - let Deno handle 404 */
		if (query[0]) {
			size_t path_len = strlen(path);
			snprintf(path + path_len, sizeof(path) - path_len, "?%s", query);
		}
		return call_ssr_proxy_get(fd, path);
	}
	
	fseek(fp, 0, SEEK_END);
	long fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	char *content = malloc(fsize + 1);
	if (!content) {
		fclose(fp);
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Memory allocation failed");
		return 1;
	}
	
	fread(content, 1, fsize, fp);
	fclose(fp);
	content[fsize] = '\0';
	
	/* Transpose */
	char *transposed = transp_buffer(g_transp_ctx, content, transpose, flags);
	free(content);
	
	if (!transposed) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Transposition failed");
		return 1;
	}
	
	/* Append query string to path for Deno */
	if (query[0]) {
		size_t path_len = strlen(path);
		snprintf(path + path_len, sizeof(path) - path_len, "?%s", query);
	}
	
	/* Proxy POST with transposed data */
	size_t transposed_len = strlen(transposed);
	int result = call_ssr_proxy_post(fd, path, transposed, transposed_len);
	
	free(transposed);
	return result;
}

MODULE_API void
ndx_install(void)
{
	/* Initialize transpose context */
	g_transp_ctx = transp_init();
	if (!g_transp_ctx) {
		fprintf(stderr, "[chords] Failed to initialize transp context\n");
	}
	
	ndx_load("./mods/ssr/ssr");
	ndx_load("./mods/mpfd/mpfd");
	
	/* Register API handlers */
	ndc_register_handler("POST:/chords/add", chords_handler);
	ndc_register_handler("GET:/api/chords/transpose", chords_get_handler);
	
	/* Register SSR handlers with pattern matching */
	ndc_register_handler("GET:/chords/", chords_ssr_handler);       /* List page */
	ndc_register_handler("GET:/chords/:id", chords_ssr_handler);    /* Detail page with :id pattern */
}

MODULE_API void
ndx_open(void)
{
}
