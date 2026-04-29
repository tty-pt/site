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
#include "../choir/repertoire.h"

#define SONGBOOK_ITEMS_PATH "items/songbook/items"
static unsigned index_hd = 0;
static unsigned songbook_index_hd = 0;

typedef struct {
	char song[128];
	int target_key;
	int original_key;
	int transpose;
	char format[128];
} sb_form_row_t;

typedef struct {
	char title[256];
	char choir[128];
} songbook_meta_t;

typedef struct {
	const songbook_meta_t *meta;
	const char *songs;
	const char *all_chords;
	const char *all_types;
} sb_edit_form_t;

static void
songbook_meta_read(const char *item_path, songbook_meta_t *meta)
{
	meta_field_t fields[] = {
		{ "title", meta->title, sizeof(meta->title) },
		{ "choir", meta->choir, sizeof(meta->choir) },
	};

	memset(meta, 0, sizeof(*meta));
	meta_fields_read(item_path, fields, sizeof(fields) / sizeof(fields[0]));
}

static int
songbook_meta_write(const char *item_path, const songbook_meta_t *meta)
{
	meta_field_t fields[] = {
		{ "title", (char *)meta->title, sizeof(meta->title) },
		{ "choir", (char *)meta->choir, sizeof(meta->choir) },
	};

	return meta_fields_write(item_path, fields, sizeof(fields) / sizeof(fields[0]));
}

static void
index_field_clean(char *s)
{
	for (; s && *s; s++)
		if (*s == '\t' || *s == '\n' || *s == '\r')
			*s = ' ';
}

static int
songbook_index_write_file(const char *doc_root)
{
	const char *root = (doc_root && doc_root[0]) ? doc_root : ".";
	char path[PATH_MAX], tmp[PATH_MAX];
	FILE *fp;
	unsigned c;
	const void *k, *v;

	snprintf(path, sizeof(path), "%s/items/songbook/index.tsv", root);
	snprintf(tmp, sizeof(tmp), "%s/items/songbook/index.tsv.tmp", root);
	fp = fopen(tmp, "w");
	if (!fp)
		return -1;

	c = qmap_iter(songbook_index_hd, NULL, 0);
	while (qmap_next(&k, &v, c))
		fprintf(fp, "%s\t%s\n", (const char *)k, (const char *)v);

	if (fclose(fp) != 0)
		return -1;
	return rename(tmp, path);
}

static void
songbook_index_put_meta(const char *id, const songbook_meta_t *meta)
{
	char title[256], choir[128], val[512];

	snprintf(title, sizeof(title), "%s", meta->title);
	snprintf(choir, sizeof(choir), "%s", meta->choir);
	index_field_clean(title);
	index_field_clean(choir);
	snprintf(val, sizeof(val), "%s\t%s", title, choir);
	qmap_put(songbook_index_hd, id, val);
}

static int
songbook_index_upsert(const char *doc_root, const char *id, const char *item_path)
{
	songbook_meta_t meta;

	songbook_meta_read(item_path, &meta);
	songbook_index_put_meta(id, &meta);
	return songbook_index_write_file(doc_root);
}

static void
songbook_cleanup(const char *id)
{
	qmap_del(songbook_index_hd, id);
	songbook_index_write_file(".");
}

static int
songbook_index_load(const char *doc_root)
{
	const char *root = (doc_root && doc_root[0]) ? doc_root : ".";
	char path[PATH_MAX], line[1024];
	FILE *fp;

	snprintf(path, sizeof(path), "%s/items/songbook/index.tsv", root);
	fp = fopen(path, "r");
	if (!fp)
		return -1;

	while (fgets(line, sizeof(line), fp)) {
		char *id = line, *title, *choir, val[512];
		char *nl = strpbrk(line, "\r\n");
		if (nl) *nl = '\0';
		title = strchr(id, '\t');
		if (!title) continue;
		*title++ = '\0';
		choir = strchr(title, '\t');
		if (!choir) choir = "";
		else *choir++ = '\0';
		snprintf(val, sizeof(val), "%s\t%s", title, choir);
		qmap_put(songbook_index_hd, id, val);
	}

	fclose(fp);
	return 0;
}

