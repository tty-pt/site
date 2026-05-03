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
#include "../ssr/ssr.h"
#include "../index/index.h"
#include "../common/common.h"
#include "../auth/auth.h"
#include "../../lib/transp/transp.h"

#define CHORDS_ITEMS_PATH "items/song/items"
#define SONG_TYPE_INDEX_BUF_SIZE 8192
#define SONG_CHORD_DATA_BUF_SIZE 65536
#define VIEWER_ZOOM_MIN 70
#define VIEWER_ZOOM_MAX 170
#define VIEWER_ZOOM_DEFAULT 100

static transp_ctx_t *g_transp_ctx = NULL;
static unsigned index_hd = 0;
static unsigned song_index_hd = 0;
static unsigned type_index_hd = 0;
static char g_doc_root[256] = ".";

typedef struct {
	char title[256], type[256], yt[512], audio[512], pdf[512], author[256];
} song_meta_t;

static void song_meta_read(const char *path, song_meta_t *m)
{
	meta_field_t f[] = {
		{ "title", m->title, 256 }, { "type", m->type, 256 },
		{ "yt", m->yt, 512 },       { "audio", m->audio, 512 },
		{ "pdf", m->pdf, 512 },     { "author", m->author, 256 }
	};
	memset(m, 0, sizeof(*m));
	meta_fields_read(path, f, 6);
}

static int song_meta_write(const char *path, const song_meta_t *m)
{
	meta_field_t f[] = { { "title", (char *)m->title, 0 },
		             { "type", (char *)m->type, 0 },
		             { "yt", (char *)m->yt, 0 },
		             { "audio", (char *)m->audio, 0 },
		             { "pdf", (char *)m->pdf, 0 },
		             { "author", (char *)m->author, 0 } };
	return meta_fields_write(path, f, 6);
}

static void song_type_index_add(const char *type, const char *id)
{
	const char *t = (type && type[0]) ? type : "any";
	char *l_v = (char *)qmap_get(type_index_hd, t);
	char l[SONG_TYPE_INDEX_BUF_SIZE] = { 0 };
	if (l_v)
		snprintf(l, sizeof(l), "%s", l_v);
	if (l[0] && strlen(l) < sizeof(l) - 2)
		strcat(l, ",");
	if (strlen(l) < sizeof(l) - strlen(id) - 2)
		strcat(l, id);
	qmap_put(type_index_hd, t, l);
}

static void song_type_index_remove_from(const char *type, const char *id)
{
	const char *t = (type && type[0]) ? type : "any";
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
	qmap_put(type_index_hd, t, nl);
}

static void song_type_index_remove(const char *id)
{
	const char *old = (const char *)qmap_get(song_index_hd, id);
	char buf[768], *type;
	if (!old)
		return;
	snprintf(buf, sizeof(buf), "%s", old);
	type = strchr(buf, '\t');
	if (!type)
		return;
	type = strchr(type + 1, '\t');
	if (!type)
		return;
	*type = '\0';
	type = strchr(buf, '\t') + 1;
	song_type_index_remove_from(type, id);
}

static int song_index_write_file(const char *doc)
{
	char path[PATH_MAX];
	const char *root = (doc && doc[0]) ? doc : ".";
	snprintf(path, sizeof(path), "%s/items/song/index.tsv", root);
	return index_tsv_save(song_index_hd, path);
}

static void song_format_index_val(const song_meta_t *m, char *out, size_t sz)
{
	char title[256], type[256], author[256];
	snprintf(title, sizeof(title), "%s", m->title);
	snprintf(type, sizeof(type), "%s", m->type[0] ? m->type : "any");
	snprintf(author, sizeof(author), "%s", m->author);
	index_field_clean(title);
	index_field_clean(type);
	index_field_clean(author);
	snprintf(out, sz, "%s\t%s\t%s", title, type, author);
}

static void song_index_put_meta(const char *id, const song_meta_t *m)
{
	char val[768];
	song_format_index_val(m, val, sizeof(val));
	song_type_index_remove(id);
	qmap_put(song_index_hd, id, val);
	char type[256];
	snprintf(type, sizeof(type), "%s", m->type[0] ? m->type : "any");
	index_field_clean(type);
	song_type_index_add(type, id);
}

