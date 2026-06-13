#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

#include <ttypt/axil.h>
#include <ttypt/ndx-mod.h>
#include <ttypt/ndx.h>
#include <ttypt/qmap.h>

#include "../index/index.h"
#include "../common/common.h"
#include "../source/source.h"

#include "../auth/auth.h"

#include "../song/song.h"
#include "fields.h"

#define CHOIR_SONGS_PATH "items/choir/items"
static source_field_t choir_repertoire_fields_buf[CHOIR_REPERTOIRE_FIELD_COUNT];

static int choir_repertoire_entry_id(
        const char *choir_id,
        const char *song_id,
        char *entry_id,
        size_t entry_id_sz)
{
	return snprintf(entry_id, entry_id_sz, "%s_%s", choir_id, song_id);
}

static int handle_choir_song_add_auth(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	char s_id[128] = { 0 };
	if (csrf_check_query(fd, body))
		return 1;
	int s_len = axil_query_param("song_id", s_id, sizeof(s_id) - 1);
	if (s_len <= 0)
		return bad_request(fd, "Missing song_id");
	datalist_extract_id(s_id, s_id, sizeof(s_id));

	char entry_id[64];
	choir_repertoire_entry_id(ctx->id, s_id, entry_id, sizeof(entry_id));
	fprintf(stderr,
	        "DBG: add song: choir_id=%s song_id=%s entry_id=%s\n",
	        ctx->id,
	        s_id,
	        entry_id);

	unsigned dh = source_parse_form("choir.repertoire");
	if (dh == 0)
		return server_error(fd, "OOM");
	qmap_put(dh, "song", s_id);
	qmap_put(dh, "transpose", "0");
	qmap_put(dh, "choir", ctx->id);
	if (!qmap_get(dh, "format"))
		qmap_put(dh, "format", "any");
	int rc = source_update_item(fd, "choir.repertoire", entry_id, dh);
	qmap_close(dh);
	if (rc == -1)
		return server_error(fd, "Failed to add song to repertoire");
	if (rc != 0)
		return 1;

	return redirect_to_item(fd, "choir", ctx->id);
}

static int handle_choir_song_add(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        CHOIR_SONGS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	        NULL,
	        NULL,
	        handle_choir_song_add_auth,
	        NULL);
}

static int handle_choir_song_key_auth(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	char k_s[32] = { 0 };
	if (csrf_check_query(fd, body))
		return 1;
	axil_query_param("key", k_s, sizeof(k_s) - 1);

	/* Find the repertoire entry for (choir_id, song_id) */
	source_def_t *repo_def = source_find("choir.repertoire");
	if (!repo_def || !repo_def->fields_hd)
		return server_error(fd, "Repertoire source not found");

	char entry_id[64];
	choir_repertoire_entry_id(
	        ctx->id, ctx->song_id, entry_id, sizeof(entry_id));
	if (!qmap_get(repo_def->source_hd, entry_id))
		return redirect_to_item(fd, "choir", ctx->id);

	unsigned dh = qmap_open(NULL, "row_data", QM_STR, QM_STR, 0x1F, 0);
	qmap_put(dh, "transpose", k_s);
	source_update_item(fd, "choir.repertoire", entry_id, dh);
	qmap_close(dh);

	return redirect_to_item(fd, "choir", ctx->id);
}

static int handle_choir_song_key(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        CHOIR_SONGS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP | ICTX_SONG_ID,
	        NULL,
	        NULL,
	        handle_choir_song_key_auth,
	        NULL);
}

static int handle_choir_song_del_auth(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	if (csrf_check_query(fd, body))
		return 1;

	/* Find the repertoire entry for (choir_id, song_id) */
	source_def_t *repo_def = source_find("choir.repertoire");
	if (!repo_def || !repo_def->fields_hd)
		return server_error(fd, "Repertoire source not found");

	char entry_id[64];
	choir_repertoire_entry_id(
	        ctx->id, ctx->song_id, entry_id, sizeof(entry_id));
	if (!qmap_get(repo_def->source_hd, entry_id))
		return redirect_to_item(fd, "choir", ctx->id);

	source_delete_item(fd, repo_def, entry_id);

	return redirect_to_item(fd, "choir", ctx->id);
}

