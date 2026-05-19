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
#include "../ssr/ssr.h"
#include "../index/index.h"
#include "../common/common.h"
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
static unsigned type_dataset_hd = 0;
static unsigned song_meta_qtype = 0;
static char g_doc_root[256] = ".";

typedef struct {
	char title[256], type[SONG_TYPES_BUF_SIZE], yt[512], audio[512], pdf[512], author[256];
} song_meta_t;

static int song_hex_digit_value(int ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	return -1;
}

static size_t song_url_decode_component(
        const char *src, size_t src_len, char *out, size_t out_len)
{
	size_t si;
	size_t oi;

	if (!out || out_len == 0)
		return 0;

	oi = 0;
	for (si = 0; si < src_len && oi + 1 < out_len; si++) {
		if (src[si] == '+') {
			out[oi++] = ' ';
			continue;
		}
		if (src[si] == '%' && si + 2 < src_len) {
			int hi;
			int lo;

			hi = song_hex_digit_value(src[si + 1]);
			lo = song_hex_digit_value(src[si + 2]);
			if (hi >= 0 && lo >= 0) {
				out[oi++] = (char)((hi << 4) | lo);
				si += 2;
				continue;
			}
		}
		out[oi++] = src[si];
	}
	out[oi] = '\0';
	return oi;
}

static void song_trim(char *s)
{
	char *start;
	char *end;

	if (!s || !s[0])
		return;

	start = s;
	while (*start == ' ' || *start == '\t' || *start == '\r' ||
	       *start == '\n')
		start++;
	if (start != s)
		memmove(s, start, strlen(start) + 1);

	end = s + strlen(s);
	while (end > s) {
		if (end[-1] != ' ' && end[-1] != '\t' && end[-1] != '\r' &&
		    end[-1] != '\n')
			break;
		end--;
	}
	*end = '\0';
}

static int song_types_contains(const char *types, const char *type)
{
	char copy[SONG_TYPES_BUF_SIZE];
	char *tok;
	char *saveptr;

	if (!types || !types[0] || !type || !type[0])
		return 0;

	snprintf(copy, sizeof(copy), "%s", types);
	tok = strtok_r(copy, "\n", &saveptr);
	while (tok) {
		if (strcmp(tok, type) == 0)
			return 1;
		tok = strtok_r(NULL, "\n", &saveptr);
	}

	return 0;
}

static int song_types_append(char *out, size_t out_sz, const char *type)
{
	size_t used;
	size_t len;

	if (!out || !type || !type[0])
		return 0;
	if (song_types_contains(out, type))
		return 0;

	used = strlen(out);
	len = strlen(type);
	if (used && used + 1 >= out_sz)
		return -1;
	if (used)
		out[used++] = '\n';
	if (used + len >= out_sz)
		return -1;
	memcpy(out + used, type, len);
	used += len;
	out[used] = '\0';
	return 0;
}

static void song_types_normalize(
        const char *input, char *out, size_t out_sz)
{
	char copy[SONG_TYPES_BUF_SIZE];
	char *tok;
	char *saveptr;

	if (!out || out_sz == 0)
		return;

	copy[0] = '\0';
	if (input && input[0])
		snprintf(copy, sizeof(copy), "%s", input);
	out[0] = '\0';
	if (!copy[0])
		return;

	tok = strtok_r(copy, "\r\n", &saveptr);
	while (tok) {
		char type[256];

		snprintf(type, sizeof(type), "%s", tok);
		song_trim(type);
		if (type[0])
			song_types_append(out, out_sz, type);
		tok = strtok_r(NULL, "\r\n", &saveptr);
	}
}

