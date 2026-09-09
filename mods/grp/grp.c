#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

#include <ttypt/axil.h>
#include <ttypt/axil-hyle.h>
#include <ttypt/xy-mod.h>
#include <ttypt/xy.h>
#include <ttypt/qmap.h>

#include "../index/index.h"
#include "../common/common.h"
#include "../source/source.h"

#include "../auth/auth.h"

#include "../song/song.h"
#include "fields.h"
#include "dict.h"

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

struct rep_pinned_ctx {
	rep_row_t *rows;
	int *n_rows;
	int max_rows;
};

static void rep_pinned_cb(int idx, const char *key, unsigned fhd, void *user)
{
	(void)idx;
	struct rep_pinned_ctx *c = user;
	if (*c->n_rows >= c->max_rows)
		return;
	const char *sid = qmap_field_get(fhd, key, "song");
	if (!sid)
		return;
	const char *pv = qmap_field_get(fhd, key, "pinned");
	if (pv && atoi(pv) == 0)
		return;
	if (rep_row_find(c->rows, *c->n_rows, sid) >= 0)
		return;

	rep_row_t *r = &c->rows[(*c->n_rows)++];
	memset(r, 0, sizeof(*r));
	snprintf(r->song, sizeof(r->song), "%s", sid);
	const char *ts = qmap_field_get(fhd, key, "transpose");
	r->transpose = ts ? atoi(ts) : 0;
	const char *fm = qmap_field_get(fhd, key, "format");
	snprintf(r->format, sizeof(r->format), "%s", fm && fm[0] ? fm : "any");
	r->pinned = 1;
}

struct rep_tally_ctx {
	rep_tally_t *tally;
	int *n_tally;
};

static void rep_tally_song_cb(
        int idx, const char *key, unsigned fhd, void *user)
{
	(void)idx;
	struct rep_tally_ctx *c = user;
	const char *sid = qmap_field_get(fhd, key, "song");
	if (!sid)
		return;

	int ti = rep_tally_find(c->tally, *c->n_tally, sid);
	if (ti < 0) {
		if (*c->n_tally >= REP_MAX_SONGS)
			return;
		ti = (*c->n_tally)++;
		rep_tally_t *t = &c->tally[ti];
		memset(t, 0, sizeof(*t));
		snprintf(t->song, sizeof(t->song), "%s", sid);
		const char *fm = qmap_field_get(fhd, key, "format");
		snprintf(t->format, sizeof(t->format), "%s", fm && fm[0] ? fm : "any");
	}
	rep_tally_t *t = &c->tally[ti];
	const char *ts = qmap_field_get(fhd, key, "transpose");
	rep_tally_bump(t, ts ? atoi(ts) : 0);
}

static int rep_collect_merged(const char *grp_id, rep_row_t *rows, int max_rows)
{
	rep_tally_t tally[REP_MAX_SONGS];
	int n_tally = 0, n_rows = 0;

	if (!grp_id || !grp_id[0] || !rows || max_rows <= 0)
		return 0;

	/* Pinned pass: collect pinned rows first from grp.songs partition */
	struct rep_pinned_ctx pinned_ctx = { rows, &n_rows, max_rows };
	source_ordered_for_each("grp.songs", grp_id, rep_pinned_cb, &pinned_ctx);

	/* Tally pass: across gigs, tally transposes per song */
	const char *gig_ids[REP_MAX_GIGS];
	size_t n_gigs = source_find_referencing(
	        "gig.items", "grp", grp_id, gig_ids, REP_MAX_GIGS);
	struct rep_tally_ctx tally_ctx = { tally, &n_tally };
	for (size_t gi = 0; gi < n_gigs; gi++) {
		source_ordered_for_each(
		        "gig.songs", gig_ids[gi], rep_tally_song_cb, &tally_ctx);
	}

	/* Append derived rows in tally order (first-seen), resolving majority
	 * key */
	for (int i = 0; i < n_tally && n_rows < max_rows; i++) {
		rep_tally_t *t = &tally[i];
		int best = 0;

		if (rep_row_find(rows, n_rows, t->song) >= 0)
			continue;

		for (int ki = 1; ki < t->n_keys; ki++) {
			if (t->counts[ki] > t->counts[best])
				best = ki;
		}

		memset(&rows[n_rows], 0, sizeof(rows[n_rows]));
		snprintf(
		        rows[n_rows].song, sizeof(rows[n_rows].song), "%s",
		        t->song);
		snprintf(
		        rows[n_rows].format, sizeof(rows[n_rows].format), "%s",
		        t->format);
		rows[n_rows].transpose = t->keys[best];
		rows[n_rows].pinned = 0;
		n_rows++;
	}

	return n_rows;
}

