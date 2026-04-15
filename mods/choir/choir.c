#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

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

/* Check if user owns a choir */
static int
check_choir_ownership(const char *choir_path, const char *username)
{
	if (!username || !*username)
		return 0;

	char owner_path[1024];
	snprintf(owner_path, sizeof(owner_path), "%s/.owner", choir_path);

	FILE *fp = fopen(owner_path, "r");
	if (!fp)
		return 0;

	char owner[64] = {0};
	size_t n = fread(owner, 1, sizeof(owner) - 1, fp);
	fclose(fp);
	if (n == 0)
		return 0;
	owner[n] = '\0';

	if (owner[n - 1] == '\n')
		owner[n - 1] = '\0';

	return strcmp(owner, username) == 0;
}


static unsigned index_hd = 0;

#if 0
/* Default format types if choir doesn't define custom ones */
static const char *default_formats[] = {
	"entrada",
	"aleluia",
	"ofertorio",
	"santo",
	"comunhao",
	"acao_de_gracas",
	"saida",
	"any",
	NULL
};

/* POST /api/choir/create - Create new choir */
static int
handle_choir_create(int fd, char *body)
{
	/* Get current user */
	char cookie[256] = {0};
	char token[64] = {0};
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	const char *username = call_get_session_user(token);

	if (!username || !*username) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 401);
		ndc_body(fd, "Login required");
		return 1;
	}

	/* Parse form data */
	call_mpfd_parse(fd, body);

	char id[128] = {0};
	char title[256] = {0};
	int id_len = call_mpfd_get("id", id, sizeof(id) - 1);
	int title_len = call_mpfd_get("title", title, sizeof(title) - 1);

	if (id_len <= 0 || title_len <= 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing id or title");
		return 1;
	}

	id[id_len] = '\0';
	title[title_len] = '\0';

	/* Build choir directory path */
	char doc_root[256] = {0};
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	char choir_path[512];
	snprintf(choir_path, sizeof(choir_path), "%s/items/choir/items/%s",
		doc_root[0] ? doc_root : ".", id);

	/* Create directory */
	if (mkdir(choir_path, 0755) == -1 && errno != EEXIST) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to create choir directory");
		return 1;
	}

	/* Write .owner file */
	char owner_path[1024];
	snprintf(owner_path, sizeof(owner_path), "%s/.owner", choir_path);
	FILE *ofp = fopen(owner_path, "w");
	if (!ofp) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to write owner file");
		return 1;
	}
	fwrite(username, 1, strlen(username), ofp);
	fclose(ofp);

	/* Write title file */
	char title_path[1024];
	snprintf(title_path, sizeof(title_path), "%s/title", choir_path);
	FILE *tfp = fopen(title_path, "w");
	if (!tfp) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to write title file");
		return 1;
	}
	fwrite(title, 1, strlen(title), tfp);
	fclose(tfp);

	/* Initialize counter to 0 */
	char counter_path[1024];
	snprintf(counter_path, sizeof(counter_path), "%s/counter", choir_path);
	FILE *cfp = fopen(counter_path, "w");
	if (cfp) {
		fprintf(cfp, "0");
		fclose(cfp);
	}

	/* Add to index database */
	qmap_put(choir_index_db, id, title);

	/* Redirect to choir page */
	char location[256];
	snprintf(location, sizeof(location), "/choir/%s", id);
	ndc_header(fd, "Location", location);
	ndc_header(fd, "Connection", "close");
	ndc_set_flags(fd, DF_TO_CLOSE);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* POST /api/choir/:id/edit - Edit choir */
