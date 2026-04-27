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

#define CHOIR_REPERTOIRE_IMPL
#include "choir.h"
#undef CHOIR_REPERTOIRE_IMPL

typedef struct {
	char title[256];
	char counter[32];
	char format[2048];
} choir_meta_t;

static const char *CHOIR_DEFAULT_FORMATS =
	"entrada\naleluia\nofertorio\nsanto\ncomunhao\nacao_de_gracas\nsaida\nany";

static void
choir_meta_read(const char *item_path, choir_meta_t *meta)
{
	meta_field_t fields[] = {
		{ "title", meta->title, sizeof(meta->title) },
		{ "counter", meta->counter, sizeof(meta->counter) },
	};

	memset(meta, 0, sizeof(*meta));
	meta_fields_read(item_path, fields, sizeof(fields) / sizeof(fields[0]));
	if (!meta->counter[0])
		snprintf(meta->counter, sizeof(meta->counter), "0");

	char *format = slurp_item_child_file(item_path, "format");
	if (format) {
		snprintf(meta->format, sizeof(meta->format), "%s", format);
		size_t len = strlen(meta->format);
		while (len > 0 &&
				(meta->format[len - 1] == '\n' || meta->format[len - 1] == '\r'))
			meta->format[--len] = '\0';
		free(format);
	} else {
		snprintf(meta->format, sizeof(meta->format), "%s", CHOIR_DEFAULT_FORMATS);
	}
}

static int
choir_meta_write(const char *item_path, const choir_meta_t *meta)
{
	meta_field_t fields[] = {
		{ "title", (char *)meta->title, sizeof(meta->title) },
	};

	if (meta_fields_write(item_path, fields, sizeof(fields) / sizeof(fields[0])) != 0)
		return -1;
	return write_item_child_file(item_path, "format",
		meta->format, strlen(meta->format));
}

#include "repertoire_impl.inc"

#define CHOIR_SONGS_PATH "items/choir/items"

/* POST /api/choir/:id/edit - Edit choir title and formats */
static int
handle_choir_edit(int fd, char *body)
{
	item_ctx_t ctx;
	if (item_ctx_load(&ctx, fd, CHOIR_SONGS_PATH, ICTX_NEED_LOGIN))
		return 1;

	if (item_require_access(fd, ctx.item_path, ctx.username,
			ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
			"Choir not found", "You don't own this choir"))
		return 1;

	mpfd_parse(fd, body);

	choir_meta_t meta;
	choir_meta_read(ctx.item_path, &meta);
	int title_len = mpfd_get("title", meta.title, sizeof(meta.title) - 1);
	int format_len = mpfd_get("format", meta.format, sizeof(meta.format) - 1);

	if (title_len > 0) {
		meta.title[title_len] = '\0';
	}

	if (format_len > 0) {
		meta.format[format_len] = '\0';
	}

	if ((title_len > 0 || format_len > 0) &&
			choir_meta_write(ctx.item_path, &meta) != 0) {
		return server_error(fd, "Failed to write choir metadata");
	}

	char location[256];
	snprintf(location, sizeof(location), "/choir/%s", ctx.id);
	return redirect(fd, location);
}