typedef void (*rep_entry_cb)(
        const char *song_id, int transpose, const char *format, int pinned,
        void *user);

XY_IMPL(int, rep_for_each_merged,
        const char *, grp_id,
        rep_entry_cb, cb,
        void *, user)
{
	rep_row_t rows[REP_MAX_SONGS];
	int n_rows;

	if (!grp_id || !grp_id[0] || !cb)
		return -1;

	n_rows = rep_collect_merged(grp_id, rows, REP_MAX_SONGS);
	for (int i = 0; i < n_rows; i++) {
		cb(rows[i].song, rows[i].transpose, rows[i].format,
		   rows[i].pinned, user);
	}
	return 0;
}

struct rep_snapshot_ctx {
	rep_row_t *cur;
	int *n_cur;
};

static void rep_snapshot_cb(
        int idx, const char *key, unsigned fhd, void *user)
{
	(void)idx;
	struct rep_snapshot_ctx *c = user;
	if (*c->n_cur >= REP_MAX_SONGS)
		return;
	const char *sid = qmap_field_get(fhd, key, "song");
	if (!sid)
		return;
	rep_row_t *r = &c->cur[(*c->n_cur)++];
	memset(r, 0, sizeof(*r));
	snprintf(r->song, sizeof(r->song), "%s", sid);
	const char *ts = qmap_field_get(fhd, key, "transpose");
	r->transpose = ts ? atoi(ts) : 0;
	const char *fm = qmap_field_get(fhd, key, "format");
	snprintf(r->format, sizeof(r->format), "%s", fm && fm[0] ? fm : "any");
	const char *pv = qmap_field_get(fhd, key, "pinned");
	r->pinned = pv ? atoi(pv) : 0;
}

XY_IMPL(int, rep_rebuild, const char *, grp_id)
{
	rep_row_t cur[REP_MAX_SONGS], want[REP_MAX_SONGS];
	int n_cur = 0, n_want = 0;
	int changed;

	if (!grp_id || !grp_id[0])
		return -1;

	/* Current partition snapshot: read what's currently in grp.songs */
	struct rep_snapshot_ctx snap_ctx = { cur, &n_cur };
	source_ordered_for_each("grp.songs", grp_id, rep_snapshot_cb, &snap_ctx);

	/* Desired list: ONLY pinned rows (pinned=1) hit the disk partition */
	for (int i = 0; i < n_cur; i++) {
		if (!cur[i].pinned)
			continue;
		if (rep_row_find(want, n_want, cur[i].song) >= 0)
			continue;
		if (n_want >= REP_MAX_SONGS)
			return -1;
		want[n_want++] = cur[i];
	}

	/* Compare-before-write: if stored partition already matches want
	 * exactly, done */
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

	source_ordered_clear("grp.songs", grp_id);
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
		source_ordered_append("grp.songs", grp_id, names, vals, 4);
	}
	source_ordered_save("grp.songs", grp_id);
	return 0;
}

static int grp_song_index(const char *grp_id, const char *song_id)
{
	int total = source_ordered_count("grp.songs", grp_id);
	unsigned fhd = source_get_fields_hd("grp.songs");
	if (!fhd)
		return -1;
	for (int i = 0; i < total; i++) {
		const char *key = source_ordered_key_at("grp.songs", grp_id, i);
		if (!key)
			continue;
		const char *sid = qmap_field_get(fhd, key, "song");
		if (sid && strcmp(sid, song_id) == 0)
			return i;
	}
	return -1;
}

static int grp_on_song_change(const axil_hyle_partition_change_ctx_t *c)
{
	rep_rebuild(c->parent_id);
	return 0;
}

static int
handle_grp_song_view_auth(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;
	int pk = 0;

	rep_row_t rows[REP_MAX_SONGS];
	int n_rows = rep_collect_merged(ctx->id, rows, REP_MAX_SONGS);
	for (int i = 0; i < n_rows; i++) {
		if (strcmp(rows[i].song, ctx->sub_id) == 0) {
			pk = rows[i].transpose;
			break;
		}
	}

	int t = 0;
	if (pk != 0)
		t = pk - song_get_original_key_root(ctx->doc_root, ctx->sub_id);
	char loc[512];
	snprintf(loc, sizeof(loc), "/song/%s?t=%d", ctx->sub_id, t);
	return axil_redirect(fd, loc);
}

static int handle_grp_song_view(int fd, char *body)
{
	return with_module_item_access(
	        fd, body, "grp", ICTX_SUB_ID, NULL, NULL,
	        handle_grp_song_view_auth, NULL);
}

