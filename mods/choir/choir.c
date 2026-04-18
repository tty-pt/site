#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx-mod.h>
#include <ttypt/ndx.h>
#include <ttypt/qmap.h>

#include "../index/index.h"
#include "../common/common.h"
#include "../auth/auth.h"
#include "../mpfd/mpfd.h"
#include "../song/song.h"

#define CHOIR_SONGS_PATH "items/choir/items"

/* POST /api/choir/:id/edit - Edit choir title and formats */
static int
handle_choir_edit(int fd, char *body)
{
	const char *username = call_get_request_user(fd);
	if (call_require_login(fd, username)) return 1;

	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	char doc_root[256] = {0};
	call_get_doc_root(fd, doc_root, sizeof(doc_root));

	char choir_path[512];
	snprintf(choir_path, sizeof(choir_path), "%s/items/choir/items/%s",
		doc_root, id);

	struct stat st;
	if (stat(choir_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 404);
		ndc_body(fd, "Choir not found");
		return 1;
	}

	if (call_require_ownership(fd, choir_path, username, "You don't own this choir")) return 1;

	call_mpfd_parse(fd, body);

	char title[256] = {0};
	char format[2048] = {0};
	int title_len = call_mpfd_get("title", title, sizeof(title) - 1);
	int format_len = call_mpfd_get("format", format, sizeof(format) - 1);

	if (title_len > 0) {
		title[title_len] = '\0';
		char title_path[PATH_MAX];
		snprintf(title_path, sizeof(title_path), "%s/title", choir_path);
		FILE *tfp = fopen(title_path, "w");
		if (tfp) {
			fwrite(title, 1, strlen(title), tfp);
			fclose(tfp);
		}
	}

	if (format_len > 0) {
		format[format_len] = '\0';
		char format_path[PATH_MAX];
		snprintf(format_path, sizeof(format_path), "%s/format", choir_path);
		FILE *ffp = fopen(format_path, "w");
		if (ffp) {
			fwrite(format, 1, strlen(format), ffp);
			fclose(ffp);
		}
	}

	char location[256];
	snprintf(location, sizeof(location), "/choir/%s", id);
	return call_redirect(fd, location);
}

