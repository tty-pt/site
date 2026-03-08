#include <ttypt/ndx-mod.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

#include <ttypt/ndc.h>
#include "../mpfd/mpfd.h"
#include "../index/index.h"
#include "../../lib/transp/transp.h"

#define CHORDS_ITEMS_PATH "items/song/items"

/* Global transpose context (initialized once in ndx_install) */
static transp_ctx_t *g_transp_ctx = NULL;
static unsigned index_hd;

/* 
 * NDX API: Transpose chord chart text
 */
NDX_DEF(int, song_transpose, const char *, input, int, semitones, int, flags, char **, output)
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

/* Parse transpose params from query string */
static int
parse_transpose_params(const char *query, int *transpose, int *flags, int *show_media)
{
	*transpose = 0;
	*flags = 0;
	*show_media = 0;
	
	if (!query || !*query) return 0;
	
	char *query_copy = strdup(query);
	if (!query_copy) return -1;
	
	char *saveptr;
	char *param = strtok_r(query_copy, "&", &saveptr);
	int needs_transpose = 0;
	
	while (param) {
		char *eq = strchr(param, '=');
		if (eq) {
			*eq = '\0';
			char *key = param;
			char *value = eq + 1;
			
			if (strcmp(key, "t") == 0) {
				*transpose = atoi(value);
				needs_transpose = 1;
			} else if (strcmp(key, "b") == 0 && strcmp(value, "1") == 0) {
				*flags |= TRANSP_BEMOL;
				needs_transpose = 1;
			} else if (strcmp(key, "l") == 0 && strcmp(value, "1") == 0) {
				*flags |= TRANSP_LATIN;
				needs_transpose = 1;
			} else if (strcmp(key, "m") == 0 && strcmp(value, "1") == 0) {
				*show_media = 1;
				needs_transpose = 1;
			}
		}
		param = strtok_r(NULL, "&", &saveptr);
	}
	
	free(query_copy);
	return needs_transpose ? 0 : 1;
}

/* Escape string for JSON - caller must free result */
static char *
json_escape(const char *str)
{
	if (!str) return NULL;
	
	size_t len = strlen(str);
	size_t max_escaped = len * 4 + 1;
	char *result = malloc(max_escaped);
	if (!result) return NULL;
	
	char *dst = result;
	const char *src = str;
	
	while (*src) {
		switch (*src) {
			case '\\': *dst++ = '\\'; *dst++ = '\\'; break;
			case '"':  *dst++ = '\\'; *dst++ = '"'; break;
			case '\n': *dst++ = '\\'; *dst++ = 'n'; break;
			case '\r': *dst++ = '\\'; *dst++ = 'r'; break;
			case '\t': *dst++ = '\\'; *dst++ = 't'; break;
			default:    *dst++ = *src; break;
		}
		src++;
	}
	*dst = '\0';
	
	return result;
}

/* GET /api/song/:id/edit - JSON API for transposition */
static int
api_song_transpose_handler(int fd, char *body)
{
	(void)body;
	
	char doc_root[256] = {0};
	char query[1024] = {0};
	char id[128] = {0};
	
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	ndc_env_get(fd, query, "QUERY_STRING");
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");
	
	if (!id[0]) {
		ndc_header(fd, "Content-Type", "application/json");
		ndc_head(fd, 400);
		ndc_body(fd, "{\"error\":\"Missing id\"}");
		return 0;
	}
	
	int transpose = 0;
	int flags = 0;
	int show_media = 0;
	
	if (parse_transpose_params(query, &transpose, &flags, &show_media) != 0) {
		ndc_header(fd, "Content-Type", "application/json");
		ndc_head(fd, 400);
		ndc_body(fd, "{\"error\":\"Invalid params\"}");
		return 0;
	}
	
	/* Read chord file */
	char filepath[512];
	snprintf(filepath, sizeof(filepath), "%s/%s/%s/data.txt",
			doc_root, CHORDS_ITEMS_PATH, id);
	
	FILE *fp = fopen(filepath, "r");
	if (!fp) {
		ndc_header(fd, "Content-Type", "application/json");
		ndc_head(fd, 404);
		ndc_body(fd, "{\"error\":\"Song not found\"}");
		return 0;
	}
	
	fseek(fp, 0, SEEK_END);
	long fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	char *content = malloc(fsize + 1);
	if (!content) {
		fclose(fp);
		ndc_header(fd, "Content-Type", "application/json");
		ndc_head(fd, 500);
		ndc_body(fd, "{\"error\":\"Memory error\"}");
		return 0;
	}
	
	fread(content, 1, fsize, fp);
	fclose(fp);
	content[fsize] = '\0';
	
	/* Transpose */
	char *transposed = transp_buffer(g_transp_ctx, content, transpose, flags);
	free(content);
	
	if (!transposed) {
		ndc_header(fd, "Content-Type", "application/json");
		ndc_head(fd, 500);
		ndc_body(fd, "{\"error\":\"Transposition failed\"}");
		return 0;
	}
	
	/* Return JSON */
	ndc_header(fd, "Content-Type", "application/json");
	ndc_head(fd, 200);
	
	/* Build JSON response with proper escaping */
	char *escaped = json_escape(transposed);
	if (escaped) {
		size_t response_len = strlen(escaped) + 32;
		char *response = malloc(response_len);
		if (response) {
			snprintf(response, response_len, "{\"data\":\"%s\",\"showMedia\":%d}", escaped, show_media);
			ndc_body(fd, response);
			free(response);
		} else {
			ndc_body(fd, "{\"data\":\"\",\"showMedia\":0}");
		}
		free(escaped);
	} else {
		ndc_body(fd, "{\"data\":\"\",\"showMedia\":0}");
	}
	
	free(transposed);
	return 0;
}