#include "ux/all.c"

static void ch_load_gigs(
        const char *grp_id, unsigned sb_fields_hd, ch_sb_entry_t *gigs,
        int *n_gigs)
{
	*n_gigs = 0;
	const char *gig_ids[CH_MAX_GIGS];
	size_t n = source_find_referencing(
	        "gig.items", "grp", grp_id, gig_ids, CH_MAX_GIGS);
	for (size_t i = 0; i < n && *n_gigs < CH_MAX_GIGS; i++) {
		const char *t =
		        sb_fields_hd ? qmap_get_field_str(sb_fields_hd, gig_ids[i], "title")
		                     : NULL;
		ch_sb_entry_t *e = &gigs[(*n_gigs)++];
		snprintf(e->title, sizeof(e->title), "%s", t ? t : gig_ids[i]);
		snprintf(e->href, sizeof(e->href), "/gig/%s", gig_ids[i]);
	}
}

struct rep_load_ctx {
	const char *grp_id;
	unsigned sf_hd;
	ch_rep_entry_t *repertoire;
	int *n_repertoire;
};

static void ch_load_rep_cb(
        const char *song_id, int transpose, const char *format, int pinned,
        void *user)
{
	struct rep_load_ctx *ctx = user;
	if (*ctx->n_repertoire >= CH_MAX_REP_SONGS)
		return;

	const char *st = song_id;
	if (ctx->sf_hd) {
		const char *s =
		        qmap_get_field_str(ctx->sf_hd, song_id, "title");
		if (s)
			st = s;
	}

	int ok = song_get_original_key(song_id);
	const char *tg = target_key_name(ok, transpose, 0);

	ch_rep_entry_t *e = &ctx->repertoire[(*ctx->n_repertoire)++];
	snprintf(e->title, sizeof(e->title), "%s", st);
	snprintf(
	        e->song_href, sizeof(e->song_href), "/grp/%s/song/%s",
	        ctx->grp_id, song_id);
	snprintf(
	        e->key_label, sizeof(e->key_label), "%s \xe2\x80\xa2 Key: %s%s",
	        format ? format : "any", tg,
	        pinned ? " \xe2\x80\xa2 pinned" : "");
	e->orig_key = ok;
	e->transpose = transpose;
	e->pinned = pinned;
	snprintf(
	        e->key_action, sizeof(e->key_action), "/api/grp/%s/song/%s/key",
	        ctx->grp_id, song_id);
	snprintf(
	        e->rem_action, sizeof(e->rem_action),
	        "/api/grp/%s/song/%s/remove", ctx->grp_id, song_id);
}

static void ch_load_repertoire(
        const char *grp_id, unsigned sf_hd, ch_rep_entry_t *repertoire,
        int *n_repertoire)
{
	*n_repertoire = 0;
	struct rep_load_ctx ctx = {
		.grp_id = grp_id,
		.sf_hd = sf_hd,
		.repertoire = repertoire,
		.n_repertoire = n_repertoire,
	};
	rep_for_each_merged(grp_id, ch_load_rep_cb, &ctx);
}

/* ── HTTP handlers ──────────────────────────────────────── */

static bud_node *grp_detail_build_body(
        int fd, const item_ctx_t *ctx, unsigned cf_hd, const char *title,
        const char *owner, int is_owner, const char *csrf_token)
{
	bud_node *body_frag = bud_fragment();
	bud_node *header = ch_render_detail_header(title, owner);
	if (header)
		bud_append(body_frag, header);

	ch_sb_entry_t gigs[CH_MAX_GIGS];
	int n_gigs;
	ch_rep_entry_t repertoire[CH_MAX_REP_SONGS];
	int n_repertoire;

	unsigned sb_hd = source_get_fields_hd("gig.items");
	ch_load_gigs(ctx->id, sb_hd, gigs, &n_gigs);
	if (n_gigs > 0) {
		bud_append(body_frag, ch_render_gigs_section(gigs, n_gigs));
	}

	unsigned sf_hd = source_get_fields_hd("song.items");
	if (sf_hd) {
		ch_load_repertoire(ctx->id, sf_hd, repertoire, &n_repertoire);
		bud_append(
		        body_frag, ch_render_repertoire_section(
		                           repertoire, n_repertoire, is_owner,
		                           csrf_token));
	}

	char members_buf[1024] = { 0 };
	auth_group_get_members(ctx->id, members_buf, sizeof(members_buf));
	bud_append(
	        body_frag, ch_render_members_section(
	                           ctx->id, members_buf, is_owner, owner,
	                           csrf_token));

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
		bud_append(body_frag, ch_render_add_song_section(ctx->id, csrf_token));
	}

	return body_frag;
}

