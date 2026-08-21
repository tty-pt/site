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

static char g_doc_root[256] = ".";

/* ── Auto-repertoire (see AUTO-LIST.md) ─────────────────── */

#define REP_MAX_SONGS 512
#define REP_MAX_KEYS 8
#define REP_MAX_GIGS 256

typedef struct {
	char song[128];
	char format[64];
	int keys[REP_MAX_KEYS]; /* distinct transposes, first-seen order */
	int counts[REP_MAX_KEYS];
	int n_keys;
} rep_tally_t;

typedef struct {
	char song[128];
	char format[64];
	int transpose;
	int pinned;
} rep_row_t;

static int rep_tally_find(rep_tally_t *tally, int n_tally, const char *song_id)
{
	for (int i = 0; i < n_tally; i++)
		if (strcmp(tally[i].song, song_id) == 0)
			return i;
	return -1;
}

static void rep_tally_bump(rep_tally_t *t, int transpose)
{
	int ki;
	for (ki = 0; ki < t->n_keys; ki++)
		if (t->keys[ki] == transpose)
			break;
	if (ki < t->n_keys) {
		t->counts[ki]++;
		return;
	}
	if (t->n_keys >= REP_MAX_KEYS)
		return;
	t->keys[t->n_keys] = transpose;
	t->counts[t->n_keys] = 1;
	t->n_keys++;
}

static int rep_row_find(rep_row_t *rows, int n_rows, const char *song_id)
{
	for (int i = 0; i < n_rows; i++)
		if (strcmp(rows[i].song, song_id) == 0)
			return i;
	return -1;
}

