#include <ttypt/xy-mod.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <json-c/json.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>

#include <ttypt/axil.h>
#include <ttypt/qmap.h>
#include "../index/index.h"

#include "../common/common.h"
#include "../source/source.h"
#include "hyle/source.h"
#include "../auth/auth.h"
#include "../../lib/transp/transp.h"
#include "../../lib/transp/parse.h"
#include "fields.h"

static transp_ctx_t *g_transp_ctx = NULL;
static char g_doc_root[256] = ".";

static void song_meta_read(const char *path, song_cache_t *m)
{
	source_meta_read(path, (const source_desc_t *)song_fields, SONG_FIELD_COUNT, m, sizeof(*m));
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

XY_IMPL(int, song_get_viewer_zoom, const char *, user)
{
	char *r = song_viewer_pref_read(user, "chords-zoom");
	int v = r ? atoi(r) : VIEWER_ZOOM_DEFAULT;
	free(r);
	return (v < VIEWER_ZOOM_MIN || v > VIEWER_ZOOM_MAX)
	               ? VIEWER_ZOOM_DEFAULT
	               : v;
}

XY_IMPL(int, song_set_viewer_zoom, const char *, user, int, zoom)
{
	char b[16];
	snprintf(b, sizeof(b), "%d", zoom);
	return song_viewer_pref_write(user, "chords-zoom", b);
}

XY_IMPL(int, song_transpose_root,
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

static void song_load_saved_prefs(const char *user, int *f, int *m)
{
	if (!user || !user[0])
		return;
	char *rl = song_viewer_pref_read(user, "chords-latin");
	char *rm = song_viewer_pref_read(user, "chords-media");
	if (rl && atoi(rl))
		*f |= TRANSP_LATIN;
	if (rm && atoi(rm))
		*m = 1;
	free(rl);
	free(rm);
}

static void song_parse_prefs(
        int fd, const char *username, char *qs, int *t, int *f, int *m,
        int *zoom)
{
	int pf;
	*zoom = 0;
	parse_transpose_qs(qs, t, &pf, m);
	if (pf & TPARAM_LATIN)
		*f |= TRANSP_LATIN;
	if (pf & TPARAM_HTML)
		*f |= TRANSP_HTML;

	if (qs[0] && username && username[0]) {
		char pv[2];
		pv[0] = (*f & TRANSP_LATIN) ? '1' : '0';
		pv[1] = '\0';
		song_viewer_pref_write(username, "chords-latin", pv);
		pv[0] = (*m) ? '1' : '0';
		song_viewer_pref_write(username, "chords-media", pv);
	}
	if (qs[0]) {
		char zb[16];
		if (axil_query_param("z", zb, sizeof(zb)) > 0) {
			*zoom = atoi(zb);
			if (*zoom < VIEWER_ZOOM_MIN)
				*zoom = VIEWER_ZOOM_MIN;
			if (*zoom > VIEWER_ZOOM_MAX)
				*zoom = VIEWER_ZOOM_MAX;
			if (username && username[0])
				song_set_viewer_zoom(username, *zoom);
		}
	}

	if (!qs[0])
		song_load_saved_prefs(username, f, m);
}

static int api_song_viewer_prefs_handler(int fd, char *body)
{
	const char *u = get_request_user(fd);
	if (!u || !u[0]) {
		axil_respond(fd, 204, "");
		return 0;
	}
	char method[16] = { 0 };
	axil_env_get(fd, method, sizeof(method), "REQUEST_METHOD");
	if (strcmp(method, "GET") == 0) {
		char qs[1024] = { 0 };
		axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");
		axil_query_parse(qs);
	} else {
		axil_query_parse(body);
	}
	char b[16];
	if (axil_query_param("v", b, sizeof(b)) >= 0)
		song_set_viewer_zoom(u, atoi(b));
	if (axil_query_param("b", b, sizeof(b)) >= 0)
		song_viewer_pref_write(u, "chords-bemol", b);
	if (axil_query_param("l", b, sizeof(b)) >= 0)
		song_viewer_pref_write(u, "chords-latin", b);
	if (axil_query_param("m", b, sizeof(b)) >= 0)
		song_viewer_pref_write(u, "chords-media", b);
	if (axil_query_param("z", b, sizeof(b)) >= 0)
		song_set_viewer_zoom(u, atoi(b));
	axil_respond(fd, 204, "");
	return 0;
}

/* ── State specs (computed once at init) ─────────────────────── */

static source_state_field_t song_state_specs[SONG_FIELD_COUNT + 1];

struct song_custom_overlay {
	char *trans;
};

static void song_custom_overlay_fn(struct json_object *jo, void *user_data)
{
	struct song_custom_overlay *data =
	        (struct song_custom_overlay *)user_data;
	json_object_object_add(
	        jo, "data",
	        json_object_new_string(data->trans ? data->trans : ""));
}

static int

song_details_auth(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;
	int t = 0, f = 0, m = 0, v_z = 0;
	char qs[1024] = { 0 };
	char *trans = NULL;
	int k = 0;
	axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");
	song_parse_prefs(fd, ctx->username, qs, &t, &f, &m, &v_z);

	if (v_z == 0) {
		if (ctx->username && ctx->username[0])
			v_z = song_get_viewer_zoom(ctx->username);
		else
			v_z = VIEWER_ZOOM_DEFAULT;
	}

	song_transpose_root(ctx->doc_root, ctx->id, t, f, &trans, &k);

	struct song_custom_overlay custom_data = { trans };

	app_state_t tmp;
	memset(&tmp, 0, sizeof(tmp));
	tmp.is_owner = (ctx->username && ctx->username[0])
	                       ? item_owner_check(ctx->item_path, ctx->username)
	                       : 0;
	tmp.transpose = t;
	tmp.zoom = v_z;
	tmp.use_latin = (f & TRANSP_LATIN) != 0;
	tmp.show_media = m;
	tmp.original_key = k;

	int rc = source_respond_page_state(
	        fd, "song.items", ctx->id, song_state_specs, &tmp, (const source_desc_t *)song_fields,
	        song_custom_overlay_fn, &custom_data);

	free(trans);
	return rc;
}

static int api_song_transpose_handler(int fd, char *body)
{
	return with_module_item_access(
	        fd, body, "song", 0, NULL, NULL, song_details_auth, NULL);
}

XY_IMPL(int, song_get_original_key_root,
	const char *, doc,
	const char *, id)
{
	int k = 0;
	char *o = NULL;
	song_transpose_root(doc, id, 0, 0, &o, &k);
	free(o);
	return k;
}

XY_IMPL(int, song_get_original_key, const char *, id)
{
	return song_get_original_key_root(g_doc_root, id);
}

XY_IMPL(char *, song_get_pref, const char *, user, const char *, name)
{
	return song_viewer_pref_read(user, name);
}

static const char *derive_song_lyrics(const void *ctx, const char *row_id, const char *field_name, void *user)
{
	(void)ctx; (void)field_name; (void)user;

	char path[PATH_MAX];
	item_path_build_root(g_doc_root, "song", row_id, path, sizeof(path));
	item_child_path(path, "lyrics.txt", path, sizeof(path));

	static __thread char buf[65536];
	char *c = slurp_file(path);
	if (!c) {
		buf[0] = '\0';
		return buf;
	}
	strncpy(buf, c, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	free(c);
	return buf;
}

XY_IMPL(int, source_after_update,
        int, fd,
        const char *, dataset_id,
        const char *, id,
        unsigned, data_handle)
{
	if (!dataset_id || strcmp(dataset_id, "song.items") != 0)
		return 0;
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

		transp_song_t song = {0};
		int key = -1;
		if (transp_song_parse(data, &song, &key) == 0) {
			size_t cap = strlen(data) + 1;
			char *lyrics = malloc(cap);
			if (lyrics) {
				size_t pos = 0;
				for (size_t i = 0; i < song.nlines; i++) {
					transp_pline_t *pl = &song.lines[i];
					if (!pl->is_chord_line && pl->len > 0) {
						if (pos + pl->len + 1 < cap) {
							memcpy(lyrics + pos, pl->text, pl->len);
							pos += pl->len;
							lyrics[pos++] = '\n';
						}
					}
				}
				if (pos > 0) pos--;
				lyrics[pos] = '\0';

				char lyrics_path[PATH_MAX];
				item_child_path(path, "lyrics.txt", lyrics_path, sizeof(lyrics_path));
				write_file_path(lyrics_path, lyrics, pos);
				free(lyrics);
			}
			transp_song_free(&song);
		}
	}

	return 0;
}

#include "ux/detail.c"
#include "ux/form.c"

/* ── HTTP handlers ────────────────────────────────────────────── */

static int
song_detail_auth(int fd, char *body, const item_ctx_t *ctx, void *user_data)
{
	(void)body;
	(void)user_data;

	int is_owner = item_owner_check(ctx->item_path, ctx->username);

	int t = 0, f = 0, m = 0, zoom = 0;
	char qs[1024] = { 0 };
	char *trans = NULL;
	int k = 0;
	json_object *jo = NULL;
	axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");
	song_parse_prefs(fd, ctx->username, qs, &t, &f, &m, &zoom);

	f |= TRANSP_HTML;

	song_transpose_root(g_doc_root, ctx->id, t, f, &trans, &k);

	source_build_state_json("song.items", ctx->id, song_state_specs, &jo);
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

	memset(&app_state, 0, sizeof(app_state));
	snprintf(app_state.cache.id, sizeof(app_state.cache.id), "%s", ctx->id);
	app_state.transpose = t;
	app_state.use_latin = (f & TRANSP_LATIN) != 0;
	app_state.show_media = m;
	app_state.original_key = k;
	app_state.is_owner = is_owner;
	if (zoom)
		app_state.zoom = zoom;
	else
		app_state.zoom = song_get_viewer_zoom(ctx->username);
	snprintf(
	        app_state.chord_html, sizeof(app_state.chord_html), "%s",
	        trans ? trans : "");
	if (ctx->username && ctx->username[0]) {
		snprintf(
		        app_state.page_user, sizeof(app_state.page_user), "%s",
		        ctx->username);
		snprintf(
		        app_state.save_url, sizeof(app_state.save_url),
		        "/api/song/prefs");
	}
	snprintf(app_state.path, sizeof(app_state.path), "/song/%s", ctx->id);

	source_overlay_from_desc(
	        jo, &app_state, (const source_desc_t *)song_fields, BUD_OVERLAY_INT, BUD_OVERLAY_STR);

	bud_state_apply(
	        &app_state, song_fields, json_object_to_json_string(jo));

	bud_node *layout = bud_app_render();
	{
		char state_buf[16384];
		snprintf(
		        state_buf, sizeof(state_buf),
		        "<script type=\"application/json\" "
		        "id=\"bud-state\">%s</script>",
		        json_object_to_json_string(jo));
		json_object_put(jo);

		free(trans);
		return site_ui_respond_page(
		        fd, app_state.cache.title, app_state.path,
		        site_ui_module_icon("song"), app_state.page_user,
		        state_buf, "song_detail", layout);
	}
}

static int song_detail_handler(int fd, char *body)
{
	return with_module_item_access(
	        fd, body, "song", 0, "Song not found", NULL, song_detail_auth,
	        NULL);
}

static int song_edit_auth(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;
	song_cache_t meta;

	song_meta_read(ctx->item_path, &meta);
	source_resolve_meta_display(
	        "song.items", ctx->id, (const source_desc_t *)song_fields, SONG_FIELD_COUNT, &meta);

	char data_path[PATH_MAX];
	item_child_path(
	        ctx->item_path, "data.txt", data_path, sizeof(data_path));
	char *data_val = slurp_file(data_path);

	const char *csrf_token = csrf_setup(fd);

	bud_node *form =
	        song_form_content(1, ctx->id, &meta, data_val, csrf_token);
	free(data_val);

	return site_ui_respond_edit_page(
	        fd, ctx->username, "song", site_ui_module_icon("song"),
	        meta.title, ctx->id, form);
}

static int song_edit_get_handler(int fd, char *body)
{
	return with_module_item_access(
	        fd, body, "song", ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	        "Song not found", NULL, song_edit_auth, NULL);
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
	        fd, user, "song", site_ui_module_icon("song"), form);
}

void xy_install(void)
{
	char dr[256] = { 0 };
	srand((unsigned)time(NULL));
	resolve_doc_root(0, dr, sizeof(dr));
	strncpy(g_doc_root, dr, sizeof(g_doc_root) - 1);
	g_transp_ctx = transp_init();
	xy_load("./mods/index/index");
	xy_load("./mods/mpfd/mpfd");

	/* Precompute state specs from field table */
	source_build_state_specs(
	        (const source_desc_t *)song_fields, song_state_specs, SONG_FIELD_COUNT);

	source_setup(
	        "song.types", "name", sizeof(song_type_cache_t),
	        "var/song.types", (const source_desc_t *)song_type_fields, SONG_TYPE_FIELD_COUNT,
	        SOURCE_FLAG_VOLATILE, NULL);

	ref_field_register("song.items", "type");

	hyle_register_derive("song.lyrics_from_data", derive_song_lyrics, NULL);

	source_setup(
	        "song.items", NULL, sizeof(song_cache_t), "var/song",
	        (const source_desc_t *)song_fields, SONG_FIELD_COUNT, 0, &song_list_view);

	index_open("Song", "song.items", NULL, NULL, NULL, NULL, NULL, NULL);

	standard_item_handlers_t handlers = {
		.detail = song_detail_handler,
		.add_get = song_add_get_handler,
		.edit_get = song_edit_get_handler,
	};
	register_standard_item_handlers("song", &handlers);
	axil_register_handler(
	        "GET:/api/song/:id/transpose", api_song_transpose_handler);
	axil_register_handler(
	        "GET:/api/song/prefs", api_song_viewer_prefs_handler);
	axil_register_handler(
	        "POST:/api/song/prefs", api_song_viewer_prefs_handler);
}