static int
grp_detail_auth(int fd, char *body, const item_ctx_t *ctx, void *user_data)
{
	(void)body;
	(void)user_data;

	unsigned cf_hd = source_get_fields_hd("grp.items");
	if (!cf_hd)
		return server_error(fd, "No fields_hd");

	const char *title = qmap_get_field_str(cf_hd, ctx->id, "title");
	if (!title)
		return respond_error(fd, 404, "Group not found");

	char owner_buf[64] = { 0 };
	item_owner_read(ctx->item_path, owner_buf, sizeof(owner_buf));

	/* Self-heal: recompute the repertoire from the gigs before
	 * rendering. A no-op when in sync (compare-before-write); heals
	 * drift from deleted gigs or missed runtime hooks. */
	rep_rebuild(ctx->id);

	int is_owner = (ctx->username && ctx->username[0])
	                       ? item_owner_check(ctx->item_path, ctx->username)
	                       : 0;
	const char *csrf_token = csrf_setup(fd);

	bud_node *body_frag = grp_detail_build_body(
	        fd, ctx, cf_hd, title, owner_buf, is_owner,
	        csrf_token);

	return site_ui_respond_item_detail(fd, ctx, "grp", title, body_frag);
}

static int handle_grp_member_action_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	char action[32] = { 0 };
	char member[64] = { 0 };
	char back[512] = { 0 };

	axil_req_param(fd, body, "action", action, sizeof(action));
	axil_req_param(fd, body, "member", member, sizeof(member));
	axil_req_param(fd, body, "back", back, sizeof(back));

	if (!member[0])
		return respond_error(fd, 400, "Missing member username");

	if (strcmp(action, "del") == 0 || strcmp(action, "remove") == 0) {
		auth_group_del_member(ctx->id, member);
	} else {
		auth_group_add_member(ctx->id, member);
	}

	if (back[0] && strncmp(back, "/grp/", 5) == 0)
		return axil_redirect(fd, back);

	return redirect_to_item(fd, "grp", ctx->id);
}

static int handle_grp_members(int fd, char *body)
{
	return with_module_item_access(
	        fd, body, "grp",
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP | ICTX_CSRF_QUERY,
	        "Group not found", "Only group owner can manage members",
	        handle_grp_member_action_authorized, NULL);
}

static int handle_grp_add(int fd, char *body)
{
	char id[256] = { 0 };
	const char *user = get_request_user(fd);
	if (index_add_item(fd, body, id, sizeof(id)) != 0)
		return 1;

	auth_create_group(id);
	if (user && *user)
		auth_group_add_member(id, user);
	module_item_group_record(fd, "grp", id, id);

	char location[512];
	snprintf(location, sizeof(location), "/grp/%s", id);
	return axil_redirect(fd, location);
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

	i18n_register_dict(grp_dict, GRP_DICT_COUNT);

	{
		char doc_root[256] = { 0 };
		resolve_doc_root(0, doc_root, sizeof(doc_root));
		strncpy(g_doc_root, doc_root, sizeof(g_doc_root) - 1);
	}
	axil_register_handler(
	        "GET:/grp/:id/song/:song_id", handle_grp_song_view);
	axil_register_handler("POST:/api/grp/:id/members", handle_grp_members);

	static const axil_hyle_param_alias_t grp_song_aliases[] = {
		{ "transpose", { "key", "t", NULL } },
		{ "format",    { "fmt", NULL } },
		{ NULL }
	};

	axil_hyle_partition_routes_spec_t song_part_routes = {
		.module = "grp",
		.child_resource = "song",
		.children_resource = "songs",
		.partition_source = "grp.songs",
		.primary_field = "song",
		.pin_field = "pinned",
		.positional = 0,
		.update_action = "key",
		.redirect_pattern = "/%s/%s",
		.aliases = grp_song_aliases,
		.on_change = (axil_hyle_partition_change_fn)grp_on_song_change,
	};
	axil_hyle_register_partition_routes(&song_part_routes);

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
		source_register_ordered(&(source_ordered_def_t){
		        .source_id = "grp.songs",
		        .fields = ch_song_fields,
		        .field_count = 4,
		        .partition_field = "grp",
		        .record_id = 0,
		        .flags = SOURCE_AUTO_RECORD,
		        .load_fn = source_dsv_load,
		        .save_fn = source_dsv_save,
		        .persist_user = g_doc_root,
		});
	}

	index_open(
	        "Group", "grp.items", NULL, grp_detail_handler, handle_grp_add,
	        NULL, NULL, "grp");
}