XY_IMPL(int, rep_rebuild, const char *, grp_id)
{
	source_def_t *sb_def;
	unsigned rfhd, gfhd;
	rep_tally_t tally[REP_MAX_SONGS];
	rep_row_t cur[REP_MAX_SONGS], want[REP_MAX_SONGS];
	int n_tally = 0, n_cur = 0, n_want = 0;
	uint32_t inv_buf[REP_MAX_GIGS];
	size_t n_inv;
	uint32_t grp_pos;
	int changed;

	if (!grp_id || !grp_id[0])
		return -1;
	sb_def = source_find("gig.items");
	rfhd = hyle_source_get_fields_hd("grp.songs");
	if (!sb_def || !rfhd)
		return -1;
	/* Inverse-index positions are keyed by the grp's own row position
	 * in grp.items (same lookup as ch_load_gigs), NOT by the id inside
	 * gig.items. */
	grp_pos = qmap_pos(hyle_source_get_fields_hd("grp.items"), grp_id);
	if (grp_pos == QM_MISS)
		return -1;

	/* Tally pass: per song across the grp's gigs, count distinct
	 * transposes (first-seen order wins ties) and keep the format. */
	gfhd = hyle_source_get_fields_hd("gig.songs");
	n_inv = qmap_inv_get(
	        sb_def->fields_hd, "grp", grp_pos, inv_buf, REP_MAX_GIGS);
	for (size_t gi = 0; gi < n_inv; gi++) {
		const char *sb_id =
		        qmap_get_key(sb_def->fields_hd, inv_buf[gi]);
		int total;

		if (!sb_id || !gfhd)
			continue;
		total = hyle_source_ordered_count("gig.songs", sb_id);
		for (int i = 0; i < total; i++) {
			const char *k = hyle_source_ordered_key_at(
			        "gig.songs", sb_id, i);
			const char *sid, *ts, *fm;
			rep_tally_t *t;
			int ti;

			if (!k)
				continue;
			sid = qmap_field_get(gfhd, k, "song");
			if (!sid)
				continue;
			ti = rep_tally_find(tally, n_tally, sid);
			if (ti < 0) {
				if (n_tally >= REP_MAX_SONGS)
					return -1;
				ti = n_tally++;
				t = &tally[ti];
				memset(t, 0, sizeof(*t));
				snprintf(t->song, sizeof(t->song), "%s", sid);
				fm = qmap_field_get(gfhd, k, "format");
				snprintf(
				        t->format, sizeof(t->format), "%s",
				        fm && fm[0] ? fm : "any");
			}
			t = &tally[ti];
			ts = qmap_field_get(gfhd, k, "transpose");
			rep_tally_bump(t, ts ? atoi(ts) : 0);
		}
	}

	/* Current partition snapshot */
	{
		int total = hyle_source_ordered_count("grp.songs", grp_id);
		for (int i = 0; i < total; i++) {
			const char *k = hyle_source_ordered_key_at(
			        "grp.songs", grp_id, i);
			const char *sid, *ts, *fm, *pv;
			rep_row_t *r;

			if (!k)
				continue;
			sid = qmap_field_get(rfhd, k, "song");
			if (!sid)
				continue;
			if (n_cur >= REP_MAX_SONGS)
				return -1;
			r = &cur[n_cur++];
			memset(r, 0, sizeof(*r));
			snprintf(r->song, sizeof(r->song), "%s", sid);
			ts = qmap_field_get(rfhd, k, "transpose");
			r->transpose = ts ? atoi(ts) : 0;
			fm = qmap_field_get(rfhd, k, "format");
			snprintf(
			        r->format, sizeof(r->format), "%s",
			        fm && fm[0] ? fm : "any");
			pv = qmap_field_get(rfhd, k, "pinned");
			r->pinned = pv ? atoi(pv) : 0;
		}
	}

	/* Desired list: pinned rows verbatim (deduped), then derived */
	for (int i = 0; i < n_cur; i++) {
		if (!cur[i].pinned)
			continue;
		if (rep_row_find(want, n_want, cur[i].song) >= 0)
			continue;
		if (n_want >= REP_MAX_SONGS)
			return -1;
		want[n_want++] = cur[i];
	}
	for (int i = 0; i < n_tally; i++) {
		rep_tally_t *t = &tally[i];
		int best = 0;

		if (rep_row_find(want, n_want, t->song) >= 0)
			continue;
		if (n_want >= REP_MAX_SONGS)
			return -1;
		for (int ki = 1; ki < t->n_keys; ki++)
			if (t->counts[ki] > t->counts[best])
				best = ki;
		memset(&want[n_want], 0, sizeof(want[n_want]));
		snprintf(
		        want[n_want].song, sizeof(want[n_want].song), "%s",
		        t->song);
		snprintf(
		        want[n_want].format, sizeof(want[n_want].format), "%s",
		        t->format);
		want[n_want].transpose = t->keys[best];
		want[n_want].pinned = 0;
		n_want++;
	}

	/* Compare-before-write: redundant calls must stay free */
	changed = (n_want != n_cur);
	for (int i = 0; !changed && i < n_want; i++) {
		if (want[i].pinned != cur[i].pinned ||
		    want[i].transpose != cur[i].transpose ||
		    strcmp(want[i].song, cur[i].song) != 0 ||
		    strcmp(want[i].format, cur[i].format) != 0)
			changed = 1;
	}
	if (!changed)
		return 0;

	hyle_source_ordered_clear("grp.songs", grp_id);
	for (int i = 0; i < n_want; i++) {
		const char *names[] = { "song", "transpose", "format",
			                "pinned" };
		const char *vals[4];
		char tr[16], pv[16];

		snprintf(tr, sizeof(tr), "%d", want[i].transpose);
		snprintf(pv, sizeof(pv), "%d", want[i].pinned);
		vals[0] = want[i].song;
		vals[1] = tr;
		vals[2] = want[i].format;
		vals[3] = pv;
		hyle_source_ordered_append("grp.songs", grp_id, names, vals, 4);
	}
	hyle_source_ordered_save("grp.songs", grp_id);
	return 0;
}

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

