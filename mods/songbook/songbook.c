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
#include <ttypt/ndx.h>
#include <ttypt/ndx-mod.h>
#include <ttypt/qmap.h>

#include "../common/common.h"
#include "../auth/auth.h"
#include "../mpfd/mpfd.h"
#include "../index/index.h"
#include "../song/song.h"

#define SONGBOOK_ITEMS_PATH "items/songbook/items"
#define SONG_ITEMS_PATH "items/song/items"

/* Get random chord by type/format - uses song module's type index */
static int
get_random_chord_by_type(const char *type, char *out_id, size_t out_len)
{
	char *random_id = NULL;
	if (call_song_get_random_by_type(type, &random_id) != 0)
		return -1;

	if (random_id) {
		strncpy(out_id, random_id, out_len - 1);
		out_id[out_len - 1] = '\0';
		free(random_id);
		return 0;
	}

	return -1;
}

/* GET /songbook/:id/edit - read songbook and proxy to Fresh */
static int
handle_sb_edit_get(int fd, char *body)
{
	(void)body;

	const char *username = call_get_request_user(fd);
	if (call_require_login(fd, username))
		return 1;

	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	char doc_root[256] = {0};
	call_get_doc_root(fd, doc_root, sizeof(doc_root));

	char sb_path[512];
	snprintf(sb_path, sizeof(sb_path), "%s/items/songbook/items/%s", doc_root, id);

	/* Check if songbook exists */
	struct stat st;
	if (stat(sb_path, &st) != 0 || !S_ISDIR(st.st_mode))
		return call_respond_plain(fd, 404, "Songbook not found");

	if (call_require_ownership(fd, sb_path, username, NULL))
		return 1;

	/* Read title */
	char title[256] = {0};
	call_read_meta_file(sb_path, "title", title, sizeof(title));

	/* Read choir */
	char choir[128] = {0};
	call_read_meta_file(sb_path, "choir", choir, sizeof(choir));

	/* Read data.txt and resolve originalKey per song */
	char data_path[PATH_MAX];
	snprintf(data_path, sizeof(data_path), "%s/data.txt", sb_path);
	char *data_content = call_slurp_file(data_path);

	char *ac_json = call_build_all_songs_json(doc_root, 1);
	if (!ac_json) { free(data_content); return 1; }

	char *at_json = call_song_get_types_json(0);
	if (!at_json) { free(ac_json); free(data_content); return 1; }

	/* Build 4-field songs string: chord_id:transpose:format:originalKey */
	char songs_buf[65536] = {0};
	size_t songs_pos = 0;
	if (data_content) {
		char *line = strtok(data_content, "\n");
		while (line) {
			char chord_id[128] = {0};
			int transpose = 0;
			char format[64] = {0};
			char *c1 = strchr(line, ':');
			if (c1) {
				size_t idlen = c1 - line;
				if (idlen > 127) idlen = 127;
				strncpy(chord_id, line, idlen);
				char *c2 = strchr(c1 + 1, ':');
				if (c2) {
					strncpy(format, c2 + 1, sizeof(format) - 1);
					/* strip trailing \r */
					size_t fl = strlen(format);
					while (fl > 0 && (format[fl-1] == '\r' || format[fl-1] == '\n'))
						format[--fl] = '\0';
					*c2 = '\0';
				}
				transpose = atoi(c1 + 1);
			} else {
				strncpy(chord_id, line, sizeof(chord_id) - 1);
			}

			int original_key = 0;
			if (chord_id[0]) {
				char song_data_path[PATH_MAX];
				snprintf(song_data_path, sizeof(song_data_path),
					"%s/%s/%s/data.txt", doc_root, SONG_ITEMS_PATH, chord_id);
				char *song_data = call_slurp_file(song_data_path);
				if (song_data) {
					char *transposed = NULL;
					call_song_reset_key(0);
					call_song_transpose(song_data, 0, 0, &transposed, &original_key);
					if (original_key < 0) original_key = 0;
					free(song_data);
					free(transposed);
				}
			}

			songs_pos += snprintf(songs_buf + songs_pos,
				sizeof(songs_buf) - songs_pos,
				"%s:%d:%s:%d\n",
				chord_id, transpose, format[0] ? format : "any", original_key);

			line = strtok(NULL, "\n");
		}
		free(data_content);
	}

	char enc_title[512], enc_choir[256], enc_songs[65536 * 3];
	call_url_encode(title, enc_title, sizeof(enc_title));
	call_url_encode(choir, enc_choir, sizeof(enc_choir));
	call_url_encode(songs_buf, enc_songs, sizeof(enc_songs));

	size_t ac_enc_cap = strlen(ac_json) * 3 + 4;
	char *enc_ac = malloc(ac_enc_cap);
	if (!enc_ac) { free(at_json); free(ac_json); return 1; }
	call_url_encode(ac_json, enc_ac, ac_enc_cap);
	free(ac_json);

	size_t at_enc_cap = strlen(at_json) * 3 + 4;
	char *enc_at = malloc(at_enc_cap);
	if (!enc_at) { free(enc_ac); free(at_json); return 1; }
	call_url_encode(at_json, enc_at, at_enc_cap);
	free(at_json);

	size_t pb_cap = strlen(enc_title) + strlen(enc_choir) + strlen(enc_songs) + strlen(enc_ac) + strlen(enc_at) + 80;
	char *post_body = malloc(pb_cap);
	if (!post_body) { free(enc_at); free(enc_ac); return 1; }
	int len = snprintf(post_body, pb_cap,
		"title=%s&choir=%s&songs=%s&allChords=%s&allTypes=%s",
		enc_title, enc_choir, enc_songs, enc_ac, enc_at);
	free(enc_ac);
	free(enc_at);

	int r = call_core_post(fd, post_body, len);
	free(post_body);
	return r;
}

