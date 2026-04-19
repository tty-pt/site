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
	if (stat(choir_path, &st) != 0 || !S_ISDIR(st.st_mode))
		return call_respond_error(fd, 404, "Choir not found");

	if (call_require_ownership(fd, choir_path, username, "You don't own this choir")) return 1;

	call_mpfd_parse(fd, body);

	char title[256] = {0};
	char format[2048] = {0};
	int title_len = call_mpfd_get("title", title, sizeof(title) - 1);
	int format_len = call_mpfd_get("format", format, sizeof(format) - 1);

	if (title_len > 0) {
		title[title_len] = '\0';
		call_write_meta_file(choir_path, "title", title, strlen(title));
	}

	if (format_len > 0) {
		format[format_len] = '\0';
		call_write_meta_file(choir_path, "format", format, strlen(format));
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

			int original_key = call_song_get_original_key(sid);

			char esc_stitle[512];
			call_json_escape(song_title, esc_stitle, sizeof(esc_stitle));

			char item_json[1024];
			snprintf(item_json, sizeof(item_json),
				"%s{\"id\":\"%s\",\"title\":\"%s\",\"preferredKey\":%d,\"originalKey\":%d,\"format\":\"%s\"}",
				first ? "" : ",", sid, esc_stitle, pkey, original_key, fmt);
			first = 0;

			if (strlen(songs_json) + strlen(item_json) < sizeof(songs_json) - 2) {
				strcat(songs_json, item_json);
			}
		}
		fclose(sfp);
	}
	strcat(songs_json, "]");

	/* Build songbooks JSON array — scan items/songbook/items/ for choir match */
	char sb_items_path[PATH_MAX];
	snprintf(sb_items_path, sizeof(sb_items_path), "%s/items/songbook/items", doc_root);
	char songbooks_json[16384] = "[";
	int sb_first = 1;
	int sb_count = 0;
	DIR *sbdp = opendir(sb_items_path);
	if (sbdp) {
		struct dirent *sde;
		while ((sde = readdir(sbdp)) != NULL) {
			if (sde->d_name[0] == '.') continue;
			char sb_choir_path[PATH_MAX];
			snprintf(sb_choir_path, sizeof(sb_choir_path),
				"%s/%s/choir", sb_items_path, sde->d_name);
			FILE *cfp = fopen(sb_choir_path, "r");
			if (!cfp) continue;
			char sb_choir[128] = {0};
			size_t n = fread(sb_choir, 1, sizeof(sb_choir) - 1, cfp);
			fclose(cfp);
			sb_choir[n] = '\0';
			/* strip trailing newline */
			while (n > 0 && (sb_choir[n-1] == '\n' || sb_choir[n-1] == '\r'))
				sb_choir[--n] = '\0';
			if (strcmp(sb_choir, id) != 0) continue;
			sb_count++;
			char sb_title[256] = {0};
			char sb_path2[PATH_MAX];
			snprintf(sb_path2, sizeof(sb_path2), "%s/%s", sb_items_path, sde->d_name);
			call_read_meta_file(sb_path2, "title", sb_title, sizeof(sb_title));
			char esc_sb_title[512];
			call_json_escape(sb_title, esc_sb_title, sizeof(esc_sb_title));
			char sb_item[640];
			snprintf(sb_item, sizeof(sb_item),
				"%s{\"id\":\"%s\",\"title\":\"%s\"}",
				sb_first ? "" : ",", sde->d_name, esc_sb_title);
			sb_first = 0;
			if (strlen(songbooks_json) + strlen(sb_item) < sizeof(songbooks_json) - 2)
				strcat(songbooks_json, sb_item);
		}
		closedir(sbdp);
	}
	strcat(songbooks_json, "]");

	/* derive counter from scan */
	snprintf(counter, sizeof(counter), "%d", sb_count);
	char *all_songs_json = call_build_all_songs_json(doc_root, 0);
	if (!all_songs_json) return NULL;

	call_json_escape(title, esc_title, sizeof(esc_title));
	call_json_escape(owner, esc_owner, sizeof(esc_owner));
	call_json_escape(counter, esc_counter, sizeof(esc_counter));
	call_json_escape(format, esc_format, sizeof(esc_format));

	size_t resp_len = strlen(esc_title) + strlen(esc_owner) +
		strlen(esc_counter) + strlen(esc_format) + strlen(songs_json) +
		strlen(all_songs_json) + strlen(songbooks_json) + 256;

	char *response = malloc(resp_len);
	if (response) {
		snprintf(response, resp_len,
			"{"
			"\"title\":\"%s\","
			"\"owner\":\"%s\","
			"\"counter\":\"%s\","
			"\"formats\":\"%s\","
			"\"songs\":%s,"
			"\"allSongs\":%s,"
			"\"songbooks\":%s"
			"}",
			esc_title, esc_owner, esc_counter, esc_format,
			songs_json, all_songs_json, songbooks_json);
	}

	free(all_songs_json);
	return response;
}