static void
handle_song_add(int fd, char *doc_root)
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

	ndc_header(fd, "Location", "/song/");
	ndc_head(fd, 303);
	ndc_close(fd);
}

static int
song_handler(int fd, char *body)
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
	handle_song_add(fd, doc_root);
	return 0;
}

/* SSR handler for /song/* routes
 * Handles transposition when query params present, delegates to Deno
 */
static int
song_details_handler(int fd, char *body)
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
	
	/* Have ID - check if transposition requested */
	int transpose = 0;
	int flags = 0;
	int needs_transpose = 0;
	
	if (query[0]) {
		/* Parse query params */
		char *saveptr;
		char *query_copy = strdup(query);
		if (!query_copy)
			return call_core_get(fd, path);
		
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
		return call_core_get(fd, path);
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

		return call_core_get(fd, path);
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
	int result = call_core_post(fd, transposed, transposed_len);
	
	free(transposed);
	return result;
}

/* GET /song/:id/edit - read files and proxy to Fresh */
static int
song_edit_get_handler(int fd, char *body)
{
	(void)body;

	char doc_root[256] = { 0 };
	char id[128] = { 0 };

	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	if (!id[0]) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing song ID");
		return 1;
	}

	char item_path[512];
	snprintf(item_path, sizeof(item_path), "%s/%s/%s",
		doc_root[0] ? doc_root : ".", CHORDS_ITEMS_PATH, id);

	/* Read title */
	char title[256] = { 0 };
	char title_path[1024];
	snprintf(title_path, sizeof(title_path), "%s/title", item_path);
	FILE *tfp = fopen(title_path, "r");
	if (tfp) {
		if (fgets(title, sizeof(title) - 1, tfp)) {
			size_t len = strlen(title);
			if (len > 0 && title[len - 1] == '\n')
				title[len - 1] = '\0';
		}
		fclose(tfp);
	}

	/* Read type */
	char type[256] = { 0 };
	char type_path[1024];
	snprintf(type_path, sizeof(type_path), "%s/type", item_path);
	FILE *typefp = fopen(type_path, "r");
	if (typefp) {
		if (fgets(type, sizeof(type) - 1, typefp)) {
			size_t len = strlen(type);
			if (len > 0 && type[len - 1] == '\n')
				type[len - 1] = '\0';
		}
		fclose(typefp);
	}

	/* Read yt */
	char yt[512] = { 0 };
	char yt_path[1024];
	snprintf(yt_path, sizeof(yt_path), "%s/yt", item_path);
	FILE *ytfp = fopen(yt_path, "r");
	if (ytfp) {
		if (fgets(yt, sizeof(yt) - 1, ytfp)) {
			size_t len = strlen(yt);
			if (len > 0 && yt[len - 1] == '\n')
				yt[len - 1] = '\0';
		}
		fclose(ytfp);
	}

	/* Read audio */
	char audio[512] = { 0 };
	char audio_path[1024];
	snprintf(audio_path, sizeof(audio_path), "%s/audio", item_path);
	FILE *audiofp = fopen(audio_path, "r");
	if (audiofp) {
		if (fgets(audio, sizeof(audio) - 1, audiofp)) {
			size_t len = strlen(audio);
			if (len > 0 && audio[len - 1] == '\n')
				audio[len - 1] = '\0';
		}
		fclose(audiofp);
	}

	/* Read pdf */
	char pdf[512] = { 0 };
	char pdf_path[1024];
	snprintf(pdf_path, sizeof(pdf_path), "%s/pdf", item_path);
	FILE *pdfp = fopen(pdf_path, "r");
	if (pdfp) {
		if (fgets(pdf, sizeof(pdf) - 1, pdfp)) {
			size_t len = strlen(pdf);
			if (len > 0 && pdf[len - 1] == '\n')
				pdf[len - 1] = '\0';
		}
		fclose(pdfp);
	}

	/* Read data */
	char *data_content = NULL;
	size_t data_len = 0;
	char data_path[1024];
	snprintf(data_path, sizeof(data_path), "%s/data.txt", item_path);
	FILE *dfp = fopen(data_path, "r");
	if (dfp) {
		fseek(dfp, 0, SEEK_END);
		long fsize = ftell(dfp);
		fseek(dfp, 0, SEEK_SET);
		if (fsize > 0) {
			data_content = malloc(fsize + 1);
			if (data_content) {
				data_len = fread(data_content, 1, fsize, dfp);
				data_content[data_len] = '\0';
			}
		}
		fclose(dfp);
	}

	/* Build POST body for Fresh */
	char post_body[16384] = { 0 };
	size_t pos = 0;

	pos += snprintf(post_body + pos, sizeof(post_body) - pos, "title=");
	for (size_t i = 0; title[i] && pos < sizeof(post_body) - 4; i++) {
		char c = title[i];
		if (c == '%') pos += snprintf(post_body + pos, 4, "%%25");
		else if (c == ' ') pos += snprintf(post_body + pos, 4, "%%20");
		else if (c == '\n') pos += snprintf(post_body + pos, 4, "%%0A");
		else post_body[pos++] = c;
	}

	pos += snprintf(post_body + pos, sizeof(post_body) - pos, "&type=");
	for (size_t i = 0; type[i] && pos < sizeof(post_body) - 4; i++) {
		char c = type[i];
		if (c == '%') pos += snprintf(post_body + pos, 4, "%%25");
		else if (c == ' ') pos += snprintf(post_body + pos, 4, "%%20");
		else if (c == '\n') pos += snprintf(post_body + pos, 4, "%%0A");
		else post_body[pos++] = c;
	}

	pos += snprintf(post_body + pos, sizeof(post_body) - pos, "&yt=");
	for (size_t i = 0; yt[i] && pos < sizeof(post_body) - 4; i++) {
		char c = yt[i];
		if (c == '%') pos += snprintf(post_body + pos, 4, "%%25");
		else if (c == ' ') pos += snprintf(post_body + pos, 4, "%%20");
		else if (c == '\n') pos += snprintf(post_body + pos, 4, "%%0A");
		else post_body[pos++] = c;
	}

	pos += snprintf(post_body + pos, sizeof(post_body) - pos, "&audio=");
	for (size_t i = 0; audio[i] && pos < sizeof(post_body) - 4; i++) {
		char c = audio[i];
		if (c == '%') pos += snprintf(post_body + pos, 4, "%%25");
		else if (c == ' ') pos += snprintf(post_body + pos, 4, "%%20");
		else if (c == '\n') pos += snprintf(post_body + pos, 4, "%%0A");
		else post_body[pos++] = c;
	}

	pos += snprintf(post_body + pos, sizeof(post_body) - pos, "&pdf=");
	for (size_t i = 0; pdf[i] && pos < sizeof(post_body) - 4; i++) {
		char c = pdf[i];
		if (c == '%') pos += snprintf(post_body + pos, 4, "%%25");
		else if (c == ' ') pos += snprintf(post_body + pos, 4, "%%20");
		else if (c == '\n') pos += snprintf(post_body + pos, 4, "%%0A");
		else post_body[pos++] = c;
	}

	if (data_content && data_len > 0) {
		pos += snprintf(post_body + pos, sizeof(post_body) - pos, "&data=");
		for (size_t i = 0; i < data_len && pos < sizeof(post_body) - 10; i++) {
			char c = data_content[i];
			if (c == '\n') pos += snprintf(post_body + pos, 4, "%%0A");
			else if (c == '\r') { /* skip */ }
			else if (c == '%') pos += snprintf(post_body + pos, 4, "%%25");
			else if (c == ' ') pos += snprintf(post_body + pos, 4, "%%20");
			else post_body[pos++] = c;
		}
		free(data_content);
	}

	/* POST to Fresh */
	return call_core_post(fd, post_body, strlen(post_body));
}