static int
handle_grp_song_add_auth(int fd, char *body, const item_ctx_t *ctx, void *user)
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
	/* Manual adds are pinned: they survive rep_rebuild. */
	const char *names[] = { "song", "transpose", "format", "pinned" };
	const char *vals[] = { s_id, tr, fmt, "1" };
	hyle_source_ordered_append("grp.songs", ctx->id, names, vals, 4);
	hyle_source_ordered_save("grp.songs", ctx->id);

	return redirect_to_item(fd, "grp", ctx->id);
}

static int handle_grp_song_add(int fd, char *body)
{
	return with_module_item_access(
	        fd, body, "grp",
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP | ICTX_CSRF_QUERY, NULL,
	        NULL, handle_grp_song_add_auth, NULL);
}

static int
handle_grp_song_key_auth(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	char k_s[32] = { 0 };
	axil_query_param("key", k_s, sizeof(k_s) - 1);

	int idx = grp_song_index(ctx->id, ctx->song_id);
	if (idx >= 0) {
		const char *key =
		        hyle_source_ordered_key_at("grp.songs", ctx->id, idx);
		/* Setting a key pins the entry: the preferred key is a
		 * group setting that rep_rebuild preserves. */
		const char *names[] = { "transpose", "pinned" };
		const char *vals[] = { k_s, "1" };
		hyle_source_put("grp.songs", key, names, vals, 2);
		hyle_source_ordered_save("grp.songs", ctx->id);
	}

	return redirect_to_item(fd, "grp", ctx->id);
}

static int handle_grp_song_key(int fd, char *body)
{
	return with_module_item_access(
	        fd, body, "grp",
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP | ICTX_SONG_ID |
	                ICTX_CSRF_QUERY,
	        NULL, NULL, handle_grp_song_key_auth, NULL);
}

static int
handle_grp_song_del_auth(int fd, char *body, const item_ctx_t *ctx, void *user)
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
	return with_module_item_access(
	        fd, body, "grp",
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP | ICTX_SONG_ID |
	                ICTX_CSRF_QUERY,
	        NULL, NULL, handle_grp_song_del_auth, NULL);
}

static int
handle_grp_song_view_auth(int fd, char *body, const item_ctx_t *ctx, void *user)
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
	return with_module_item_access(
	        fd, body, "grp", ICTX_SONG_ID, NULL, NULL,
	        handle_grp_song_view_auth, NULL);
}

#include "ux/all.c"

static void ch_load_gigs(
        source_def_t *sb_def, uint32_t grp_pos, ch_sb_entry_t *gigs,
        int *n_gigs)
{
	*n_gigs = 0;
	uint32_t inv_buf[256];
	size_t n =
	        qmap_inv_get(sb_def->fields_hd, "grp", grp_pos, inv_buf, 256);
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
		const char *pv = qmap_field_get(fhd, rk, "pinned");
		if (!sr)
			continue;
		if (!fm)
			fm = "any";

		int tp = ts ? atoi(ts) : 0;
		int pinned = pv ? atoi(pv) : 0;

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
		        "%s \xe2\x80\xa2 Key: %s%s", fm, tg,
		        pinned ? " \xe2\x80\xa2 pinned" : "");
		e->orig_key = ok;
		e->transpose = tp;
		e->pinned = pinned;
		snprintf(
		        e->key_action, sizeof(e->key_action),
		        "/api/grp/%s/song/%s/key", grp_id, sr);
		snprintf(
		        e->rem_action, sizeof(e->rem_action),
		        "/api/grp/%s/song/%s/remove", grp_id, sr);
	}
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
	        fd, ctx->username, "grp", site_ui_module_icon("grp"), title,
	        ctx->id, form);
}

static int grp_edit_get_handler(int fd, char *body)
{
	return with_module_item_access(
	        fd, body, "grp", ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP, NULL,
	        NULL, grp_edit_auth, NULL);
}

/* ── Detail handler ──────────────────────────────────────── */