static int
song_index_upsert(const char *doc, const char *id, const char *item_path)
{
	song_meta_t m;
	song_meta_read(item_path, &m);
	song_index_put_meta(id, &m);
	return song_index_write_file(doc);
}

static void song_index_delete(const char *id)
{
	song_type_index_remove(id);
	qmap_del(song_index_hd, id);
	song_index_write_file(g_doc_root);
}

static void song_load_cb(const char *id, const char *val, void *user)
{
	(void)user;
	char buf[768], *type;
	snprintf(buf, sizeof(buf), "%s", val);
	type = strchr(buf, '\t');
	if (!type)
		return;
	type++;
	char *next = strchr(type, '\t');
	if (next)
		*next = '\0';
	song_type_index_add(type, id);
}

static int song_item_read_for_index(const char *path, char *out, size_t sz)
{
	song_meta_t m;
	song_meta_read(path, &m);
	song_format_index_val(&m, out, sz);
	return 0;
}

static void build_type_index(const char *doc)
{
	song_index_hd =
	        qmap_open(NULL, "song_idx", QM_STR, QM_STR, 0x3FF, QM_SORTED);
	type_index_hd = qmap_open(NULL, "type_idx", QM_STR, QM_STR, 0x3FF, 0);
	char path[PATH_MAX];
	const char *root = (doc && doc[0]) ? doc : ".";
	snprintf(path, sizeof(path), "%s/items/song/index.tsv", root);
	if (index_tsv_load(song_index_hd, path, song_load_cb, NULL) != 0) {
		index_tsv_rebuild(
		        root, "song", song_index_hd, song_item_read_for_index);
		unsigned c = qmap_iter(song_index_hd, NULL, 0);
		const void *k, *v;
		while (qmap_next(&k, &v, c))
			song_load_cb((const char *)k, (const char *)v, NULL);
		index_tsv_save(song_index_hd, path);
	}
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

NDX_LISTENER(int, song_read_title,
	const char *, doc,
	const char *, id,
	char *, out,
	size_t, sz)
{
	char p[PATH_MAX];
	item_path_build_root(doc, "song", id, p, sizeof(p));
	return read_meta_file(p, "title", out, sz);
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

NDX_LISTENER(int, song_get_random_by_type, const char *, t, char **, out)
{
	char *l = (char *)qmap_get(type_index_hd, t);
	if (!l)
		l = (char *)qmap_get(type_index_hd, "any");
	if (!l)
		return -1;
	int c = 1;
	for (char *p = l; *p; p++)
		if (*p == ',')
			c++;
	int idx = rand() % c;
	char *cp = strdup(l), *tok = strtok(cp, ",");
	for (int i = 0; i < idx && tok; i++)
		tok = strtok(NULL, ",");
	if (tok && out)
		*out = strdup(tok);
	free(cp);
	return (*out) ? 0 : -1;
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
	int ssr_response = user != NULL;
	int t = 0, f = ssr_response ? TRANSP_HTML : 0, m = 0;
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
		if (ssr_response && ctx->username && ctx->username[0]) {
			song_viewer_pref_write(
			        ctx->username,
			        "chords-bemol",
			        (f & TRANSP_BEMOL) ? "1" : "0");
			song_viewer_pref_write(
			        ctx->username,
			        "chords-latin",
			        (f & TRANSP_LATIN) ? "1" : "0");
		}
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
	if (ssr_response) {
		static __thread char s_query[512];
		struct ModuleEntryFfi modules_snap[64];
		size_t modules_len;
		int owner = (ctx->username && ctx->username[0])
		                    ? item_check_ownership(
		                              ctx->item_path, ctx->username)
		                    : 0;
		ndc_env_get(fd, s_query, "QUERY_STRING");
		SSR_FILL_MODULES(modules_snap, modules_len);
		struct SongDetailRenderFfi req = {
			.title = meta.title,
			.data = trans ? trans : "",
			.yt = meta.yt,
			.audio = meta.audio,
			.pdf = meta.pdf,
			.categories = meta.type,
			.author = meta.author,
			.original_key = k,
			.viewer_zoom = v_z,
			.show_media = m != 0,
			.viewer_bemol = (f & TRANSP_BEMOL) != 0,
			.viewer_latin = (f & TRANSP_LATIN) != 0,
			.owner = owner != 0,
			.id = ctx->id,
			.query = s_query,
			.remote_user = ctx->username ? ctx->username : "",
			.modules = modules_snap,
			.modules_len = modules_len,
		};
		rc = ssr_render_song_detail(fd, &req);
	} else {
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
		int owner = (ctx->username && ctx->username[0])
		                    ? item_check_ownership(
		                              ctx->item_path, ctx->username)
		                    : 0;
		json_object_kv_bool(jo, "owner", owner);
		char *json = json_object_finish(jo);
		if (!json) {
			free(trans);
			return respond_error(fd, 500, "Failed to finish JSON");
		}
		rc = respond_json(fd, 200, json);
		free(json);
	}
	free(trans);
	return rc;
}

static int song_details_handler(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        CHORDS_ITEMS_PATH,
	        0,
	        NULL,
	        NULL,
	        song_details_auth,
	        (void *)TRANSP_HTML);
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

static int
song_edit_get_auth(int fd, char *body, const item_ctx_t *ctx, void *u)
{
	(void)body;
	(void)u;
	song_meta_t m;
	song_meta_read(ctx->item_path, &m);
	char dp[PATH_MAX];
	item_child_path(ctx->item_path, "data.txt", dp, sizeof(dp));
	char *c = slurp_file(dp);
	form_body_t *fb = form_body_new(0);
	form_body_add(fb, "title", m.title);
	form_body_add(fb, "type", m.type);
	form_body_add(fb, "yt", m.yt);
	form_body_add(fb, "audio", m.audio);
	form_body_add(fb, "pdf", m.pdf);
	form_body_add(fb, "author", m.author);
	if (c)
		form_body_add(fb, "data", c);
	free(c);
	return core_post_form(fd, fb);
}

static int song_edit_get_handler(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        CHORDS_ITEMS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	        NULL,
	        NULL,
	        song_edit_get_auth,
	        NULL);
}

static int
song_edit_post_auth(int fd, char *body, const item_ctx_t *ctx, void *u)
{
	(void)u;
	ndc_query_parse(body);
	song_meta_t m = { 0 };
	int tl = ndc_query_param("title", m.title, 255);
	if (tl > 0)
		m.title[tl] = 0;
	int yl = ndc_query_param("yt", m.yt, 511);
	if (yl > 0)
		m.yt[yl] = 0;
	int al = ndc_query_param("audio", m.audio, 511);
	if (al > 0)
		m.audio[al] = 0;
	int pl = ndc_query_param("pdf", m.pdf, 511);
	if (pl > 0)
		m.pdf[pl] = 0;
	int ul = ndc_query_param("author", m.author, 255);
	if (ul > 0)
		m.author[ul] = 0;
	int ty = ndc_query_param("type", m.type, 255);
	if (ty > 0)
		m.type[ty] = 0;
	if (song_meta_write(ctx->item_path, &m) != 0)
		return server_error(fd, "Failed to write song metadata");
	index_put(index_hd, (char *)ctx->id, m.title);
	song_index_put_meta(ctx->id, &m);
	song_index_write_file(ctx->doc_root);
	char db[SONG_CHORD_DATA_BUF_SIZE];
	int dl = ndc_query_param("data", db, SONG_CHORD_DATA_BUF_SIZE - 1);
	char dp[PATH_MAX];
	item_child_path(ctx->item_path, "data.txt", dp, sizeof(dp));
	if (write_file_path(dp, db, dl > 0 ? dl : 0) != 0)
		return server_error(fd, "Failed to write song data");
	return redirect_to_item(fd, "song", ctx->id);
}

static int song_edit_post_handler(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        CHORDS_ITEMS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	        NULL,
	        NULL,
	        song_edit_post_auth,
	        NULL);
}