static int
handle_choir_edit(int fd, char *body)
{
	/* Get current user */
	char cookie[256] = {0};
	char token[64] = {0};
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	const char *username = call_get_session_user(token);

	if (!username || !*username) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 401);
		ndc_body(fd, "Login required");
		return 1;
	}

	/* Get choir ID from URL pattern */
	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	/* Build choir path */
	char doc_root[256] = {0};
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	char choir_path[512];
	snprintf(choir_path, sizeof(choir_path), "%s/items/choir/items/%s",
		doc_root[0] ? doc_root : ".", id);

	/* Check if choir exists */
	struct stat st;
	if (stat(choir_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 404);
		ndc_body(fd, "Choir not found");
		return 1;
	}

	/* Check ownership */
	if (!check_choir_ownership(choir_path, username)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "You don't own this choir");
		return 1;
	}

	/* Parse form data */
	call_mpfd_parse(fd, body);

	char title[256] = {0};
	char format[2048] = {0};
	int title_len = call_mpfd_get("title", title, sizeof(title) - 1);
	int format_len = call_mpfd_get("format", format, sizeof(format) - 1);

	if (title_len > 0) {
		title[title_len] = '\0';

		/* Update title file */
		char title_path[1024];
		snprintf(title_path, sizeof(title_path), "%s/title", choir_path);
		FILE *tfp = fopen(title_path, "w");
		if (tfp) {
			fwrite(title, 1, strlen(title), tfp);
			fclose(tfp);

			/* Update in database */
			qmap_put(choir_index_db, id, title);
		}
	}

	if (format_len > 0) {
		format[format_len] = '\0';

		/* Update format file */
		char format_path[1024];
		snprintf(format_path, sizeof(format_path), "%s/format", choir_path);
		FILE *ffp = fopen(format_path, "w");
		if (ffp) {
			fwrite(format, 1, strlen(format), ffp);
			fclose(ffp);
		}
	}

	/* Redirect back to choir page */
	char location[256];
	snprintf(location, sizeof(location), "/choir/%s", id);
	ndc_header(fd, "Location", location);
	ndc_header(fd, "Connection", "close");
	ndc_set_flags(fd, DF_TO_CLOSE);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* DELETE /api/choir/:id - Delete choir */
static int
handle_choir_delete(int fd, char *body)
{
	(void)body;

	/* Get current user */
	char cookie[256] = {0};
	char token[64] = {0};
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	const char *username = call_get_session_user(token);

	if (!username || !*username) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 401);
		ndc_body(fd, "Login required");
		return 1;
	}

	/* Get choir ID from URL pattern */
	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	/* Build choir path */
	char doc_root[256] = {0};
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	char choir_path[512];
	snprintf(choir_path, sizeof(choir_path), "%s/items/choir/items/%s",
		doc_root[0] ? doc_root : ".", id);

	/* Check if choir exists */
	struct stat st;
	if (stat(choir_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 404);
		ndc_body(fd, "Choir not found");
		return 1;
	}

	/* Check ownership */
	if (!check_choir_ownership(choir_path, username)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "You don't own this choir");
		return 1;
	}

	/* Remove from database */
	qmap_del(choir_index_db, id);

	/* Delete directory (simple approach - just remove key files) */
	char path_buf[1024];
	snprintf(path_buf, sizeof(path_buf), "%s/.owner", choir_path);
	unlink(path_buf);
	snprintf(path_buf, sizeof(path_buf), "%s/title", choir_path);
	unlink(path_buf);
	snprintf(path_buf, sizeof(path_buf), "%s/counter", choir_path);
	unlink(path_buf);
	snprintf(path_buf, sizeof(path_buf), "%s/format", choir_path);
	unlink(path_buf);
	rmdir(choir_path);

	/* Redirect to choir list */
	ndc_header(fd, "Location", "/choir");
	ndc_header(fd, "Connection", "close");
	ndc_set_flags(fd, DF_TO_CLOSE);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* Parse choir song line: song_id:preferred_key:format */
static int
parse_choir_song_line(const char *line, char *song_id, int *preferred_key, char *format)
{
	char *colon1 = strchr(line, ':');
	if (!colon1)
		return -1;

	char *colon2 = strchr(colon1 + 1, ':');
	if (!colon2)
		return -1;

	size_t id_len = colon1 - line;
	if (id_len > 127) id_len = 127;
	strncpy(song_id, line, id_len);
	song_id[id_len] = '\0';

	*preferred_key = atoi(colon1 + 1);

	strncpy(format, colon2 + 1, 127);
	format[127] = '\0';

	size_t fmt_len = strlen(format);
#endif

/* POST /api/choir/:id/edit - Edit choir title and formats */
static int
handle_choir_edit(int fd, char *body)
{
	char cookie[256] = {0};
	char token[64] = {0};
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	const char *username = call_get_session_user(token);

	if (!username || !*username) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 401);
		ndc_body(fd, "Login required");
		return 1;
	}

	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	char doc_root[256] = {0};
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	if (!doc_root[0])
		strcpy(doc_root, ".");

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

	if (!check_choir_ownership(choir_path, username)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "You don't own this choir");
		return 1;
	}

	call_mpfd_parse(fd, body);

	char title[256] = {0};
	char format[2048] = {0};
	int title_len = call_mpfd_get("title", title, sizeof(title) - 1);
	int format_len = call_mpfd_get("format", format, sizeof(format) - 1);

	if (title_len > 0) {
		title[title_len] = '\0';
		char title_path[1024];
		snprintf(title_path, sizeof(title_path), "%s/title", choir_path);
		FILE *tfp = fopen(title_path, "w");
		if (tfp) {
			fwrite(title, 1, strlen(title), tfp);
			fclose(tfp);
		}
	}

	if (format_len > 0) {
		format[format_len] = '\0';
		char format_path[1024];
		snprintf(format_path, sizeof(format_path), "%s/format", choir_path);
		FILE *ffp = fopen(format_path, "w");
		if (ffp) {
			fwrite(format, 1, strlen(format), ffp);
			fclose(ffp);
		}
	}

	char location[256];
	snprintf(location, sizeof(location), "/choir/%s", id);
	ndc_header(fd, "Location", location);
	ndc_header(fd, "Connection", "close");
	ndc_set_flags(fd, DF_TO_CLOSE);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* Parse choir song line: song_id:preferred_key:format */