static char *
choir_json(int fd)
{
	char doc_root[256] = {0};
	char id[128] = {0};
	char choir_path[512];

	char title[256] = {0}, owner[64] = {0}, counter[32] = {0}, format[2048] = {0};
	char esc_title[512], esc_owner[128], esc_counter[64], esc_format[4096];

	call_get_doc_root(fd, doc_root, sizeof(doc_root));
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	if (!id[0]) return NULL;

	snprintf(choir_path, sizeof(choir_path), "%s/items/choir/items/%s", doc_root, id);

	if (call_read_meta_file(choir_path, "title", title, sizeof(title)) != 0)
		return NULL;

	/* Get owner username */
	call_item_read_owner(choir_path, owner, sizeof(owner));

	if (call_read_meta_file(choir_path, "counter", counter, sizeof(counter)) != 0)
		snprintf(counter, sizeof(counter), "0");

	char path_buf[PATH_MAX];
	snprintf(path_buf, sizeof(path_buf), "%s/format", choir_path);
	FILE *ffp = fopen(path_buf, "r");
	if (ffp) {
		size_t n = fread(format, 1, sizeof(format) - 1, ffp);
		format[n] = '\0';
		if (n > 0 && format[n - 1] == '\n') format[n - 1] = '\0';
		fclose(ffp);
	}

	char songs_json[16384] = "[";
	int first = 1;

	char songs_path[PATH_MAX];
	snprintf(songs_path, sizeof(songs_path), "%s/songs", choir_path);
	FILE *sfp = fopen(songs_path, "r");
	if (sfp) {
		char line[256];
		while (fgets(line, sizeof(line), sfp)) {
			char sid[128] = {0};
			int pkey = 0;
			char fmt[64] = {0};

			if (call_parse_item_line(line, sid, &pkey, fmt) != 0)
				continue;

			if (!sid[0])
				continue;

			char song_title[256] = {0};
			char song_path[PATH_MAX];
			snprintf(song_path, sizeof(song_path), "%s/items/song/items/%s",
				doc_root, sid);
			call_read_meta_file(song_path, "title", song_title, sizeof(song_title));

			char esc_stitle[512];
			call_json_escape(song_title, esc_stitle, sizeof(esc_stitle));

			char item_json[1024];
			snprintf(item_json, sizeof(item_json),
				"%s{\"id\":\"%s\",\"title\":\"%s\",\"preferredKey\":%d,\"format\":\"%s\"}",
				first ? "" : ",", sid, esc_stitle, pkey, fmt);
			first = 0;

			if (strlen(songs_json) + strlen(item_json) < sizeof(songs_json) - 2) {
				strcat(songs_json, item_json);
			}
		}
		fclose(sfp);
	}
	strcat(songs_json, "]");

	/* Build allSongs JSON array from items/song/items/ */
	size_t all_songs_cap = 65536;
	char *all_songs_json = malloc(all_songs_cap);
	if (!all_songs_json) return NULL;
	strcpy(all_songs_json, "[");
	int all_first = 1;

	char songs_dir[512];
	snprintf(songs_dir, sizeof(songs_dir), "%s/items/song/items", doc_root);
	DIR *sdp = opendir(songs_dir);
	if (sdp) {
		struct dirent *sde;
		while ((sde = readdir(sdp)) != NULL) {
			if (sde->d_name[0] == '.') continue;

			char song_item_path[PATH_MAX];
			snprintf(song_item_path, sizeof(song_item_path),
				"%s/%s", songs_dir, sde->d_name);

			char stitle[256] = {0};
			call_read_meta_file(song_item_path, "title", stitle, sizeof(stitle));

			char esc_sid[256], esc_stitle2[512];
			call_json_escape(sde->d_name, esc_sid, sizeof(esc_sid));
			call_json_escape(stitle, esc_stitle2, sizeof(esc_stitle2));

			char item[800];
			snprintf(item, sizeof(item),
				"%s{\"id\":\"%s\",\"title\":\"%s\"}",
				all_first ? "" : ",", esc_sid, esc_stitle2);
			all_first = 0;

			size_t cur = strlen(all_songs_json);
			size_t need = cur + strlen(item) + 4;
			if (need >= all_songs_cap) {
				all_songs_cap = need * 2;
				char *tmp = realloc(all_songs_json, all_songs_cap);
				if (!tmp) { free(all_songs_json); closedir(sdp); return NULL; }
				all_songs_json = tmp;
			}
			strcat(all_songs_json, item);
		}
		closedir(sdp);
	}
	strcat(all_songs_json, "]");

	call_json_escape(title, esc_title, sizeof(esc_title));
	call_json_escape(owner, esc_owner, sizeof(esc_owner));
	call_json_escape(counter, esc_counter, sizeof(esc_counter));
	call_json_escape(format, esc_format, sizeof(esc_format));

	size_t resp_len = strlen(esc_title) + strlen(esc_owner) +
		strlen(esc_counter) + strlen(esc_format) + strlen(songs_json) +
		strlen(all_songs_json) + 256;

	char *response = malloc(resp_len);
	if (response) {
		snprintf(response, resp_len,
			"{"
			"\"title\":\"%s\","
			"\"owner\":\"%s\","
			"\"counter\":\"%s\","
			"\"formats\":\"%s\","
			"\"songs\":%s,"
			"\"allSongs\":%s"
			"}",
			esc_title, esc_owner, esc_counter, esc_format,
			songs_json, all_songs_json);
	}

	free(all_songs_json);
	return response;
}

static int
choir_details_handler(int fd, char *body)
{
	(void)body;

	char *json = choir_json(fd);
	int result = call_core_post(fd, json, json ? strlen(json) : 0);
	free(json);
	return result;
}

