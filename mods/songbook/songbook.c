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
#include "../choir/choir.h"

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

	item_ctx_t ctx;
	if (call_item_ctx_load(&ctx, fd, SONGBOOK_ITEMS_PATH, ICTX_NEED_LOGIN))
		return 1;

	/* Check if songbook exists */
	struct stat st;
	if (stat(ctx.item_path, &st) != 0 || !S_ISDIR(st.st_mode))
		return call_respond_error(fd, 404, "Songbook not found");

	if (call_require_ownership(fd, ctx.item_path, ctx.username, NULL))
		return 1;

	/* Read title */
	char title[256] = {0};
	call_read_meta_file(ctx.item_path, "title", title, sizeof(title));

	/* Read choir */
	char choir[128] = {0};
	call_read_meta_file(ctx.item_path, "choir", choir, sizeof(choir));

	/* Read data.txt and resolve originalKey per song */
	char data_path[PATH_MAX];
	snprintf(data_path, sizeof(data_path), "%s/data.txt", ctx.item_path);
	char *data_content = call_slurp_file(data_path);

	char *ac_json = call_build_all_songs_json(ctx.doc_root, 1);
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
			char format[128] = {0};

			if (call_parse_item_line(line, chord_id, &transpose, format) != 0) {
				/* Fallback: treat entire line as bare chord id. */
				strncpy(chord_id, line, sizeof(chord_id) - 1);
				/* strip trailing \r\n */
				size_t il = strlen(chord_id);
				while (il > 0 && (chord_id[il-1] == '\r' || chord_id[il-1] == '\n'))
					chord_id[--il] = '\0';
			}

			int original_key = 0;
			if (chord_id[0]) {
				char song_data_path[PATH_MAX];
				snprintf(song_data_path, sizeof(song_data_path),
					"%s/%s/%s/data.txt", ctx.doc_root, SONG_ITEMS_PATH, chord_id);
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

	/* Build POST body to Fresh */
	form_body_t *fb = call_form_body_new(0);
	if (!fb) { free(ac_json); free(at_json); return call_respond_error(fd, 500, "OOM"); }
	call_form_body_add(fb, "title", title);
	call_form_body_add(fb, "choir", choir);
	call_form_body_add(fb, "songs", songs_buf);
	call_form_body_add(fb, "allChords", ac_json);
	call_form_body_add(fb, "allTypes", at_json);
	free(ac_json);
	free(at_json);

	size_t pb_len = 0;
	char *post_body = call_form_body_finish(fb, &pb_len);
	if (!post_body) return call_respond_error(fd, 500, "OOM");

	int r = call_core_post(fd, post_body, pb_len);
	free(post_body);
	return r;
}

/* POST /songbook/:id/edit - Edit songbook */
static int
handle_sb_edit(int fd, char *body)
{
	item_ctx_t ctx;
	if (call_item_ctx_load(&ctx, fd, SONGBOOK_ITEMS_PATH, ICTX_NEED_LOGIN))
		return 1;

	/* Check if songbook exists */
	struct stat st;
	if (stat(ctx.item_path, &st) != 0 || !S_ISDIR(st.st_mode))
		return call_respond_error(fd, 404, "Songbook not found");

	if (call_require_ownership(fd, ctx.item_path, ctx.username,
			"You don't own this songbook"))
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
			call_datalist_extract_id(sv, sv, sizeof(sv));
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

		char *ac_json = call_build_all_songs_json(ctx.doc_root, 1);
		if (!ac_json) return 1;
		char *at_json = call_song_get_types_json(0);
		if (!at_json) { free(ac_json); return 1; }

		/* Read title + choir from disk (not form — keep stable) */
		char title[256] = {0}, choir[128] = {0};
		call_read_meta_file(ctx.item_path, "title", title, sizeof(title));
		call_read_meta_file(ctx.item_path, "choir", choir, sizeof(choir));

		form_body_t *fb = call_form_body_new(0);
		if (!fb) { free(ac_json); free(at_json); return call_respond_error(fd, 500, "OOM"); }
		call_form_body_add(fb, "title", title);
		call_form_body_add(fb, "choir", choir);
		call_form_body_add(fb, "songs", songs_buf);
		call_form_body_add(fb, "allChords", ac_json);
		call_form_body_add(fb, "allTypes", at_json);
		free(ac_json);
		free(at_json);

		size_t pb_len = 0;
		char *post_body = call_form_body_finish(fb, &pb_len);
		if (!post_body) return call_respond_error(fd, 500, "OOM");
		int r = call_core_post(fd, post_body, pb_len);
		free(post_body);
		return r;
	}

	if (amount < 0)
		return call_bad_request(fd, "Invalid amount");

	/* Build new data.txt content */
	char data_path[PATH_MAX];
	snprintf(data_path, sizeof(data_path), "%s/data.txt", ctx.item_path);

	FILE *dfp = fopen(data_path, "w");
	if (!dfp)
		return call_server_error(fd, "Failed to write data file");

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
		call_datalist_extract_id(song, song, sizeof(song));

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
	snprintf(location, sizeof(location), "/songbook/%s", ctx.id);
	return call_redirect(fd, location);
}