static char *
choir_json(int fd)
{
	char doc_root[256] = {0};
	char id[128] = {0};
	char choir_path[512];

	char owner[64] = {0};
	choir_meta_t meta;

	get_doc_root(fd, doc_root, sizeof(doc_root));
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");

	if (!id[0]) return NULL;

	if (item_path_build(fd, "choir", id, choir_path, sizeof(choir_path)) != 0)
		return NULL;

	choir_meta_read(choir_path, &meta);
	if (!meta.title[0])
		return NULL;

	/* Get owner username */
	item_read_owner(choir_path, owner, sizeof(owner));

	/* Build songs JSON array (choir repertoire) */
	json_array_t *songs_ja = json_array_new(0);
	if (!songs_ja) return NULL;

	char songs_path[PATH_MAX];
	repertoire_row_t *songs_rows = NULL;
	size_t songs_count = 0;
	snprintf(songs_path, sizeof(songs_path), "%s/songs", choir_path);
	if (repertoire_rows_load(songs_path, &songs_rows, &songs_count) != 0) {
		free(songs_ja->buf);
		free(songs_ja);
		return NULL;
	}
	for (size_t i = 0; i < songs_count; i++) {
		repertoire_row_t *row = &songs_rows[i];
		char song_title[256] = {0};
		song_read_title(doc_root, row->id, song_title, sizeof(song_title));

		int original_key = song_get_original_key(row->id);

		json_array_begin_object(songs_ja);
		json_array_kv_str(songs_ja, "id", row->id);
		json_array_kv_str(songs_ja, "title", song_title);
		json_array_kv_int(songs_ja, "preferredKey", row->value);
		json_array_kv_int(songs_ja, "originalKey", original_key);
		json_array_kv_str(songs_ja, "format", row->format);
		json_array_end_object(songs_ja);
	}
	repertoire_rows_dispose(songs_rows);
	char *songs_json = json_array_finish(songs_ja);
	if (!songs_json) return NULL;

	/* Build songbooks JSON array — scan items/songbook/items/ for choir match */
	char sb_items_path[PATH_MAX - 512];
	snprintf(sb_items_path, sizeof(sb_items_path), "%s/items/songbook/items", doc_root);
	json_array_t *sb_ja = json_array_new(0);
	if (!sb_ja) { free(songs_json); return NULL; }
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
			read_meta_file(sb_path2, "title", sb_title, sizeof(sb_title));
			json_array_begin_object(sb_ja);
			json_array_kv_str(sb_ja, "id", sde->d_name);
			json_array_kv_str(sb_ja, "title", sb_title);
			json_array_end_object(sb_ja);
		}
		closedir(sbdp);
	}
	char *songbooks_json = json_array_finish(sb_ja);
	if (!songbooks_json) { free(songs_json); return NULL; }

	/* derive counter from scan */
	snprintf(meta.counter, sizeof(meta.counter), "%d", sb_count);
	char *all_songs_json = build_all_songs_json(doc_root, 0);
	if (!all_songs_json) { free(songs_json); free(songbooks_json); return NULL; }

	json_object_t *jo = json_object_new(0);
	if (!jo) {
		free(songs_json);
		free(songbooks_json);
		free(all_songs_json);
		return NULL;
	}

	if (json_object_kv_str(jo, "title", meta.title) != 0 ||
			json_object_kv_str(jo, "owner", owner) != 0 ||
			json_object_kv_str(jo, "counter", meta.counter) != 0 ||
			json_object_kv_str(jo, "formats", meta.format) != 0 ||
			json_object_kv_raw(jo, "songs", songs_json) != 0 ||
			json_object_kv_raw(jo, "allSongs", all_songs_json) != 0 ||
			json_object_kv_raw(jo, "songbooks", songbooks_json) != 0) {
		json_object_free(jo);
		free(songs_json);
		free(songbooks_json);
		free(all_songs_json);
		return NULL;
	}

	char *response = json_object_finish(jo);

	free(songs_json);
	free(songbooks_json);
	free(all_songs_json);
	return response;
}

static int
choir_details_handler(int fd, char *body)
{
	(void)body;

	char *json = choir_json(fd);
	if (!json)
		return respond_error(fd, 404, "Choir not found");
	int result = core_post_json(fd, json);
	free(json);
	return result;
}

/* GET /api/choir/:id/songs - List choir songs with details */
static int
handle_choir_songs_list(int fd, char *body)
{
	(void)body;

	item_ctx_t ctx;
	if (item_ctx_load(&ctx, fd, CHOIR_SONGS_PATH, 0))
		return 1;

	char songs_path[PATH_MAX];
	if (item_child_path(ctx.item_path, "songs",
			songs_path, sizeof(songs_path)) != 0)
		return respond_error(fd, 500, "Failed to resolve songs path");

	json_array_t *ja = json_array_new(0);
	repertoire_row_t *rows = NULL;
	size_t row_count = 0;
	if (!ja)
		return respond_error(fd, 500, "OOM");
	if (repertoire_rows_load(songs_path, &rows, &row_count) != 0) {
		free(ja->buf);
		free(ja);
		return respond_error(fd, 500, "Failed to read songs file");
	}
	for (size_t i = 0; i < row_count; i++) {
		char song_title[256] = {0};
		song_read_title(ctx.doc_root, rows[i].id, song_title, sizeof(song_title));

		json_array_begin_object(ja);
		json_array_kv_str(ja, "id", rows[i].id);
		json_array_kv_str(ja, "title", song_title);
		json_array_kv_int(ja, "preferredKey", rows[i].value);
		json_array_kv_str(ja, "format", rows[i].format);
		json_array_end_object(ja);
	}
	repertoire_rows_dispose(rows);

	char *songs_json = json_array_finish(ja);
	if (!songs_json) return respond_error(fd, 500, "OOM");
	int r = respond_json(fd, 200, songs_json);
	free(songs_json);
	return r;
}