static int
parse_choir_song_line(const char *line, char *song_id, int *preferred_key, char *format)
{
	char *colon1 = strchr(line, ':');
	if (!colon1)
		return -1;

	char *colon2 = strchr(colon1 + 1, ':');
	if (!colon2)
		return -1;

	size_t id_len = colon1 - line;
	if (id_len > 127) id_len = 127;
	strncpy(song_id, line, id_len);
	song_id[id_len] = '\0';

	*preferred_key = atoi(colon1 + 1);

	strncpy(format, colon2 + 1, 127);
	format[127] = '\0';

	size_t fmt_len = strlen(format);
	while (fmt_len > 0 && (format[fmt_len - 1] == '\n' || format[fmt_len - 1] == '\r')) {
		format[--fmt_len] = '\0';
	}

	return 0;
}

static char *
choir_json(int fd)
{
	char doc_root[256] = {0};
	char id[128] = {0};
	char choir_path[512];

	char title[256] = {0}, owner[64] = {0}, counter[32] = {0}, format[2048] = {0};
	char esc_title[512], esc_owner[128], esc_counter[64], esc_format[4096];

	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	if (!id[0]) return NULL;

	snprintf(choir_path, sizeof(choir_path), "%s/items/choir/items/%s",
		doc_root[0] ? doc_root : ".", id);

	char path_buf[1024];

	snprintf(path_buf, sizeof(path_buf), "%s/title", choir_path);
	FILE *tfp = fopen(path_buf, "r");
	if (tfp) {
		if (fgets(title, sizeof(title) - 1, tfp)) {
			size_t l = strlen(title);
			if (l > 0 && title[l - 1] == '\n') title[l - 1] = '\0';
		}
		fclose(tfp);
	} else {
		return NULL;
	}

	snprintf(path_buf, sizeof(path_buf), "%s/.owner", choir_path);
	FILE *ofp = fopen(path_buf, "r");
	if (ofp) {
		if (fgets(owner, sizeof(owner) - 1, ofp)) {
			size_t l = strlen(owner);
			if (l > 0 && owner[l - 1] == '\n') owner[l - 1] = '\0';
		}
		fclose(ofp);
	}

	snprintf(path_buf, sizeof(path_buf), "%s/counter", choir_path);
	FILE *cfp = fopen(path_buf, "r");
	if (cfp) {
		if (fgets(counter, sizeof(counter) - 1, cfp)) {
			size_t l = strlen(counter);
			if (l > 0 && counter[l - 1] == '\n') counter[l - 1] = '\0';
		}
		fclose(cfp);
	} else {
		snprintf(counter, sizeof(counter), "0");
	}

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

	char songs_path[512];
	snprintf(songs_path, sizeof(songs_path), "%s/songs", choir_path);
	FILE *sfp = fopen(songs_path, "r");
	if (sfp) {
		char line[256];
		while (fgets(line, sizeof(line), sfp)) {
			char sid[128] = {0};
			int pkey = 0;
			char fmt[64] = {0};

			if (parse_choir_song_line(line, sid, &pkey, fmt) != 0)
				continue;

			if (!sid[0])
				continue;

			char song_title[256] = {0};
			char song_path[512];
			snprintf(song_path, sizeof(song_path), "%s/items/song/items/%s/title",
				doc_root[0] ? doc_root : ".", sid);
			FILE *tfp = fopen(song_path, "r");
			if (tfp) {
				if (fgets(song_title, sizeof(song_title) - 1, tfp)) {
					size_t l = strlen(song_title);
					if (l > 0 && song_title[l - 1] == '\n') song_title[l - 1] = '\0';
				}
				fclose(tfp);
			}

			char esc_stitle[512];
			call_json_escape(song_title, esc_stitle, sizeof(esc_stitle));

			char item_json[640];
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
	snprintf(songs_dir, sizeof(songs_dir), "%s/items/song/items",
		doc_root[0] ? doc_root : ".");
	DIR *sdp = opendir(songs_dir);
	if (sdp) {
		struct dirent *sde;
		while ((sde = readdir(sdp)) != NULL) {
			if (sde->d_name[0] == '.') continue;

			char song_title_path[768];
			snprintf(song_title_path, sizeof(song_title_path),
				"%s/%s/title", songs_dir, sde->d_name);
			FILE *stfp = fopen(song_title_path, "r");
			if (!stfp) continue;

			char stitle[256] = {0};
			if (fgets(stitle, sizeof(stitle) - 1, stfp)) {
				size_t l = strlen(stitle);
				if (l > 0 && stitle[l - 1] == '\n') stitle[l - 1] = '\0';
			}
			fclose(stfp);

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
	char songs_path[512];

	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	ndc_env_get(fd, choir_id, "PATTERN_PARAM_ID");

	if (!choir_id[0]) {
		ndc_head(fd, 400);
		ndc_body(fd, "Missing choir ID");
		return 0;
	}

	snprintf(choir_path, sizeof(choir_path), "%s/%s/%s",
		doc_root[0] ? doc_root : ".", CHOIR_SONGS_PATH, choir_id);
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

		if (parse_choir_song_line(line, song_id, &preferred_key, format) != 0)
			continue;

		if (!song_id[0])
			continue;

		char song_title[256] = {0};
		char song_path[512];
		snprintf(song_path, sizeof(song_path), "%s/items/song/items/%s/title",
			doc_root[0] ? doc_root : ".", song_id);
		FILE *tfp = fopen(song_path, "r");
		if (tfp) {
			if (fgets(song_title, sizeof(song_title) - 1, tfp)) {
				size_t l = strlen(song_title);
				if (l > 0 && song_title[l - 1] == '\n') song_title[l - 1] = '\0';
			}
			fclose(tfp);
		}

		char esc_title[512];
		call_json_escape(song_title, esc_title, sizeof(esc_title));

		char item_json[640];
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
	char cookie[256] = {0};
	char token[64] = {0};
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	const char *username = call_get_session_user(token);

	if (!username || !*username) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 401);
		ndc_body(fd, "Login required");
		return 0;
	}

	char doc_root[256] = {0};
	char choir_id[128] = {0};
	char choir_path[512];
	char songs_path[512];

	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	ndc_env_get(fd, choir_id, "PATTERN_PARAM_ID");

	if (!choir_id[0]) {
		ndc_head(fd, 400);
		ndc_body(fd, "Missing choir ID");
		return 0;
	}

	snprintf(choir_path, sizeof(choir_path), "%s/%s/%s",
		doc_root[0] ? doc_root : ".", CHOIR_SONGS_PATH, choir_id);
	snprintf(songs_path, sizeof(songs_path), "%s/songs", choir_path);

	if (!check_choir_ownership(choir_path, username)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "Not authorized");
		return 0;
	}

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
	ndc_header(fd, "Location", location);
	ndc_header(fd, "Connection", "close");
	ndc_set_flags(fd, DF_TO_CLOSE);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* POST /api/choir/:id/song/:song_id/key - Set preferred key */
static int
handle_choir_song_key(int fd, char *body)
{
	char cookie[256] = {0};
	char token[64] = {0};
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	const char *username = call_get_session_user(token);

	if (!username || !*username) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 401);
		ndc_body(fd, "Login required");
		return 0;
	}

	char doc_root[256] = {0};
	char choir_id[128] = {0};
	char song_id[128] = {0};
	char choir_path[512];
	char songs_path[512];

	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	ndc_env_get(fd, choir_id, "PATTERN_PARAM_ID");
	ndc_env_get(fd, song_id, "PATTERN_PARAM_SONG_ID");

	if (!choir_id[0] || !song_id[0]) {
		ndc_head(fd, 400);
		ndc_body(fd, "Missing parameters");
		return 0;
	}

	snprintf(choir_path, sizeof(choir_path), "%s/%s/%s",
		doc_root[0] ? doc_root : ".", CHOIR_SONGS_PATH, choir_id);
	snprintf(songs_path, sizeof(songs_path), "%s/songs", choir_path);

	if (!check_choir_ownership(choir_path, username)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "Not authorized");
		return 0;
	}

	call_query_parse(body);

	char key_str[32] = {0};
	int key_len = call_query_param("key", key_str, sizeof(key_str) - 1);
	int preferred_key = 0;
	if (key_len > 0) {
		preferred_key = atoi(key_str);
	}

	char line[256];
	char temp_path[512];
	snprintf(temp_path, sizeof(temp_path), "%s/songs.tmp", choir_path);

	FILE *rfp = fopen(songs_path, "r");
	FILE *wfp = fopen(temp_path, "w");
	if (!wfp) {
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to update");
		return 0;
	}

	while (fgets(line, sizeof(line), rfp)) {
		char sid[128] = {0};
		int pkey = 0;
		char fmt[64] = {0};

		if (parse_choir_song_line(line, sid, &pkey, fmt) == 0 && strcmp(sid, song_id) == 0) {
			fprintf(wfp, "%s:%d:%s\n", sid, preferred_key, fmt);
		} else {
			fputs(line, wfp);
		}
	}
	fclose(rfp);
	fclose(wfp);

	rename(temp_path, songs_path);

	char location[256];
	snprintf(location, sizeof(location), "/choir/%s", choir_id);
	ndc_header(fd, "Location", location);
	ndc_header(fd, "Connection", "close");
	ndc_set_flags(fd, DF_TO_CLOSE);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

