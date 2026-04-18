#include <ttypt/ndx-mod.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <dirent.h>

#include <ttypt/ndc.h>
#include <ttypt/qmap.h>
#include "../mpfd/mpfd.h"
#include "../index/index.h"
#include "../common/common.h"
#include "../auth/auth.h"
#include "../../lib/transp/transp.h"

#define CHORDS_ITEMS_PATH "items/song/items"

/* Global transpose context (initialized once in ndx_install) */
static transp_ctx_t *g_transp_ctx = NULL;

/* Type index: type -> qmap of song IDs */
static unsigned type_index_hd = 0;

static void build_type_index(const char *doc_root)
{
	type_index_hd = qmap_open(NULL, "type_idx", QM_STR, QM_STR, 0x3FF, 0);
	if (!type_index_hd) {
		fprintf(stderr, "[song] Failed to open type index\n");
		return;
	}

	char songs_path[512];
	snprintf(songs_path, sizeof(songs_path), "%s/%s", doc_root, CHORDS_ITEMS_PATH);

	DIR *dir = opendir(songs_path);
	if (!dir) {
		perror("opendir songs");
		return;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;

		char type_path[PATH_MAX];
		snprintf(type_path, sizeof(type_path), "%s/%s/type",
			songs_path, entry->d_name);

		char song_type[64] = "any";
		FILE *tfp = fopen(type_path, "r");
		if (tfp) {
			if (fgets(song_type, sizeof(song_type) - 1, tfp)) {
				size_t len = strlen(song_type);
				if (len > 0 && song_type[len - 1] == '\n')
					song_type[len - 1] = '\0';
			}
			fclose(tfp);
		}

		/* Get or create the song list for this type */
		char *song_list_val = (char *)qmap_get(type_index_hd, song_type);
		char song_list[8192] = {0};
		if (song_list_val) {
			snprintf(song_list, sizeof(song_list), "%s", song_list_val);
		}

		/* Append this song ID (comma-separated) */
		size_t current_len = strlen(song_list);
		if (current_len > 0 && current_len < sizeof(song_list) - 2) {
			strcat(song_list, ",");
		}
		if (current_len < sizeof(song_list) - strlen(entry->d_name) - 2) {
			strcat(song_list, entry->d_name);
		}

		qmap_put(type_index_hd, song_type, song_list);
	}

	closedir(dir);

	fprintf(stderr, "[song] Type index built\n");
}

NDX_DEF(int, song_get_random_by_type, const char *, type, char **, out_id)
{
	if (!type || !type_index_hd) {
		return -1;
	}

	/* Get list of songs for this type */
	char *song_list = (char *)qmap_get(type_index_hd, type);
	if (!song_list || !*song_list) {
		/* Try "any" as fallback */
		song_list = (char *)qmap_get(type_index_hd, "any");
		if (!song_list || !*song_list) {
			return -1;
		}
	}

	/* Count songs */
	int count = 1;
	for (char *p = song_list; *p; p++) {
		if (*p == ',') count++;
	}

	/* Pick random */
	int idx = rand() % count;

	/* Find and copy the idx-th song */
	char *copy = strdup(song_list);
	if (!copy) return -1;

	char *token = strtok(copy, ",");
	for (int i = 0; i < idx && token; i++) {
		token = strtok(NULL, ",");
	}

	if (token && out_id) {
		*out_id = strdup(token);
	}

	free(copy);

	return (*out_id) ? 0 : -1;
}

/* 
 * NDX API: Transpose chord chart text
 */
NDX_DEF(int, song_transpose, const char *, input, int, semitones, int, flags, char **, output, int *, key)
{
	if (!g_transp_ctx || !input || (!output && !key)) {
		return -1;
	}
	
	char *result = transp_buffer(g_transp_ctx, input, semitones, flags);
	if (!result) {
		return -1;
	}
	
	if (output) {
		*output = result;
	} else {
		free(result);
	}
	
	if (key) {
		*key = transp_get_key(g_transp_ctx);
	}
	
	return 0;
}

/*
 * NDX API: Reset transpose key detection
 */