static int handle_choir_song_delete(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        CHOIR_SONGS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP | ICTX_SONG_ID,
	        NULL,
	        NULL,
	        handle_choir_song_del_auth,
	        NULL);
}

static int handle_choir_song_view_auth(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;
	int pk = 0;

	source_def_t *repo_def = source_find("choir.repertoire");
	if (repo_def && repo_def->fields_hd) {
		char entry_id[64];
		choir_repertoire_entry_id(
		        ctx->id, ctx->song_id, entry_id, sizeof(entry_id));
		const char *tv = qmap_field_get(
		        repo_def->fields_hd, entry_id, "transpose");
		if (tv)
			pk = atoi(tv);
	}

	int t = 0;
	if (pk != 0)
		t = pk -
		    song_get_original_key_root(ctx->doc_root, ctx->song_id);
	char loc[512];
	snprintf(loc, sizeof(loc), "/song/%s?t=%d", ctx->song_id, t);
	return axil_redirect(fd, loc);
}

static int handle_choir_song_view(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        CHOIR_SONGS_PATH,
	        ICTX_SONG_ID,
	        NULL,
	        NULL,
	        handle_choir_song_view_auth,
	        NULL);
}

#include "ux/all.c"

static void ch_load_songbooks(source_def_t *sb_def, uint32_t choir_pos)
{
	g_ch_n_songbooks = 0;
	uint32_t inv_buf[256];
	size_t n = qmap_inv_get(
	        sb_def->fields_hd, "choir", choir_pos, inv_buf, 256);
	for (size_t i = 0; i < n && g_ch_n_songbooks < CH_MAX_SONGBOOKS; i++) {
		const char *sb_id = qmap_get_key(sb_def->fields_hd, inv_buf[i]);
		if (!sb_id)
			continue;
		const char *t =
		        qmap_get_field_str(sb_def->fields_hd, sb_id, "title");
		if (!t)
			t = sb_id;
		ch_sb_entry_t *e = &g_ch_songbooks[g_ch_n_songbooks++];
		snprintf(e->title, sizeof(e->title), "%s", t);
		snprintf(e->href, sizeof(e->href), "/songbook/%s", sb_id);
	}
}

static void ch_load_repertoire(
        source_def_t *repo_def,
        uint32_t choir_pos,
        unsigned sf_hd,
        const char *choir_id)
{
	g_ch_n_repertoire = 0;
	uint32_t inv_buf[256];
	size_t n = qmap_inv_get(
	        repo_def->fields_hd, "choir", choir_pos, inv_buf, 256);
	for (size_t i = 0; i < n && g_ch_n_repertoire < CH_MAX_REP_SONGS; i++) {
		const char *rk = qmap_get_key(repo_def->fields_hd, inv_buf[i]);
		if (!rk)
			continue;

		const char *sr =
		        qmap_field_get(repo_def->fields_hd, rk, "song");
		const char *ts =
		        qmap_field_get(repo_def->fields_hd, rk, "transpose");
		const char *fm =
		        qmap_field_get(repo_def->fields_hd, rk, "format");
		if (!sr)
			continue;
		if (!fm)
			fm = "any";

		int tp = ts ? atoi(ts) : 0;

		const char *st = sr;
		if (sf_hd) {
			const char *s = qmap_get_field_str(sf_hd, sr, "title");
			if (s)
				st = s;
		}

		int ok = song_get_original_key(sr);
		const char *tg = target_key_name(ok, tp, 0);

		ch_rep_entry_t *e = &g_ch_repertoire[g_ch_n_repertoire++];
		snprintf(e->title, sizeof(e->title), "%s", st);
		snprintf(
		        e->song_href,
		        sizeof(e->song_href),
		        "/choir/%s/song/%s",
		        choir_id,
		        sr);
		snprintf(
		        e->key_label,
		        sizeof(e->key_label),
		        "%s \xe2\x80\xa2 Key: %s",
		        fm,
		        tg);
		e->orig_key = ok;
		e->transpose = tp;
		snprintf(
		        e->key_action,
		        sizeof(e->key_action),
		        "/api/choir/%s/song/%s/key",
		        choir_id,
		        sr);
		snprintf(
		        e->rem_action,
		        sizeof(e->rem_action),
		        "/api/choir/%s/song/%s/remove",
		        choir_id,
		        sr);
	}
}