static int
grp_detail_auth(int fd, char *body, const item_ctx_t *ctx, void *user_data)
{
	(void)body;
	(void)user_data;
	unsigned cf_hd, sf_hd;
	const char *title, *owner;
	char owner_buf[64] = { 0 };
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

	item_owner_read(ctx->item_path, owner_buf, sizeof(owner_buf));
	owner = owner_buf;
	is_owner = item_owner_check(ctx->item_path, ctx->username);

	/* Self-heal: recompute the repertoire from the gigs before
	 * rendering. A no-op when in sync (compare-before-write); heals
	 * drift from deleted gigs or missed runtime hooks. */
	rep_rebuild(ctx->id);

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

	uint32_t grp_pos = qmap_pos(cf_hd, ctx->id);
	if (grp_pos != QM_MISS) {
		source_def_t *sb_def = source_find("gig.items");
		if (sb_def && sb_def->fields_hd) {
			ch_load_gigs(sb_def, grp_pos, gigs, &n_gigs);
			bud_append(
			        body_frag,
			        ch_render_gigs_section(gigs, n_gigs));
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
		char qs[1024] = { 0 };

		axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");
		memset(&g_ch_pick_state, 0, sizeof(g_ch_pick_state));
		snprintf(
		        g_ch_pick_state.module, sizeof(g_ch_pick_state.module),
		        "song");
		snprintf(
		        g_ch_pick_state.username,
		        sizeof(g_ch_pick_state.username), "%s",
		        ctx->username ? ctx->username : "");
		list_fill_state(&g_ch_pick_state, "song.items", qs, 0);

		bud_append(
		        body_frag,
		        ch_render_add_song_section(ctx->id, csrf_token));
	}

	/* ── Assemble page ──────────────────────────────────── */

	snprintf(path, sizeof(path), "/grp/%s", ctx->id);
	layout = site_ui_layout(
	        page_title, path, site_ui_module_icon("grp"), ctx->username,
	        site_ui_item_menu("grp", ctx->id, is_owner), body_frag);

	return site_ui_respond_page(
	        fd, page_title, path, site_ui_module_icon("grp"), ctx->username,
	        NULL, NULL, layout);
}

static int grp_detail_handler(int fd, char *body)
{
	return with_module_item_access(
	        fd, body, "grp", 0, NULL, NULL, grp_detail_auth, NULL);
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
	axil_register_handler("POST:/api/grp/:id/songs", handle_grp_song_add);
	axil_register_handler(
	        "POST:/api/grp/:id/song/:song_id/key", handle_grp_song_key);
	axil_register_handler(
	        "DELETE:/api/grp/:id/song/:song_id", handle_grp_song_delete);
	axil_register_handler(
	        "POST:/api/grp/:id/song/:song_id/remove",
	        handle_grp_song_delete);

	source_setup(
	        "grp.items", NULL, sizeof(grp_cache_t), "var/grp", grp_fields,
	        GRP_FIELD_COUNT, 0, &grp_list_view);

	/* Register ordered source for grp songs (data.txt persistence).
	 * "pinned" marks user-owned rows that rep_rebuild never touches;
	 * it must stay the LAST field (legacy 3-column rows rely on
	 * trailing-column tolerance in source_dsv_load). */
	{
		static const hyle_field_t ch_song_fields[] = {
			{ "song", HYLE_FIELD_REFERENCE, 1, "song.items", NULL,
			  1, 0, 0, 0, 0, NULL },
			{ "transpose", HYLE_FIELD_INT, 1, NULL, NULL, 0, 0, 0,
			  0, 0, NULL },
			{ "format", HYLE_FIELD_STRING, 1, NULL, NULL, 1, 0, 0,
			  0, 16, NULL },
			{ "pinned", HYLE_FIELD_INT, 1, NULL, NULL, 0, 0, 0, 0,
			  0, NULL },
		};
		hyle_source_register_ordered(
		        "grp.songs", ch_song_fields, 4, "grp", 0,
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