/* GET /api/choir/:id/songs - List choir songs with details */
static int
handle_choir_songs_list(int fd, char *body)
{
	(void)body;

	char doc_root[256] = {0};
	char choir_id[128] = {0};
	char choir_path[512];
	char songs_path[PATH_MAX];

	call_get_doc_root(fd, doc_root, sizeof(doc_root));
	ndc_env_get(fd, choir_id, "PATTERN_PARAM_ID");

	if (!choir_id[0]) {
		ndc_head(fd, 400);
		ndc_body(fd, "Missing choir ID");
		return 0;
	}

	snprintf(choir_path, sizeof(choir_path), "%s/%s/%s",
		doc_root, CHOIR_SONGS_PATH, choir_id);
	snprintf(songs_path, sizeof(songs_path), "%s/songs", choir_path);

	FILE *sfp = fopen(songs_path, "r");
	if (!sfp) {
		ndc_header(fd, "Content-Type", "application/json");
		ndc_head(fd, 200);
		ndc_body(fd, "[]");
		return 0;
	}

	char line[256];
	char songs_json[16384] = "[";
	int first = 1;

	while (fgets(line, sizeof(line), sfp)) {
		char song_id[128] = {0};
		int preferred_key = 0;
		char format[64] = {0};

		if (call_parse_item_line(line, song_id, &preferred_key, format) != 0)
			continue;

		if (!song_id[0])
			continue;

		char song_title[256] = {0};
		char song_path[PATH_MAX];
		snprintf(song_path, sizeof(song_path), "%s/items/song/items/%s",
			doc_root, song_id);
		call_read_meta_file(song_path, "title", song_title, sizeof(song_title));

		char esc_title[512];
		call_json_escape(song_title, esc_title, sizeof(esc_title));

		char item_json[1024];
		snprintf(item_json, sizeof(item_json),
			"%s{\"id\":\"%s\",\"title\":\"%s\",\"preferredKey\":%d,\"format\":\"%s\"}",
			first ? "" : ",", song_id, esc_title, preferred_key, format);
		first = 0;

		if (strlen(songs_json) + strlen(item_json) < sizeof(songs_json) - 2) {
			strcat(songs_json, item_json);
		}
	}
	fclose(sfp);

	strcat(songs_json, "]");

	ndc_header(fd, "Content-Type", "application/json");
	ndc_head(fd, 200);
	ndc_body(fd, songs_json);
	return 0;
}

/* POST /api/choir/:id/songs - Add song to choir repertoire */
static int
handle_choir_song_add(int fd, char *body)
{
	const char *username = call_get_request_user(fd);
	if (call_require_login(fd, username)) return 0;

	char doc_root[256] = {0};
	char choir_id[128] = {0};
	char choir_path[512];
	char songs_path[PATH_MAX];

	call_get_doc_root(fd, doc_root, sizeof(doc_root));
	ndc_env_get(fd, choir_id, "PATTERN_PARAM_ID");

	if (!choir_id[0]) {
		ndc_head(fd, 400);
		ndc_body(fd, "Missing choir ID");
		return 0;
	}

	snprintf(choir_path, sizeof(choir_path), "%s/%s/%s",
		doc_root, CHOIR_SONGS_PATH, choir_id);
	snprintf(songs_path, sizeof(songs_path), "%s/songs", choir_path);

	if (call_require_ownership(fd, choir_path, username, NULL)) return 0;

	call_query_parse(body);

	char song_id[128] = {0};
	char format[64] = {0};
	int song_id_len = call_query_param("song_id", song_id, sizeof(song_id) - 1);
	int format_len = call_query_param("format", format, sizeof(format) - 1);

	if (song_id_len <= 0) {
		ndc_head(fd, 400);
		ndc_body(fd, "Missing song_id");
		return 0;
	}
	song_id[song_id_len] = '\0';

	if (format_len > 0) {
		format[format_len] = '\0';
	} else {
		strcpy(format, "any");
	}

	FILE *afp = fopen(songs_path, "a");
	if (!afp) {
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to open songs file");
		return 0;
	}
	fprintf(afp, "%s:0:%s\n", song_id, format);
	fclose(afp);

	char location[256];
	snprintf(location, sizeof(location), "/choir/%s", choir_id);
	return call_redirect(fd, location);
}