static void
songbook_index_rebuild(const char *doc_root)
{
	char path[PATH_MAX];
	DIR *dir;
	struct dirent *entry;

	if (module_items_path_build(doc_root, "songbook", path, sizeof(path)) != 0)
		return;
	dir = opendir(path);
	if (!dir)
		return;

	while ((entry = readdir(dir)) != NULL) {
		char item_path[PATH_MAX];
		if (entry->d_name[0] == '.')
			continue;
		if (item_path_build_root(doc_root, "songbook", entry->d_name,
				item_path, sizeof(item_path)) != 0)
			continue;
		songbook_index_upsert(doc_root, entry->d_name, item_path);
	}
	closedir(dir);
}

static int
sb_edit_form_build(int fd, form_body_t *fb, void *user)
{
	sb_edit_form_t *form = user;

	if (form_body_add(fb, "title", form->meta->title) != 0 ||
			form_body_add(fb, "choir", form->meta->choir) != 0 ||
			form_body_add(fb, "songs", form->songs) != 0 ||
			form_body_add(fb, "allChords", form->all_chords) != 0 ||
			form_body_add(fb, "allTypes", form->all_types) != 0)
		return respond_error(fd, 500, "OOM");

	return 0;
}

/* Get random chord by type/format - uses song module's type index */
static int
get_random_chord_by_type(const char *type, char *out_id, size_t out_len)
{
	char *random_id = NULL;
	if (song_get_random_by_type(type, &random_id) != 0)
		return -1;

	if (random_id) {
		strncpy(out_id, random_id, out_len - 1);
		out_id[out_len - 1] = '\0';
		free(random_id);
		return 0;
	}

	return -1;
}

static void
sb_form_row_load(int idx, sb_form_row_t *row)
{
	char song_key[32], key_key[32], orig_key[32], fmt_key[32];
	char key_str[16] = {0};
	char orig_str[16] = {0};

	memset(row, 0, sizeof(*row));

	snprintf(song_key, sizeof(song_key), "song_%d", idx);
	snprintf(key_key,  sizeof(key_key),  "key_%d",  idx);
	snprintf(orig_key, sizeof(orig_key), "orig_%d", idx);
	snprintf(fmt_key,  sizeof(fmt_key),  "fmt_%d",  idx);

	mpfd_get(song_key, row->song, sizeof(row->song) - 1);
	mpfd_get(key_key,  key_str,   sizeof(key_str)   - 1);
	mpfd_get(orig_key, orig_str,  sizeof(orig_str)  - 1);
	mpfd_get(fmt_key,  row->format, sizeof(row->format) - 1);

	datalist_extract_id(row->song, row->song, sizeof(row->song));

	row->target_key = key_str[0] ? atoi(key_str) : 0;
	row->original_key = orig_str[0] ? atoi(orig_str) : 0;
	row->transpose = ((row->target_key - row->original_key) % 12 + 12) % 12;

	if (!row->format[0])
		snprintf(row->format, sizeof(row->format), "any");
}

static int
sb_form_row_to_repertoire(const sb_form_row_t *form_row, repertoire_row_t *row)
{
	if (!form_row || !row)
		return -1;

	memset(row, 0, sizeof(*row));
	snprintf(row->id, sizeof(row->id), "%s", form_row->song);
	row->value = form_row->transpose;
	snprintf(row->format, sizeof(row->format), "%s",
		form_row->format[0] ? form_row->format : "any");
	return 0;
}

static int
sb_song_json_append(json_array_t *songs_ja, const char *doc_root,
	const repertoire_row_t *row)
{
	char chord_title[256] = {0};
	int original_key = 0;
	char *transposed = NULL;

	song_read_title(doc_root, row->id, chord_title, sizeof(chord_title));
	if (song_transpose_root(doc_root, row->id, row->value, 0x04,
			&transposed, &original_key) != 0) {
		original_key = 0;
	}

	json_array_begin_object(songs_ja);
	json_array_kv_str(songs_ja, "chordId", row->id);
	json_array_kv_int(songs_ja, "transpose", row->value);
	json_array_kv_str(songs_ja, "format", row->format);
	json_array_kv_str(songs_ja, "chordTitle", chord_title);
	json_array_kv_str(songs_ja, "chordData", transposed ? transposed : "");
	json_array_kv_int(songs_ja, "originalKey", original_key);
	json_array_end_object(songs_ja);

	free(transposed);
	return 0;
}