static char *song_types_json_from_text(const char *types)
{
	json_array_t *arr;
	char copy[SONG_TYPES_BUF_SIZE];
	char *tok;
	char *saveptr;
	char *json;

	arr = json_array_new(0);
	if (!arr)
		return NULL;

	if (!types || !types[0]) {
		json = json_array_finish(arr);
		if (!json)
			return strdup("[]");
		return json;
	}

	snprintf(copy, sizeof(copy), "%s", types);
	tok = strtok_r(copy, "\n", &saveptr);
	while (tok) {
		char type[256];
		char escaped[512];
		char raw[520];

		snprintf(type, sizeof(type), "%s", tok);
		song_trim(type);
		if (type[0]) {
			if (json_escape(
			            type,
			            escaped,
			            sizeof(escaped) - 1) != 0)
			{
				json_array_free(arr);
				return NULL;
			}
			snprintf(raw, sizeof(raw), "\"%s\"", escaped);
			if (json_array_append_raw(arr, raw) != 0) {
				json_array_free(arr);
				return NULL;
			}
		}
		tok = strtok_r(NULL, "\n", &saveptr);
	}

	json = json_array_finish(arr);
	if (!json)
		return strdup("[]");
	return json;
}

typedef int (*song_type_each_cb)(const char *type, void *user);

static int song_types_for_each(
        const char *types, song_type_each_cb cb, void *user)
{
	char copy[SONG_TYPES_BUF_SIZE];
	char *tok;
	char *saveptr;

	if (!types || !types[0] || !cb)
		return 0;

	snprintf(copy, sizeof(copy), "%s", types);
	tok = strtok_r(copy, "\n", &saveptr);
	while (tok) {
		char type[256];
		int rc;

		snprintf(type, sizeof(type), "%s", tok);
		song_trim(type);
		if (type[0]) {
			rc = cb(type, user);
			if (rc != 0)
				return rc;
		}
		tok = strtok_r(NULL, "\n", &saveptr);
	}

	return 0;
}

static int song_types_from_body(const char *body, char *out, size_t out_sz)
{
	const char *p;
	size_t used;

	if (!out || out_sz == 0) {
		return -1;
	}

	out[0] = '\0';
	if (!body || !body[0])
		return 0;

	used = 0;
	p = body;
	while (*p) {
		const char *amp;
		const char *eq;
		size_t key_len;
		char key[256];
		char value[256];
		size_t value_len;

		amp = strchr(p, '&');
		if (!amp)
			amp = p + strlen(p);
		eq = memchr(p, '=', (size_t)(amp - p));
		key_len = eq ? (size_t)(eq - p) : (size_t)(amp - p);
		if (key_len >= sizeof(key))
			key_len = sizeof(key) - 1;
		song_url_decode_component(p, key_len, key, sizeof(key));
		if (strcmp(key, "type") == 0) {
			value[0] = '\0';
			if (eq && eq + 1 <= amp) {
				value_len = song_url_decode_component(
				        eq + 1,
				        (size_t)(amp - (eq + 1)),
				        value,
				        sizeof(value));
			} else {
				value_len = 0;
			}
			song_trim(value);
			value_len = strlen(value);
			if (value[0]) {
				if (used && used + 1 >= out_sz)
					return -1;
				if (song_types_contains(out, value)) {
					if (*amp == '\0')
						break;
					p = amp + 1;
					continue;
				}
				if (used)
					out[used++] = '\n';
				if (used + value_len >= out_sz)
					return -1;
				memcpy(out + used, value, value_len);
				used += value_len;
				out[used] = '\0';
			}
		}
		if (*amp == '\0')
			break;
		p = amp + 1;
	}

	song_types_normalize(out, out, out_sz);
	return (int)strlen(out);
}

static void song_meta_read(const char *path, song_meta_t *m)
{
	meta_field_t f[] = {
		{ "title", m->title, 256 },
		{ "type", m->type, SONG_TYPES_BUF_SIZE },
		{ "yt", m->yt, 512 },       { "audio", m->audio, 512 },
		{ "pdf", m->pdf, 512 },     { "author", m->author, 256 }
	};
	memset(m, 0, sizeof(*m));
	meta_fields_read(path, f, sizeof(f) / sizeof(f[0]));
	song_types_normalize(m->type, m->type, sizeof(m->type));
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
	song_types_normalize(m->type, types, sizeof(types));
	return meta_fields_write(path, f, sizeof(f) / sizeof(f[0]));
}