/* POST /api/choir/:id/song/:song_id/key - Set preferred key */
static int
handle_choir_song_key(int fd, char *body)
{
	const char *username = call_get_request_user(fd);
	if (call_require_login(fd, username)) return 0;

	char doc_root[256] = {0};
	char choir_id[128] = {0};
	char song_id[128] = {0};
	char choir_path[512];
	char songs_path[PATH_MAX];

	call_get_doc_root(fd, doc_root, sizeof(doc_root));
	ndc_env_get(fd, choir_id, "PATTERN_PARAM_ID");
	ndc_env_get(fd, song_id, "PATTERN_PARAM_SONG_ID");

	if (!choir_id[0] || !song_id[0]) {
		ndc_head(fd, 400);
		ndc_body(fd, "Missing parameters");
		return 0;
	}

	snprintf(choir_path, sizeof(choir_path), "%s/%s/%s",
		doc_root, CHOIR_SONGS_PATH, choir_id);
	snprintf(songs_path, sizeof(songs_path), "%s/songs", choir_path);

	if (call_require_ownership(fd, choir_path, username, NULL)) return 0;

	call_query_parse(body);

	char key_str[32] = {0};
	int key_len = call_query_param("key", key_str, sizeof(key_str) - 1);
	int preferred_key = 0;
	if (key_len > 0) {
		preferred_key = atoi(key_str);
	}

	char line[256];
	char temp_path[PATH_MAX];
	snprintf(temp_path, sizeof(temp_path), "%s/songs.tmp", choir_path);

	FILE *rfp = fopen(songs_path, "r");
	FILE *wfp = fopen(temp_path, "w");
	if (!wfp) {
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to update");
		return 0;
	}

	while (rfp && fgets(line, sizeof(line), rfp)) {
		char sid[128] = {0};
		int pkey = 0;
		char fmt[64] = {0};

		if (call_parse_item_line(line, sid, &pkey, fmt) == 0 && strcmp(sid, song_id) == 0) {
			fprintf(wfp, "%s:%d:%s\n", sid, preferred_key, fmt);
		} else {
			fputs(line, wfp);
		}
	}
	if (rfp) fclose(rfp);
	fclose(wfp);

	rename(temp_path, songs_path);

	char location[256];
	snprintf(location, sizeof(location), "/choir/%s", choir_id);
	return call_redirect(fd, location);
}

/* DELETE /api/choir/:id/song/:song_id - Remove song from repertoire */
static int
handle_choir_song_delete(int fd, char *body)
{
	(void)body;

	const char *username = call_get_request_user(fd);
	if (call_require_login(fd, username)) return 0;

	char doc_root[256] = {0};
	char choir_id[128] = {0};
	char song_id[128] = {0};
	char choir_path[512];
	char songs_path[PATH_MAX];

	call_get_doc_root(fd, doc_root, sizeof(doc_root));
	ndc_env_get(fd, choir_id, "PATTERN_PARAM_ID");
	ndc_env_get(fd, song_id, "PATTERN_PARAM_SONG_ID");

	if (!choir_id[0] || !song_id[0]) {
		ndc_head(fd, 400);
		ndc_body(fd, "Missing parameters");
		return 0;
	}

	snprintf(choir_path, sizeof(choir_path), "%s/%s/%s",
		doc_root, CHOIR_SONGS_PATH, choir_id);
	snprintf(songs_path, sizeof(songs_path), "%s/songs", choir_path);

	if (call_require_ownership(fd, choir_path, username, NULL)) return 0;

	char line[256];
	char temp_path[PATH_MAX];
	snprintf(temp_path, sizeof(temp_path), "%s/songs.tmp", choir_path);

	FILE *rfp = fopen(songs_path, "r");
	FILE *wfp = fopen(temp_path, "w");
	if (!wfp) {
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to update");
		return 0;
	}

	int found = 0;
	while (rfp && fgets(line, sizeof(line), rfp)) {
		char sid[128] = {0};
		int pkey = 0;
		char fmt[64] = {0};

		if (call_parse_item_line(line, sid, &pkey, fmt) == 0 && strcmp(sid, song_id) == 0) {
			found = 1;
		} else {
			fputs(line, wfp);
		}
	}
	if (rfp) fclose(rfp);
	fclose(wfp);

	rename(temp_path, songs_path);

	if (found) {
		char location[256];
		snprintf(location, sizeof(location), "/choir/%s", choir_id);
		return call_redirect(fd, location);
	} else {
		ndc_head(fd, 404);
		ndc_body(fd, "Song not found");
	}
	return 0;
}