static int
sb_edit_song_row_append(char *songs_buf, size_t songs_sz, size_t *songs_pos,
	const char *doc_root, const repertoire_row_t *row)
{
	char chord_id[128] = {0};
	snprintf(chord_id, sizeof(chord_id), "%s", row->id);

	int original_key = 0;
	if (chord_id[0]) {
		original_key = song_get_original_key_root(doc_root, chord_id);
	}

	*songs_pos += snprintf(songs_buf + *songs_pos,
		songs_sz - *songs_pos,
		"%s:%d:%s:%d\n",
		chord_id, row->value,
		row->format[0] ? row->format : "any", original_key);
	if (*songs_pos >= songs_sz)
		return -1;

	return 0;
}

static int
sb_form_rows_collect(int amount, repertoire_row_t **rows_out, size_t *count_out)
{
	repertoire_row_t *rows;

	if (!rows_out || !count_out || amount < 0)
		return -1;

	*rows_out = NULL;
	*count_out = 0;

	if (amount == 0)
		return 0;

	rows = calloc((size_t)amount, sizeof(*rows));
	if (!rows)
		return -1;

	for (int i = 0; i < amount; i++) {
		sb_form_row_t form_row;
		sb_form_row_load(i, &form_row);
		sb_form_row_to_repertoire(&form_row, &rows[i]);
	}

	*rows_out = rows;
	*count_out = (size_t)amount;
	return 0;
}

static int
sb_form_rows_build_edit_songs(int amount, int append_blank,
	char *songs_buf, size_t songs_sz)
{
	size_t songs_pos = 0;
	for (int i = 0; i < amount; i++) {
		sb_form_row_t row;
		sb_form_row_load(i, &row);

		songs_pos += snprintf(songs_buf + songs_pos,
			songs_sz - songs_pos,
			"%s:%d:%s:%d\n",
			row.song, row.transpose, row.format, row.original_key);
		if (songs_pos >= songs_sz)
			return -1;
	}

	if (append_blank) {
		songs_pos += snprintf(songs_buf + songs_pos,
			songs_sz - songs_pos, "::any:0\n");
		if (songs_pos >= songs_sz)
			return -1;
	}

	return 0;
}

/* GET /songbook/:id/edit - read songbook and render the edit form */
static int
handle_sb_edit_get_authorized(int fd, char *body,
	const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;

	songbook_meta_t meta;
	songbook_meta_read(ctx->item_path, &meta);

	/* Read data.txt and resolve originalKey per song */
	char data_path[PATH_MAX];
	item_child_path(ctx->item_path, "data.txt", data_path, sizeof(data_path));
	repertoire_row_t *rows = NULL;
	size_t row_count = 0;
	if (repertoire_rows_load(data_path, &rows, &row_count) != 0)
		return respond_error(fd, 500, "Failed to read data file");

	char *ac_json = build_all_songs_json(ctx->doc_root, 1);
	if (!ac_json) { free(rows); return 1; }

	char *at_json = song_get_types_json(0);
	if (!at_json) { free(ac_json); free(rows); return 1; }

	/* Build 4-field songs string: chord_id:transpose:format:originalKey */
	char songs_buf[65536] = {0};
	size_t songs_pos = 0;
	if (row_count == 0) {
		songs_pos += snprintf(songs_buf + songs_pos,
			sizeof(songs_buf) - songs_pos, "::any:0\n");
		if (songs_pos >= sizeof(songs_buf)) {
			free(rows);
			free(ac_json);
			free(at_json);
			return respond_error(fd, 500, "OOM");
		}
	} else {
		for (size_t i = 0; i < row_count; i++) {
			if (sb_edit_song_row_append(songs_buf, sizeof(songs_buf), &songs_pos,
					ctx->doc_root, &rows[i]) != 0) {
				free(rows);
				free(ac_json);
				free(at_json);
				return respond_error(fd, 500, "OOM");
			}
		}
	}
	free(rows);

	sb_edit_form_t form = {
		.meta = &meta,
		.songs = songs_buf,
		.all_chords = ac_json,
		.all_types = at_json,
	};
	int rc = core_post_form_builder(fd, sb_edit_form_build, &form);
	free(ac_json);
	free(at_json);
	return rc;
}

static int
handle_sb_edit_get(int fd, char *body)
{
	return with_item_access(fd, body, SONGBOOK_ITEMS_PATH,
		ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
		"Songbook not found", "Forbidden",
		handle_sb_edit_get_authorized, NULL);
}

