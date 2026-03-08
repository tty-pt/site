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
#include "../common/common.h"
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

/* Parse transpose params from query string using common module */
/* Parse transpose params from query string using common module */
static int
parse_transpose_params(const char *query, int *transpose, int *flags, int *show_media)
{
	*transpose = 0;
	*show_media = 0;

	if (!query || !*query) return 0;

	char query_copy[1024];
	snprintf(query_copy, sizeof(query_copy), "%s", query);

	if (call_query_parse(query_copy) != 0)
		return -1;

	char buf[32];

	if (call_query_param("t", buf, sizeof(buf)) > 0)
		*transpose = atoi(buf);

	if (call_query_exists("b"))
		*flags |= TRANSP_BEMOL;

	if (call_query_exists("l"))
		*flags |= TRANSP_LATIN;

	/* ADD THIS LINE: Sets the HTML flag so transp_buffer knows to wrap divs */
	if (call_query_exists("h"))
		*flags |= TRANSP_HTML;

	if (call_query_exists("m"))
		*show_media = 1;

	return (*transpose || *flags || *show_media) ? 0 : 1;
}

static void read_meta_file(
		const char *item_path,
		const char *name,
		char *buf, size_t sz)
{
	char p[1024];
	snprintf(p, sizeof(p), "%s/%s", item_path, name);
	FILE *mfp = fopen(p, "r");

	if (!mfp)
		return;
	if (fgets(buf, (int)sz - 1, mfp)) {
		size_t l = strlen(buf);
		if (l > 0 && buf[l - 1] == '\n') buf[l - 1] = '\0';
	}
	fclose(mfp);
}

char *song_json(int fd, int flags) {
	char doc_root[256] = {0};
	char query[1024] = {0};
	char id[128] = {0};
	int transpose = 0;
	int show_media = 0;
	char item_path[512];
	char filepath[552];

	/* Buffers for raw and escaped strings */
	char title[256] = {0}, yt[512] = {0}, audio[512] = {0}, pdf[512] = {0};
	char esc_title[512], esc_yt[1024], esc_audio[1024], esc_pdf[1024];

	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	ndc_env_get(fd, query, "QUERY_STRING");
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	if (!id[0]) return NULL;

	if (parse_transpose_params(query, &transpose, &flags, &show_media) != 0)
		return NULL;

	snprintf(item_path, sizeof(item_path), "%s/%s/%s",
			doc_root[0] ? doc_root : ".", CHORDS_ITEMS_PATH, id);

	/* Load metadata fields */
	read_meta_file(item_path, "title", title, sizeof(title));
	read_meta_file(item_path, "yt", yt, sizeof(yt));
	read_meta_file(item_path, "audio", audio, sizeof(audio));
	read_meta_file(item_path, "pdf", pdf, sizeof(pdf));

	/* Read and Transpose Chord File */
	snprintf(filepath, sizeof(filepath), "%s/data.txt", item_path);
	FILE *fp = fopen(filepath, "r");
	if (!fp) return NULL;

	fseek(fp, 0, SEEK_END);
	long fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	char *content = malloc(fsize + 1);
	if (!content) { fclose(fp); return NULL; }

	fread(content, 1, fsize, fp);
	fclose(fp);
	content[fsize] = '\0';

	char *transposed = transp_buffer(g_transp_ctx, content, transpose, flags);
	free(content);

	if (!transposed) return NULL;

	/* Escape all fields for JSON safety */
	size_t data_esc_len = 3 * strlen(transposed);
	char *escaped_data = malloc(data_esc_len);
	if (!escaped_data) { free(transposed); return NULL; }

	call_json_escape(transposed, escaped_data, data_esc_len);
	free(transposed);

	call_json_escape(title, esc_title, sizeof(esc_title));
	call_json_escape(yt, esc_yt, sizeof(esc_yt));
	call_json_escape(audio, esc_audio, sizeof(esc_audio));
	call_json_escape(pdf, esc_pdf, sizeof(esc_pdf));

	/* Build the final JSON string */
	size_t resp_len = strlen(escaped_data) + strlen(esc_title) +
		strlen(esc_yt) + strlen(esc_audio) +
		strlen(esc_pdf) + 512;

	char *response = malloc(resp_len);
	if (response) {
		snprintf(response, resp_len,
				"{"
				"\"title\":\"%s\","
				"\"data\":\"%s\","
				"\"showMedia\":%d,"
				"\"yt\":\"%s\","
				"\"audio\":\"%s\","
				"\"pdf\":\"%s\""
				"}",
				esc_title, escaped_data, show_media, esc_yt, esc_audio, esc_pdf);
	}

	free(escaped_data);
	return response;
}

/* GET /api/song/:id/edit - JSON API for transposition */
static int
api_song_transpose_handler(int fd, char *body)
{
	(void)body;

	char *json = song_json(fd, 0);

	if (!json) {
		ndc_header(fd, "Content-Type", "application/json");
		ndc_head(fd, 500);
		ndc_body(fd, "{\"error\":\"Failed to generate content\"}");
		return 0;
	}

	ndc_header(fd, "Content-Type", "application/json");
	ndc_head(fd, 200);
	ndc_body(fd, json);
	
	free(json);
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
	
	char *json = song_json(fd, TRANSP_HTML);
	int result = call_core_post(fd, json, strlen(json));
	free(json);
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
	pos += call_url_encode(title, post_body + pos, sizeof(post_body) - pos);
	
	pos += snprintf(post_body + pos, sizeof(post_body) - pos, "&type=");
	pos += call_url_encode(type, post_body + pos, sizeof(post_body) - pos);
	
	pos += snprintf(post_body + pos, sizeof(post_body) - pos, "&yt=");
	pos += call_url_encode(yt, post_body + pos, sizeof(post_body) - pos);
	
	pos += snprintf(post_body + pos, sizeof(post_body) - pos, "&audio=");
	pos += call_url_encode(audio, post_body + pos, sizeof(post_body) - pos);
	
	pos += snprintf(post_body + pos, sizeof(post_body) - pos, "&pdf=");
	pos += call_url_encode(pdf, post_body + pos, sizeof(post_body) - pos);

	if (data_content && data_len > 0) {
		pos += snprintf(post_body + pos, sizeof(post_body) - pos, "&data=");
		pos += call_url_encode(data_content, post_body + pos, sizeof(post_body) - pos);
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

	ndx_load("./mods/common/common");
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