/* POST /song/:id/edit - save edit */
static int
song_edit_post_handler(int fd, char *body)
{
	(void)body;

	char doc_root[256] = { 0 };
	char id[128] = { 0 };

	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	if (!id[0]) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing song ID");
		return 1;
	}

	char item_path[512];
	snprintf(item_path, sizeof(item_path), "%s/%s/%s",
		doc_root[0] ? doc_root : ".", CHORDS_ITEMS_PATH, id);

	/* Get title */
	char title[256] = { 0 };
	int title_len = call_mpfd_get("title", title, sizeof(title) - 1);
	if (title_len > 0)
		title[title_len] = '\0';

	/* Get type */
	char type[256] = { 0 };
	int type_len = call_mpfd_get("type", type, sizeof(type) - 1);
	if (type_len > 0)
		type[type_len] = '\0';

	/* Get yt */
	char yt[512] = { 0 };
	call_mpfd_get("yt", yt, sizeof(yt) - 1);

	/* Get audio */
	char audio[512] = { 0 };
	call_mpfd_get("audio", audio, sizeof(audio) - 1);

	/* Get pdf */
	char pdf[512] = { 0 };
	call_mpfd_get("pdf", pdf, sizeof(pdf) - 1);

	/* Get data */
	int data_len = call_mpfd_len("data");
	char *data_content = NULL;
	if (data_len > 0) {
		data_content = malloc(data_len + 1);
		if (data_content) {
			call_mpfd_get("data", data_content, data_len);
			data_content[data_len] = '\0';
		}
	}

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
		FILE *tfp = fopen(type_path, "w");
		if (tfp) {
			fwrite(type, 1, strlen(type), tfp);
			fclose(tfp);
		}
	}

	/* Write yt file */
	if (yt[0]) {
		char yt_path[1024];
		snprintf(yt_path, sizeof(yt_path), "%s/yt", item_path);
		FILE *tfp = fopen(yt_path, "w");
		if (tfp) {
			fwrite(yt, 1, strlen(yt), tfp);
			fclose(tfp);
		}
	}

	/* Write audio file */
	if (audio[0]) {
		char audio_path[1024];
		snprintf(audio_path, sizeof(audio_path), "%s/audio", item_path);
		FILE *tfp = fopen(audio_path, "w");
		if (tfp) {
			fwrite(audio, 1, strlen(audio), tfp);
			fclose(tfp);
		}
	}

	/* Write pdf file */
	if (pdf[0]) {
		char pdf_path[1024];
		snprintf(pdf_path, sizeof(pdf_path), "%s/pdf", item_path);
		FILE *tfp = fopen(pdf_path, "w");
		if (tfp) {
			fwrite(pdf, 1, strlen(pdf), tfp);
			fclose(tfp);
		}
	}

	/* Write data file */
	if (data_content) {
		char data_path[1024];
		snprintf(data_path, sizeof(data_path), "%s/data.txt", item_path);
		FILE *dfp = fopen(data_path, "w");
		if (dfp) {
			fwrite(data_content, 1, data_len, dfp);
			fclose(dfp);
		}
		free(data_content);
	}

	/* Redirect to song page */
	char location[256];
	snprintf(location, sizeof(location), "/song/%s", id);
	ndc_header(fd, "Location", location);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

void ndx_install(void)
{
	/* Initialize transpose context */
	g_transp_ctx = transp_init();
	if (!g_transp_ctx)
		fprintf(stderr, "[song] Failed to initialize"
				" transp context\n");

	ndx_load("./mods/index/index");
	ndx_load("./mods/mpfd/mpfd");

	ndc_register_handler("GET:/song/:id",
			song_details_handler);
	ndc_register_handler("GET:/song/:id/edit",
			song_edit_get_handler);
	ndc_register_handler("POST:/song/:id/edit",
			song_edit_post_handler);

	ndc_register_handler("GET:/api/song/:id/transpose",
			api_song_transpose_handler);

	index_hd = call_index_open("Song", 0, 1);
}

void ndx_open(void) {}
