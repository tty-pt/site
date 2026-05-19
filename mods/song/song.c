#include <ttypt/ndx-mod.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <dirent.h>

#include <ttypt/ndc.h>
#include <ttypt/qmap.h>
#include "../index/index.h"

#define DATASET_HOOK_IMPL
#include "../common/common.h"
#include "../source/source.h"
#include "../auth/auth.h"
#include "../../lib/transp/transp.h"

#define CHORDS_ITEMS_PATH "items/song/items"
#define SONG_TYPE_INDEX_BUF_SIZE 8192
#define SONG_TYPES_BUF_SIZE 2048
#define SONG_CHORD_DATA_BUF_SIZE 65536
#define VIEWER_ZOOM_MIN 70
#define VIEWER_ZOOM_MAX 170
#define VIEWER_ZOOM_DEFAULT 100

static transp_ctx_t *g_transp_ctx = NULL;
static unsigned index_hd = 0;
static unsigned type_index_hd = 0;
static unsigned type_source_hd = 0;
static unsigned type_fields_hd = 0;
static uint32_t song_type_record_id;
static char g_doc_root[256] = ".";

typedef struct {
	char title[256], type[SONG_TYPES_BUF_SIZE], yt[512], audio[512],
	        pdf[512], author[256];
} song_meta_t;

typedef struct {
	char id[64];
	char title[256];
	char type[SONG_TYPES_BUF_SIZE];
	char author[256];
	char yt[512];
	char audio[512];
	char pdf[512];
	char owner[32];
} song_item_t;

static uint32_t song_record_id;

typedef struct {
	char id[64];
	char name[256];
} song_type_item_t;

static const qmap_record_field_t song_type_record_fields[] = {
	{ "id",
	  QM_STR,
	  offsetof(song_type_item_t, id),
	  sizeof(((song_type_item_t *)0)->id) },
	{ "name",
	  QM_STR,
	  offsetof(song_type_item_t, name),
	  sizeof(((song_type_item_t *)0)->name) },
};

static const qmap_record_field_t song_record_fields_basic[] = {
	{ "id",
	  QM_STR,
	  offsetof(song_item_t, id),
	  sizeof(((song_item_t *)0)->id) },
	{ "title",
	  QM_STR,
	  offsetof(song_item_t, title),
	  sizeof(((song_item_t *)0)->title) },
	{ "type",
	  QM_STR,
	  offsetof(song_item_t, type),
	  sizeof(((song_item_t *)0)->type) },
	{ "author",
	  QM_STR,
	  offsetof(song_item_t, author),
	  sizeof(((song_item_t *)0)->author) },
	{ "yt",
	  QM_STR,
	  offsetof(song_item_t, yt),
	  sizeof(((song_item_t *)0)->yt) },
	{ "audio",
	  QM_STR,
	  offsetof(song_item_t, audio),
	  sizeof(((song_item_t *)0)->audio) },
	{ "pdf",
	  QM_STR,
	  offsetof(song_item_t, pdf),
	  sizeof(((song_item_t *)0)->pdf) },
	{ "data", QM_VSTR, 0, 0 },
	{ "owner",
	  QM_STR,
	  offsetof(song_item_t, owner),
	  sizeof(((song_item_t *)0)->owner) },
};
#define SONG_FIELD_COUNT                                                       \
	(sizeof(song_record_fields_basic) / sizeof(song_record_fields_basic[0]))

static const source_field_t song_items_fields[] = {
	{ "id", NULL, SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ .name = "title", .file = "title",
	  .type = SOURCE_FIELD_STRING, .writable = 1,
	  .required = 1, .min_length = 1 },
	{ "type",
	  "type",
	  SOURCE_FIELD_MULTI_REFERENCE,
	  1,
	  "song.types",
	  "songs" },
	{ "author", "author", SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "yt", "yt", SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "audio", "audio", SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "pdf", "pdf", SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "data", "data.txt", SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "owner", "owner", SOURCE_FIELD_STRING, 0, NULL, NULL },
};

static const source_def_t song_items_def = {
	.id = "song.items",
	.key_field = "id",
	.items_path = "items/song/items",
	.access_policy = SOURCE_ACCESS_PUBLIC,
	.fields = song_items_fields,
	.field_count = sizeof(song_items_fields) / sizeof(song_items_fields[0]),
};