/* DELETE /api/choir/:id/song/:song_id - Remove song from repertoire */
static int
handle_choir_song_delete(int fd, char *body)
{
	(void)body;

	char cookie[256] = {0};
	char token[64] = {0};
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	const char *username = call_get_session_user(token);

	if (!username || !*username) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 401);
		ndc_body(fd, "Login required");
		return 0;
	}

	char doc_root[256] = {0};
	char choir_id[128] = {0};
	char song_id[128] = {0};
	char choir_path[512];
	char songs_path[512];

	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	ndc_env_get(fd, choir_id, "PATTERN_PARAM_ID");
	ndc_env_get(fd, song_id, "PATTERN_PARAM_SONG_ID");

	if (!choir_id[0] || !song_id[0]) {
		ndc_head(fd, 400);
		ndc_body(fd, "Missing parameters");
		return 0;
	}

	snprintf(choir_path, sizeof(choir_path), "%s/%s/%s",
		doc_root[0] ? doc_root : ".", CHOIR_SONGS_PATH, choir_id);
	snprintf(songs_path, sizeof(songs_path), "%s/songs", choir_path);

	if (!check_choir_ownership(choir_path, username)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "Not authorized");
		return 0;
	}

	char line[256];
	char temp_path[512];
	snprintf(temp_path, sizeof(temp_path), "%s/songs.tmp", choir_path);

	FILE *rfp = fopen(songs_path, "r");
	FILE *wfp = fopen(temp_path, "w");
	if (!wfp) {
		ndc_head(fd, 500);
		ndc_body(fd, "Failed to update");
		return 0;
	}

	int found = 0;
	while (fgets(line, sizeof(line), rfp)) {
		char sid[128] = {0};
		int pkey = 0;
		char fmt[64] = {0};

		if (parse_choir_song_line(line, sid, &pkey, fmt) == 0 && strcmp(sid, song_id) == 0) {
			found = 1;
		} else {
			fputs(line, wfp);
		}
	}
	fclose(rfp);
	fclose(wfp);

	rename(temp_path, songs_path);

	if (found) {
		char location[256];
		snprintf(location, sizeof(location), "/choir/%s", choir_id);
		ndc_header(fd, "Location", location);
		ndc_header(fd, "Connection", "close");
		ndc_set_flags(fd, DF_TO_CLOSE);
		ndc_head(fd, 303);
		ndc_close(fd);
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
	char songs_path[512];

	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	ndc_env_get(fd, choir_id, "PATTERN_PARAM_ID");
	ndc_env_get(fd, song_id, "PATTERN_PARAM_SONG_ID");

	if (!choir_id[0] || !song_id[0]) {
		ndc_head(fd, 400);
		ndc_body(fd, "Missing parameters");
		return 0;
	}

	snprintf(songs_path, sizeof(songs_path), "%s/%s/%s/songs",
		doc_root[0] ? doc_root : ".", CHOIR_SONGS_PATH, choir_id);

	int preferred_key = 0;
	char format[64] = {0};

	FILE *sfp = fopen(songs_path, "r");
	if (sfp) {
		char line[256];
		while (fgets(line, sizeof(line), sfp)) {
			char sid[128] = {0};
			int pkey = 0;
			char fmt[64] = {0};

			if (parse_choir_song_line(line, sid, &pkey, fmt) == 0 && strcmp(sid, song_id) == 0) {
				preferred_key = pkey;
				strncpy(format, fmt, sizeof(format) - 1);
				break;
			}
		}
		fclose(sfp);
	}

	int transpose = 0;
	int original_key = 0;

	if (preferred_key != 0) {
		char song_data_path[512];
		snprintf(song_data_path, sizeof(song_data_path), "%s/items/song/items/%s/data.txt",
			doc_root[0] ? doc_root : ".", song_id);

		FILE *dfp = fopen(song_data_path, "r");
		if (dfp) {
			char data[4096] = {0};
			size_t n = fread(data, 1, sizeof(data) - 1, dfp);
			data[n] = '\0';
			fclose(dfp);

			call_song_transpose(data, 0, 0, NULL, &original_key);
			transpose = preferred_key - original_key;
		}
	}

	char redirect_url[512];
	snprintf(redirect_url, sizeof(redirect_url), "/song/%s?t=%d", song_id, transpose);

	ndc_header(fd, "Location", redirect_url);
	ndc_header(fd, "Connection", "close");
	ndc_set_flags(fd, DF_TO_CLOSE);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

static int
handle_choir_add_get(int fd, char *body)
{
	return call_core_get(fd, body);
}

/* POST /api/choir/:id/song/:song_id/remove - Remove song (HTML form-friendly alias for DELETE) */
static int
handle_choir_song_remove(int fd, char *body)
{
	(void)body;

	char cookie[256] = {0};
	char token[64] = {0};
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	const char *username = call_get_session_user(token);

	if (!username || !*username) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 401);
		ndc_body(fd, "Login required");
		return 0;
	}

	char doc_root[256] = {0};
	char choir_id[128] = {0};
	char song_id[128] = {0};
	char choir_path[512];
	char songs_path[512];

	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	ndc_env_get(fd, choir_id, "PATTERN_PARAM_ID");
	ndc_env_get(fd, song_id, "PATTERN_PARAM_SONG_ID");

	if (!choir_id[0] || !song_id[0]) {
		ndc_head(fd, 400);
		ndc_body(fd, "Missing parameters");
		return 0;
	}

	snprintf(choir_path, sizeof(choir_path), "%s/%s/%s",
		doc_root[0] ? doc_root : ".", CHOIR_SONGS_PATH, choir_id);
	snprintf(songs_path, sizeof(songs_path), "%s/songs", choir_path);

	if (!check_choir_ownership(choir_path, username)) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "Not authorized");
		return 0;
	}

	char line[256];
	char temp_path[512];
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

		if (parse_choir_song_line(line, sid, &pkey, fmt) == 0 && strcmp(sid, song_id) == 0) {
			/* skip - removing this entry */
		} else {
			fputs(line, wfp);
		}
	}
	if (rfp) fclose(rfp);
	fclose(wfp);

	rename(temp_path, songs_path);

	/* Redirect back to choir detail page */
	char location[256];
	snprintf(location, sizeof(location), "/choir/%s", choir_id);
	ndc_header(fd, "Location", location);
	ndc_header(fd, "Connection", "close");
	ndc_set_flags(fd, DF_TO_CLOSE);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
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
	ndc_register_handler("GET:/choir/:id", choir_details_handler);
	ndc_register_handler("GET:/choir/:id/song/:song_id", handle_choir_song_view);
	ndc_register_handler("GET:/api/choir/:id/songs", handle_choir_songs_list);
	ndc_register_handler("POST:/api/choir/:id/songs", handle_choir_song_add);
	ndc_register_handler("POST:/api/choir/:id/song/:song_id/key", handle_choir_song_key);
	ndc_register_handler("DELETE:/api/choir/:id/song/:song_id", handle_choir_song_delete);
	ndc_register_handler("POST:/api/choir/:id/song/:song_id/remove", handle_choir_song_remove);
	ndc_register_handler("POST:/api/choir/:id/edit", handle_choir_edit);

	index_hd = call_index_open("Choir", 0, 1);
}

void ndx_open(void) {}
