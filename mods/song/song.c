#include <ttypt/ndx-mod.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
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
#define VIEWER_ZOOM_MIN 70
#define VIEWER_ZOOM_MAX 170
#define VIEWER_ZOOM_DEFAULT 100
#define VIEWER_BOOL_DEFAULT 0

/* Global transpose context (initialized once in ndx_install) */
static transp_ctx_t *g_transp_ctx = NULL;

/* Type index: type -> qmap of song IDs */
static unsigned type_index_hd = 0;

typedef struct {
	char title[256];
	char type[256];
	char yt[512];
	char audio[512];
	char pdf[512];
	char author[256];
} song_meta_t;

static int song_viewer_pref_write_raw(const char *username, const char *name,
	const char *value);

static int
song_viewer_zoom_clamp(int zoom)
{
	if (zoom < VIEWER_ZOOM_MIN || zoom > VIEWER_ZOOM_MAX)
		return VIEWER_ZOOM_DEFAULT;
	return zoom;
}

static int
song_viewer_pref_path(const char *username, const char *name,
	char *out, size_t out_sz)
{
	char suffix[PATH_MAX];

	if (!username || !username[0] || !name || !name[0] || !out || out_sz == 0)
		return -1;
	snprintf(suffix, sizeof(suffix), ".tty/%s", name);
	return user_path_build(username, suffix, out, out_sz);
}

static int
song_viewer_pref_dir(const char *username, char *out, size_t out_sz)
{
	if (!username || !username[0] || !out || out_sz == 0)
		return -1;
	return user_path_build(username, ".tty", out, out_sz);
}

static int
song_viewer_pref_write_int(const char *username, const char *name, int value)
{
	char buf[16];
	int len = snprintf(buf, sizeof(buf), "%d", value);

	if (len < 0 || (size_t)len >= sizeof(buf))
		return -1;
	return song_viewer_pref_write_raw(username, name, buf);
}

static int
song_viewer_pref_write_raw(const char *username, const char *name,
	const char *value)
{
	char tty_dir[PATH_MAX];
	char path[PATH_MAX];

	if (song_viewer_pref_dir(username, tty_dir, sizeof(tty_dir)) != 0)
		return -1;
	if (ensure_dir_path(tty_dir) != 0)
		return -1;
	if (song_viewer_pref_path(username, name, path, sizeof(path)) != 0)
		return -1;
	return write_file_path(path, value ? value : "", value ? strlen(value) : 0);
}

static int
song_viewer_pref_read_int(const char *username, const char *name, int fallback)
{
	char path[PATH_MAX];
	char *raw = NULL;
	int value = fallback;

	if (song_viewer_pref_path(username, name, path, sizeof(path)) != 0)
		return fallback;

	raw = slurp_file(path);
	if (!raw)
		return fallback;
	if (raw[0])
		value = atoi(raw);
	free(raw);
	return value;
}

static int
song_viewer_pref_read_bool(const char *username, const char *name)
{
	return song_viewer_pref_read_int(username, name, VIEWER_BOOL_DEFAULT) ? 1 : 0;
}

static int
song_viewer_pref_write_bool(const char *username, const char *name, int value)
{
	return song_viewer_pref_write_int(username, name, value ? 1 : 0);
}

NDX_LISTENER(int, song_get_viewer_zoom, const char *, username)
{
	return song_viewer_zoom_clamp(
		song_viewer_pref_read_int(username, "chords-zoom", VIEWER_ZOOM_DEFAULT));
}

NDX_LISTENER(int, song_set_viewer_zoom, const char *, username, int, zoom)
{
	return song_viewer_pref_write_int(username, "chords-zoom",
		song_viewer_zoom_clamp(zoom));
}

static void
song_meta_read(const char *item_path, song_meta_t *meta)
{
	meta_field_t fields[] = {
		{ "title", meta->title, sizeof(meta->title) },
		{ "type", meta->type, sizeof(meta->type) },
		{ "yt", meta->yt, sizeof(meta->yt) },
		{ "audio", meta->audio, sizeof(meta->audio) },
		{ "pdf", meta->pdf, sizeof(meta->pdf) },
		{ "author", meta->author, sizeof(meta->author) },
	};

	memset(meta, 0, sizeof(*meta));
	meta_fields_read(item_path, fields, sizeof(fields) / sizeof(fields[0]));
}