/* POST /songbook/:id/edit - Edit songbook */
static int
handle_sb_edit(int fd, char *body)
{
	const char *username = call_get_request_user(fd);
	if (call_require_login(fd, username))
		return 1;

	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	char doc_root[256] = {0};
	call_get_doc_root(fd, doc_root, sizeof(doc_root));

	char sb_path[512];
	snprintf(sb_path, sizeof(sb_path), "%s/items/songbook/items/%s", doc_root, id);

	/* Check if songbook exists */
	struct stat st;
	if (stat(sb_path, &st) != 0 || !S_ISDIR(st.st_mode))
		return call_respond_plain(fd, 404, "Songbook not found");

	if (call_require_ownership(fd, sb_path, username, "You don't own this songbook"))
		return 1;

	/* Parse form data */
	call_mpfd_parse(fd, body);

	/* Check action — "add_row" re-renders the edit page with one blank row appended */
	char action[32] = {0};
	call_mpfd_get("action", action, sizeof(action) - 1);

	/* Get amount */
	char amount_str[16] = {0};
	int amount_len = call_mpfd_get("amount", amount_str, sizeof(amount_str) - 1);
	int amount = amount_len > 0 ? atoi(amount_str) : 0;

	if (strcmp(action, "add_row") == 0) {
		/* Reconstruct songs string from submitted fields, append one blank row */
		char songs_buf[65536] = {0};
		size_t songs_pos = 0;
		for (int i = 0; i < amount; i++) {
			char sk[32], kk[32], ok[32], fk[32];
			char sv[128] = {0}, kv[16] = {0}, ov[16] = {0}, fv[128] = {0};
			snprintf(sk, sizeof(sk), "song_%d", i);
			snprintf(kk, sizeof(kk), "key_%d",  i);
			snprintf(ok, sizeof(ok), "orig_%d", i);
			snprintf(fk, sizeof(fk), "fmt_%d",  i);
			call_mpfd_get(sk, sv, sizeof(sv) - 1);
			call_mpfd_get(kk, kv, sizeof(kv) - 1);
			call_mpfd_get(ok, ov, sizeof(ov) - 1);
			call_mpfd_get(fk, fv, sizeof(fv) - 1);
			/* Extract id from "Title [id]" format */
			char *bracket = strrchr(sv, '[');
			if (bracket) {
				char *end = strchr(bracket + 1, ']');
				if (end) { *end = '\0'; memmove(sv, bracket + 1, end - bracket); }
			}
			/* Convert key+orig back to transpose for storage format */
			int target_key = kv[0] ? atoi(kv) : 0;
			int orig        = ov[0] ? atoi(ov) : 0;
			int transpose   = ((target_key - orig) % 12 + 12) % 12;
			songs_pos += snprintf(songs_buf + songs_pos,
				sizeof(songs_buf) - songs_pos,
				"%s:%d:%s:%s\n",
				sv, transpose, fv[0] ? fv : "any", ov[0] ? ov : "0");
		}
		/* Append blank row: originalKey=0, transpose=0, so target key=0 (C) */
		songs_pos += snprintf(songs_buf + songs_pos,
			sizeof(songs_buf) - songs_pos, "::any:0\n");

		/* Read allChords + allTypes JSON */
		char doc_root[256] = {0};
		call_get_doc_root(fd, doc_root, sizeof(doc_root));
		char *ac_json = call_build_all_songs_json(doc_root, 1);
		if (!ac_json) return 1;
		char *at_json = call_song_get_types_json(0);
		if (!at_json) { free(ac_json); return 1; }

		/* Read title + choir from disk (not form — keep stable) */
		char sb_id[128] = {0};
		ndc_env_get(fd, sb_id, "PATTERN_PARAM_ID");
		char sb_path[512];
		snprintf(sb_path, sizeof(sb_path), "%s/items/songbook/items/%s", doc_root, sb_id);
		char title[256] = {0}, choir[128] = {0};
		call_read_meta_file(sb_path, "title", title, sizeof(title));
		call_read_meta_file(sb_path, "choir", choir, sizeof(choir));

		char enc_title[512], enc_choir[256], enc_songs[65536 * 3];
		call_url_encode(title, enc_title, sizeof(enc_title));
		call_url_encode(choir, enc_choir, sizeof(enc_choir));
		call_url_encode(songs_buf, enc_songs, sizeof(enc_songs));

		size_t ac_enc_cap = strlen(ac_json) * 3 + 4;
		char *enc_ac = malloc(ac_enc_cap);
		if (!enc_ac) { free(at_json); free(ac_json); return 1; }
		call_url_encode(ac_json, enc_ac, ac_enc_cap);
		free(ac_json);

		size_t at_enc_cap = strlen(at_json) * 3 + 4;
		char *enc_at = malloc(at_enc_cap);
		if (!enc_at) { free(enc_ac); free(at_json); return 1; }
		call_url_encode(at_json, enc_at, at_enc_cap);
		free(at_json);

		size_t pb_cap = strlen(enc_title) + strlen(enc_choir) + strlen(enc_songs) + strlen(enc_ac) + strlen(enc_at) + 80;
		char *post_body = malloc(pb_cap);
		if (!post_body) { free(enc_at); free(enc_ac); return 1; }
		int len = snprintf(post_body, pb_cap,
			"title=%s&choir=%s&songs=%s&allChords=%s&allTypes=%s",
			enc_title, enc_choir, enc_songs, enc_ac, enc_at);
		free(enc_ac);
		free(enc_at);
		int r = call_core_post(fd, post_body, len);
		free(post_body);
		return r;
	}

	if (amount < 0)
		return call_respond_plain(fd, 400, "Invalid amount");

	/* Build new data.txt content */
	char data_path[PATH_MAX];
	snprintf(data_path, sizeof(data_path), "%s/data.txt", sb_path);

	FILE *dfp = fopen(data_path, "w");
	if (!dfp)
		return call_respond_plain(fd, 500, "Failed to write data file");

	for (int i = 0; i < amount; i++) {
		char song_key[32], key_key[32], orig_key[32], fmt_key[32];
		snprintf(song_key, sizeof(song_key), "song_%d", i);
		snprintf(key_key,  sizeof(key_key),  "key_%d",  i);
		snprintf(orig_key, sizeof(orig_key), "orig_%d", i);
		snprintf(fmt_key,  sizeof(fmt_key),  "fmt_%d",  i);

		char song[128] = {0};
		char key_str[16] = {0};
		char orig_str[16] = {0};
		char fmt[128] = {0};

		call_mpfd_get(song_key, song, sizeof(song) - 1);
		call_mpfd_get(key_key,  key_str,  sizeof(key_str)  - 1);
		call_mpfd_get(orig_key, orig_str, sizeof(orig_str) - 1);
		call_mpfd_get(fmt_key,  fmt,      sizeof(fmt)      - 1);

		/* Parse id from "Title [id]" datalist format */
		{
			char *bracket = strrchr(song, '[');
			if (bracket) {
				char *end = strchr(bracket + 1, ']');
				if (end) {
					*end = '\0';
					memmove(song, bracket + 1, end - bracket);
				}
			}
		}

		/* Convert target key note → semitone transpose */
		int target_key = key_str[0] ? atoi(key_str) : 0;
		int orig        = orig_str[0] ? atoi(orig_str) : 0;
		int transpose   = ((target_key - orig) % 12 + 12) % 12;

		fprintf(dfp, "%s:%d:%s\n",
			song[0] ? song : "",
			transpose,
			fmt[0] ? fmt : "any");
	}

	fclose(dfp);

	/* Redirect to view page */
	char location[256];
	snprintf(location, sizeof(location), "/songbook/%s", id);
	return call_redirect(fd, location);
}