static const source_field_t song_types_fields[] = {
	{ "name", NULL, SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "songs", NULL, SOURCE_FIELD_INVERSE, 0, "song.items", "type" },
};

static const source_def_t song_types_def = {
	.id = "song.types",
	.key_field = "name",
	.items_path = "items/song/types",
	.access_policy = SOURCE_ACCESS_PUBLIC,
	.fields = song_types_fields,
	.field_count = sizeof(song_types_fields) / sizeof(song_types_fields[0]),
};

static void song_meta_read(const char *path, song_meta_t *m)
{
	meta_field_t f[] = { { "title", m->title, 256 },
		             { "type", m->type, SONG_TYPES_BUF_SIZE },
		             { "yt", m->yt, 512 },
		             { "audio", m->audio, 512 },
		             { "pdf", m->pdf, 512 },
		             { "author", m->author, 256 } };
	memset(m, 0, sizeof(*m));
	META_READ(path, f);
	str_list_normalize(m->type, m->type, sizeof(m->type));
}

static int song_meta_write(const char *path, const song_meta_t *m)
{
	char types[SONG_TYPES_BUF_SIZE];
	meta_field_t f[] = { { "title", (char *)m->title, 0 },
		             { "type", types, 0 },
		             { "yt", (char *)m->yt, 0 },
		             { "audio", (char *)m->audio, 0 },
		             { "pdf", (char *)m->pdf, 0 },
		             { "author", (char *)m->author, 0 } };
	str_list_normalize(m->type, types, sizeof(types));
	return META_WRITE(path, f);
}

static const char *song_type_lookup_key(const char *type)
{
	static char key[64];
	if (!type || !type[0])
		return "any";
	ndc_slugify(type, strlen(type), key, sizeof(key));
	return key;
}

static int song_type_source_put(const char *display)
{
	if (!type_fields_hd || !display || !display[0])
		return 0;

	char id[64];
	ndc_slugify(display, strlen(display), id, sizeof(id));
	if (!id[0] || qmap_get(type_fields_hd, id))
		return 0;

	song_type_item_t item;
	memset(&item, 0, sizeof(item));
	snprintf(item.id, sizeof(item.id), "%s", id);
	snprintf(item.name, sizeof(item.name), "%s", display);
	qmap_put(type_fields_hd, id, &item);
	qmap_field_put(type_fields_hd, id, "name", item.name);

	qmap_put(type_source_hd, id, "");
	return 0;
}

static void song_type_source_maybe_remove(const char *display)
{
	if (!type_fields_hd || !display || !display[0])
		return;

	char id[64];
	ndc_slugify(display, strlen(display), id, sizeof(id));
	if (!id[0])
		return;

	const char *songs = (const char *)qmap_get(type_index_hd, id);
	if (!songs || !songs[0]) {
		qmap_del(type_fields_hd, id);
		qmap_del(type_source_hd, id);
	}
}

static void song_type_index_add_one(const char *type, const char *id)
{
	const char *t = song_type_lookup_key(type);
	char *l_v = (char *)qmap_get(type_index_hd, t);
	char l[SONG_TYPE_INDEX_BUF_SIZE] = { 0 };
	snprintf(
	        l,
	        sizeof(l),
	        "%s%s%s",
	        l_v ? l_v : "",
	        (l_v && l_v[0]) ? "," : "",
	        id);
	qmap_put(type_index_hd, t, l);
	if (type && type[0])
		song_type_source_put(type);
}

static void song_type_index_remove_one(const char *type, const char *id)
{
	const char *t = song_type_lookup_key(type);
	const char *l = (const char *)qmap_get(type_index_hd, t);
	char nl[SONG_TYPE_INDEX_BUF_SIZE] = { 0 }, *cp, *tok;
	if (!l)
		return;
	cp = strdup(l);
	if (!cp)
		return;
	tok = strtok(cp, ",");
	while (tok) {
		if (strcmp(tok, id) != 0) {
			if (nl[0])
				strcat(nl, ",");
			if (strlen(nl) < sizeof(nl) - strlen(tok) - 2)
				strcat(nl, tok);
		}
		tok = strtok(NULL, ",");
	}
	free(cp);
	if (nl[0])
		qmap_put(type_index_hd, t, nl);
	else
		qmap_del(type_index_hd, t);
	if (type && type[0])
		song_type_source_maybe_remove(type);
}