static void ch_load_options(unsigned s_data_hd, unsigned sf_hd)
{
	g_ch_n_options = 0;
	if (!s_data_hd)
		return;
	uint32_t cur = qmap_iter(s_data_hd, NULL, 0);
	const void *k, *v;
	while (qmap_next(&k, &v, cur) && g_ch_n_options < CH_MAX_OPT_SONGS) {
		const char *si = (const char *)k;
		const char *st = qmap_get_field_str(sf_hd, si, "title");
		if (!st)
			st = si;
		ch_opt_entry_t *o = &g_ch_options[g_ch_n_options++];
		snprintf(o->id, sizeof(o->id), "%s", si);
		snprintf(o->title, sizeof(o->title), "%s", st);
	}
	qmap_fin(cur);
}

/* ── HTTP handlers ──────────────────────────────────────── */

static int choir_add_get_handler(int fd, char *body)
{
	(void)body;
	const char *user = require_user(fd);
	if (!user)
		return 1;

	const char *csrf_token = csrf_setup(fd);

	bud_node *form = ch_render_add_form(csrf_token);
	return site_ui_respond_add_page(
	        fd, user, "choir", "\xf0\x9f\x8e\xb6", form);
}

/* ── Edit GET handler ────────────────────────────────────── */

static int choir_edit_get_handler(int fd, char *body)
{
	(void)body;
	char id[128] = { 0 };
	const char *user;
	char item_path_buf[512];
	unsigned fields_hd;
	const char *title, *format;

	if (check_item_access(
	            fd,
	            "choir",
	            id,
	            sizeof(id),
	            &user,
	            item_path_buf,
	            sizeof(item_path_buf)))
		return 1;

	fields_hd = source_get_fields_hd("choir.items");
	if (!fields_hd)
		return server_error(fd, "No fields_hd");

	title = qmap_get_field_str(fields_hd, id, "title");
	if (!title)
		title = "";

	format = qmap_get_field_str(fields_hd, id, "format");
	if (!format)
		format = "";

	const char *csrf_token = csrf_setup(fd);

	char action[256];
	char cancel_href[256];
	snprintf(action, sizeof(action), "/choir/%s/edit", id);
	snprintf(cancel_href, sizeof(cancel_href), "/choir/%s", id);

	bud_node *form = ch_render_edit_form(
	        action, csrf_token, title, format, cancel_href);

	return site_ui_respond_edit_page(
	        fd, user, "choir", "\xf0\x9f\x8e\xb6", title, id, form);
}

/* ── Detail handler ──────────────────────────────────────── */