/* POST /api/songbook/:id/transpose - Transpose single song */
static int
handle_sb_transpose(int fd, char *body)
{
	const char *username = call_get_request_user(fd);
	if (call_require_login(fd, username))
		return 1;

	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	char doc_root[256] = {0};
	call_get_doc_root(fd, doc_root, sizeof(doc_root));

	char sb_path[512];
	snprintf(sb_path, sizeof(sb_path), "%s/items/songbook/items/%s", doc_root, id);

	/* Check if songbook exists */
	struct stat st;
	if (stat(sb_path, &st) != 0 || !S_ISDIR(st.st_mode))
		return call_respond_plain(fd, 404, "Songbook not found");

	if (call_require_ownership(fd, sb_path, username, "You don't own this songbook"))
		return 1;

	/* Parse form data */
	call_mpfd_parse(fd, body);

	char n_str[16] = {0};
	char t_str[16] = {0};
	call_mpfd_get("n", n_str, sizeof(n_str) - 1);
	call_mpfd_get("t", t_str, sizeof(t_str) - 1);

	int line_num = atoi(n_str);
	int new_transpose = atoi(t_str);

	/* Read current data.txt */
	char data_path[PATH_MAX];
	snprintf(data_path, sizeof(data_path), "%s/data.txt", sb_path);

	FILE *fp = fopen(data_path, "r");
	if (!fp)
		return call_respond_plain(fd, 500, "Failed to read data file");

	/* Read all lines */
	char **lines = NULL;
	int line_count = 0;
	int line_capacity = 0;
	char buffer[512];

	while (fgets(buffer, sizeof(buffer), fp)) {
		if (line_count >= line_capacity) {
			line_capacity = line_capacity == 0 ? 16 : line_capacity * 2;
			lines = realloc(lines, line_capacity * sizeof(char *));
		}
		lines[line_count] = strdup(buffer);
		line_count++;
	}
	fclose(fp);

	/* Update the specified line */
	if (line_num >= 0 && line_num < line_count) {
		char chord_id[128], format[128];
		int transpose;

		if (call_parse_item_line(lines[line_num], chord_id, &transpose, format) == 0) {
			free(lines[line_num]);
			char new_line[512];
			snprintf(new_line, sizeof(new_line), "%s:%d:%s\n", chord_id, new_transpose, format);
			lines[line_num] = strdup(new_line);
		}
	}

	/* Write back */
	fp = fopen(data_path, "w");
	if (fp) {
		for (int i = 0; i < line_count; i++)
			fputs(lines[i], fp);
		fclose(fp);
	}

	for (int i = 0; i < line_count; i++)
		free(lines[i]);
	free(lines);

	/* Redirect back with anchor */
	char location[256];
	snprintf(location, sizeof(location), "/songbook/%s#%d", id, line_num);
	return call_redirect(fd, location);
}