typedef struct {
	const char *id;
} song_type_index_ctx_t;

static int song_type_index_add_cb(const char *type, void *user)
{
	song_type_index_ctx_t *ctx = (song_type_index_ctx_t *)user;
	song_type_index_add_one(type, ctx->id);
	return 0;
}

static int song_type_index_remove_cb(const char *type, void *user)
{
	song_type_index_ctx_t *ctx = (song_type_index_ctx_t *)user;
	song_type_index_remove_one(type, ctx->id);
	return 0;
}

static void song_type_index_add(const char *types, const char *id)
{
	song_type_index_ctx_t ctx;

	if (!id || !id[0])
		return;
	if (!types || !types[0]) {
		song_type_index_add_one(NULL, id);
		return;
	}

	ctx.id = id;
	str_list_for_each(types, song_type_index_add_cb, &ctx);
}

static void song_type_index_remove_from(const char *types, const char *id)
{
	song_type_index_ctx_t ctx;

	if (!id || !id[0])
		return;
	if (!types || !types[0]) {
		song_type_index_remove_one(NULL, id);
		return;
	}

	ctx.id = id;
	str_list_for_each(types, song_type_index_remove_cb, &ctx);
}

static void song_type_index_remove(const char *id)
{
	char item_path[PATH_MAX];
	char type[SONG_TYPES_BUF_SIZE] = { 0 };
	item_path_build(0, "song", id, item_path, sizeof(item_path));
	read_meta_file(item_path, "type", type, sizeof(type));
	song_type_index_remove_from(type, id);
}

static void song_index_delete(const char *id)
{
	song_type_index_remove(id);
}

static void build_type_index(const char *doc)
{
	char items_path[PATH_MAX];
	const char *root = (doc && doc[0]) ? doc : ".";
	DIR *d;
	struct dirent *e;

	type_index_hd = qmap_open(NULL, "type_idx", QM_STR, QM_STR, 0x3FF, 0);
	/* type_source_hd and type_fields_hd are already set from libhyle */
	snprintf(items_path, sizeof(items_path), "%s/items/song/items", root);

	d = opendir(items_path);
	if (!d)
		return;

	while ((e = readdir(d))) {
		char item_path[PATH_MAX + 256];
		char type[SONG_TYPES_BUF_SIZE] = { 0 };

		if (e->d_name[0] == '.')
			continue;
		snprintf(
		        item_path,
		        sizeof(item_path),
		        "%s/%s",
		        items_path,
		        e->d_name);
		read_meta_file(item_path, "type", type, sizeof(type));

		str_list_normalize(type, type, sizeof(type));
		song_type_index_add(type, e->d_name);
	}
	closedir(d);
}

static int
song_viewer_pref_path(const char *user, const char *name, char *out, size_t sz)
{
	char s[PATH_MAX];
	snprintf(s, sizeof(s), ".tty/%s", name);
	return user_path_build(user, s, out, sz);
}

static int
song_viewer_pref_write(const char *user, const char *name, const char *val)
{
	char d[PATH_MAX], p[PATH_MAX];
	user_path_build(user, ".tty", d, sizeof(d));
	ensure_dir_path(d);
	song_viewer_pref_path(user, name, p, sizeof(p));
	return write_file_path(p, val, val ? strlen(val) : 0);
}

static char *song_viewer_pref_read(const char *user, const char *name)
{
	char p[PATH_MAX];
	if (song_viewer_pref_path(user, name, p, sizeof(p)) != 0)
		return NULL;
	return slurp_file(p);
}

NDX_LISTENER(int, song_get_viewer_zoom, const char *, user)
{
	char *r = song_viewer_pref_read(user, "chords-zoom");
	int v = r ? atoi(r) : VIEWER_ZOOM_DEFAULT;
	free(r);
	return (v < VIEWER_ZOOM_MIN || v > VIEWER_ZOOM_MAX)
	               ? VIEWER_ZOOM_DEFAULT
	               : v;
}

NDX_LISTENER(int, song_set_viewer_zoom, const char *, user, int, zoom)
{
	char b[16];
	snprintf(b, sizeof(b), "%d", zoom);
	return song_viewer_pref_write(user, "chords-zoom", b);
}