NDX_DEF(int, song_reset_key, int, dummy)
{
	(void)dummy;
	transp_reset_key(g_transp_ctx);
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
	call_read_meta_file(item_path, "title", title, sizeof(title));
	call_read_meta_file(item_path, "yt", yt, sizeof(yt));
	call_read_meta_file(item_path, "audio", audio, sizeof(audio));
	call_read_meta_file(item_path, "pdf", pdf, sizeof(pdf));

	/* Read and Transpose Chord File */
	snprintf(filepath, sizeof(filepath), "%s/data.txt", item_path);
	FILE *fp = fopen(filepath, "r");

	char *transposed = NULL;

	/* Reset key detection for each new song */
	transp_reset_key(g_transp_ctx);

	if (fp) {
		fseek(fp, 0, SEEK_END);
		long fsize = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		char *content = malloc(fsize + 1);
		if (!content) { fclose(fp); return NULL; }

		fread(content, 1, fsize, fp);
		fclose(fp);
		content[fsize] = '\0';

		transposed = transp_buffer(g_transp_ctx, content, transpose, flags);
		free(content);

		if (!transposed) return NULL;
	} else {
		/* No data.txt yet — treat as empty chord sheet */
		transposed = strdup("");
		if (!transposed) return NULL;
	}

	/* Get detected key after transposition */
	int original_key = transp_get_key(g_transp_ctx);
	if (original_key < 0) original_key = 0;

	/* Escape all fields for JSON safety */
	size_t data_esc_len = 3 * strlen(transposed) + 1;
	char *escaped_data = malloc(data_esc_len);
	if (!escaped_data) { free(transposed); return NULL; }

	call_json_escape(transposed, escaped_data, data_esc_len);
	free(transposed);

	call_json_escape(title, esc_title, sizeof(esc_title));
	call_json_escape(yt, esc_yt, sizeof(esc_yt));
	call_json_escape(audio, esc_audio, sizeof(esc_audio));
	call_json_escape(pdf, esc_pdf, sizeof(esc_pdf));

	/* Determine ownership */
	int is_owner = 0;
	{
		const char *username = call_get_request_user(fd);
		if (username && *username)
			is_owner = call_item_check_ownership(item_path, username);
	}

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
				"\"pdf\":\"%s\","
				"\"originalKey\":%d,"
				"\"owner\":%s"
				"}",
				esc_title, escaped_data, show_media, esc_yt, esc_audio, esc_pdf, original_key,
				is_owner ? "true" : "false");
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


/* SSR handler for /song/* routes
 * Handles transposition when query params present, delegates to Deno
 */
static int
song_details_handler(int fd, char *body)
{
	(void)body;
	
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

	/* Auth + ownership check */
	const char *username = call_get_request_user(fd);
	if (call_require_login(fd, username))
		return 1;

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

	if (!call_item_check_ownership(item_path, username)) {
		struct stat st;
		if (stat(item_path, &st) != 0) {
			ndc_header(fd, "Content-Type", "text/plain");
			ndc_head(fd, 404);
			ndc_body(fd, "Song not found");
			return 1;
		}
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "Forbidden");
		return 1;
	}

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
	char doc_root[256] = { 0 };
	char id[128] = { 0 };

	/* Auth check */
	const char *username = call_get_request_user(fd);
	if (call_require_login(fd, username))
		return 1;

	int parse_result = call_query_parse(body);
	if (parse_result == -1) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Failed to parse form body");
		return 1;
	}

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

	/* Ownership check */
	if (!call_item_check_ownership(item_path, username)) {
		/* Distinguish 404 from 403 */
		struct stat st;
		if (stat(item_path, &st) != 0) {
			ndc_header(fd, "Content-Type", "text/plain");
			ndc_head(fd, 404);
			ndc_body(fd, "Song not found");
			return 1;
		}
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "You don't own this song");
		return 1;
	}

	/* Get title */
	char title[256] = { 0 };
	int title_len = call_query_param("title", title, sizeof(title) - 1);
	if (title_len > 0)
		title[title_len] = '\0';

	/* Get type */
	char type[256] = { 0 };
	int type_len = call_query_param("type", type, sizeof(type) - 1);
	if (type_len > 0)
		type[type_len] = '\0';

	/* Get yt */
	char yt[512] = { 0 };
	call_query_param("yt", yt, sizeof(yt) - 1);

	/* Get audio */
	char audio[512] = { 0 };
	call_query_param("audio", audio, sizeof(audio) - 1);

	/* Get pdf */
	char pdf[512] = { 0 };
	call_query_param("pdf", pdf, sizeof(pdf) - 1);

	/* Get data */
	char *data_content = NULL;
	int data_len = 0;
	{
		/* Use a generous fixed buffer for chord data */
		char data_buf[65536] = { 0 };
		int got = call_query_param("data", data_buf, sizeof(data_buf) - 1);
		if (got > 0) {
			data_len = got;
			data_content = malloc(data_len + 1);
			if (data_content) {
				memcpy(data_content, data_buf, data_len);
				data_content[data_len] = '\0';
			}
		}
	}

	/* Write title file */
	{
		char title_path[1024];
		snprintf(title_path, sizeof(title_path), "%s/title", item_path);
		FILE *tfp = fopen(title_path, "w");
		if (tfp) {
			fwrite(title, 1, strlen(title), tfp);
			fclose(tfp);
		}
	}

	/* Write type file */
	{
		char type_path[1024];
		snprintf(type_path, sizeof(type_path), "%s/type", item_path);
		FILE *tfp = fopen(type_path, "w");
		if (tfp) {
			fwrite(type, 1, strlen(type), tfp);
			fclose(tfp);
		}
	}

	/* Write yt file */
	{
		char yt_path[1024];
		snprintf(yt_path, sizeof(yt_path), "%s/yt", item_path);
		FILE *tfp = fopen(yt_path, "w");
		if (tfp) {
			fwrite(yt, 1, strlen(yt), tfp);
			fclose(tfp);
		}
	}

	/* Write audio file */
	{
		char audio_path[1024];
		snprintf(audio_path, sizeof(audio_path), "%s/audio", item_path);
		FILE *tfp = fopen(audio_path, "w");
		if (tfp) {
			fwrite(audio, 1, strlen(audio), tfp);
			fclose(tfp);
		}
	}

	/* Write pdf file */
	{
		char pdf_path[1024];
		snprintf(pdf_path, sizeof(pdf_path), "%s/pdf", item_path);
		FILE *tfp = fopen(pdf_path, "w");
		if (tfp) {
			fwrite(pdf, 1, strlen(pdf), tfp);
			fclose(tfp);
		}
	}

	/* Write data file */
	{
		char data_path[1024];
		snprintf(data_path, sizeof(data_path), "%s/data.txt", item_path);
		FILE *dfp = fopen(data_path, "w");
		if (dfp) {
			if (data_content)
				fwrite(data_content, 1, data_len, dfp);
			fclose(dfp);
		}
		free(data_content);
	}

	/* Redirect to song page */
	char location[256];
	snprintf(location, sizeof(location), "/song/%s", id);
	return call_redirect(fd, location);
}