/* POST /api/choir/:id/songs - Add song to choir repertoire */
static int
handle_choir_song_add(int fd, char *body)
{
	item_ctx_t ctx;
	if (item_ctx_load(&ctx, fd, CHOIR_SONGS_PATH,
			ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP))
		return 1;

	char songs_path[PATH_MAX];
	if (item_child_path(ctx.item_path, "songs",
			songs_path, sizeof(songs_path)) != 0)
		return respond_error(fd, 500, "Failed to resolve songs path");

	ndc_query_parse(body);

	char song_id[128] = {0};
	char format[64] = {0};
	int song_id_len = ndc_query_param("song_id", song_id, sizeof(song_id) - 1);
	int format_len = ndc_query_param("format", format, sizeof(format) - 1);

	if (song_id_len <= 0)
		return bad_request(fd, "Missing song_id");
	song_id[song_id_len] = '\0';

	/* Extract id from "Title [id]" datalist format */
	datalist_extract_id(song_id, song_id, sizeof(song_id));

	if (format_len > 0) {
		format[format_len] = '\0';
	} else {
		strcpy(format, "any");
	}

	repertoire_row_t row = {0};
	snprintf(row.id, sizeof(row.id), "%s", song_id);
	row.value = 0;
	snprintf(row.format, sizeof(row.format), "%s", format);
	if (repertoire_file_append(songs_path, &row) != 0)
		return server_error(fd, "Failed to open songs file");

	char location[256];
	snprintf(location, sizeof(location), "/choir/%s", ctx.id);
	return redirect(fd, location);
}

/* POST /api/choir/:id/song/:song_id/key - Set preferred key */
struct key_cb_ctx { const char *song_id; int new_key; };

static int
song_key_cb(int idx, const char *raw, int parsed,
	const char *sid, int ival, const char *fmt,
	void *user, char *out, size_t out_sz)
{
	(void)idx; (void)raw; (void)ival;
	struct key_cb_ctx *c = user;
	if (parsed && strcmp(sid, c->song_id) == 0) {
		snprintf(out, out_sz, "%s:%d:%s\n", sid, c->new_key, fmt);
		return REPERTOIRE_LINE_REPLACE;
	}
	return REPERTOIRE_LINE_KEEP;
}

static int
handle_choir_song_key(int fd, char *body)
{
	item_ctx_t ctx;
	if (item_ctx_load(&ctx, fd, CHOIR_SONGS_PATH,
			ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP | ICTX_SONG_ID))
		return 1;

	char songs_path[PATH_MAX];
	if (item_child_path(ctx.item_path, "songs",
			songs_path, sizeof(songs_path)) != 0)
		return respond_error(fd, 500, "Failed to resolve songs path");

	ndc_query_parse(body);

	char key_str[32] = {0};
	int key_len = ndc_query_param("key", key_str, sizeof(key_str) - 1);
	struct key_cb_ctx cbc = {
		.song_id = ctx.song_id,
		.new_key = (key_len > 0) ? atoi(key_str) : 0,
	};

	if (repertoire_file_rewrite(songs_path, song_key_cb, &cbc) < 0)
		return server_error(fd, "Failed to update");

	char location[256];
	snprintf(location, sizeof(location), "/choir/%s", ctx.id);
	return redirect(fd, location);
}

/* DELETE /api/choir/:id/song/:song_id - Remove song from repertoire */
static int
song_delete_cb(int idx, const char *raw, int parsed,
	const char *sid, int ival, const char *fmt,
	void *user, char *out, size_t out_sz)
{
	(void)idx; (void)raw; (void)ival; (void)fmt; (void)out; (void)out_sz;
	const char *target = user;
	if (parsed && strcmp(sid, target) == 0)
		return REPERTOIRE_LINE_SKIP;
	return REPERTOIRE_LINE_KEEP;
}