static void
song_meta_write(const char *item_path, const song_meta_t *meta)
{
	meta_field_t fields[] = {
		{ "title", (char *)meta->title, sizeof(meta->title) },
		{ "type", (char *)meta->type, sizeof(meta->type) },
		{ "yt", (char *)meta->yt, sizeof(meta->yt) },
		{ "audio", (char *)meta->audio, sizeof(meta->audio) },
		{ "pdf", (char *)meta->pdf, sizeof(meta->pdf) },
		{ "author", (char *)meta->author, sizeof(meta->author) },
	};

	meta_fields_write(item_path, fields, sizeof(fields) / sizeof(fields[0]));
}

static int
song_data_path_build(const char *item_path, char *out, size_t out_sz)
{
	return item_child_path(item_path, "data.txt", out, out_sz);
}

static int
song_item_path_build(const char *doc_root, const char *id, char *out, size_t out_sz)
{
	return item_path_build_root(doc_root, "song", id, out, out_sz);
}

NDX_LISTENER(int, song_read_title,
	const char *, doc_root, const char *, song_id, char *, out, size_t, out_sz)
{
	char item_path[PATH_MAX];
	if (song_item_path_build(doc_root, song_id, item_path, sizeof(item_path)) != 0)
		return -1;
	return read_meta_file(item_path, "title", out, out_sz);
}

static int
song_read_type(const char *doc_root, const char *song_id, char *out, size_t out_sz)
{
	char item_path[PATH_MAX];
	if (song_item_path_build(doc_root, song_id, item_path, sizeof(item_path)) != 0)
		return -1;
	return read_meta_file(item_path, "type", out, out_sz);
}

static char *
song_read_data_file(const char *doc_root, const char *song_id)
{
	char item_path[PATH_MAX];
	char data_path[PATH_MAX];
	if (song_item_path_build(doc_root, song_id, item_path, sizeof(item_path)) != 0)
		return NULL;
	if (song_data_path_build(item_path, data_path, sizeof(data_path)) != 0)
		return NULL;
	return slurp_file(data_path);
}

NDX_LISTENER(int, song_transpose_root,
	const char *, doc_root, const char *, song_id,
	int, semitones, int, flags, char **, output, int *, key)
{
	if (!g_transp_ctx || !song_id || !song_id[0])
		return -1;

	char *content = song_read_data_file(doc_root, song_id);
	if (!content)
		return -1;

	transp_reset_key(g_transp_ctx);
	char *result = transp_buffer(g_transp_ctx, content, semitones, flags);
	free(content);

	int detected_key = transp_get_key(g_transp_ctx);
	if (key)
		*key = detected_key < 0 ? 0 : detected_key;

	if (output) {
		*output = result;
	} else if (result) {
		free(result);
	}

	return 0;
}

