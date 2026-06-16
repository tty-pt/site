#include <ttypt/ndx-mod.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <json-c/json.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <dirent.h>

#include <ttypt/axil.h>
#include <ttypt/qmap.h>
#include "../index/index.h"

#include "../common/common.h"
#include "../source/source.h"
#include "../auth/auth.h"
#include "../../lib/transp/transp.h"
#include "fields.h"

#define CHORDS_ITEMS_PATH "items/song/items"

#define SONG_TYPES_BUF_SIZE 2048
#define VIEWER_ZOOM_MIN 70
#define VIEWER_ZOOM_MAX 170
#define VIEWER_ZOOM_DEFAULT 100

static transp_ctx_t *g_transp_ctx = NULL;
static char g_doc_root[256] = ".";

static void song_meta_read(const char *path, song_cache_t *m)
{
	source_meta_read(path, song_fields, SONG_FIELD_COUNT, m, sizeof(*m));
	str_list_normalize(m->type, m->type, sizeof(m->type));
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

static void song_load_saved_prefs(const char *user, int *f)
{
	if (!user || !user[0])
		return;
	char *rb = song_viewer_pref_read(user, "chords-bemol");
	char *rl = song_viewer_pref_read(user, "chords-latin");
	if (rb && atoi(rb))
		*f |= TRANSP_BEMOL;
	if (rl && atoi(rl))
		*f |= TRANSP_LATIN;
	free(rb);
	free(rl);
}

static int parse_params(const char *q, int *t, int *f, int *m)
{
	*t = 0;
	*m = 0;
	if (!q || !*q)
		return 0;
	char c[1024];
	snprintf(c, sizeof(c), "%s", q);
	axil_query_parse(c);
	char b[32];
	if (axil_query_param("t", b, sizeof(b)) > 0)
		*t = atoi(b);
	if (axil_query_param("b", b, sizeof(b)) >= 0 && atoi(b) != 0)
		*f |= TRANSP_BEMOL;
	if (axil_query_param("l", b, sizeof(b)) >= 0 && atoi(b) != 0)
		*f |= TRANSP_LATIN;
	if (axil_query_param("h", b, sizeof(b)) >= 0 && atoi(b) != 0)
		*f |= TRANSP_HTML;
	if (axil_query_param("m", b, sizeof(b)) >= 0 && atoi(b) != 0)
		*m = 1;
	return 0;
}

static int api_song_viewer_prefs_handler(int fd, char *body)
{
	const char *u = get_request_user(fd);
	if (!u || !u[0]) {
		axil_respond(fd, 204, "");
		return 0;
	}
	axil_query_parse(body);
	char b[16];
	if (axil_query_param("v", b, sizeof(b)) >= 0)
		song_set_viewer_zoom(u, atoi(b));
	if (axil_query_param("b", b, sizeof(b)) >= 0)
		song_viewer_pref_write(u, "chords-bemol", b);
	if (axil_query_param("l", b, sizeof(b)) >= 0)
		song_viewer_pref_write(u, "chords-latin", b);
	if (axil_query_param("z", b, sizeof(b)) >= 0)
		song_set_viewer_zoom(u, atoi(b));
	axil_respond(fd, 204, "");
	return 0;
}

/* ── State specs (computed once at init) ─────────────────────── */

static source_state_field_t song_state_specs[SONG_FIELD_COUNT + 1];

static int
song_details_auth(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;
	int t = 0, f = 0, m = 0;
	char q[1024] = { 0 };
	axil_env_get(fd, q, "QUERY_STRING");
	parse_params(q, &t, &f, &m);

	int v_z = VIEWER_ZOOM_DEFAULT;
	if (ctx->username && ctx->username[0])
		v_z = song_get_viewer_zoom(ctx->username);

	if (q[0]) {
		if (ctx->username && ctx->username[0]) {
			char pv[2] = { (f & TRANSP_BEMOL) ? '1' : '0', '\0' };
			song_viewer_pref_write(
			        ctx->username, "chords-bemol", pv);
			pv[0] = (f & TRANSP_LATIN) ? '1' : '0';
			song_viewer_pref_write(
			        ctx->username, "chords-latin", pv);
		}
		char tmp[16] = { 0 };
		axil_query_param("z", tmp, sizeof(tmp));
		if (tmp[0]) {
			int zv = atoi(tmp);
			if (ctx->username && ctx->username[0])
				song_set_viewer_zoom(ctx->username, zv);
			v_z = zv;
		}
	} else {
		song_load_saved_prefs(ctx->username, &f);
	}

	char *trans = NULL;
	int k = 0;
	song_transpose_root(ctx->doc_root, ctx->id, t, f, &trans, &k);

	json_object *jo = NULL;
	source_build_state_json("song.items", ctx->id, song_state_specs, &jo);
	if (!jo) {
		free(trans);
		return respond_error(fd, 404, "Song not found");
	}
	json_object_object_add(
	        jo, "data", json_object_new_string(trans ? trans : ""));

	app_state_t tmp;
	memset(&tmp, 0, sizeof(tmp));
	tmp.is_owner =
	        (ctx->username && ctx->username[0])
	                ? item_check_ownership(ctx->item_path, ctx->username)
	                : 0;
	tmp.zoom = v_z;
	tmp.use_bemol = (f & TRANSP_BEMOL) != 0;
	tmp.use_latin = (f & TRANSP_LATIN) != 0;
	tmp.show_media = m;
	tmp.original_key = k;
	source_overlay_from_desc(
	        jo, &tmp, song_fields, BUD_OVERLAY_INT, BUD_OVERLAY_STR);

	const char *json_str = json_object_to_json_string(jo);
	char *json = strdup(json_str ? json_str : "{}");
	json_object_put(jo);
	if (!json) {
		free(trans);
		return respond_error(fd, 500, "Failed to finish JSON");
	}
	int rc = respond_json(fd, 200, json);
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
	(void)id;
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

NDX_LISTENER(int, source_after_update,
        int, fd,
        const char *, dataset_id,
        const char *, id,
        unsigned, data_handle)
{
	(void)dataset_id;
	char path[PATH_MAX], dr[256] = { 0 };
	get_doc_root(fd, dr, sizeof(dr));
	if (item_path_build_root(
	            dr[0] ? dr : g_doc_root, "song", id, path, sizeof(path)) !=
	    0)
		return 0;

	const char *data = (const char *)qmap_get(data_handle, "data");
	if (data) {
		char data_path[PATH_MAX];
		item_child_path(path, "data.txt", data_path, sizeof(data_path));
		write_file_path(data_path, data, strlen(data));
	}

	return 0;
}

#include "ux/all.c"

/* ── HTTP handlers ────────────────────────────────────────────── */

static int song_detail_handler(int fd, char *body)
{
	(void)body;
	char id[128] = { 0 };
	const char *user = get_request_user(fd);

	axil_env_get(fd, id, "PATTERN_PARAM_ID");
	if (!id[0])
		return bad_request(fd, "Missing ID");

	char item_path_buf[512];
	if (item_path_build(
	            fd, "song", id, item_path_buf, sizeof(item_path_buf)) != 0)
		return server_error(fd, "Failed to resolve path");

	int is_owner = item_check_ownership(item_path_buf, user);

	int t = 0, f = 0, m = 0;
	char q[1024] = { 0 };
	axil_env_get(fd, q, "QUERY_STRING");
	parse_params(q, &t, &f, &m);

	int url_z = 0;
	if (q[0] && user && user[0]) {
		char b[16];
		if (axil_query_param("l", b, sizeof(b)) >= 0)
			song_viewer_pref_write(user, "chords-latin", b);
		if (axil_query_param("b", b, sizeof(b)) >= 0)
			song_viewer_pref_write(user, "chords-bemol", b);
		if (axil_query_param("z", b, sizeof(b)) > 0) {
			url_z = atoi(b);
			song_set_viewer_zoom(user, url_z);
		}
	}
	if (q[0] && (!user || !user[0])) {
		char zb[16];
		if (axil_query_param("z", zb, sizeof(zb)) > 0)
			url_z = atoi(zb);
	}

	if (!q[0])
		song_load_saved_prefs(user, &f);

	f |= TRANSP_HTML;

	char *trans = NULL;
	int k = 0;
	song_transpose_root(g_doc_root, id, t, f, &trans, &k);

	json_object *jo = NULL;
	source_build_state_json("song.items", id, song_state_specs, &jo);
	if (!jo)
		return respond_error(fd, 404, "Song not found");

	const char *title = NULL;
	json_object *jval;
	if (json_object_object_get_ex(jo, "title", &jval))
		title = json_object_get_string(jval);
	if (!title || !title[0]) {
		json_object_put(jo);
		return respond_error(fd, 404, "Song not found");
	}

	/* Populate computed values into app_state for overlay + bud template */
	memset(&app_state, 0, sizeof(app_state));
	snprintf(app_state.cache.id, sizeof(app_state.cache.id), "%s", id);
	app_state.transpose = t;
	app_state.use_bemol = (f & TRANSP_BEMOL) != 0;
	app_state.use_latin = (f & TRANSP_LATIN) != 0;
	app_state.show_media = m;
	app_state.original_key = k;
	app_state.is_owner = is_owner;
	if (url_z)
		app_state.zoom = url_z;
	else
		app_state.zoom = song_get_viewer_zoom(user);
	snprintf(
	        app_state.chord_html,
	        sizeof(app_state.chord_html),
	        "%s",
	        trans ? trans : "");
	if (user && user[0]) {
		snprintf(
		        app_state.page_user,
		        sizeof(app_state.page_user),
		        "%s",
		        user);
		snprintf(
		        app_state.save_url,
		        sizeof(app_state.save_url),
		        "/api/song/prefs");
	}
	snprintf(app_state.path, sizeof(app_state.path), "/song/%s", id);

	source_overlay_from_desc(
	        jo, &app_state, song_fields, BUD_OVERLAY_INT, BUD_OVERLAY_STR);

	/* Populate record fields from JSON into app_state (for bud template) */
	bud_state_apply(
	        &app_state, song_fields, json_object_to_json_string(jo));

	bud_node *layout = bud_app_render();
	char *html;
	{
		char state_buf[16384];
		snprintf(
		        state_buf,
		        sizeof(state_buf),
		        "<script type=\"application/json\" "
		        "id=\"bud-state\">%s</script>",
		        json_object_to_json_string(jo));
		json_object_put(jo);

		free(trans);
		return site_ui_respond_page(
		        fd, app_state.cache.title, state_buf, "song", layout);
	}
}

static int song_edit_get_handler(int fd, char *body)
{
	(void)body;
	char id[128] = { 0 };
	const char *user;
	char item_path_buf[512];
	song_cache_t meta;

	if (check_item_access(
	            fd,
	            "song",
	            id,
	            sizeof(id),
	            &user,
	            item_path_buf,
	            sizeof(item_path_buf)))
		return 1;
	song_meta_read(item_path_buf, &meta);
	source_resolve_meta_display(
	        "song.items", id, song_fields, SONG_FIELD_COUNT, &meta);

	char data_path[PATH_MAX];
	item_child_path(
	        item_path_buf, "data.txt", data_path, sizeof(data_path));
	char *data_val = slurp_file(data_path);

	const char *csrf_token = csrf_setup(fd);

	bud_node *form = song_form_content(1, id, &meta, data_val, csrf_token);
	free(data_val);

	return site_ui_respond_edit_page(
	        fd, user, "song", "\xf0\x9f\x8e\xb8", meta.title, id, form);
}

static int song_add_get_handler(int fd, char *body)
{
	(void)body;
	const char *user = require_user(fd);
	if (!user)
		return 1;

	const char *csrf_token = csrf_setup(fd);

	bud_node *form = song_form_content(0, NULL, NULL, NULL, csrf_token);
	return site_ui_respond_add_page(
	        fd, user, "song", "\xf0\x9f\x8e\xb8", form);
}

void ndx_install(void)
{
	char dr[256] = { 0 };
	srand((unsigned)time(NULL));
	resolve_doc_root(0, dr, sizeof(dr));
	strncpy(g_doc_root, dr, sizeof(g_doc_root) - 1);
	g_transp_ctx = transp_init();
	ndx_load("./mods/index/index");
	ndx_load("./mods/mpfd/mpfd");

	/* Precompute state specs from field table */
	source_build_state_specs(
	        song_fields, song_state_specs, SONG_FIELD_COUNT);

	source_setup(
	        "song.types",
	        "name",
	        sizeof(song_type_cache_t),
	        "items/song/types",
	        song_type_fields,
	        SONG_TYPE_FIELD_COUNT,
	        SOURCE_FLAG_VOLATILE);

	ref_field_register("song.items", "type");

	source_setup(
	        "song.items",
	        NULL,
	        sizeof(song_cache_t),
	        "items/song/items",
	        song_fields,
	        SONG_FIELD_COUNT,
	        0);

	index_open("Song", "song.items", song_cleanup, NULL, NULL, NULL, NULL);

	axil_register_handler("GET:/song/add", song_add_get_handler);
	axil_register_handler("GET:/song/:id/edit", song_edit_get_handler);
	axil_register_handler("GET:/song/:id", song_detail_handler);
	axil_register_handler(
	        "GET:/api/song/:id/transpose", api_song_transpose_handler);
	axil_register_handler(
	        "POST:/api/song/prefs", api_song_viewer_prefs_handler);
}