/* GET /choir/:id/song/:song_id - View song with choir preferred key */
static int
handle_choir_song_view(int fd, char *body)
{
	(void)body;

	char doc_root[256] = {0};
	char choir_id[128] = {0};
	char song_id[128] = {0};
	char songs_path[PATH_MAX];

	call_get_doc_root(fd, doc_root, sizeof(doc_root));
	ndc_env_get(fd, choir_id, "PATTERN_PARAM_ID");
	ndc_env_get(fd, song_id, "PATTERN_PARAM_SONG_ID");

	if (!choir_id[0] || !song_id[0]) {
		ndc_head(fd, 400);
		ndc_body(fd, "Missing parameters");
		return 0;
	}

	snprintf(songs_path, sizeof(songs_path), "%s/%s/%s/songs",
		doc_root, CHOIR_SONGS_PATH, choir_id);

	int preferred_key = 0;
	char format[64] = {0};

	FILE *sfp = fopen(songs_path, "r");
	if (sfp) {
		char line[256];
		while (fgets(line, sizeof(line), sfp)) {
			char sid[128] = {0};
			int pkey = 0;
			char fmt[64] = {0};

			if (call_parse_item_line(line, sid, &pkey, fmt) == 0 && strcmp(sid, song_id) == 0) {
				preferred_key = pkey;
				strncpy(format, fmt, sizeof(format) - 1);
				break;
			}
		}
		fclose(sfp);
	}
	(void)format;

	int transpose = 0;
	int original_key = 0;

	if (preferred_key != 0) {
		char song_data_path[512];
		snprintf(song_data_path, sizeof(song_data_path), "%s/items/song/items/%s/data.txt",
			doc_root, song_id);

		char *data = call_slurp_file(song_data_path);
		if (data) {
			call_song_transpose(data, 0, 0, NULL, &original_key);
			free(data);
			transpose = preferred_key - original_key;
		}
	}

	char redirect_url[512];
	snprintf(redirect_url, sizeof(redirect_url), "/song/%s?t=%d", song_id, transpose);
	return call_redirect(fd, redirect_url);
}

static int
handle_choir_add_get(int fd, char *body)
{
	return call_core_get(fd, body);
}

/* GET /choir/:id/edit - read choir data and proxy to Fresh */
static int
handle_choir_edit_get(int fd, char *body)
{
	(void)body;

	char id[128] = {0};
	char doc_root[256] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");
	call_get_doc_root(fd, doc_root, sizeof(doc_root));

	char choir_path[512];
	snprintf(choir_path, sizeof(choir_path), "%s/items/choir/items/%s", doc_root, id);

	struct stat st;
	if (stat(choir_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 404);
		ndc_body(fd, "Choir not found");
		return 1;
	}

	const char *username = call_get_request_user(fd);
	if (call_require_login(fd, username)) return 1;
	if (call_require_ownership(fd, choir_path, username, "You don't own this choir")) return 1;

	char title[256] = {0}, format[2048] = {0};

	call_read_meta_file(choir_path, "title", title, sizeof(title));

	char path_buf[PATH_MAX];
	snprintf(path_buf, sizeof(path_buf), "%s/format", choir_path);
	FILE *ffp = fopen(path_buf, "r");
	if (ffp) {
		size_t n = fread(format, 1, sizeof(format) - 1, ffp);
		format[n] = '\0';
		if (n > 0 && format[n-1] == '\n') format[n-1] = '\0';
		fclose(ffp);
	} else {
		strcpy(format, "entrada\naleluia\nofertorio\nsanto\ncomunhao\nacao_de_gracas\nsaida\nany");
	}

	char enc_id[256], enc_title[512], enc_format[6144];
	call_url_encode(id, enc_id, sizeof(enc_id));
	call_url_encode(title, enc_title, sizeof(enc_title));
	call_url_encode(format, enc_format, sizeof(enc_format));

	char post_body[8192];
	int len = snprintf(post_body, sizeof(post_body),
		"id=%s&title=%s&format=%s",
		enc_id, enc_title, enc_format);

	return call_core_post(fd, post_body, len);
}