static int song_read_type(const char *doc, const char *id, char *out, size_t sz)
{
	char p[PATH_MAX];
	item_path_build_root(doc, "song", id, p, sizeof(p));
	return read_meta_file(p, "type", out, sz);
}

NDX_LISTENER(int, song_transpose_root,
	const char *, doc,
	const char *, id,
	int, semi,
	int, fl,
	char **, out,
	int *, key)
{
	char p[PATH_MAX], dp[PATH_MAX];
	item_path_build_root(doc, "song", id, p, sizeof(p));
	item_child_path(p, "data.txt", dp, sizeof(dp));
	char *c = slurp_file(dp);
	if (!c)
		return -1;
	transp_reset_key(g_transp_ctx);
	*out = transp_buffer(g_transp_ctx, c, semi, fl);
	int k = transp_get_key(g_transp_ctx);
	if (key)
		*key = k < 0 ? 0 : k;
	free(c);
	return 0;
}

static int parse_params(const char *q, int *t, int *f, int *m)
{
	*t = 0;
	*m = 0;
	if (!q || !*q)
		return 0;
	char c[1024];
	snprintf(c, sizeof(c), "%s", q);
	ndc_query_parse(c);
	char b[32];
	if (ndc_query_param("t", b, sizeof(b)) > 0)
		*t = atoi(b);
	if (ndc_query_param("b", b, sizeof(b)) >= 0)
		*f |= TRANSP_BEMOL;
	if (ndc_query_param("l", b, sizeof(b)) >= 0)
		*f |= TRANSP_LATIN;
	if (ndc_query_param("h", b, sizeof(b)) >= 0)
		*f |= TRANSP_HTML;
	if (ndc_query_param("m", b, sizeof(b)) >= 0)
		*m = 1;
	return 0;
}

static int api_song_viewer_prefs_handler(int fd, char *body)
{
	const char *u = get_request_user(fd);
	if (!u || !u[0]) {
		ndc_respond(fd, 204, "");
		return 0;
	}
	ndc_query_parse(body);
	char b[16];
	if (ndc_query_param("v", b, sizeof(b)) >= 0)
		song_set_viewer_zoom(u, atoi(b));
	if (ndc_query_param("b", b, sizeof(b)) >= 0)
		song_viewer_pref_write(u, "chords-bemol", b);
	if (ndc_query_param("l", b, sizeof(b)) >= 0)
		song_viewer_pref_write(u, "chords-latin", b);
	ndc_respond(fd, 204, "");
	return 0;
}

static int
song_details_auth(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;
	int t = 0, f = 0, m = 0;
	char q[1024] = { 0 };
	ndc_env_get(fd, q, "QUERY_STRING");
	parse_params(q, &t, &f, &m);

	int v_z = VIEWER_ZOOM_DEFAULT, v_b = 0, v_l = 0;
	if (ctx->username && ctx->username[0]) {
		v_z = song_get_viewer_zoom(ctx->username);
		char *rb = song_viewer_pref_read(ctx->username, "chords-bemol");
		char *rl = song_viewer_pref_read(ctx->username, "chords-latin");
		v_b = rb ? atoi(rb) : 0;
		v_l = rl ? atoi(rl) : 0;
		free(rb);
		free(rl);
	}

	if (q[0]) {
	} else {
		if (v_b)
			f |= TRANSP_BEMOL;
		if (v_l)
			f |= TRANSP_LATIN;
	}

	char *trans = NULL;
	int k = 0;
	song_transpose_root(ctx->doc_root, ctx->id, t, f, &trans, &k);
	song_meta_t meta;
	song_meta_read(ctx->item_path, &meta);

	int rc = 0;
	json_object_t *jo = json_object_new(0);
	if (!jo) {
		free(trans);
		return respond_error(fd, 500, "OOM");
	}
	json_object_kv_str(jo, "id", ctx->id);
	json_object_kv_str(jo, "title", meta.title);
	json_object_kv_str(jo, "data", trans ? trans : "");
	json_object_kv_str(jo, "yt", meta.yt);
	json_object_kv_str(jo, "audio", meta.audio);
	json_object_kv_str(jo, "pdf", meta.pdf);
	json_object_kv_str(jo, "categories", meta.type);
	json_object_kv_str(jo, "author", meta.author);
	json_object_kv_bool(jo, "showMedia", m);
	json_object_kv_int(jo, "originalKey", k);
	json_object_kv_int(jo, "viewerZoom", v_z);
	json_object_kv_bool(jo, "viewerBemol", (f & TRANSP_BEMOL) != 0);
	json_object_kv_bool(jo, "viewerLatin", (f & TRANSP_LATIN) != 0);
	int owner =
	        (ctx->username && ctx->username[0])
	                ? item_check_ownership(ctx->item_path, ctx->username)
	                : 0;
	json_object_kv_bool(jo, "owner", owner);
	char *json = json_object_finish(jo);
	if (!json) {
		free(trans);
		return respond_error(fd, 500, "Failed to finish JSON");
	}
	rc = respond_json(fd, 200, json);
	free(json);
	free(trans);
	return rc;
}