static void build_type_index(const char *doc_root)
{
	type_index_hd = qmap_open(NULL, "type_idx", QM_STR, QM_STR, 0x3FF, 0);
	if (!type_index_hd) {
		fprintf(stderr, "[song] Failed to open type index\n");
		return;
	}

	char songs_path[512];
	if (module_items_path_build(doc_root, "song",
			songs_path, sizeof(songs_path)) != 0)
		return;

	DIR *dir = opendir(songs_path);
	if (!dir) {
		perror("opendir songs");
		return;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;

		char song_type[64] = "any";
		song_read_type(doc_root, entry->d_name, song_type, sizeof(song_type));

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

NDX_LISTENER(int, song_get_random_by_type, const char *, type, char **, out_id)
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
NDX_LISTENER(int, song_transpose, const char *, input, int, semitones, int, flags, char **, output, int *, key)
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
NDX_LISTENER(int, song_reset_key, int, dummy)
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

static int
api_song_viewer_prefs_handler(int fd, char *body)
{
	const char *username = get_request_user(fd);
	char bool_buf[8] = {0};
	char zoom_buf[16] = {0};
	int has_value = 0;

	if (!username || !username[0])
		return respond_plain(fd, 204, "");

	if (!body || ndc_query_parse(body) != 0)
		return bad_request(fd, "Failed to parse viewer prefs");

	if (ndc_query_param("v", zoom_buf, sizeof(zoom_buf) - 1) >= 0) {
		if (song_set_viewer_zoom(username, atoi(zoom_buf)) != 0)
			return server_error(fd, "Failed to save zoom");
		has_value = 1;
	}

	if (ndc_query_param("b", bool_buf, sizeof(bool_buf) - 1) >= 0) {
		if (song_viewer_pref_write_bool(username, "chords-bemol",
				atoi(bool_buf) != 0) != 0)
			return server_error(fd, "Failed to save flats option");
		has_value = 1;
	}

	if (ndc_query_param("l", bool_buf, sizeof(bool_buf) - 1) >= 0) {
		if (song_viewer_pref_write_bool(username, "chords-latin",
				atoi(bool_buf) != 0) != 0)
			return server_error(fd, "Failed to save latin option");
		has_value = 1;
	}

	if (!has_value)
		return bad_request(fd, "Missing viewer prefs");

	return respond_plain(fd, 204, "");
}


char *song_json(int fd, int flags, int sync_viewer_prefs) {
	char doc_root[256] = {0};
	char query[1024] = {0};
	char id[128] = {0};
	int transpose = 0;
	int show_media = 0;
	char item_path[512];
	char filepath[552];
	song_meta_t meta;
	const char *username = NULL;
	int use_bemol = 0;
	int use_latin = 0;
	int viewer_zoom = VIEWER_ZOOM_DEFAULT;
	int viewer_bemol = VIEWER_BOOL_DEFAULT;
	int viewer_latin = VIEWER_BOOL_DEFAULT;

	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	ndc_env_get(fd, query, "QUERY_STRING");
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	if (!id[0]) return NULL;

	username = get_request_user(fd);
	if (username && *username) {
		viewer_zoom = song_get_viewer_zoom(username);
		viewer_bemol = song_viewer_pref_read_bool(username, "chords-bemol");
		viewer_latin = song_viewer_pref_read_bool(username, "chords-latin");
	}

	if (query[0]) {
		if (parse_transpose_params(query, &transpose, &flags, &show_media) != 0)
			return NULL;
		use_bemol = (flags & TRANSP_BEMOL) != 0;
		use_latin = (flags & TRANSP_LATIN) != 0;
		if (sync_viewer_prefs && username && *username) {
			if (song_viewer_pref_write_bool(username, "chords-bemol",
					use_bemol) != 0)
				return NULL;
			if (song_viewer_pref_write_bool(username, "chords-latin",
					use_latin) != 0)
				return NULL;
			viewer_bemol = use_bemol;
			viewer_latin = use_latin;
		}
	} else {
		use_bemol = viewer_bemol;
		use_latin = viewer_latin;
		if (use_bemol)
			flags |= TRANSP_BEMOL;
		if (use_latin)
			flags |= TRANSP_LATIN;
	}

	if (song_item_path_build(doc_root[0] ? doc_root : ".", id,
			item_path, sizeof(item_path)) != 0)
		return NULL;

	song_meta_read(item_path, &meta);

	/* Read and Transpose Chord File */
	if (song_data_path_build(item_path, filepath, sizeof(filepath)) != 0)
		return NULL;
	char *content = slurp_file(filepath);

	char *transposed = NULL;

	/* Reset key detection for each new song */
	transp_reset_key(g_transp_ctx);

	if (content) {
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

	/* Determine ownership */
	int is_owner = 0;
	{
		if (username && *username)
			is_owner = item_check_ownership(item_path, username);
	}

	json_object_t *jo = json_object_new(0);
	if (!jo) {
		free(transposed);
		return NULL;
	}
	if (json_object_kv_str(jo, "title", meta.title) != 0 ||
			json_object_kv_str(jo, "data", transposed) != 0 ||
			json_object_kv_bool(jo, "showMedia", show_media) != 0 ||
			json_object_kv_str(jo, "yt", meta.yt) != 0 ||
			json_object_kv_str(jo, "audio", meta.audio) != 0 ||
			json_object_kv_str(jo, "pdf", meta.pdf) != 0 ||
			json_object_kv_int(jo, "originalKey", original_key) != 0 ||
			json_object_kv_int(jo, "viewerZoom", viewer_zoom) != 0 ||
			json_object_kv_bool(jo, "viewerBemol", use_bemol) != 0 ||
			json_object_kv_bool(jo, "viewerLatin", use_latin) != 0 ||
			json_object_kv_bool(jo, "owner", is_owner) != 0 ||
			json_object_kv_str(jo, "categories", meta.type) != 0 ||
			json_object_kv_str(jo, "author", meta.author) != 0) {
		json_object_free(jo);
		free(transposed);
		return NULL;
	}

	char *response = json_object_finish(jo);
	free(transposed);
	return response;
}

/* GET /api/song/:id/edit - JSON API for transposition */
static int
api_song_transpose_handler(int fd, char *body)
{
	(void)body;

	char *json = song_json(fd, 0, 0);

	if (!json)
		return respond_json(fd, 500, "{\"error\":\"Failed to generate content\"}");

	respond_json(fd, 200, json);
	free(json);
	return 0;
}


/* SSR handler for /song/... routes
 * Handles transposition when query params present, delegates to Deno
 */
static int
song_details_handler(int fd, char *body)
{
	(void)body;

	char *json = song_json(fd, TRANSP_HTML, 1);
	if (!json)
		return respond_error(fd, 404, "Song not found");
	int result = core_post_json(fd, json);
	free(json);
	return result;
}

/* GET /song/:id/edit - read files and proxy to Fresh */
static int
song_edit_get_handler(int fd, char *body)
{
	(void)body;

	item_ctx_t ctx;
	if (item_ctx_load(&ctx, fd, CHORDS_ITEMS_PATH,
			ICTX_NEED_LOGIN)) return 1;

	if (item_require_access(fd, ctx.item_path, ctx.username,
			ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
			"Song not found", "Forbidden"))
		return 1;

	song_meta_t meta;
	song_meta_read(ctx.item_path, &meta);

	/* Read data */
	char data_path[PATH_MAX];
	song_data_path_build(ctx.item_path, data_path, sizeof(data_path));
	char *data_content = slurp_file(data_path);

	/* Build POST body for Fresh */
	form_body_t *fb = form_body_new(0);
	if (!fb) {
		if (data_content) free(data_content);
		return respond_error(fd, 500, "OOM");
	}
	form_body_add(fb, "title", meta.title);
	form_body_add(fb, "type", meta.type);
	form_body_add(fb, "yt", meta.yt);
	form_body_add(fb, "audio", meta.audio);
	form_body_add(fb, "pdf", meta.pdf);
	if (data_content && data_content[0]) {
		form_body_add(fb, "data", data_content);
	}
	if (data_content) free(data_content);
	form_body_add(fb, "author", meta.author);

	return core_post_form(fd, fb);
}

/* POST /song/:id/edit - save edit */
static int
song_edit_post_handler(int fd, char *body)
{
	item_ctx_t ctx;
	if (item_ctx_load(&ctx, fd, CHORDS_ITEMS_PATH,
			ICTX_NEED_LOGIN)) return 1;

	int parse_result = ndc_query_parse(body);
	if (parse_result == -1)
		return bad_request(fd, "Failed to parse form body");

	if (item_require_access(fd, ctx.item_path, ctx.username,
			ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
			"Song not found", "You don't own this song"))
		return 1;

	song_meta_t meta = {0};
	int title_len = ndc_query_param("title", meta.title, sizeof(meta.title) - 1);
	if (title_len > 0)
		meta.title[title_len] = '\0';
	int type_len = ndc_query_param("type", meta.type, sizeof(meta.type) - 1);
	if (type_len > 0)
		meta.type[type_len] = '\0';
	ndc_query_param("yt", meta.yt, sizeof(meta.yt) - 1);
	ndc_query_param("audio", meta.audio, sizeof(meta.audio) - 1);
	ndc_query_param("pdf", meta.pdf, sizeof(meta.pdf) - 1);
	ndc_query_param("author", meta.author, sizeof(meta.author) - 1);

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

	song_meta_write(ctx.item_path, &meta);

	/* Write data file */
	{
		char data_path[PATH_MAX];
		song_data_path_build(ctx.item_path, data_path, sizeof(data_path));
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
	snprintf(location, sizeof(location), "/song/%s", ctx.id);
	return redirect(fd, location);
}

/* Remove song id from type_index_hd when a song is deleted */
static void song_cleanup(const char *id)
{
	char item_path[PATH_MAX];
	char song_type[64] = {0};

	if (!type_index_hd)
		return;

	if (song_read_type(".", id, song_type, sizeof(song_type)) != 0)
		return;
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
NDX_LISTENER(int, song_get_original_key_root,
	const char *, doc_root, const char *, song_id)
{
	int key = 0;
	if (song_transpose_root(doc_root, song_id, 0, 0, NULL, &key) != 0)
		return 0;
	return key;
}

NDX_LISTENER(int, song_get_original_key, const char *, song_id)
{
	return song_get_original_key_root(".", song_id);
}

/* Parse a colon-separated item line: id:int_field:format\n */
NDX_LISTENER(int, parse_item_line,
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
NDX_LISTENER(char *, song_get_types_json, int, dummy)
{
	(void)dummy;
	json_array_t *ja = json_array_new(0);
	if (!ja) return NULL;

	unsigned cur = qmap_iter(type_index_hd, NULL, 0);
	const void *key, *val;
	while (qmap_next(&key, &val, cur)) {
		const char *type = (const char *)key;
		char esc[256];
		json_escape(type, esc, sizeof(esc));
		char entry[260];
		snprintf(entry, sizeof(entry), "\"%s\"", esc);
		json_array_append_raw(ja, entry);
	}
	return json_array_finish(ja);
}

/* Build a JSON array of all songs from items/song/items/.
 * If include_type is non-zero each object includes a "type" field.
 * Returns a malloc'd string the caller must free, or NULL on error. */
NDX_LISTENER(char *, build_all_songs_json, const char *, doc_root, int, include_type)
{
	json_array_t *ja = json_array_new(0);
	if (!ja) return NULL;

	char songs_dir[512];
	snprintf(songs_dir, sizeof(songs_dir), "%s/%s", doc_root, CHORDS_ITEMS_PATH);

	DIR *dp = opendir(songs_dir);
	if (dp) {
		struct dirent *de;
		while ((de = readdir(dp)) != NULL) {
			if (de->d_name[0] == '.') continue;

			char item_path[PATH_MAX];
			snprintf(item_path, sizeof(item_path), "%s/%s", songs_dir, de->d_name);

			song_meta_t meta;
			song_meta_read(item_path, &meta);

			json_array_begin_object(ja);
			json_array_kv_str(ja, "id", de->d_name);
			json_array_kv_str(ja, "title", meta.title);
			if (include_type)
				json_array_kv_str(ja, "type", meta.type);
			json_array_end_object(ja);
		}
		closedir(dp);
	}
	return json_array_finish(ja);
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

	ndx_load("./mods/common/common");
	ndx_load("./mods/index/index");
	ndx_load("./mods/mpfd/mpfd");
	ndx_load("./mods/auth/auth");

	index_open("Song", 0, 1, song_cleanup);
	build_type_index(doc_root);

	ndc_register_handler("GET:/song/:id",
			song_details_handler);
	ndc_register_handler("GET:/song/:id/edit",
			song_edit_get_handler);
	ndc_register_handler("POST:/song/:id/edit",
			song_edit_post_handler);

	ndc_register_handler("GET:/api/song/:id/transpose",
			api_song_transpose_handler);
	ndc_register_handler("POST:/api/song/prefs",
			api_song_viewer_prefs_handler);
}