/* POST /api/songbook/:id/randomize - Randomize song selection */
static int
handle_sb_randomize(int fd, char *body)
{
	const char *username = call_get_request_user(fd);
	if (call_require_login(fd, username))
		return 1;

	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	char doc_root[256] = {0};
	call_get_doc_root(fd, doc_root, sizeof(doc_root));

	char sb_path[512];
	snprintf(sb_path, sizeof(sb_path), "%s/items/songbook/items/%s", doc_root, id);

	struct stat st;
	if (stat(sb_path, &st) != 0)
		return call_respond_plain(fd, 404, "Songbook not found");

	if (call_require_ownership(fd, sb_path, username, "You don't own this songbook"))
		return 1;

	/* Parse form data */
	call_mpfd_parse(fd, body);

	char n_str[16] = {0};
	call_mpfd_get("n", n_str, sizeof(n_str) - 1);
	int line_num = atoi(n_str);

	/* Read current data.txt */
	char data_path[PATH_MAX];
	snprintf(data_path, sizeof(data_path), "%s/data.txt", sb_path);

	FILE *fp = fopen(data_path, "r");
	if (!fp)
		return call_respond_plain(fd, 500, "Failed to read data file");

	/* Read all lines */
	char **lines = NULL;
	int line_count = 0;
	int line_capacity = 0;
	char buffer[512];

	while (fgets(buffer, sizeof(buffer), fp)) {
		if (line_count >= line_capacity) {
			line_capacity = line_capacity == 0 ? 16 : line_capacity * 2;
			lines = realloc(lines, line_capacity * sizeof(char *));
		}
		lines[line_count] = strdup(buffer);
		line_count++;
	}
	fclose(fp);

	/* Get format from specified line and randomize */
	if (line_num >= 0 && line_num < line_count) {
		char chord_id[128], format[128];
		int transpose;

		if (call_parse_item_line(lines[line_num], chord_id, &transpose, format) == 0) {
			char new_chord[128] = {0};
			if (get_random_chord_by_type(format, new_chord, sizeof(new_chord)) == 0) {
				free(lines[line_num]);
				char new_line[512];
				snprintf(new_line, sizeof(new_line), "%s:%d:%s\n", new_chord, transpose, format);
				lines[line_num] = strdup(new_line);
			}
		}
	}

	/* Write back */
	fp = fopen(data_path, "w");
	if (fp) {
		for (int i = 0; i < line_count; i++)
			fputs(lines[i], fp);
		fclose(fp);
	}

	for (int i = 0; i < line_count; i++)
		free(lines[i]);
	free(lines);

	/* Redirect back with anchor */
	char location[256];
	snprintf(location, sizeof(location), "/songbook/%s#%d", id, line_num);
	return call_redirect(fd, location);
}