/* POST /api/choir/:id/song/:song_id/remove - Remove song (HTML form-friendly alias for DELETE) */
static int
handle_choir_song_remove(int fd, char *body)
{
	(void)body;

	const char *username = call_get_request_user(fd);
	if (call_require_login(fd, username)) return 0;

	char doc_root[256] = {0};
	char choir_id[128] = {0};
	char song_id[128] = {0};
	char choir_path[512];
	char songs_path[PATH_MAX];

	call_get_doc_root(fd, doc_root, sizeof(doc_root));
	ndc_env_get(fd, choir_id, "PATTERN_PARAM_ID");
	ndc_env_get(fd, song_id, "PATTERN_PARAM_SONG_ID");

	if (!choir_id[0] || !song_id[0]) {
		ndc_head(fd, 400);
		ndc_body(fd, "Missing parameters");
		return 0;
	}

	snprintf(choir_path, sizeof(choir_path), "%s/%s/%s",
		doc_root, CHOIR_SONGS_PATH, choir_id);
	snprintf(songs_path, sizeof(songs_path), "%s/songs", choir_path);

	if (call_require_ownership(fd, choir_path, username, NULL)) return 0;

	char line[256];
	char temp_path[PATH_MAX];
	snprintf(temp_path, sizeof(temp_path), "%s/songs.tmp", choir_path);

	FILE *rfp = fopen(songs_path, "r");
	FILE *wfp = fopen(temp_path, "w");
	if (!wfp) {
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to update");
		return 0;
	}

	while (rfp && fgets(line, sizeof(line), rfp)) {
		char sid[128] = {0};
		int pkey = 0;
		char fmt[64] = {0};

		if (call_parse_item_line(line, sid, &pkey, fmt) == 0 && strcmp(sid, song_id) == 0) {
			/* skip - removing this entry */
		} else {
			fputs(line, wfp);
		}
	}
	if (rfp) fclose(rfp);
	fclose(wfp);

	rename(temp_path, songs_path);

	char location[256];
	snprintf(location, sizeof(location), "/choir/%s", choir_id);
	return call_redirect(fd, location);
}

void
ndx_install(void)
{
	ndx_load("./mods/common/common");
	ndx_load("./mods/index/index");
	ndx_load("./mods/auth/auth");
	ndx_load("./mods/mpfd/mpfd");
	ndx_load("./mods/song/song");

	ndc_register_handler("GET:/choir/add", handle_choir_add_get);
	ndc_register_handler("GET:/choir/:id/edit", handle_choir_edit_get);
	ndc_register_handler("GET:/choir/:id", choir_details_handler);
	ndc_register_handler("GET:/choir/:id/song/:song_id", handle_choir_song_view);
	ndc_register_handler("GET:/api/choir/:id/songs", handle_choir_songs_list);
	ndc_register_handler("POST:/api/choir/:id/songs", handle_choir_song_add);
	ndc_register_handler("POST:/api/choir/:id/song/:song_id/key", handle_choir_song_key);
	ndc_register_handler("DELETE:/api/choir/:id/song/:song_id", handle_choir_song_delete);
	ndc_register_handler("POST:/api/choir/:id/song/:song_id/remove", handle_choir_song_remove);
	ndc_register_handler("POST:/api/choir/:id/edit", handle_choir_edit);

	call_index_open("Choir", 0, 1, NULL);
}

void ndx_open(void) {}