static int
handle_choir_song_delete(int fd, char *body)
{
	(void)body;

	item_ctx_t ctx;
	if (item_ctx_load(&ctx, fd, CHOIR_SONGS_PATH,
			ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP | ICTX_SONG_ID))
		return 1;

	char songs_path[PATH_MAX];
	if (item_child_path(ctx.item_path, "songs",
			songs_path, sizeof(songs_path)) != 0)
		return respond_error(fd, 500, "Failed to resolve songs path");

	int changed = repertoire_file_rewrite(songs_path,
		song_delete_cb, (void *)ctx.song_id);
	if (changed < 0)
		return server_error(fd, "Failed to update");

	if (changed > 0) {
		char location[256];
		snprintf(location, sizeof(location), "/choir/%s", ctx.id);
		return redirect(fd, location);
	}
	return not_found(fd, "Song not found");
}

/* GET /choir/:id/song/:song_id - View song with choir preferred key */
static int
handle_choir_song_view(int fd, char *body)
{
	(void)body;

	item_ctx_t ctx;
	if (item_ctx_load(&ctx, fd, CHOIR_SONGS_PATH, ICTX_SONG_ID))
		return 1;

	char songs_path[PATH_MAX];
	if (item_child_path(ctx.item_path, "songs",
			songs_path, sizeof(songs_path)) != 0)
		return respond_error(fd, 500, "Failed to resolve songs path");

	int preferred_key = 0;
	char format[64] = {0};
	repertoire_row_t *rows = NULL;
	size_t row_count = 0;
	if (repertoire_rows_load(songs_path, &rows, &row_count) != 0)
		return server_error(fd, "Failed to read songs file");
	for (size_t i = 0; i < row_count; i++) {
		if (strcmp(rows[i].id, ctx.song_id) == 0) {
			preferred_key = rows[i].value;
			strncpy(format, rows[i].format, sizeof(format) - 1);
			break;
		}
	}
	repertoire_rows_dispose(rows);
	(void)format;

	int transpose = 0;
	int original_key = 0;

	if (preferred_key != 0) {
		original_key = song_get_original_key_root(ctx.doc_root, ctx.song_id);
		transpose = preferred_key - original_key;
	}

	char redirect_url[512];
	snprintf(redirect_url, sizeof(redirect_url), "/song/%s?t=%d", ctx.song_id, transpose);
	return redirect(fd, redirect_url);
}

static int
handle_choir_add_get(int fd, char *body)
{
	return core_get(fd, body);
}

/* GET /choir/:id/edit - read choir data and proxy to Fresh */
static int
handle_choir_edit_get(int fd, char *body)
{
	(void)body;

	item_ctx_t ctx;
	if (item_ctx_load(&ctx, fd, CHOIR_SONGS_PATH, ICTX_NEED_LOGIN))
		return 1;

	if (item_require_access(fd, ctx.item_path, ctx.username,
			ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
			"Choir not found", "You don't own this choir"))
		return 1;

	choir_meta_t meta;
	choir_meta_read(ctx.item_path, &meta);

	form_body_t *fb = form_body_new(0);
	if (!fb) return respond_error(fd, 500, "OOM");
	form_body_add(fb, "id", ctx.id);
	form_body_add(fb, "title", meta.title);
	form_body_add(fb, "format", meta.format);
	return core_post_form(fd, fb);
}

/* POST /api/choir/:id/song/:song_id/remove - Remove song (HTML form-friendly alias for DELETE) */
static int
handle_choir_song_remove(int fd, char *body)
{
	return handle_choir_song_delete(fd, body);
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
	ndc_register_handler("GET:/choir/:id/", choir_details_handler);
	ndc_register_handler("GET:/choir/:id/song/:song_id", handle_choir_song_view);
	ndc_register_handler("GET:/api/choir/:id/songs", handle_choir_songs_list);
	ndc_register_handler("POST:/api/choir/:id/songs", handle_choir_song_add);
	ndc_register_handler("POST:/api/choir/:id/song/:song_id/key", handle_choir_song_key);
	ndc_register_handler("DELETE:/api/choir/:id/song/:song_id", handle_choir_song_delete);
	ndc_register_handler("POST:/api/choir/:id/song/:song_id/remove", handle_choir_song_remove);
	ndc_register_handler("POST:/api/choir/:id/edit", handle_choir_edit);

	index_open("Choir", 0, 1, NULL);
}
