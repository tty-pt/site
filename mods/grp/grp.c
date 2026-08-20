#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

#include <ttypt/axil.h>
#include <ttypt/xy-mod.h>
#include <ttypt/xy.h>
#include <ttypt/qmap.h>

#include "../index/index.h"
#include <hyle/source.h>
#include "../common/common.h"
#include "../source/source.h"

#include "../auth/auth.h"

#include "../song/song.h"
#include "fields.h"

#define GRP_SONGS_PATH "var/grp"

static char g_doc_root[256] = ".";

static int grp_song_index(const char *grp_id, const char *song_id)
{
	int total = hyle_source_ordered_count("grp.songs", grp_id);
	unsigned fhd = hyle_source_get_fields_hd("grp.songs");
	if (!fhd)
		return -1;
	for (int i = 0; i < total; i++) {
		const char *key =
		        hyle_source_ordered_key_at("grp.songs", grp_id, i);
		if (!key)
			continue;
		const char *sid = qmap_field_get(fhd, key, "song");
		if (sid && strcmp(sid, song_id) == 0)
			return i;
	}
	return -1;
}

static int handle_grp_song_add_auth(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	char s_id[128] = { 0 };
	int s_len = axil_query_param("song_id", s_id, sizeof(s_id) - 1);
	if (s_len <= 0)
		return bad_request(fd, "Missing song_id");
	datalist_extract_id(s_id, s_id, sizeof(s_id));

	char fmt[64] = "any";
	char tr[16] = "0";
	axil_query_param("format", fmt, sizeof(fmt) - 1);
	axil_query_param("transpose", tr, sizeof(tr) - 1);
	if (!fmt[0])
		snprintf(fmt, sizeof(fmt), "any");
	if (!tr[0])
		snprintf(tr, sizeof(tr), "0");
	const char *names[] = { "song", "transpose", "format" };
	const char *vals[] = { s_id, tr, fmt };
	hyle_source_ordered_append("grp.songs", ctx->id, names, vals, 3);
	hyle_source_ordered_save("grp.songs", ctx->id);

	return redirect_to_item(fd, "grp", ctx->id);
}

static int handle_grp_song_add(int fd, char *body)
{
	return with_item_access(
	        fd, body, GRP_SONGS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP | ICTX_CSRF_QUERY, NULL,
	        NULL, handle_grp_song_add_auth, NULL);
}

static int handle_grp_song_key_auth(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	char k_s[32] = { 0 };
	axil_query_param("key", k_s, sizeof(k_s) - 1);

	int idx = grp_song_index(ctx->id, ctx->song_id);
	if (idx >= 0) {
		const char *key =
		        hyle_source_ordered_key_at("grp.songs", ctx->id, idx);
		const char *names[] = { "transpose" };
		const char *vals[] = { k_s };
		hyle_source_put("grp.songs", key, names, vals, 1);
		hyle_source_ordered_save("grp.songs", ctx->id);
	}

	return redirect_to_item(fd, "grp", ctx->id);
}

static int handle_grp_song_key(int fd, char *body)
{
	return with_item_access(
	        fd, body, GRP_SONGS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP | ICTX_SONG_ID |
	                ICTX_CSRF_QUERY,
	        NULL, NULL, handle_grp_song_key_auth, NULL);
}

static int handle_grp_song_del_auth(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;

	int idx = grp_song_index(ctx->id, ctx->song_id);
	if (idx >= 0) {
		hyle_source_ordered_remove_at("grp.songs", ctx->id, idx);
		hyle_source_ordered_save("grp.songs", ctx->id);
	}

	return redirect_to_item(fd, "grp", ctx->id);
}

static int handle_grp_song_delete(int fd, char *body)
{
	return with_item_access(
	        fd, body, GRP_SONGS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP | ICTX_SONG_ID |
	                ICTX_CSRF_QUERY,
	        NULL, NULL, handle_grp_song_del_auth, NULL);
}