static int api_song_transpose_handler(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        CHORDS_ITEMS_PATH,
	        0,
	        NULL,
	        NULL,
	        song_details_auth,
	        NULL);
}

static void song_cleanup(const char *id)
{
	char item_path[PATH_MAX];
	char type[SONG_TYPES_BUF_SIZE] = { 0 };

	song_index_delete(id);
	item_path_build(0, "song", id, item_path, sizeof(item_path));
	read_meta_file(item_path, "type", type, sizeof(type));
	str_list_normalize(type, type, sizeof(type));
	song_type_index_add(type, id);
}

NDX_LISTENER(int, song_get_original_key_root,
	const char *, doc,
	const char *, id)
{
	int k = 0;
	char *o = NULL;
	song_transpose_root(doc, id, 0, 0, &o, &k);
	free(o);
	return k;
}

NDX_LISTENER(int, song_get_original_key, const char *, id)
{
	return song_get_original_key_root(g_doc_root, id);
}

NDX_LISTENER(char *, song_get_pref, const char *, user, const char *, name)
{
	return song_viewer_pref_read(user, name);
}

NDX_LISTENER(int, song_transpose,
	const char *, id,
	int, semi,
	int, fl,
	char **, out)
{
	return song_transpose_root(g_doc_root, id, semi, fl, out, NULL);
}

static void
song_type_display_from_ids(const char *ids, char *out, size_t out_sz)
{
	char copy[SONG_TYPES_BUF_SIZE];
	char *tok;
	char *saveptr;

	out[0] = '\0';
	if (!ids || !ids[0])
		return;

	snprintf(copy, sizeof(copy), "%s", ids);
	tok = strtok_r(copy, "\r\n", &saveptr);
	while (tok) {
		char t[256];
		snprintf(t, sizeof(t), "%s", tok);
		str_trim(t);
		if (!t[0]) {
			tok = strtok_r(NULL, "\r\n", &saveptr);
			continue;
		}
		song_type_item_t *ti =
		        (song_type_item_t *)qmap_get(type_fields_hd, t);
		str_list_append(out, out_sz, ti ? ti->name : t);
		if (ti)
			song_type_source_put(ti->name);
		tok = strtok_r(NULL, "\r\n", &saveptr);
	}
}