/* POST /api/songbook/:id/transpose - Transpose single song */
struct sb_transpose_cb { int target_idx; int new_transpose; };

static int
sb_transpose_cb(int idx, const char *raw, int parsed,
	const char *sid, int ival, const char *fmt,
	void *user, char *out, size_t out_sz)
{
	(void)raw; (void)ival;
	struct sb_transpose_cb *c = user;
	if (parsed && idx == c->target_idx) {
		snprintf(out, out_sz, "%s:%d:%s\n", sid, c->new_transpose, fmt);
		return SONGS_LINE_REPLACE;
	}
	return SONGS_LINE_KEEP;
}

static int
handle_sb_transpose(int fd, char *body)
{
	item_ctx_t ctx;
	if (call_item_ctx_load(&ctx, fd, SONGBOOK_ITEMS_PATH,
			ICTX_NEED_LOGIN)) return 1;

	/* Distinguish 404 (missing) from 403 (not owner) */
	if (!call_item_check_ownership(ctx.item_path, ctx.username)) {
		struct stat st;
		if (stat(ctx.item_path, &st) != 0 || !S_ISDIR(st.st_mode))
			return call_respond_error(fd, 404, "Songbook not found");
		return call_respond_error(fd, 403, "You don't own this songbook");
	}

	/* Parse form data */
	call_mpfd_parse(fd, body);

	char n_str[16] = {0};
	char t_str[16] = {0};
	call_mpfd_get("n", n_str, sizeof(n_str) - 1);
	call_mpfd_get("t", t_str, sizeof(t_str) - 1);

	struct sb_transpose_cb cbc = {
		.target_idx = atoi(n_str),
		.new_transpose = atoi(t_str),
	};

	char data_path[PATH_MAX];
	snprintf(data_path, sizeof(data_path), "%s/data.txt", ctx.item_path);

	if (call_songs_file_rewrite(data_path, sb_transpose_cb, &cbc) < 0)
		return call_server_error(fd, "Failed to update");

	char location[256];
	snprintf(location, sizeof(location), "/songbook/%s#%d",
		ctx.id, cbc.target_idx);
	return call_redirect(fd, location);
}

/* POST /api/songbook/:id/randomize - Randomize song selection */
static int
sb_randomize_cb(int idx, const char *raw, int parsed,
	const char *sid, int ival, const char *fmt,
	void *user, char *out, size_t out_sz)
{
	(void)raw; (void)sid;
	int target_idx = *(int *)user;
	if (parsed && idx == target_idx) {
		char new_chord[128] = {0};
		if (get_random_chord_by_type(fmt, new_chord, sizeof(new_chord)) == 0) {
			snprintf(out, out_sz, "%s:%d:%s\n", new_chord, ival, fmt);
			return SONGS_LINE_REPLACE;
		}
	}
	return SONGS_LINE_KEEP;
}