static char *
songbook_json(int fd)
{
	char doc_root[256] = {0};
	char id[128] = {0};
	char sb_path[512];
	char path_buf[1024];

	char title[256] = {0}, owner[64] = {0}, choir[64] = {0};
	char data[8192] = {0};
	char esc_title[512], esc_owner[128], esc_choir[128];

	call_get_doc_root(fd, doc_root, sizeof(doc_root));
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	if (!id[0]) return NULL;

	snprintf(sb_path, sizeof(sb_path), "%s/%s/%s",
		doc_root, SONGBOOK_ITEMS_PATH, id);

	/* Read title */
	if (call_read_meta_file(sb_path, "title", title, sizeof(title)) != 0)
		return NULL;

	/* Read owner */
	call_item_read_owner(sb_path, owner, sizeof(owner));

	/* Read choir */
	call_read_meta_file(sb_path, "choir", choir, sizeof(choir));

	/* Read data.txt - contains lines of chord_id:transpose:format */
	snprintf(path_buf, sizeof(path_buf), "%s/data.txt", sb_path);
	FILE *dfp = fopen(path_buf, "r");
	if (dfp) {
		size_t n = fread(data, 1, sizeof(data) - 1, dfp);
		data[n] = '\0';
		fclose(dfp);
	}

	/* Parse data lines and build songs JSON array */
	char *songs_json = malloc(16384);
	if (!songs_json) return NULL;
	songs_json[0] = '[';
	songs_json[1] = '\0';

	char *line = strtok(data, "\n");
	int first = 1;
	while (line) {
		char chord_id[128] = {0};
		int transpose = 0;
		char format[64] = {0};

		char *colon1 = strchr(line, ':');
		if (colon1) {
			size_t id_len = colon1 - line;
			if (id_len > 127) id_len = 127;
			strncpy(chord_id, line, id_len);

			char *colon2 = strchr(colon1 + 1, ':');
			if (colon2) {
				strncpy(format, colon2 + 1, sizeof(format) - 1);
				*colon2 = '\0';
			}
			transpose = atoi(colon1 + 1);
		} else {
			strncpy(chord_id, line, sizeof(chord_id) - 1);
		}

		/* Remove trailing \r if present */
		size_t fmt_len = strlen(format);
		while (fmt_len > 0 && (format[fmt_len - 1] == '\r' || format[fmt_len - 1] == '\n')) {
			format[--fmt_len] = '\0';
		}

		if (chord_id[0]) {
			/* Read chord title */
			char chord_title[256] = {0};
			char chord_title_esc[512];
			char chord_path[512];
			int original_key = 0;
			snprintf(chord_path, sizeof(chord_path), "%s/%s/%s/title",
				doc_root, SONG_ITEMS_PATH, chord_id);
			FILE *ctfp = fopen(chord_path, "r");
			if (ctfp) {
				if (fgets(chord_title, sizeof(chord_title) - 1, ctfp)) {
					size_t l = strlen(chord_title);
					if (l > 0 && chord_title[l - 1] == '\n') chord_title[l - 1] = '\0';
				}
				fclose(ctfp);
			}

			/* Read and transpose chord data */
			char chord_data[4096] = {0};
			char chord_data_esc[12288];
			snprintf(chord_path, sizeof(chord_path), "%s/%s/%s/data.txt",
				doc_root, SONG_ITEMS_PATH, chord_id);
			FILE *cdfp = fopen(chord_path, "r");
			if (cdfp) {
				size_t n = fread(chord_data, 1, sizeof(chord_data) - 1, cdfp);
				chord_data[n] = '\0';
				fclose(cdfp);

			call_song_reset_key(0);
			char *transposed = NULL;
			call_song_transpose(chord_data, transpose, 0x04 /* TRANSP_HTML */, &transposed, &original_key);
			if (original_key < 0) original_key = 0;
			if (transposed) {
				call_json_escape(transposed, chord_data_esc, sizeof(chord_data_esc));
				free(transposed);
			} else {
				chord_data_esc[0] = '\0';
			}
			}

			call_json_escape(chord_title, chord_title_esc, sizeof(chord_title_esc));

			char esc_format[128];
			call_json_escape(format, esc_format, sizeof(esc_format));

			if (!first) strcat(songs_json, ",");
			first = 0;

			char song_buf[20480];
			snprintf(song_buf, sizeof(song_buf),
				"{\"chordId\":\"%s\",\"transpose\":%d,\"format\":\"%s\",\"chordTitle\":\"%s\",\"chordData\":\"%s\",\"originalKey\":%d}",
				chord_id, transpose, esc_format, chord_title_esc, chord_data_esc, original_key);
			strcat(songs_json, song_buf);
		}

		line = strtok(NULL, "\n");
	}

	strcat(songs_json, "]");

	call_json_escape(title, esc_title, sizeof(esc_title));
	call_json_escape(owner, esc_owner, sizeof(esc_owner));
	call_json_escape(choir, esc_choir, sizeof(esc_choir));

	size_t resp_len = strlen(esc_title) + strlen(esc_owner) +
		strlen(esc_choir) + strlen(songs_json) + 256;

	char *response = malloc(resp_len);
	if (response) {
		snprintf(response, resp_len,
			"{"
			"\"id\":\"%s\","
			"\"title\":\"%s\","
			"\"owner\":\"%s\","
			"\"choir\":\"%s\","
			"\"songs\":%s"
			"}",
			id, esc_title, esc_owner, esc_choir, songs_json);
	}

	free(songs_json);
	return response;
}