/* POST /songbook/:id/edit - Edit songbook */
static int
handle_sb_edit_authorized(int fd, char *body,
	const item_ctx_t *ctx, void *user)
{
	(void)user;

	/* Parse form data */
	mpfd_parse(fd, body);

	/* Check action — "add_row" re-renders the edit page with one blank row appended */
	char action[32] = {0};
	mpfd_get("action", action, sizeof(action) - 1);

	/* Get amount */
	char amount_str[16] = {0};
	int amount_len = mpfd_get("amount", amount_str, sizeof(amount_str) - 1);
	int amount = amount_len > 0 ? atoi(amount_str) : 0;

	if (strcmp(action, "add_row") == 0) {
		char songs_buf[65536] = {0};
		if (sb_form_rows_build_edit_songs(amount, 1,
				songs_buf, sizeof(songs_buf)) != 0)
			return respond_error(fd, 500, "OOM");

		char *ac_json = build_all_songs_json(ctx->doc_root, 1);
		if (!ac_json) return 1;
		char *at_json = song_get_types_json(0);
		if (!at_json) { free(ac_json); return 1; }

		songbook_meta_t meta;
		songbook_meta_read(ctx->item_path, &meta);

		sb_edit_form_t form = {
			.meta = &meta,
			.songs = songs_buf,
			.all_chords = ac_json,
			.all_types = at_json,
		};
		int rc = core_post_form_builder(fd, sb_edit_form_build, &form);
		free(ac_json);
		free(at_json);
		return rc;
	}

	if (amount < 0)
		return bad_request(fd, "Invalid amount");

	songbook_meta_t meta;
	songbook_meta_read(ctx->item_path, &meta);
	int title_len = mpfd_get("title", meta.title, sizeof(meta.title) - 1);
	int choir_len = mpfd_get("choir", meta.choir, sizeof(meta.choir) - 1);
	if (title_len > 0)
		meta.title[title_len] = '\0';
	if (choir_len > 0)
		meta.choir[choir_len] = '\0';
	if (title_len > 0 || choir_len > 0) {
		if (songbook_meta_write(ctx->item_path, &meta) != 0)
			return server_error(fd, "Failed to write songbook metadata");
		index_put(index_hd, (char *)ctx->id, meta.title);
		songbook_index_put_meta(ctx->id, &meta);
		songbook_index_write_file(ctx->doc_root);
	}

	/* Build new data.txt content */
	char data_path[PATH_MAX];
	item_child_path(ctx->item_path, "data.txt", data_path, sizeof(data_path));

	repertoire_row_t *rows = NULL;
	size_t row_count = 0;
	if (sb_form_rows_collect(amount, &rows, &row_count) != 0)
		return respond_error(fd, 500, "OOM");
	if (repertoire_rows_write(data_path, rows, row_count) != 0) {
		free(rows);
		return server_error(fd, "Failed to write data file");
	}
	free(rows);

	/* Redirect to view page */
	char location[256];
	snprintf(location, sizeof(location), "/songbook/%s", ctx->id);
	return redirect(fd, location);
}

static int
handle_sb_edit(int fd, char *body)
{
	return with_item_access(fd, body, SONGBOOK_ITEMS_PATH,
		ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
		"Songbook not found", "You don't own this songbook",
		handle_sb_edit_authorized, NULL);
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
		return REPERTOIRE_LINE_REPLACE;
	}
	return REPERTOIRE_LINE_KEEP;
}

static int
handle_sb_transpose_authorized(int fd, char *body,
	const item_ctx_t *ctx, void *user)
{
	(void)user;

	/* Parse form data */
	mpfd_parse(fd, body);

	char n_str[16] = {0};
	char t_str[16] = {0};
	mpfd_get("n", n_str, sizeof(n_str) - 1);
	mpfd_get("t", t_str, sizeof(t_str) - 1);

	struct sb_transpose_cb cbc = {
		.target_idx = atoi(n_str),
		.new_transpose = atoi(t_str),
	};

	char data_path[PATH_MAX];
	item_child_path(ctx->item_path, "data.txt", data_path, sizeof(data_path));

	if (repertoire_file_rewrite(data_path, sb_transpose_cb, &cbc) < 0)
		return server_error(fd, "Failed to update");

	char location[256];
	snprintf(location, sizeof(location), "/songbook/%s#%d",
		ctx->id, cbc.target_idx);
	return redirect(fd, location);
}