static const char *song_type_lookup_key(const char *type)
{
	return (type && type[0]) ? type : "any";
}

static int song_type_dataset_put(const char *type)
{
	json_object_t *jo;
	char *json;

	if (!type_dataset_hd || !type || !type[0])
		return 0;
	if (qmap_get(type_dataset_hd, type))
		return 0;

	jo = json_object_new(0);
	if (!jo)
		return -1;
	if (json_object_kv_str(jo, "id", type) != 0 ||
	    json_object_kv_str(jo, "name", type) != 0)
	{
		json_object_free(jo);
		return -1;
	}

	json = json_object_finish(jo);
	if (!json)
		return -1;

	qmap_put(type_dataset_hd, type, json);
	return 0;
}

static void song_type_dataset_maybe_remove(const char *type)
{
	const char *songs;

	if (!type_dataset_hd || !type || !type[0])
		return;

	songs = (const char *)qmap_get(type_index_hd, type);
	if (!songs || !songs[0])
		qmap_del(type_dataset_hd, type);
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
		song_type_dataset_put(type);
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
		song_type_dataset_maybe_remove(type);
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
	song_types_for_each(types, song_type_index_add_cb, &ctx);
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
	song_types_for_each(types, song_type_index_remove_cb, &ctx);
}

static void song_type_index_remove(const char *id)
{
	char item_path[PATH_MAX];
	char type[SONG_TYPES_BUF_SIZE] = { 0 };
	item_path_build(0, "song", id, item_path, sizeof(item_path));
	read_meta_file(item_path, "type", type, sizeof(type));
	song_type_index_remove_from(type, id);
}

static int song_index_write_file(const char *doc)
{
	(void)doc;
	return 0;
}

static void song_index_put_meta(const char *id, const song_meta_t *m)
{
	(void)id;
	(void)m;
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
}

static void song_load_cb(const char *id, const char *val, void *user)
{
	(void)id;
	(void)val;
	(void)user;
}

static int song_tsv_load(const char *path)
{
	(void)path;
	return 0;
}

static void song_index_rebuild(const char *root)
{
	(void)root;
}