static int
handle_sb_randomize(int fd, char *body)
{
	item_ctx_t ctx;
	if (call_item_ctx_load(&ctx, fd, SONGBOOK_ITEMS_PATH,
			ICTX_NEED_LOGIN)) return 1;

	/* Distinguish 404 (missing) from 403 (not owner) */
	if (!call_item_check_ownership(ctx.item_path, ctx.username)) {
		struct stat st;
		if (stat(ctx.item_path, &st) != 0)
			return call_respond_error(fd, 404, "Songbook not found");
		return call_respond_error(fd, 403, "You don't own this songbook");
	}

	/* Parse form data */
	call_mpfd_parse(fd, body);

	char n_str[16] = {0};
	call_mpfd_get("n", n_str, sizeof(n_str) - 1);
	int line_num = atoi(n_str);

	char data_path[PATH_MAX];
	snprintf(data_path, sizeof(data_path), "%s/data.txt", ctx.item_path);

	if (call_songs_file_rewrite(data_path, sb_randomize_cb, &line_num) < 0)
		return call_server_error(fd, "Failed to update");

	char location[256];
	snprintf(location, sizeof(location), "/songbook/%s#%d", ctx.id, line_num);
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
	json_array_t *songs_ja = call_json_array_new(0);
	if (!songs_ja) return NULL;

	char *line = strtok(data, "\n");
	while (line) {
		char chord_id[128] = {0};
		int transpose = 0;
		char format[64] = {0};

		if (call_parse_item_line(line, chord_id, &transpose, format) != 0) {
			line = strtok(NULL, "\n");
			continue;
		}

		if (chord_id[0]) {
			/* Read chord title */
			char chord_title[256] = {0};
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
			char *chord_data_ptr = NULL;
			snprintf(chord_path, sizeof(chord_path), "%s/%s/%s/data.txt",
				doc_root, SONG_ITEMS_PATH, chord_id);
			chord_data_ptr = call_slurp_file(chord_path);
			char *transposed = NULL;
			if (chord_data_ptr) {
				call_song_reset_key(0);
				call_song_transpose(chord_data_ptr, transpose, 0x04 /* TRANSP_HTML */,
					&transposed, &original_key);
				if (original_key < 0) original_key = 0;
				free(chord_data_ptr);
			}

			call_json_array_begin_object(songs_ja);
			call_json_array_kv_str(songs_ja, "chordId", chord_id);
			call_json_array_kv_int(songs_ja, "transpose", transpose);
			call_json_array_kv_str(songs_ja, "format", format);
			call_json_array_kv_str(songs_ja, "chordTitle", chord_title);
			call_json_array_kv_str(songs_ja, "chordData", transposed ? transposed : "");
			call_json_array_kv_int(songs_ja, "originalKey", original_key);
			call_json_array_end_object(songs_ja);

			if (transposed) free(transposed);
		}

		line = strtok(NULL, "\n");
	}

	char *songs_json = call_json_array_finish(songs_ja);
	if (!songs_json) return NULL;

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
	if (!json)
		return call_respond_error(fd, 404, "Songbook not found");
	int result = call_core_post(fd, json, strlen(json));
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
		char sb_item_path[512];
		snprintf(sb_item_path, sizeof(sb_item_path),
			"./items/songbook/items/%s", id);
		call_write_meta_file(sb_item_path, "choir", choir, strlen(choir));

		/* Pre-populate data.txt with one random song per choir format type */
		char doc_root[256] = {0};
		call_get_doc_root(fd, doc_root, sizeof(doc_root));

		char format_path[PATH_MAX];
		snprintf(format_path, sizeof(format_path),
			"%s/items/choir/items/%s/format", doc_root, choir);

		FILE *ffp = fopen(format_path, "r");
		if (ffp) {
			char data_path[PATH_MAX];
			snprintf(data_path, sizeof(data_path),
				"./items/songbook/items/%s/data.txt", id);
			FILE *dfp = fopen(data_path, "w");

			char type[128];
			while (dfp && fgets(type, sizeof(type), ffp)) {
				/* strip trailing newline/whitespace */
				size_t tlen = strlen(type);
				while (tlen > 0 && (type[tlen-1] == '\n' || type[tlen-1] == '\r' || type[tlen-1] == ' '))
					type[--tlen] = '\0';
				if (tlen == 0) continue;

				char song_id[256] = {0};
				if (get_random_chord_by_type(type, song_id, sizeof(song_id)) == 0)
					fprintf(dfp, "%s:0:%s\n", song_id, type);
			}

			if (dfp) fclose(dfp);
			fclose(ffp);
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