static int handle_grp_song_view_auth(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;
	int pk = 0;

	int idx = grp_song_index(ctx->id, ctx->song_id);
	if (idx >= 0) {
		unsigned fhd = hyle_source_get_fields_hd("grp.songs");
		const char *key =
		        hyle_source_ordered_key_at("grp.songs", ctx->id, idx);
		const char *tv = qmap_field_get(fhd, key, "transpose");
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

static int handle_grp_song_view(int fd, char *body)
{
	return with_item_access(
	        fd, body, GRP_SONGS_PATH, ICTX_SONG_ID, NULL, NULL,
	        handle_grp_song_view_auth, NULL);
}

#include "ux/all.c"

static void ch_load_gigs(
        source_def_t *sb_def, uint32_t grp_pos, ch_sb_entry_t *gigs,
        int *n_gigs)
{
	*n_gigs = 0;
	uint32_t inv_buf[256];
	size_t n = qmap_inv_get(
	        sb_def->fields_hd, "grp", grp_pos, inv_buf, 256);
	for (size_t i = 0; i < n && *n_gigs < CH_MAX_GIGS; i++) {
		const char *sb_id = qmap_get_key(sb_def->fields_hd, inv_buf[i]);
		if (!sb_id)
			continue;
		const char *t =
		        qmap_get_field_str(sb_def->fields_hd, sb_id, "title");
		if (!t)
			t = sb_id;
		ch_sb_entry_t *e = &gigs[(*n_gigs)++];
		snprintf(e->title, sizeof(e->title), "%s", t);
		snprintf(e->href, sizeof(e->href), "/gig/%s", sb_id);
	}
}

static void ch_load_repertoire(
        const char *grp_id, unsigned sf_hd, ch_rep_entry_t *repertoire,
        int *n_repertoire)
{
	*n_repertoire = 0;
	int total = hyle_source_ordered_count("grp.songs", grp_id);
	unsigned fhd = hyle_source_get_fields_hd("grp.songs");
	if (!fhd)
		return;

	for (int i = 0; i < total && *n_repertoire < CH_MAX_REP_SONGS; i++) {
		const char *rk =
		        hyle_source_ordered_key_at("grp.songs", grp_id, i);
		if (!rk)
			continue;

		const char *sr = qmap_field_get(fhd, rk, "song");
		const char *ts = qmap_field_get(fhd, rk, "transpose");
		const char *fm = qmap_field_get(fhd, rk, "format");
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

		ch_rep_entry_t *e = &repertoire[(*n_repertoire)++];
		snprintf(e->title, sizeof(e->title), "%s", st);
		snprintf(
		        e->song_href, sizeof(e->song_href), "/grp/%s/song/%s",
		        grp_id, sr);
		snprintf(
		        e->key_label, sizeof(e->key_label),
		        "%s \xe2\x80\xa2 Key: %s", fm, tg);
		e->orig_key = ok;
		e->transpose = tp;
		snprintf(
		        e->key_action, sizeof(e->key_action),
		        "/api/grp/%s/song/%s/key", grp_id, sr);
		snprintf(
		        e->rem_action, sizeof(e->rem_action),
		        "/api/grp/%s/song/%s/remove", grp_id, sr);
	}
}

static void ch_load_options(
        unsigned s_data_hd, unsigned sf_hd, ch_opt_entry_t *options,
        int *n_options)
{
	*n_options = 0;
	if (!s_data_hd)
		return;
	uint32_t cur = qmap_iter(s_data_hd, NULL, 0);
	const void *k, *v;
	while (qmap_next(&k, &v, cur) && *n_options < CH_MAX_OPT_SONGS) {
		const char *si = (const char *)k;
		const char *st = qmap_get_field_str(sf_hd, si, "title");
		if (!st)
			st = si;
		ch_opt_entry_t *o = &options[(*n_options)++];
		snprintf(o->id, sizeof(o->id), "%s", si);
		snprintf(o->title, sizeof(o->title), "%s", st);
	}
	qmap_fin(cur);
}

/* ── HTTP handlers ──────────────────────────────────────── */

static int grp_add_get_handler(int fd, char *body)
{
	(void)body;
	const char *user = require_user(fd);
	if (!user)
		return 1;

	const char *csrf_token = csrf_setup(fd);

	bud_node *form = ch_render_add_form(csrf_token);
	return site_ui_respond_add_page(
	        fd, user, "grp", site_ui_module_icon("grp"), form);
}

/* ── Edit GET handler ────────────────────────────────────── */

static int
grp_edit_auth(int fd, char *body, const item_ctx_t *ctx, void *user_data)
{
	(void)body;
	(void)user_data;
	unsigned fields_hd;
	const char *title, *format;

	fields_hd = source_get_fields_hd("grp.items");
	if (!fields_hd)
		return server_error(fd, "No fields_hd");

	title = qmap_get_field_str(fields_hd, ctx->id, "title");
	if (!title)
		title = "";

	format = qmap_get_field_str(fields_hd, ctx->id, "format");
	if (!format)
		format = "";

	const char *csrf_token = csrf_setup(fd);

	char action[256];
	char cancel_href[256];
	snprintf(action, sizeof(action), "/grp/%s/edit", ctx->id);
	snprintf(cancel_href, sizeof(cancel_href), "/grp/%s", ctx->id);

	bud_node *form = ch_render_edit_form(
	        action, csrf_token, title, format, cancel_href);

	return site_ui_respond_edit_page(
	        fd, ctx->username, "grp", site_ui_module_icon("grp"),
	        title, ctx->id,
	        form);
}

static int grp_edit_get_handler(int fd, char *body)
{
	return with_item_access(
	        fd, body, "var/grp",
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP, NULL, NULL,
	        grp_edit_auth, NULL);
}

/* ── Detail handler ──────────────────────────────────────── */

static int
grp_detail_auth(int fd, char *body, const item_ctx_t *ctx, void *user_data)
{
	(void)body;
	(void)user_data;
	unsigned cf_hd, sf_hd;
	const char *title, *owner;
	char page_title[256];
	char path[256];
	bud_node *layout;
	int is_owner = 0;
	const char *csrf_token = csrf_setup(fd);

	cf_hd = source_get_fields_hd("grp.items");
	if (!cf_hd)
		return server_error(fd, "No fields_hd");

	title = qmap_get_field_str(cf_hd, ctx->id, "title");
	if (!title)
		return respond_error(fd, 404, "Group not found");

	owner = qmap_get_field_str(cf_hd, ctx->id, "owner");
	if (!owner)
		owner = "";

	is_owner =
	        (ctx->username && ctx->username[0] &&
	         strcmp(ctx->username, owner) == 0);

	snprintf(page_title, sizeof(page_title), "group: %s", title);

	/* ── Build body content ────────────────────────────── */

	bud_node *body_frag = bud_fragment();
	{
		bud_node *header = ch_render_detail_header(title, owner);
		if (header)
			bud_append(body_frag, header);
	}

	ch_sb_entry_t gigs[CH_MAX_GIGS];
	int n_gigs;
	ch_rep_entry_t repertoire[CH_MAX_REP_SONGS];
	int n_repertoire;
	ch_opt_entry_t options[CH_MAX_OPT_SONGS];
	int n_options;

	uint32_t grp_pos = qmap_pos(cf_hd, ctx->id);
	if (grp_pos != QM_MISS) {
		source_def_t *sb_def = source_find("gig.items");
		if (sb_def && sb_def->fields_hd) {
			ch_load_gigs(
			        sb_def, grp_pos, gigs, &n_gigs);
			bud_append(
			        body_frag, ch_render_gigs_section(
			                           gigs, n_gigs));
		}
	}

	if (grp_pos != QM_MISS) {
		sf_hd = source_get_fields_hd("song.items");
		if (sf_hd) {
			ch_load_repertoire(
			        ctx->id, sf_hd, repertoire, &n_repertoire);
			bud_append(
			        body_frag, ch_render_repertoire_section(
			                           repertoire, n_repertoire,
			                           is_owner, csrf_token));
		}
	}

	if (is_owner) {
		sf_hd = source_get_fields_hd("song.items");
		ch_load_options(
		        source_get_data_hd("song.items"), sf_hd, options,
		        &n_options);

		bud_append(
		        body_frag,
		        ch_render_add_song_section(
		                options, n_options, ctx->id, csrf_token));
	}

	/* ── Assemble page ──────────────────────────────────── */

	snprintf(path, sizeof(path), "/grp/%s", ctx->id);
	layout = site_ui_layout(
	        page_title, path, site_ui_module_icon("grp"),
	        ctx->username,
	        site_ui_item_menu("grp", ctx->id, is_owner), body_frag);

	return site_ui_respond_page(fd, page_title, NULL, NULL, layout);
}

static int grp_detail_handler(int fd, char *body)
{
	return with_item_access(
	        fd, body, "var/grp", 0, NULL, NULL, grp_detail_auth,
	        NULL);
}

void xy_install(void)
{
	xy_load("./mods/index/index");
	xy_load("./mods/mpfd/mpfd");
	xy_load("./mods/song/song");

	{
		char doc_root[256] = { 0 };
		resolve_doc_root(0, doc_root, sizeof(doc_root));
		strncpy(g_doc_root, doc_root, sizeof(g_doc_root) - 1);
	}
	axil_register_handler(
	        "GET:/grp/:id/song/:song_id", handle_grp_song_view);
	axil_register_handler(
	        "POST:/api/grp/:id/songs", handle_grp_song_add);
	axil_register_handler(
	        "POST:/api/grp/:id/song/:song_id/key", handle_grp_song_key);
	axil_register_handler(
	        "DELETE:/api/grp/:id/song/:song_id",
	        handle_grp_song_delete);
	axil_register_handler(
	        "POST:/api/grp/:id/song/:song_id/remove",
	        handle_grp_song_delete);

	source_setup(
	        "grp.items", NULL, sizeof(grp_cache_t), "var/grp",
	        grp_fields, GRP_FIELD_COUNT, 0);

	/* Register ordered source for grp songs (data.txt persistence) */
	{
		static const hyle_field_t ch_song_fields[] = {
			{ "song", HYLE_FIELD_REFERENCE, 1, "song.items", NULL,
			  1, 0, 0, 0, 0, NULL },
			{ "transpose", HYLE_FIELD_INT, 1, NULL, NULL, 0, 0, 0,
			  0, 0, NULL },
			{ "format", HYLE_FIELD_STRING, 1, NULL, NULL, 1, 0, 0,
			  0, 16, NULL },
		};
		hyle_source_register_ordered(
		        "grp.songs", ch_song_fields, 3, "grp", 0,
		        HYLE_AUTO_RECORD, source_dsv_load, source_dsv_save,
		        g_doc_root);
	}

	index_open("Group", "grp.items", NULL, NULL, NULL, NULL, NULL, "grp");
	standard_item_handlers_t handlers = {
		.detail = grp_detail_handler,
		.add_get = grp_add_get_handler,
		.edit_get = grp_edit_get_handler,
	};
	register_standard_item_handlers("grp", &handlers);
}