static void build_type_index(const char *doc)
{
	char items_path[PATH_MAX];
	const char *root = (doc && doc[0]) ? doc : ".";
	DIR *d;
	struct dirent *e;

	type_index_hd = qmap_open(NULL, "type_idx", QM_STR, QM_STR, 0x3FF, 0);
	type_dataset_hd =
	        qmap_open(NULL, "song_types_dataset", QM_STR, QM_STR, 0x3FF, 0);
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

		song_types_normalize(type, type, sizeof(type));
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
	int c, idx, i;
	char *l = (char *)qmap_get(type_index_hd, t);
	char *cp, *tok;

	if (!l)
		l = (char *)qmap_get(type_index_hd, "any");
	if (!l)
		return -1;
	c = 1;
	for (char *p = l; *p; p++)
		if (*p == ',')
			c++;
	idx = rand() % c;
	cp = strdup(l);
	tok = strtok(cp, ",");
	for (i = 0; i < idx && tok; i++)
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

char *song_get_types_json(int dummy);

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
	char *at_json = song_get_types_json(0);
	form_body_add(fb, "title", m.title);
	form_body_add(fb, "type", m.type);
	form_body_add(fb, "yt", m.yt);
	form_body_add(fb, "audio", m.audio);
	form_body_add(fb, "pdf", m.pdf);
	form_body_add(fb, "author", m.author);
	if (c)
		form_body_add(fb, "data", c);
	free(c);
	if (at_json) {
		form_body_add(fb, "allTypes", at_json);
		free(at_json);
	}
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
	char old_type[SONG_TYPES_BUF_SIZE] = { 0 };
	song_meta_t m = { 0 };
	char *body_copy;
	int tl;
	int yl;
	int al;
	int pl;
	int ul;
	char db[SONG_CHORD_DATA_BUF_SIZE];
	int dl;
	char dp[PATH_MAX];

	body_copy = strdup(body ? body : "");
	if (!body_copy)
		return server_error(fd, "OOM");
	ndc_query_parse(body_copy);
	{
		char csrf_submitted[33] = { 0 };
		ndc_query_param(
		        "csrf_token", csrf_submitted, sizeof(csrf_submitted));
		if (csrf_validate(fd, csrf_submitted)) {
			free(body_copy);
			return respond_error(fd, 403, "Forbidden");
		}
	}
	read_meta_file(ctx->item_path, "type", old_type, sizeof(old_type));
	song_types_normalize(old_type, old_type, sizeof(old_type));
	tl = ndc_query_param("title", m.title, 255);
	if (tl > 0)
		m.title[tl] = 0;
	yl = ndc_query_param("yt", m.yt, 511);
	if (yl > 0)
		m.yt[yl] = 0;
	al = ndc_query_param("audio", m.audio, 511);
	if (al > 0)
		m.audio[al] = 0;
	pl = ndc_query_param("pdf", m.pdf, 511);
	if (pl > 0)
		m.pdf[pl] = 0;
	ul = ndc_query_param("author", m.author, 255);
	if (ul > 0)
		m.author[ul] = 0;
	if (song_types_from_body(body, m.type, sizeof(m.type)) < 0) {
		free(body_copy);
		return respond_error(fd, 400, "Invalid type list");
	}
	free(body_copy);
	if (song_meta_write(ctx->item_path, &m) != 0)
		return server_error(fd, "Failed to write song metadata");
	if (strcmp(old_type, m.type) != 0) {
		song_type_index_remove_from(old_type, ctx->id);
		song_type_index_add(m.type, ctx->id);
	}
	index_put(index_hd, (char *)ctx->id, m.title);
	song_index_put_meta(ctx->id, &m);
	song_index_write_file(ctx->doc_root);
	dl = ndc_query_param("data", db, SONG_CHORD_DATA_BUF_SIZE - 1);
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
	char item_path[PATH_MAX];
	char type[SONG_TYPES_BUF_SIZE] = { 0 };

	song_index_delete(id);
	item_path_build(0, "song", id, item_path, sizeof(item_path));
	read_meta_file(item_path, "type", type, sizeof(type));
	song_types_normalize(type, type, sizeof(type));
	song_type_index_add(type, id);
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
	unsigned c = qmap_iter(type_dataset_hd, NULL, 0);
	const void *k, *v;
	while (qmap_next(&k, &v, c)) {
		char e[256];
		json_escape((const char *)k, e, sizeof(e));
		char en[260];
		snprintf(en, sizeof(en), "\"%s\"", e);
		json_array_append_raw(ja, en);
	}
	char *result = json_array_finish(ja);
	return result;
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

NDX_LISTENER(char *, song_get_pref, const char *, user, const char *, name)
{
	return song_viewer_pref_read(user, name);
}

NDX_LISTENER(int, song_get_original_key, const char *, id)
{
	return song_get_original_key_root(g_doc_root, id);
}

NDX_LISTENER(int, song_transpose,
	const char *, id,
	int, semi,
	int, fl,
	char **, out)
{
	return song_transpose_root(g_doc_root, id, semi, fl, out, NULL);
}

NDX_LISTENER(char *, build_all_songs_json, int, inc_t)
{
	(void)inc_t;
	char *json = NULL;
	if (dataset_get_json(0, "song.items", NULL, &json) != 0 || !json)
		return NULL;
	return json;
}

typedef int (*song_for_each_cb_t)(const char *, const char *, void *);

NDX_LISTENER(int, song_for_each, song_for_each_cb_t, cb, void *, user)
{
	if (index_hd == 0)
		return 0;

	const void *id_ptr;
	const void *val_ptr;
	size_t iter = qmap_iter(index_hd, NULL, 0);
	int result = 0;

	while (result == 0 && qmap_next(&id_ptr, &val_ptr, iter)) {
		const char *id = (const char *)id_ptr;
		char *json = (char *)qmap_get(index_hd, id);
		if (!json)
			continue;

		const char *title = "";
		char *title_start = strstr(json, "\"title\"");
		if (title_start) {
			title_start = strchr(title_start, ':');
			if (title_start) {
				title_start++;
				while (*title_start == ' ' ||
				       *title_start == '"')
					title_start++;
				char *title_end = strchr(title_start, '"');
				if (title_end) {
					size_t title_len =
					        title_end - title_start;
					if (title_len > 255)
						title_len = 255;
					static __thread char title_buf[256];
					title_len = (title_len < 255)
					                    ? title_len
					                    : 255;
					memcpy(title_buf,
					       title_start,
					       title_len);
					title_buf[title_len] = '\0';
					title = title_buf;
				}
			}
		}

		result = cb(id, title, user);
	}

	return result;
}

static size_t
song_format_line(const char *id, const char *val, char *out, size_t out_sz)
{
	const song_meta_t *m = (const song_meta_t *)val;
	char type[SONG_TYPES_BUF_SIZE];
	size_t i;

	snprintf(type, sizeof(type), "%s", m->type[0] ? m->type : "any");
	for (i = 0; type[i]; i++) {
		if (type[i] == '\n')
			type[i] = ',';
	}
	return (size_t)snprintf(
	        out, out_sz, "%s\t%s\t%s\r\n", id, m->title, type);
}

static int song_type_dataset_row_json(
        json_object_t *jo, const char *key, const void *val, void *user)
{
	(void)val;
	(void)user;
	json_object_kv_str(jo, "name", key);
	return 0;
}

static int song_dataset_row_json(
        json_object_t *jo, const char *key, const void *val, void *user)
{
	char *type_json;
	(void)user;
	const song_meta_t *m = (const song_meta_t *)val;
	json_object_kv_str(jo, "id", key);
	json_object_kv_str(jo, "title", m->title);
	type_json = song_types_json_from_text(m->type);
	if (!type_json)
		return -1;
	json_object_kv_raw(jo, "type", type_json);
	free(type_json);
	return 0;
}

static int song_dataset_create(
        int fd,
        const char *user,
        unsigned data_hd,
        void *udata,
        char *new_key,
        size_t new_key_sz)
{
	(void)udata;
	(void)user;
	const char *title = (const char *)qmap_get(data_hd, "title");
	const char *type = (const char *)qmap_get(data_hd, "type");
	const char *data = (const char *)qmap_get(data_hd, "data");

	if (!title || !title[0])
		return respond_error(fd, 400, "Missing title");

	char id[256];
	if (index_id(id, sizeof(id), title, strlen(title)) != 0)
		return server_error(fd, "Failed to generate ID");

	char path[PATH_MAX], dr[256] = { 0 };
	get_doc_root(fd, dr, sizeof(dr));
	if (item_path_build_root(
	            dr[0] ? dr : g_doc_root, "song", id, path, sizeof(path)) !=
	    0)
		return server_error(fd, "Path build failed");

	if (ensure_dir_path(path) != 0)
		return server_error(fd, "Failed to create item directory");

	song_meta_t m;
	memset(&m, 0, sizeof(m));
	strncpy(m.title, title, sizeof(m.title) - 1);
	if (type)
		song_types_normalize(type, m.type, sizeof(m.type));
	if (song_meta_write(path, &m) != 0)
		return server_error(fd, "Failed to write meta.txt");

	char data_path[PATH_MAX];
	item_child_path(path, "data.txt", data_path, sizeof(data_path));
	if (write_file_path(
	            data_path, data ? data : "", data ? strlen(data) : 0) != 0)
		return server_error(fd, "Failed to write data.txt");

	song_type_index_add(m.type, id);

	song_index_upsert(dr[0] ? dr : g_doc_root, id, path);
	snprintf(new_key, new_key_sz, "%s", id);
	return 0;
}

static int song_dataset_update(
        int fd, const char *user, const char *id, unsigned data_hd, void *udata)
{
	(void)udata;
	(void)user;
	char path[PATH_MAX], dr[256] = { 0 };
	char old_type[SONG_TYPES_BUF_SIZE] = { 0 };
	get_doc_root(fd, dr, sizeof(dr));
	if (item_path_build_root(
	            dr[0] ? dr : g_doc_root, "song", id, path, sizeof(path)) !=
	    0)
		return respond_error(fd, 404, "Song not found");

	song_meta_t m;
	song_meta_read(path, &m);
	snprintf(old_type, sizeof(old_type), "%s", m.type);

	const char *title = (const char *)qmap_get(data_hd, "title");
	const char *type = (const char *)qmap_get(data_hd, "type");
	const char *data = (const char *)qmap_get(data_hd, "data");

	int has_meta = 0;
	if (title) {
		strncpy(m.title, title, sizeof(m.title) - 1);
		has_meta = 1;
	}
	if (type) {
		song_types_normalize(type, m.type, sizeof(m.type));
		has_meta = 1;
	}
	if (has_meta) {
		if (song_meta_write(path, &m) != 0)
			return server_error(fd, "Failed to write meta.txt");
		if (strcmp(old_type, m.type) != 0) {
			song_type_index_remove_from(old_type, id);
			song_type_index_add(m.type, id);
		}
		song_index_upsert(dr[0] ? dr : g_doc_root, id, path);
	}

	if (data) {
		char data_path[PATH_MAX];
		item_child_path(path, "data.txt", data_path, sizeof(data_path));
		if (write_file_path(data_path, data, strlen(data)) != 0)
			return server_error(fd, "Failed to write data.txt");
	}

	return 0;
}

static int
song_dataset_delete(int fd, const char *user, const char *id, void *udata)
{
	(void)udata;
	(void)user;
	char path[PATH_MAX], dr[256] = { 0 };
	get_doc_root(fd, dr, sizeof(dr));
	if (item_path_build_root(
	            dr[0] ? dr : g_doc_root, "song", id, path, sizeof(path)) !=
	    0)
		return respond_error(fd, 404, "Song not found");

	if (item_remove_path_recursive(path) != 0)
		return server_error(fd, "Failed to delete song directory");

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

	index_hd =
	        index_open("Song", 0, 1, song_cleanup, NULL, NULL, NULL, NULL);

	{
		static const dataset_field_t fields[] = {
			{ "id", NULL, DATASET_FIELD_STRING, 0 },
			{ "title", "title", DATASET_FIELD_STRING, 1 },
			{ "type",
			  "type",
			  DATASET_FIELD_MULTI_REFERENCE,
			  1,
			  "song.types",
			  "songs" },
			{ "author", "author", DATASET_FIELD_STRING, 1 },
			{ "yt", "yt", DATASET_FIELD_STRING, 1 },
			{ "audio", "audio", DATASET_FIELD_STRING, 1 },
			{ "pdf", "pdf", DATASET_FIELD_STRING, 1 },
			{ "data", "data.txt", DATASET_FIELD_STRING, 1 },
			{ "owner", "owner", DATASET_FIELD_STRING, 0 },
		};
		dataset_def_t def = { .id = "song.items",
			              .key_field = "id",
			              .items_path = "items/song/items",
			              .access_policy = DATASET_ACCESS_PUBLIC,
			              .fields = fields,
			              .field_count = sizeof(fields) /
			                             sizeof(fields[0]),
			              .source_hd = index_hd };
		dataset_register(&def);
	}
	build_type_index(dr);

	{
		static const dataset_field_t fields[] = {
			{ "name", "name", DATASET_FIELD_STRING, 0, NULL, NULL }
		};
		dataset_def_t def = { .id = "song.types",
			              .key_field = "name",
			              .items_path = "items/song/types",
			              .access_policy = DATASET_ACCESS_PUBLIC,
			              .fields = fields,
			              .field_count = sizeof(fields) /
			                             sizeof(fields[0]),
			              .source_hd = type_dataset_hd };
		dataset_register(&def);
	}

	ndc_register_handler(
	        "GET:/api/song/:id/transpose", api_song_transpose_handler);
	ndc_register_handler(
	        "POST:/api/song/prefs", api_song_viewer_prefs_handler);
}