NDX_LISTENER(int, source_after_update,
        int, fd,
        const char *, dataset_id,
        const char *, id,
        unsigned, data_handle)
{
	(void)dataset_id;
	char path[PATH_MAX], dr[256] = { 0 };
	char old_type[SONG_TYPES_BUF_SIZE] = { 0 };
	get_doc_root(fd, dr, sizeof(dr));
	if (item_path_build_root(
	            dr[0] ? dr : g_doc_root, "song", id, path, sizeof(path)) !=
	    0)
		return 0;

	song_meta_t m;
	song_meta_read(path, &m);
	snprintf(old_type, sizeof(old_type), "%s", m.type);

	const char *title = (const char *)qmap_get(data_handle, "title");
	const char *type = (const char *)qmap_get(data_handle, "type");

	int has_meta = 0;
	if (title) {
		strncpy(m.title, title, sizeof(m.title) - 1);
		has_meta = 1;
	}
	if (type) {
		char display[SONG_TYPES_BUF_SIZE] = { 0 };
		song_type_display_from_ids(type, display, sizeof(display));
		snprintf(m.type, sizeof(m.type), "%s", display);
		has_meta = 1;
	}
	const char *author = (const char *)qmap_get(data_handle, "author");
	const char *yt = (const char *)qmap_get(data_handle, "yt");
	const char *audio = (const char *)qmap_get(data_handle, "audio");
	const char *pdf = (const char *)qmap_get(data_handle, "pdf");
	if (author) {
		strncpy(m.author, author, sizeof(m.author) - 1);
		has_meta = 1;
	}
	if (yt) {
		strncpy(m.yt, yt, sizeof(m.yt) - 1);
		has_meta = 1;
	}
	if (audio) {
		strncpy(m.audio, audio, sizeof(m.audio) - 1);
		has_meta = 1;
	}
	if (pdf) {
		strncpy(m.pdf, pdf, sizeof(m.pdf) - 1);
		has_meta = 1;
	}
	if (has_meta) {
		if (song_meta_write(path, &m) != 0)
			return 0;
		char old_norm[64], new_norm[64];
		ndc_slugify(
		        old_type, strlen(old_type), old_norm, sizeof(old_norm));
		ndc_slugify(m.type, strlen(m.type), new_norm, sizeof(new_norm));
		if (strcmp(old_norm, new_norm) != 0) {
			song_type_index_remove_from(old_type, id);
			song_type_index_add(m.type, id);
		}
	}

	const char *data = (const char *)qmap_get(data_handle, "data");
	if (data) {
		char data_path[PATH_MAX];
		item_child_path(path, "data.txt", data_path, sizeof(data_path));
		write_file_path(data_path, data, strlen(data));
	}
	return 0;
}

NDX_LISTENER(int, source_after_delete,
        int, fd,
        const char *, dataset_id,
        const char *, id)
{
	(void)dataset_id;
	(void)fd;
	song_index_delete(id);
	return 0;
}

void ndx_install(void)
{
	char dr[256] = { 0 };
	srand((unsigned)time(NULL));
	get_doc_root(0, dr, sizeof(dr));
	if (dr[0])
		strncpy(g_doc_root, dr, sizeof(g_doc_root) - 1);
	g_transp_ctx = transp_init();
	ndx_load("./mods/common/common");
	ndx_load("./mods/index/index");
	ndx_load("./mods/mpfd/mpfd");
	ndx_load("./mods/auth/auth");

	song_type_record_id = qmap_record_register(
	        "song_type",
	        sizeof(song_type_item_t),
	        song_type_record_fields,
	        sizeof(song_type_record_fields) /
	                sizeof(song_type_record_fields[0]));

	qmap_record_field_t song_record_fields[SONG_FIELD_COUNT];
	memcpy(song_record_fields,
	       song_record_fields_basic,
	       sizeof(song_record_fields));
	song_record_fields[2].type = QM_MULTI_REFERENCE;
	song_record_fields[2].target_record = song_type_record_id;
	song_record_fields[2].inverse = "songs";

	song_record_id = qmap_record_register(
	        "song",
	        sizeof(song_item_t),
	        song_record_fields,
	        sizeof(song_record_fields) / sizeof(song_record_fields[0]));

	/* Register song.types and build type index BEFORE song.items
	 * so qmap_field_put for the multi-reference type field can
	 * resolve slugs to positions in song.types's fields_hd. */
	{
		source_def_t def = song_types_def;
		def.record_id = song_type_record_id;
		source_register(&def);
	}

	/* Get libhyle-created handles and build type index */
	type_source_hd = source_get_data_hd("song.types");
	type_fields_hd = source_get_fields_hd("song.types");
	build_type_index(dr);

	{
		source_def_t def = song_items_def;
		def.record_id = song_record_id;
		ref_field_register("song.items", "type");
		source_register(&def);
	}

	/* Open index with song.items handle */
	index_hd = source_get_data_hd("song.items");
	index_open(
	        "Song",
	        1,
	        index_hd,
	        song_cleanup,
	        NULL,
	        NULL,
	        NULL,
	        NULL);

	ndc_register_handler(
	        "GET:/api/song/:id/transpose", api_song_transpose_handler);
	ndc_register_handler(
	        "POST:/api/song/prefs", api_song_viewer_prefs_handler);
}