static int choir_detail_handler(int fd, char *body)
{
	(void)body;
	char id[128] = { 0 };
	const char *user = get_request_user(fd);
	unsigned cf_hd, sf_hd;
	const char *title, *owner;
	char page_title[256];
	bud_node *layout;
	char *html;
	int is_owner = 0;
	const char *csrf_token = csrf_setup(fd);

	axil_env_get(fd, id, "PATTERN_PARAM_ID");
	if (!id[0])
		return bad_request(fd, "Missing ID");

	cf_hd = source_get_fields_hd("choir.items");
	if (!cf_hd)
		return server_error(fd, "No fields_hd");

	title = qmap_get_field_str(cf_hd, id, "title");
	if (!title)
		return respond_error(fd, 404, "Choir not found");

	owner = qmap_get_field_str(cf_hd, id, "owner");
	if (!owner)
		owner = "";

	is_owner = (user && user[0] && strcmp(user, owner) == 0);

	snprintf(page_title, sizeof(page_title), "choir: %s", title);

	/* ── Build body content ────────────────────────────── */

	bud_node *body_frag = bud_fragment();
	{
		bud_node *header = ch_render_detail_header(title, owner);
		if (header)
			bud_append(body_frag, header);
	}

	uint32_t choir_pos = qmap_pos(cf_hd, id);
	if (choir_pos != QM_MISS) {
		source_def_t *sb_def = source_find("songbook.items");
		if (sb_def && sb_def->fields_hd) {
			ch_load_songbooks(sb_def, choir_pos);
			bud_append(body_frag, ch_render_songbooks_section());
		}
	}

	if (choir_pos != QM_MISS) {
		source_def_t *repo_def = source_find("choir.repertoire");
		if (repo_def && repo_def->fields_hd) {
			sf_hd = source_get_fields_hd("song.items");
			ch_load_repertoire(repo_def, choir_pos, sf_hd, id);
			bud_append(
			        body_frag,
			        ch_render_repertoire_section(
			                is_owner, csrf_token));
		}
	}

	if (is_owner) {
		ch_load_options(source_get_data_hd("song.items"), sf_hd);
		bud_append(
		        body_frag, ch_render_add_song_section(id, csrf_token));
	}

	/* ── Assemble page ──────────────────────────────────── */

	layout = site_ui_layout(
	        page_title,
	        page_title,
	        "\xf0\x9f\x8e\xb6",
	        user,
	        site_ui_item_menu("choir", id, is_owner),
	        body_frag);

	return site_ui_respond_page(fd, page_title, NULL, "choir", layout);
}

void ndx_install(void)
{
	ndx_load("./mods/index/index");
	ndx_load("./mods/mpfd/mpfd");
	ndx_load("./mods/song/song");
	axil_register_handler(
	        "GET:/choir/:id/song/:song_id", handle_choir_song_view);
	axil_register_handler(
	        "POST:/api/choir/:id/songs", handle_choir_song_add);
	axil_register_handler(
	        "POST:/api/choir/:id/song/:song_id/key", handle_choir_song_key);
	axil_register_handler(
	        "DELETE:/api/choir/:id/song/:song_id",
	        handle_choir_song_delete);
	axil_register_handler(
	        "POST:/api/choir/:id/song/:song_id/remove",
	        handle_choir_song_delete);

	source_setup(
	        "choir.items",
	        NULL,
	        sizeof(choir_cache_t),
	        "items/choir/items",
	        choir_fields,
	        CHOIR_FIELD_COUNT,
	        0);
	source_setup(
	        "choir.repertoire",
	        NULL,
	        sizeof(choir_repertoire_cache_t),
	        "items/choir/repertoire",
	        choir_repertoire_fields,
	        CHOIR_REPERTOIRE_FIELD_COUNT,
	        0);

	index_open(
	        "Choir",
	        "choir.items",
	        NULL,
	        choir_detail_handler,
	        NULL,
	        choir_edit_get_handler,
	        NULL);
	axil_register_handler("GET:/choir/add", choir_add_get_handler);

	/* Ensure repertoire storage directory exists */
	{
		char doc_root[256] = { 0 };
		const char *root =
		        resolve_doc_root(0, doc_root, sizeof(doc_root));
		char rep_path[PATH_MAX];
		snprintf(
		        rep_path,
		        sizeof(rep_path),
		        "%s/items/choir/repertoire",
		        root);
		if (mkdir(rep_path, 0755) != 0 && errno != EEXIST)
			fprintf(stderr,
			        "WARN: could not create repertoire dir %s: "
			        "%s\n",
			        rep_path,
			        strerror(errno));
	}
}