/* Remove song id from type_index_hd when a song is deleted */
static void song_cleanup(const char *id)
{
	if (!type_index_hd)
		return;

	/* Read the song's type file */
	char type_path[512];
	snprintf(type_path, sizeof(type_path),
			"./items/song/items/%s/type", id);
	FILE *fp = fopen(type_path, "r");
	if (!fp)
		return;
	char song_type[64] = {0};
	if (!fgets(song_type, sizeof(song_type) - 1, fp)) {
		fclose(fp);
		return;
	}
	fclose(fp);
	song_type[strcspn(song_type, "\n")] = '\0';
	if (!song_type[0])
		return;

	const char *list = (const char *)qmap_get(type_index_hd, song_type);
	if (!list || !*list)
		return;

	/* Build new csv without id */
	char new_list[8192] = {0};
	char *copy = strdup(list);
	if (!copy)
		return;
	char *tok = strtok(copy, ",");
	int first = 1;
	while (tok) {
		if (strcmp(tok, id) != 0) {
			if (!first)
				strcat(new_list, ",");
			strcat(new_list, tok);
			first = 0;
		}
		tok = strtok(NULL, ",");
	}
	free(copy);

	qmap_put(type_index_hd, song_type, new_list);
}

/* Parse a colon-separated item line: id:int_field:format\n */
NDX_DEF(int, parse_item_line,
	const char *, line, char *, id_out,
	int *, int_out, char *, format_out)
{
	char *colon1 = strchr(line, ':');
	if (!colon1)
		return -1;

	char *colon2 = strchr(colon1 + 1, ':');
	if (!colon2)
		return -1;

	size_t id_len = (size_t)(colon1 - line);
	if (id_len > 127) id_len = 127;
	strncpy(id_out, line, id_len);
	id_out[id_len] = '\0';

	*int_out = atoi(colon1 + 1);

	strncpy(format_out, colon2 + 1, 127);
	format_out[127] = '\0';

	size_t fmt_len = strlen(format_out);
	while (fmt_len > 0 && (format_out[fmt_len - 1] == '\n' || format_out[fmt_len - 1] == '\r'))
		format_out[--fmt_len] = '\0';

	return 0;
}

void ndx_install(void)
{
	char doc_root[256] = {0};
	ndc_env_get(0, doc_root, "DOCUMENT_ROOT");
	if (!doc_root[0])
		strcpy(doc_root, ".");

	/* Initialize transpose context */
	g_transp_ctx = transp_init();
	if (!g_transp_ctx)
		fprintf(stderr, "[song] Failed to initialize"
				" transp context\n");

	/* Build type index */
	build_type_index(doc_root);

	ndx_load("./mods/common/common");
	ndx_load("./mods/index/index");
	ndx_load("./mods/mpfd/mpfd");
	ndx_load("./mods/auth/auth");

	call_index_open("Song", 0, 1, song_cleanup);

	ndc_register_handler("GET:/song/:id",
			song_details_handler);
	ndc_register_handler("GET:/song/:id/edit",
			song_edit_get_handler);
	ndc_register_handler("POST:/song/:id/edit",
			song_edit_post_handler);

	ndc_register_handler("GET:/api/song/:id/transpose",
			api_song_transpose_handler);
}

void ndx_open(void) {}