static int
songbook_details_handler(int fd, char *body)
{
	(void)body;

	char *json = songbook_json(fd);
	int result = call_core_post(fd, json, json ? strlen(json) : 0);
	free(json);
	return result;
}

static int
handle_sb_add(int fd, char *body)
{
	char id[256] = {0};
	if (call_index_add_item(fd, body, id, sizeof(id)) != 0)
		return 1;

	/* Write choir field if provided */
	char choir[128] = {0};
	int choir_len = call_mpfd_get("choir", choir, sizeof(choir) - 1);
	if (choir_len > 0) {
		choir[choir_len] = '\0';
		char choir_path[512];
		snprintf(choir_path, sizeof(choir_path),
			"./items/songbook/items/%s/choir", id);
		FILE *fp = fopen(choir_path, "w");
		if (fp) {
			fwrite(choir, 1, strlen(choir), fp);
			fclose(fp);
		}
	}

	char location[512];
	snprintf(location, sizeof(location), "/songbook/%s", id);
	return call_redirect(fd, location);
}

void ndx_install(void)
{
	ndx_load("./mods/index/index");
	ndx_load("./mods/mpfd/mpfd");
	ndx_load("./mods/auth/auth");
	ndx_load("./mods/song/song");

	ndc_register_handler("GET:/songbook/:id",
			songbook_details_handler);

	ndc_register_handler("POST:/songbook/:id/randomize", handle_sb_randomize);
	ndc_register_handler("POST:/songbook/:id/transpose", handle_sb_transpose);
	ndc_register_handler("GET:/songbook/:id/edit", handle_sb_edit_get);
	ndc_register_handler("POST:/songbook/:id/edit", handle_sb_edit);

	call_index_open("Songbook", 0, 1, NULL);

	/* Override the generic POST:/songbook/add to also handle the choir field */
	ndc_register_handler("POST:/songbook/add", handle_sb_add);
}