static int
choir_details_handler(int fd, char *body)
{
	(void)body;

	char *json = choir_json(fd);
	if (!json)
		return call_respond_error(fd, 404, "Choir not found");
	int result = call_core_post(fd, json, strlen(json));
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

	if (!choir_id[0])
		return call_respond_plain(fd, 400, "Missing choir ID");

	snprintf(choir_path, sizeof(choir_path), "%s/%s/%s",
		doc_root, CHOIR_SONGS_PATH, choir_id);
	snprintf(songs_path, sizeof(songs_path), "%s/songs", choir_path);

	FILE *sfp = fopen(songs_path, "r");
	if (!sfp)
		return call_respond_json(fd, 200, "[]");

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
	return call_respond_json(fd, 200, songs_json);
}

/* POST /api/choir/:id/songs - Add song to choir repertoire */
static int
handle_choir_song_add(int fd, char *body)
{
	const char *username = call_get_request_user(fd);
	if (call_require_login(fd, username)) return 1;

	char doc_root[256] = {0};
	char choir_id[128] = {0};
	char choir_path[512];
	char songs_path[PATH_MAX];

	call_get_doc_root(fd, doc_root, sizeof(doc_root));
	ndc_env_get(fd, choir_id, "PATTERN_PARAM_ID");

	if (!choir_id[0])
		return call_respond_plain(fd, 400, "Missing choir ID");

	snprintf(choir_path, sizeof(choir_path), "%s/%s/%s",
		doc_root, CHOIR_SONGS_PATH, choir_id);
	snprintf(songs_path, sizeof(songs_path), "%s/songs", choir_path);

	if (call_require_ownership(fd, choir_path, username, NULL)) return 1;

	ndc_query_parse(body);
	
	char song_id[128] = {0};
	char format[64] = {0};
	int song_id_len = ndc_query_param("song_id", song_id, sizeof(song_id) - 1);
	int format_len = ndc_query_param("format", format, sizeof(format) - 1);

	if (song_id_len <= 0)
		return call_respond_plain(fd, 400, "Missing song_id");
	song_id[song_id_len] = '\0';

	/* Extract id from "Title [id]" datalist format */
	{
		char *bracket = strrchr(song_id, '[');
		if (bracket) {
			char *end = strchr(bracket + 1, ']');
			if (end) { *end = '\0'; memmove(song_id, bracket + 1, end - bracket); }
		}
	}

	if (format_len > 0) {
		format[format_len] = '\0';
	} else {
		strcpy(format, "any");
	}

	FILE *afp = fopen(songs_path, "a");
	if (!afp)
		return call_respond_plain(fd, 500, "Failed to open songs file");
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
	if (call_require_login(fd, username)) return 1;

	char doc_root[256] = {0};
	char choir_id[128] = {0};
	char song_id[128] = {0};
	char choir_path[512];
	char songs_path[PATH_MAX];

	call_get_doc_root(fd, doc_root, sizeof(doc_root));
	ndc_env_get(fd, choir_id, "PATTERN_PARAM_ID");
	ndc_env_get(fd, song_id, "PATTERN_PARAM_SONG_ID");

	if (!choir_id[0] || !song_id[0])
		return call_respond_plain(fd, 400, "Missing parameters");

	snprintf(choir_path, sizeof(choir_path), "%s/%s/%s",
		doc_root, CHOIR_SONGS_PATH, choir_id);
	snprintf(songs_path, sizeof(songs_path), "%s/songs", choir_path);

	if (call_require_ownership(fd, choir_path, username, NULL)) return 1;

	ndc_query_parse(body);
	
	char key_str[32] = {0};
	int key_len = ndc_query_param("key", key_str, sizeof(key_str) - 1);
	int preferred_key = 0;
	if (key_len > 0) {
		preferred_key = atoi(key_str);
	}

	char line[256];
	char temp_path[PATH_MAX];
	snprintf(temp_path, sizeof(temp_path), "%s/songs.tmp", choir_path);

	FILE *rfp = fopen(songs_path, "r");
	FILE *wfp = fopen(temp_path, "w");
	if (!wfp)
		return call_respond_plain(fd, 500, "Failed to update");

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
	if (call_require_login(fd, username)) return 1;

	char doc_root[256] = {0};
	char choir_id[128] = {0};
	char song_id[128] = {0};
	char choir_path[512];
	char songs_path[PATH_MAX];

	call_get_doc_root(fd, doc_root, sizeof(doc_root));
	ndc_env_get(fd, choir_id, "PATTERN_PARAM_ID");
	ndc_env_get(fd, song_id, "PATTERN_PARAM_SONG_ID");

	if (!choir_id[0] || !song_id[0])
		return call_respond_plain(fd, 400, "Missing parameters");

	snprintf(choir_path, sizeof(choir_path), "%s/%s/%s",
		doc_root, CHOIR_SONGS_PATH, choir_id);
	snprintf(songs_path, sizeof(songs_path), "%s/songs", choir_path);

	if (call_require_ownership(fd, choir_path, username, NULL)) return 1;

	char line[256];
	char temp_path[PATH_MAX];
	snprintf(temp_path, sizeof(temp_path), "%s/songs.tmp", choir_path);

	FILE *rfp = fopen(songs_path, "r");
	FILE *wfp = fopen(temp_path, "w");
	if (!wfp)
		return call_respond_plain(fd, 500, "Failed to update");

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
	}
	return call_respond_plain(fd, 404, "Song not found");
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

	if (!choir_id[0] || !song_id[0])
		return call_respond_plain(fd, 400, "Missing parameters");

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
	if (stat(choir_path, &st) != 0 || !S_ISDIR(st.st_mode))
		return call_respond_error(fd, 404, "Choir not found");

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
	if (call_require_login(fd, username)) return 1;

	char doc_root[256] = {0};
	char choir_id[128] = {0};
	char song_id[128] = {0};
	char choir_path[512];
	char songs_path[PATH_MAX];

	call_get_doc_root(fd, doc_root, sizeof(doc_root));
	ndc_env_get(fd, choir_id, "PATTERN_PARAM_ID");
	ndc_env_get(fd, song_id, "PATTERN_PARAM_SONG_ID");

	if (!choir_id[0] || !song_id[0])
		return call_respond_plain(fd, 400, "Missing parameters");

	snprintf(choir_path, sizeof(choir_path), "%s/%s/%s",
		doc_root, CHOIR_SONGS_PATH, choir_id);
	snprintf(songs_path, sizeof(songs_path), "%s/songs", choir_path);

	if (call_require_ownership(fd, choir_path, username, NULL)) return 1;

	char line[256];
	char temp_path[PATH_MAX];
	snprintf(temp_path, sizeof(temp_path), "%s/songs.tmp", choir_path);

	FILE *rfp = fopen(songs_path, "r");
	FILE *wfp = fopen(temp_path, "w");
	if (!wfp)
		return call_respond_plain(fd, 500, "Failed to update");

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