static int
handle_sb_transpose(int fd, char *body)
{
	return with_item_access(fd, body, SONGBOOK_ITEMS_PATH,
		ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
		"Songbook not found", "You don't own this songbook",
		handle_sb_transpose_authorized, NULL);
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
			return REPERTOIRE_LINE_REPLACE;
		}
	}
	return REPERTOIRE_LINE_KEEP;
}

static int
handle_sb_randomize_authorized(int fd, char *body,
	const item_ctx_t *ctx, void *user)
{
	(void)user;

	/* Parse form data */
	mpfd_parse(fd, body);

	char n_str[16] = {0};
	mpfd_get("n", n_str, sizeof(n_str) - 1);
	int line_num = atoi(n_str);

	char data_path[PATH_MAX];
	item_child_path(ctx->item_path, "data.txt", data_path, sizeof(data_path));

	if (repertoire_file_rewrite(data_path, sb_randomize_cb, &line_num) < 0)
		return server_error(fd, "Failed to update");

	char location[256];
	snprintf(location, sizeof(location), "/songbook/%s#%d", ctx->id, line_num);
	return redirect(fd, location);
}

static int
handle_sb_randomize(int fd, char *body)
{
	return with_item_access(fd, body, SONGBOOK_ITEMS_PATH,
		ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
		"Songbook not found", "You don't own this songbook",
		handle_sb_randomize_authorized, NULL);
}

static char *
songbook_json(int fd)
{
	char doc_root[256] = {0};
	char id[128] = {0};
	char sb_path[512];
	char owner[64] = {0};
	const char *username = NULL;
	songbook_meta_t meta;
	repertoire_row_t *rows = NULL;
	size_t row_count = 0;
	int viewer_zoom = 100;

	get_doc_root(fd, doc_root, sizeof(doc_root));
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	if (!id[0]) return NULL;

	if (item_path_build(fd, "songbook", id, sb_path, sizeof(sb_path)) != 0)
		return NULL;

	songbook_meta_read(sb_path, &meta);
	if (!meta.title[0])
		return NULL;

	/* Read owner */
	item_read_owner(sb_path, owner, sizeof(owner));
	username = get_request_user(fd);
	if (username && *username)
		viewer_zoom = song_get_viewer_zoom(username);

	/* Read data.txt - contains lines of chord_id:transpose:format */
	char path_buf[1024];
	item_child_path(sb_path, "data.txt", path_buf, sizeof(path_buf));
	if (repertoire_rows_load(path_buf, &rows, &row_count) != 0)
		return NULL;

	/* Parse data lines and build songs JSON array */
	json_array_t *songs_ja = json_array_new(0);
	if (!songs_ja) {
		free(rows);
		return NULL;
	}

	for (size_t i = 0; i < row_count; i++)
		sb_song_json_append(songs_ja, doc_root, &rows[i]);
	free(rows);

	char *songs_json = json_array_finish(songs_ja);
	if (!songs_json) return NULL;

	json_object_t *jo = json_object_new(0);
	if (!jo) {
		free(songs_json);
		return NULL;
	}
	if (json_object_kv_str(jo, "id", id) != 0 ||
			json_object_kv_str(jo, "title", meta.title) != 0 ||
			json_object_kv_str(jo, "owner", owner) != 0 ||
			json_object_kv_str(jo, "choir", meta.choir) != 0 ||
			json_object_kv_int(jo, "viewerZoom", viewer_zoom) != 0 ||
			json_object_kv_raw(jo, "songs", songs_json) != 0) {
		json_object_free(jo);
		free(songs_json);
		return NULL;
	}
	char *response = json_object_finish(jo);

	free(songs_json);
	return response;
}

static int
songbook_details_handler(int fd, char *body)
{
	(void)body;

	char *json = songbook_json(fd);
	if (!json)
		return respond_error(fd, 404, "Songbook not found");
	int result = core_post_json(fd, json);
	free(json);
	return result;
}