static void song_cleanup(const char *id)
{
	song_index_delete(id);
}

NDX_LISTENER(int, parse_item_line,
	const char *, line,
	char *, id_out,
	int *, int_out,
	char *, format_out)
{
	char *colon1 = strchr(line, ':');
	if (!colon1)
		return -1;

	char *colon2 = strchr(colon1 + 1, ':');
	if (!colon2)
		return -1;

	size_t id_len = (size_t)(colon1 - line);
	if (id_len > 127)
		id_len = 127;
	strncpy(id_out, line, id_len);
	id_out[id_len] = '\0';

	*int_out = atoi(colon1 + 1);

	strncpy(format_out, colon2 + 1, 127);
	format_out[127] = '\0';

	size_t fmt_len = strlen(format_out);
	while (fmt_len > 0 && (format_out[fmt_len - 1] == '\n' ||
	                       format_out[fmt_len - 1] == '\r'))
		format_out[--fmt_len] = '\0';

	return 0;
}

NDX_LISTENER(char *, song_get_types_json, int, dummy)
{
	(void)dummy;
	json_array_t *ja = json_array_new(0);
	if (!ja)
		return NULL;
	unsigned c = qmap_iter(type_index_hd, NULL, 0);
	const void *k, *v;
	while (qmap_next(&k, &v, c)) {
		char e[256];
		json_escape((const char *)k, e, sizeof(e));
		char en[260];
		snprintf(en, sizeof(en), "\"%s\"", e);
		json_array_append_raw(ja, en);
	}
	return json_array_finish(ja);
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

NDX_LISTENER(char *, build_all_songs_json, int, inc_t)
{
	json_array_t *ja = json_array_new(0);
	if (!ja)
		return NULL;
	unsigned c = qmap_iter(song_index_hd, NULL, 0);
	const void *k, *v;
	while (qmap_next(&k, &v, c)) {
		char buf[768], *title, *type;
		snprintf(buf, sizeof(buf), "%s", (const char *)v);
		title = buf;
		type = strchr(title, '\t');
		if (!type)
			continue;
		*type++ = '\0';
		char *author = strchr(type, '\t');
		if (author)
			*author = '\0';
		json_array_begin_object(ja);
		json_array_kv_str(ja, "id", (const char *)k);
		json_array_kv_str(ja, "title", title);
		if (inc_t)
			json_array_kv_str(ja, "type", type);
		json_array_end_object(ja);
	}
	return json_array_finish(ja);
}

typedef int (*song_for_each_cb_t)(const char *, const char *, void *);

NDX_LISTENER(int, song_for_each, song_for_each_cb_t, cb, void *, user)
{
	unsigned c = qmap_iter(song_index_hd, NULL, 0);
	const void *k, *v;
	while (qmap_next(&k, &v, c)) {
		char buf[768], *title, *tab;
		snprintf(buf, sizeof(buf), "%s", (const char *)v);
		title = buf;
		tab = strchr(title, '\t');
		if (tab)
			*tab = '\0';
		int r = cb((const char *)k, title, user);
		if (r)
			return r;
	}
	return 0;
}

static int song_add_post_handler(int fd, char *body)
{
	char id[256] = { 0 }, item_path[PATH_MAX], location[512];
	if (index_add_item(fd, body, id, sizeof(id)) != 0)
		return 1;
	if (item_path_build(fd, "song", id, item_path, sizeof(item_path)) == 0)
	{
		char dr[256] = { 0 };
		get_doc_root(fd, dr, sizeof(dr));
		song_index_upsert(dr[0] ? dr : g_doc_root, id, item_path);
	}
	snprintf(location, sizeof(location), "/song/%s", id);
	return ndc_redirect(fd, location);
}

void ndx_install(void)
{
	char dr[256] = { 0 };
	get_doc_root(0, dr, sizeof(dr));
	if (dr[0])
		strncpy(g_doc_root, dr, sizeof(g_doc_root) - 1);
	g_transp_ctx = transp_init();
	ndx_load("./mods/common/common");
	ndx_load("./mods/index/index");
	ndx_load("./mods/mpfd/mpfd");
	ndx_load("./mods/auth/auth");
	index_hd = index_open("Song", 0, 1, song_cleanup);
	build_type_index(dr);
	ndc_register_handler("POST:/song/add", song_add_post_handler);
	ndc_register_handler("GET:/song/:id", song_details_handler);
	ndc_register_handler("GET:/song/:id/edit", song_edit_get_handler);
	ndc_register_handler("POST:/song/:id/edit", song_edit_post_handler);
	ndc_register_handler(
	        "GET:/api/song/:id/transpose", api_song_transpose_handler);
	ndc_register_handler(
	        "POST:/api/song/prefs", api_song_viewer_prefs_handler);
}
