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

	if (ndc_query_parse(query_copy) != 0)
		return -1;

	char buf[32];

	if (ndc_query_param("t", buf, sizeof(buf)) > 0)
		*transpose = atoi(buf);

	if (ndc_query_param("b", buf, sizeof(buf)) >= 0)
		*flags |= TRANSP_BEMOL;

	if (ndc_query_param("l", buf, sizeof(buf)) >= 0)
		*flags |= TRANSP_LATIN;

	/* ADD THIS LINE: Sets the HTML flag so transp_buffer knows to wrap divs */
	if (ndc_query_param("h", buf, sizeof(buf)) >= 0)
		*flags |= TRANSP_HTML;

	if (ndc_query_param("m", buf, sizeof(buf)) >= 0)
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
	char categories[256] = {0}, author[256] = {0};
	char esc_title[512], esc_yt[1024], esc_audio[1024], esc_pdf[1024];
	char esc_categories[512], esc_author[512];

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
	call_read_meta_file(item_path, "type", categories, sizeof(categories));
	call_read_meta_file(item_path, "author", author, sizeof(author));

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
	call_json_escape(categories, esc_categories, sizeof(esc_categories));
	call_json_escape(author, esc_author, sizeof(esc_author));

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
		strlen(esc_pdf) + strlen(esc_categories) + strlen(esc_author) + 512;

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
				"\"owner\":%s,"
				"\"categories\":\"%s\","
				"\"author\":\"%s\""
				"}",
				esc_title, escaped_data, show_media, esc_yt, esc_audio, esc_pdf, original_key,
				is_owner ? "true" : "false",
				esc_categories, esc_author);
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

	if (!json)
		return call_respond_json(fd, 500, "{\"error\":\"Failed to generate content\"}");

	call_respond_json(fd, 200, json);
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

	if (!id[0])
		return call_respond_plain(fd, 400, "Missing song ID");

	char item_path[512];
	snprintf(item_path, sizeof(item_path), "%s/%s/%s",
		doc_root[0] ? doc_root : ".", CHORDS_ITEMS_PATH, id);

	if (!call_item_check_ownership(item_path, username)) {
		struct stat st;
		if (stat(item_path, &st) != 0)
			return call_respond_plain(fd, 404, "Song not found");
		return call_respond_plain(fd, 403, "Forbidden");
	}

	/* Read metadata */
	char title[256] = { 0 };
	char type[256] = { 0 };
	char yt[512] = { 0 };
	char audio[512] = { 0 };
	char pdf[512] = { 0 };
	char author[256] = { 0 };
	call_read_meta_file(item_path, "title", title, sizeof(title));
	call_read_meta_file(item_path, "type", type, sizeof(type));
	call_read_meta_file(item_path, "yt", yt, sizeof(yt));
	call_read_meta_file(item_path, "audio", audio, sizeof(audio));
	call_read_meta_file(item_path, "pdf", pdf, sizeof(pdf));
	call_read_meta_file(item_path, "author", author, sizeof(author));

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

	pos += snprintf(post_body + pos, sizeof(post_body) - pos, "&author=");
	pos += call_url_encode(author, post_body + pos, sizeof(post_body) - pos);

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

	int parse_result = ndc_query_parse(body);
	if (parse_result == -1)
		return call_respond_plain(fd, 400, "Failed to parse form body");

	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	if (!id[0])
		return call_respond_plain(fd, 400, "Missing song ID");

	char item_path[512];
	snprintf(item_path, sizeof(item_path), "%s/%s/%s",
		doc_root[0] ? doc_root : ".", CHORDS_ITEMS_PATH, id);

	/* Ownership check */
	if (!call_item_check_ownership(item_path, username)) {
		struct stat st;
		if (stat(item_path, &st) != 0)
			return call_respond_plain(fd, 404, "Song not found");
		return call_respond_plain(fd, 403, "You don't own this song");
	}

	/* Get title */
	char title[256] = { 0 };
	int title_len = ndc_query_param("title", title, sizeof(title) - 1);
	if (title_len > 0)
		title[title_len] = '\0';

	/* Get type */
	char type[256] = { 0 };
	int type_len = ndc_query_param("type", type, sizeof(type) - 1);
	if (type_len > 0)
		type[type_len] = '\0';

	/* Get yt */
	char yt[512] = { 0 };
	ndc_query_param("yt", yt, sizeof(yt) - 1);

	/* Get audio */
	char audio[512] = { 0 };
	ndc_query_param("audio", audio, sizeof(audio) - 1);

	/* Get pdf */
	char pdf[512] = { 0 };
	ndc_query_param("pdf", pdf, sizeof(pdf) - 1);

	/* Get author */
	char author[256] = { 0 };
	ndc_query_param("author", author, sizeof(author) - 1);

	/* Get data */
	char *data_content = NULL;
	int data_len = 0;
	{
		/* Use a generous fixed buffer for chord data */
		char data_buf[65536] = { 0 };
		int got = ndc_query_param("data", data_buf, sizeof(data_buf) - 1);
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
	call_write_meta_file(item_path, "title", title, strlen(title));

	/* Write type file */
	call_write_meta_file(item_path, "type", type, strlen(type));

	/* Write yt file */
	call_write_meta_file(item_path, "yt", yt, strlen(yt));

	/* Write audio file */
	call_write_meta_file(item_path, "audio", audio, strlen(audio));

	/* Write pdf file */
	call_write_meta_file(item_path, "pdf", pdf, strlen(pdf));

	/* Write author file */
	call_write_meta_file(item_path, "author", author, strlen(author));

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

/*
 * NDX API: Get original key of a song by reading its data.txt
 * Returns chromatic index 0-11, or 0 if undetectable.
 */
NDX_DEF(int, song_get_original_key, const char *, song_id)
{
	if (!g_transp_ctx || !song_id || !song_id[0])
		return 0;

	char data_path[PATH_MAX];
	snprintf(data_path, sizeof(data_path),
		"%s/%s/data.txt", CHORDS_ITEMS_PATH, song_id);

	FILE *fp = fopen(data_path, "r");
	if (!fp)
		return 0;

	fseek(fp, 0, SEEK_END);
	long fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (fsize <= 0) {
		fclose(fp);
		return 0;
	}

	char *content = malloc(fsize + 1);
	if (!content) {
		fclose(fp);
		return 0;
	}

	fread(content, 1, fsize, fp);
	fclose(fp);
	content[fsize] = '\0';

	transp_reset_key(g_transp_ctx);
	char *out = transp_buffer(g_transp_ctx, content, 0, 0);
	free(content);
	if (out) free(out);

	int key = transp_get_key(g_transp_ctx);
	return key < 0 ? 0 : key;
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

/* Build a JSON array of all known song types from the type index.
 * Returns a malloc'd string the caller must free, or NULL on error. */
NDX_DEF(char *, song_get_types_json, int, dummy)
{
	(void)dummy;
	size_t cap = 4096;
	char *json = malloc(cap);
	if (!json) return NULL;
	strcpy(json, "[");
	int first = 1;

	unsigned cur = qmap_iter(type_index_hd, NULL, 0);
	const void *key, *val;
	while (qmap_next(&key, &val, cur)) {
		const char *type = (const char *)key;
		char esc[256];
		call_json_escape(type, esc, sizeof(esc));
		size_t needed = strlen(json) + strlen(esc) + 8;
		if (needed >= cap) {
			cap *= 2;
			char *tmp = realloc(json, cap);
			if (!tmp) { free(json); return NULL; }
			json = tmp;
		}
		if (!first) strcat(json, ",");
		first = 0;
		strcat(json, "\"");
		strcat(json, esc);
		strcat(json, "\"");
	}
	strcat(json, "]");
	return json;
}

/* Build a JSON array of all songs from items/song/items/.
 * If include_type is non-zero each object includes a "type" field.
 * Returns a malloc'd string the caller must free, or NULL on error. */
NDX_DEF(char *, build_all_songs_json, const char *, doc_root, int, include_type)
{
	size_t cap = 65536;
	char *json = malloc(cap);
	if (!json) return NULL;
	strcpy(json, "[");
	int first = 1;

	char songs_dir[512];
	snprintf(songs_dir, sizeof(songs_dir), "%s/%s", doc_root, CHORDS_ITEMS_PATH);

	DIR *dp = opendir(songs_dir);
	if (dp) {
		struct dirent *de;
		while ((de = readdir(dp)) != NULL) {
			if (de->d_name[0] == '.') continue;

			char item_path[PATH_MAX];
			snprintf(item_path, sizeof(item_path), "%s/%s", songs_dir, de->d_name);

			char stitle[256] = {0};
			call_read_meta_file(item_path, "title", stitle, sizeof(stitle));

			char esc_id[256], esc_title[512];
			call_json_escape(de->d_name, esc_id, sizeof(esc_id));
			call_json_escape(stitle, esc_title, sizeof(esc_title));

			char entry[1200];
			if (include_type) {
				char stype[64] = {0};
				call_read_meta_file(item_path, "type", stype, sizeof(stype));
				char esc_type[128];
				call_json_escape(stype, esc_type, sizeof(esc_type));
				snprintf(entry, sizeof(entry),
					"%s{\"id\":\"%s\",\"title\":\"%s\",\"type\":\"%s\"}",
					first ? "" : ",", esc_id, esc_title, esc_type);
			} else {
				snprintf(entry, sizeof(entry),
					"%s{\"id\":\"%s\",\"title\":\"%s\"}",
					first ? "" : ",", esc_id, esc_title);
			}
			first = 0;

			size_t cur = strlen(json);
			size_t need = cur + strlen(entry) + 4;
			if (need >= cap) {
				cap = need * 2;
				char *tmp = realloc(json, cap);
				if (!tmp) { free(json); closedir(dp); return NULL; }
				json = tmp;
			}
			strcat(json, entry);
		}
		closedir(dp);
	}
	strcat(json, "]");
	return json;
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