static int
handle_sb_add(int fd, char *body)
{
	char id[256] = {0};
	if (index_add_item(fd, body, id, sizeof(id)) != 0)
		return 1;

	/* Write choir field if provided */
	char choir[128] = {0};
	int choir_len = mpfd_get("choir", choir, sizeof(choir) - 1);
	if (choir_len > 0) {
		songbook_meta_t meta = {0};
		choir[choir_len] = '\0';
		char sb_item_path[512];
		if (item_path_build(fd, "songbook", id,
				sb_item_path, sizeof(sb_item_path)) != 0)
			return server_error(fd, "Failed to resolve songbook path");
		songbook_meta_read(sb_item_path, &meta);
		snprintf(meta.choir, sizeof(meta.choir), "%s", choir);
		if (songbook_meta_write(sb_item_path, &meta) != 0)
			return server_error(fd, "Failed to write songbook metadata");
		index_put(index_hd, id, meta.title);
		songbook_index_put_meta(id, &meta);
		songbook_index_write_file(".");

		/* Pre-populate data.txt with one random song per choir format type */
		char choir_item_path[PATH_MAX];
		if (item_path_build(fd, "choir", choir,
				choir_item_path, sizeof(choir_item_path)) != 0)
			return server_error(fd, "Failed to resolve choir path");

		char format_path[PATH_MAX];
		if (item_child_path(choir_item_path, "format",
				format_path, sizeof(format_path)) != 0)
			return server_error(fd, "Failed to resolve choir format path");

		FILE *ffp = fopen(format_path, "r");
		if (ffp) {
			char data_path[PATH_MAX];
			if (item_child_path(sb_item_path, "data.txt",
					data_path, sizeof(data_path)) != 0)
				return server_error(fd, "Failed to resolve songbook data path");
			char type[128];
			repertoire_row_t *rows = NULL;
			size_t row_count = 0;
			size_t row_cap = 0;
			while (fgets(type, sizeof(type), ffp)) {
				/* strip trailing newline/whitespace */
				size_t tlen = strlen(type);
				while (tlen > 0 && (type[tlen-1] == '\n' || type[tlen-1] == '\r' || type[tlen-1] == ' '))
					type[--tlen] = '\0';
				if (tlen == 0) continue;

				char song_id[256] = {0};
				if (get_random_chord_by_type(type, song_id, sizeof(song_id)) == 0) {
					repertoire_row_t *tmp;
					if (row_count == row_cap) {
						row_cap = row_cap ? row_cap * 2 : 8;
						tmp = realloc(rows, row_cap * sizeof(*rows));
						if (!tmp) {
							free(rows);
							fclose(ffp);
							return server_error(fd, "OOM");
						}
						rows = tmp;
					}
					memset(&rows[row_count], 0, sizeof(rows[row_count]));
					snprintf(rows[row_count].id, sizeof(rows[row_count].id),
						"%.*s", (int)sizeof(rows[row_count].id) - 1, song_id);
					snprintf(rows[row_count].format, sizeof(rows[row_count].format),
						"%.*s", (int)sizeof(rows[row_count].format) - 1, type);
					row_count++;
				}
			}

			if (repertoire_rows_write(data_path, rows, row_count) != 0) {
				free(rows);
				fclose(ffp);
				return server_error(fd, "Failed to write data file");
			}
			free(rows);
			fclose(ffp);
		}
	}
	else {
		char sb_item_path[512];
		if (item_path_build(fd, "songbook", id,
				sb_item_path, sizeof(sb_item_path)) == 0)
			songbook_index_upsert(".", id, sb_item_path);
	}

	char location[512];
	snprintf(location, sizeof(location), "/songbook/%s", id);
	return redirect(fd, location);
}

void ndx_install(void)
{
	char doc_root[256] = {0};
	ndc_env_get(0, doc_root, "DOCUMENT_ROOT");
	if (!doc_root[0])
		strcpy(doc_root, ".");

	ndx_load("./mods/index/index");
	ndx_load("./mods/mpfd/mpfd");
	ndx_load("./mods/auth/auth");
	ndx_load("./mods/song/song");
	ndx_load("./mods/choir/choir");
	ndx_load("./mods/common/common");

	ndc_register_handler("GET:/songbook/:id",
			songbook_details_handler);

	ndc_register_handler("POST:/songbook/:id/randomize", handle_sb_randomize);
	ndc_register_handler("POST:/songbook/:id/transpose", handle_sb_transpose);
	ndc_register_handler("GET:/songbook/:id/edit", handle_sb_edit_get);
	ndc_register_handler("POST:/songbook/:id/edit", handle_sb_edit);

	songbook_index_hd = qmap_open(NULL, "songbook_idx", QM_STR, QM_STR,
		0x3FF, QM_SORTED);
	if (songbook_index_load(doc_root) != 0)
		songbook_index_rebuild(doc_root);

	index_hd = index_open("Songbook", 0, 1, songbook_cleanup);

	/* Override the generic POST:/songbook/add to also handle the choir field */
	ndc_register_handler("POST:/songbook/add", handle_sb_add);
}
