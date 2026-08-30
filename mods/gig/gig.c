#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>
#include <sys/stat.h>

#include <ttypt/axil.h>
#include <ttypt/xy.h>
#include <ttypt/xy-mod.h>
#include <ttypt/qmap.h>

#include "../common/common.h"
#include "../source/source.h"

#include "../auth/auth.h"
#include "../mpfd/mpfd.h"
#include "../song/song.h"
#include <transp/transp_flags.h>
#include <transp/music.h>
#include "../grp/grp.h"
#include "fields.h"
#include "../index/index.h"

static char g_doc_root[256] = ".";

#define SB_OWNED_HANDLER(name, forbidden_msg, extra_flags)                     \
	static int handle_sb_##name(int fd, char *body)                        \
	{                                                                      \
		return with_module_item_access(                                \
		        fd, body, "gig",                                       \
		        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP | extra_flags,   \
		        "Gig not found", forbidden_msg,                        \
		        handle_sb_##name##_authorized, NULL);                  \
	}

static int handle_sb_transpose_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user);
static int handle_sb_randomize_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user);
static int handle_sb_song_add_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user);
static int handle_sb_song_remove_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user);
static int handle_sb_song_replace_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user);

SB_OWNED_HANDLER(transpose, "You don't own this gig", ICTX_CSRF_MPFD)
SB_OWNED_HANDLER(randomize, "You don't own this gig", ICTX_CSRF_MPFD)
SB_OWNED_HANDLER(song_add, "Forbidden", ICTX_CSRF_QUERY)
SB_OWNED_HANDLER(song_remove, "Forbidden", ICTX_CSRF_QUERY)
SB_OWNED_HANDLER(song_replace, "Forbidden", ICTX_CSRF_QUERY)

static void sb_append_song(
        const char *source_id, const char *pval, const char *song,
        const char *transpose, const char *format)
{
	const char *n[3] = { "song", "transpose", "format" };
	const char *v[3] = { song, transpose, format };
	source_ordered_append(source_id, pval, n, v, 3);
}

/* Re-sync the auto-repertoire of the grp this gig belongs to. Cheap in
 * the common case: rep_rebuild compares before writing. */
static void gig_sync_repertoire(const char *sb_id)
{
	unsigned fhd;
	const char *grp;

	if (!sb_id || !sb_id[0])
		return;
	fhd = source_get_fields_hd("gig.items");
	grp = fhd ? qmap_get_field_str(fhd, sb_id, "grp") : NULL;
	if (grp && grp[0])
		rep_rebuild(grp);
}

/* Owner-only song search for the edit-page picker (SSR only; the
 * detail page fills its picker state via the bud-state JSON). */
static void sb_load_edit_song_picks(int fd, pick_view_t *pv_out);

typedef void (*sb_song_cb)(
        const char *key, const char *song_id, int transpose, const char *format,
        void *user);

static void sb_for_each_song(const char *sb_id, sb_song_cb cb, void *user)
{
	int total = source_ordered_count("gig.songs", sb_id);
	unsigned fhd = source_get_fields_hd("gig.songs");
	for (int i = 0; i < total; i++) {
		const char *key = source_ordered_key_at("gig.songs", sb_id, i);
		if (!key)
			continue;
		const char *sid = qmap_field_get(fhd, key, "song");
		if (!sid)
			continue;
		const char *ts = qmap_field_get(fhd, key, "transpose");
		const char *fm = qmap_field_get(fhd, key, "format");
		cb(key, sid, ts ? atoi(ts) : 0, fm ? fm : "any", user);
	}
}

static void gig_meta_read(const char *item_path, gig_cache_t *meta)
{
	source_meta_read(
	        item_path, gig_fields, SB_FIELD_COUNT, meta, sizeof(*meta));
}

static int gig_meta_write(const char *item_path, const gig_cache_t *meta)
{
	return source_meta_write(item_path, gig_fields, SB_FIELD_COUNT, meta);
}

struct grp_rand_match_ctx {
	const char *type;
	char ids[4096];
	size_t id_pos;
	size_t match_count;
};

static void grp_rand_match_cb(
        const char *song_id, int transpose, const char *format, int pinned,
        void *user)
{
	(void)transpose;
	(void)pinned;
	struct grp_rand_match_ctx *ctx = user;
	const char *ftype = (format && format[0]) ? format : "any";

	if (strcmp(ftype, ctx->type) == 0 || strcmp(ctx->type, "any") == 0) {
		if (song_id &&
		    ctx->id_pos + strlen(song_id) + 1 < sizeof(ctx->ids))
		{
			strcpy(ctx->ids + ctx->id_pos, song_id);
			ctx->id_pos += strlen(song_id) + 1;
			ctx->match_count++;
		}
	}
}

/* Get a random repertoire entry for the given type from the
 * gig's grp. sb_id is the gig item ID. */
static int get_random_repertoire_by_type(
        const char *sb_id, const char *type, char *out_id, size_t out_len)
{
	unsigned gig_fhd;
	const char *grp;
	const char *p;
	int pick;

	gig_fhd = source_get_fields_hd("gig.items");
	grp = gig_fhd ? qmap_get_field_str(gig_fhd, sb_id, "grp") : NULL;
	if (!grp || !grp[0])
		return -1;

	struct grp_rand_match_ctx ctx = {
		.type = type,
		.id_pos = 0,
		.match_count = 0,
	};
	memset(ctx.ids, 0, sizeof(ctx.ids));

	rep_for_each_merged(grp, grp_rand_match_cb, &ctx);

	if (ctx.match_count == 0)
		return -1;

	/* Pick random */
	pick = rand() % ctx.match_count;
	p = ctx.ids;
	for (int i = 0; i < pick; i++)
		p += strlen(p) + 1;

	strncpy(out_id, p, out_len - 1);
	out_id[out_len - 1] = '\0';
	return 0;
}

/* POST /gig/:id/transpose - Transpose single song by index */
static int handle_sb_transpose_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	char n_str[16] = { 0 };
	char t_str[16] = { 0 };
	int idx;
	const char *key;
	const char *names[1];
	const char *vals[1];

	mpfd_get("n", n_str, sizeof(n_str));
	mpfd_get("t", t_str, sizeof(t_str));
	if (!n_str[0])
		return bad_request(fd, "Missing n");
	idx = atoi(n_str);

	key = source_ordered_key_at("gig.songs", ctx->id, idx);
	if (!key)
		return respond_error(fd, 404, "Song not found in gig");
	char location[256];

	names[0] = "transpose";
	vals[0] = t_str;
	source_put_row("gig.songs", key, names, vals, 1);
	source_ordered_save("gig.songs", ctx->id);

	gig_sync_repertoire(ctx->id);

	snprintf(location, sizeof(location), "/gig/%s", ctx->id);
	return axil_redirect(fd, location);
}

/* POST /gig/:id/randomize - Randomize song by index */
static int handle_sb_randomize_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	char n_str[16] = { 0 };
	int idx;
	const char *key;
	unsigned fhd;
	const char *fmt_val;
	const char *s_names[3];
	const char *s_vals[3];
	char new_song_id[128] = { 0 };
	char location[256];

	mpfd_get("n", n_str, sizeof(n_str));
	if (!n_str[0])
		return bad_request(fd, "Missing n");
	idx = atoi(n_str);

	key = source_ordered_key_at("gig.songs", ctx->id, idx);
	if (!key)
		return respond_error(fd, 404, "Song not found in gig");
	fhd = source_get_fields_hd("gig.songs");
	fmt_val = qmap_field_get(fhd, key, "format");
	if (!fmt_val || !fmt_val[0])
		fmt_val = "any";

	if (get_random_repertoire_by_type(
	            ctx->id, fmt_val, new_song_id, sizeof(new_song_id)) != 0)
		return respond_error(fd, 404, "No songs found for format");

	/* Replace song_id in-place */
	s_names[0] = "song";
	s_vals[0] = new_song_id;
	s_names[1] = "transpose";
	s_vals[1] = "0";
	s_names[2] = "format";
	s_vals[2] = fmt_val;
	source_put_row("gig.songs", key, s_names, s_vals, 3);
	source_ordered_save("gig.songs", ctx->id);

	gig_sync_repertoire(ctx->id);

	snprintf(location, sizeof(location), "/gig/%s", ctx->id);
	return axil_redirect(fd, location);
}

static void resolve_song_id(char *s_id, size_t s_id_sz)
{
	source_resolve_partition_key(
	        "song.items", "grp.songs", "song", s_id, s_id_sz);
}

struct seed_song_ctx {
	const char *gig_id;
};

static int seed_song_for_format(const char *format, void *user)
{
	struct seed_song_ctx *ctx = user;
	char song_id[256] = { 0 };

	if (get_random_repertoire_by_type(
	            ctx->gig_id, format, song_id, sizeof(song_id)) == 0)
		sb_append_song("gig.songs", ctx->gig_id, song_id, "0", format);
	return 0;
}

/* POST /api/gig/:id/songs - Add a song to the gig */
static int handle_sb_song_add_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	char s_id[128] = { 0 };
	char fmt_val[64] = "any";
	char back[256] = { 0 };

	if (axil_query_param("song_id", s_id, sizeof(s_id) - 1) <= 0)
		return bad_request(fd, "Missing song_id");
	datalist_extract_id(s_id, s_id, sizeof(s_id));

	resolve_song_id(s_id, sizeof(s_id));

	axil_query_param("format", fmt_val, sizeof(fmt_val) - 1);
	if (!fmt_val[0])
		snprintf(fmt_val, sizeof(fmt_val), "any");

	sb_append_song("gig.songs", ctx->id, s_id, "0", fmt_val);
	source_ordered_save("gig.songs", ctx->id);

	gig_sync_repertoire(ctx->id);

	/* Picker may ask to come back to the page it was opened from
	 * (e.g. the edit page); validate prefix to avoid open redirects. */
	axil_query_param("back", back, sizeof(back) - 1);
	if (back[0] && strncmp(back, "/gig/", 5) == 0)
		return axil_redirect(fd, back);

	return redirect_to_item(fd, "gig", ctx->id);
}

/* POST /api/gig/:id/song/:n/remove - Remove a song from the gig */
static int handle_sb_song_remove_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	char n_str[16] = { 0 };
	int idx;

	/* Prefer an explicit query param; fall back to the :n path
	 * parameter matched by the route (PATTERN_PARAM_N env). */
	if (axil_query_param("n", n_str, sizeof(n_str) - 1) <= 0)
		axil_env_get(fd, n_str, sizeof(n_str), "PATTERN_PARAM_N");
	if (!n_str[0])
		return bad_request(fd, "Missing n");
	idx = atoi(n_str);

	source_ordered_remove_at("gig.songs", ctx->id, idx);
	source_ordered_save("gig.songs", ctx->id);

	gig_sync_repertoire(ctx->id);

	return redirect_to_item(fd, "gig", ctx->id);
}

/* POST /api/gig/:id/song/:n/replace - Replace a song in the gig.
 * Only the song identity changes; the row's transpose and format are
 * preserved (the picker submits song_id only). */
static int handle_sb_song_replace_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	(void)body;
	char n_str[16] = { 0 };
	char s_id[128] = { 0 };
	char back[256] = { 0 };
	int idx;
	const char *key;
	const char *names[3];
	const char *vals[3];

	axil_query_param("n", n_str, sizeof(n_str) - 1);
	idx = atoi(n_str);

	if (axil_query_param("song_id", s_id, sizeof(s_id) - 1) <= 0) {
		/* If song_id is not provided, keep current song_id */
		unsigned cur_fhd = source_get_fields_hd("gig.songs");
		const char *cur_s = qmap_field_get(cur_fhd, key, "song");
		if (cur_s)
			snprintf(s_id, sizeof(s_id), "%s", cur_s);
	}
	if (!s_id[0])
		return bad_request(fd, "Missing song_id");
	datalist_extract_id(s_id, s_id, sizeof(s_id));
	resolve_song_id(s_id, sizeof(s_id));

	key = source_ordered_key_at("gig.songs", ctx->id, idx);
	if (!key)
		return respond_error(fd, 404, "Song not found in gig");

	/* Preserve the replaced row's key/format unless the request
	 * carries explicit new values (kept for API compatibility). */
	unsigned fhd = source_get_fields_hd("gig.songs");
	const char *cur_t = qmap_field_get(fhd, key, "transpose");
	const char *cur_f = qmap_field_get(fhd, key, "format");
	char key_val[16] = { 0 };
	char fmt_val[64] = { 0 };

	axil_query_param("transpose", key_val, sizeof(key_val) - 1);
	if (!key_val[0] && cur_t)
		snprintf(key_val, sizeof(key_val), "%s", cur_t);

	axil_query_param("format", fmt_val, sizeof(fmt_val) - 1);
	if (!fmt_val[0] && cur_f)
		snprintf(fmt_val, sizeof(fmt_val), "%s", cur_f);
	if (!fmt_val[0])
		snprintf(fmt_val, sizeof(fmt_val), "any");

	names[0] = "song";
	vals[0] = s_id;
	names[1] = "transpose";
	vals[1] = key_val[0] ? key_val : "0";
	names[2] = "format";
	vals[2] = fmt_val;
	source_put_row("gig.songs", key, names, vals, 3);
	source_ordered_save("gig.songs", ctx->id);

	gig_sync_repertoire(ctx->id);

	/* Return JSON when requested by JS fetch / AJAX */
	char accept[256] = { 0 };
	axil_header_get(fd, "Accept", accept, sizeof(accept));
	if (strstr(accept, "application/json")) {
		char title_buf[256] = { 0 };
		char type_buf[512] = { 0 };
		char yt_buf[512] = { 0 };
		char audio_buf[512] = { 0 };
		char pdf_buf[512] = { 0 };
		char *ch = NULL;
		int dk = 0;
		int flags = TRANSP_HTML;
		char l_str[4] = { 0 }, b_str[4] = { 0 };
		if (axil_query_param("l", l_str, sizeof(l_str)) >= 0 &&
		    l_str[0] == '1')
			flags |= TRANSP_LATIN;
		if (axil_query_param("b", b_str, sizeof(b_str)) >= 0 &&
		    b_str[0] == '1')
			flags |= TRANSP_BEMOL;

		int tr = key_val[0] ? atoi(key_val) : 0;
		unsigned song_hd = source_get_fields_hd("song.items");
		if (song_hd) {
			const char *st =
			        qmap_get_field_str(song_hd, s_id, "title");
			if (st)
				snprintf(
				        title_buf, sizeof(title_buf), "%s", st);
			else
				snprintf(
				        title_buf, sizeof(title_buf), "%s",
				        s_id);

			const char *_yt =
			        qmap_get_field_str(song_hd, s_id, "yt");
			const char *_audio =
			        qmap_get_field_str(song_hd, s_id, "audio");
			const char *_pdf =
			        qmap_get_field_str(song_hd, s_id, "pdf");
			if (_yt)
				snprintf(yt_buf, sizeof(yt_buf), "%s", _yt);
			if (_audio)
				snprintf(
				        audio_buf, sizeof(audio_buf), "%s",
				        _audio);
			if (_pdf)
				snprintf(pdf_buf, sizeof(pdf_buf), "%s", _pdf);

			source_resolve_ref_display_str(
			        "song.items", s_id, "type", type_buf,
			        sizeof(type_buf));
		}

		song_transpose_root(g_doc_root, s_id, tr, flags, &ch, &dk);
		const char *tgt_key =
		        target_key_name(dk, tr, (flags & TRANSP_LATIN) ? 1 : 0);

		json_object *j_resp = json_object_new_object();
		json_object_object_add(
		        j_resp, "index", json_object_new_int(idx));
		json_object_object_add(
		        j_resp, "song_id", json_object_new_string(s_id));
		json_object_object_add(
		        j_resp, "title", json_object_new_string(title_buf));
		json_object_object_add(
		        j_resp, "type", json_object_new_string(type_buf));
		json_object_object_add(
		        j_resp, "original_key", json_object_new_int(dk));
		json_object_object_add(
		        j_resp, "target_key", json_object_new_string(tgt_key));
		json_object_object_add(
		        j_resp, "transpose", json_object_new_int(tr));
		json_object_object_add(
		        j_resp, "chord_html",
		        json_object_new_string(ch ? ch : ""));
		json_object_object_add(
		        j_resp, "yt", json_object_new_string(yt_buf));
		json_object_object_add(
		        j_resp, "audio", json_object_new_string(audio_buf));
		json_object_object_add(
		        j_resp, "pdf", json_object_new_string(pdf_buf));

		const char *json_str = json_object_to_json_string(j_resp);
		free(ch);
		axil_header_set(fd, "Content-Type", "application/json");
		axil_respond(fd, 200, json_str);
		json_object_put(j_resp);
		return 1;
	}

	/* Picker may ask to come back to the page it was opened from
	 * (e.g. the edit page); validate prefix to avoid open redirects. */
	axil_query_param("back", back, sizeof(back) - 1);
	if (back[0] && strncmp(back, "/gig/", 5) == 0)
		return axil_redirect(fd, back);

	return redirect_to_item(fd, "gig", ctx->id);
}

static int handle_sb_add(int fd, char *body)
{
	char id[256] = { 0 };
	char grp[128] = { 0 };
	if (index_add_item(fd, body, id, sizeof(id)) != 0)
		return 1;

	int grp_len = mpfd_get("grp", grp, sizeof(grp));
	if (grp_len < 0)
		grp_len = 0;
	grp[grp_len] = '\0';

	source_def_t *sb_def = source_find("gig.items");
	if (sb_def) {
		unsigned dh =
		        qmap_open(NULL, "row_data", QM_STR, QM_STR, 0x1F, 0);
		qmap_put(dh, "grp", grp);
		source_update_item(fd, "gig.items", id, dh);
		qmap_close(dh);
	}

	if (grp[0]) {
		module_item_group_record(fd, "gig", id, grp);

		/* Pre-populate with one random song per grp format type. */
		{
			unsigned grp_fhd = source_get_fields_hd("grp.items");
			const char *formats =
			        grp_fhd ? qmap_get_field_str(
			                          grp_fhd, grp, "format")
			                : NULL;
			struct seed_song_ctx seed_ctx = { id };

			str_list_for_each(
			        formats, seed_song_for_format, &seed_ctx);
		}
		source_ordered_save("gig.songs", id);

		/* Seeded songs join the derived repertoire immediately. */
		gig_sync_repertoire(id);
	}

	char location[512];
	snprintf(location, sizeof(location), "/gig/%s", id);
	return axil_redirect(fd, location);
}

#include "ux/detail.c"
#include "ux/add.c"
#include "ux/edit.c"

static const hyle_schema_desc_t sb_pick_song_schema[] = {
	{ .key = "song_id",
	  .qm_type = BUD_QM_STR,
	  .type = HYLE_FIELD_REFERENCE,
	  .ref_source = "song.items",
	  .writable = 1 },
	{ 0 }
};

static const hyle_schema_desc_t sb_pick_fmt_schema[] = {
	{ .key = "format",
	  .qm_type = BUD_QM_STR,
	  .type = HYLE_FIELD_REFERENCE,
	  .ref_source = "song.types",
	  .writable = 1 },
	{ 0 }
};

static void sb_load_song_picks(int fd, const char *scope)
{
	char qs[2048] = { 0 };

	if (fd > 0)
		axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");

	if (scope && scope[0])
		hyle_bud_picker_view_collect_scoped(
		        qs, sb_pick_song_schema, NULL, &g_sb_pick_state, scope);
	else
		hyle_bud_picker_view_collect_schema(
		        qs, sb_pick_song_schema, NULL, &g_sb_pick_state, NULL);
}

static void sb_load_edit_song_picks(int fd, pick_view_t *pv_out)
{
	static const hyle_schema_desc_t edit_song_schema[] = {
		{ .key = "song",
		  .qm_type = BUD_QM_STR,
		  .type = HYLE_FIELD_REFERENCE,
		  .ref_source = "song.items",
		  .writable = 1 },
		{ 0 }
	};
	char qs[2048] = { 0 };
	if (fd > 0)
		axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");
	hyle_bud_picker_view_collect_schema(
	        qs, edit_song_schema, NULL, pv_out, NULL);
}

static char *sb_emit_state_json(void)
{
	json_object *j_root = json_object_new_object();
	char pick_json[16384];
	const char *json_str;
	char *merged = NULL;
	size_t mlen;
	int req;

	hyle_bud_state_overlay_from_desc(
	        j_root, &sb_app_state, gig_app_fields, BUD_OVERLAY_INT,
	        BUD_OVERLAY_STR);

	json_object_object_add(
	        j_root, "songs",
	        hyle_bud_state_overlay_array(
	                g_sb_songs, sb_app_state.n_songs, sizeof(g_sb_songs[0]),
	                sb_song_row_fields, BUD_OVERLAY_INT, BUD_OVERLAY_STR));

	/* Picker rows are registry-driven; ship them so the WASM
	 * hydration tree matches SSR exactly (C-ISOMORPHIC-BUD §3). */
	site_ui_picker_state_to_json(&g_sb_pick_state, j_root);
	site_ui_picker_state_to_json(&g_sb_fmt_pick_state, j_root);

	json_str = json_object_to_json_string_ext(j_root, 0);
	if (!json_str) {
		json_object_put(j_root);
		return NULL;
	}
	mlen = strlen(json_str);

	req = snprintf(
	        NULL, 0,
	        "<script type=\"application/json\" "
	        "id=\"bud-state\">%s</script>",
	        json_str);
	{
		char *sj = malloc(req + 1);
		if (sj)
			snprintf(
			        sj, req + 1,
			        "<script type=\"application/json\" "
			        "id=\"bud-state\">%s</script>",
			        json_str);
		free(merged);
		json_object_put(j_root);
		return sj;
	}
}

/* GET /api/gig/:id/transpose - Return transposed chord HTML for
 * song at index n (ephemeral — does not persist). Used by WASM bridge. */
static int api_sb_transpose_get(int fd, char *body)
{
	(void)body;
	char id[128] = { 0 };
	char qs[1024] = { 0 };
	char n_str[16] = { 0 }, t_str[16] = { 0 };
	int flags = TRANSP_HTML;
	axil_env_get(fd, id, sizeof(id), "PATTERN_PARAM_ID");
	if (!id[0])
		return bad_request(fd, "Missing ID");

	axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");
	if (!qs[0])
		return bad_request(fd, "Missing query string");

	axil_query_parse(qs);
	axil_query_param("n", n_str, sizeof(n_str) - 1);
	axil_query_param("t", t_str, sizeof(t_str) - 1);
	{
		char b[4] = { 0 };
		if (axil_query_param("b", b, sizeof(b)) >= 0 && b[0] == '1')
			flags |= TRANSP_BEMOL;
	}
	{
		char l[4] = { 0 };
		if (axil_query_param("l", l, sizeof(l)) >= 0 && l[0] == '1')
			flags |= TRANSP_LATIN;
	}
	(void)axil_query_param(
	        "m", (char[4]){ 0 }, 4); /* consumed for consistency */

	if (!n_str[0])
		return bad_request(fd, "Missing n");

	int idx = atoi(n_str);
	int transpose = t_str[0] ? atoi(t_str) : 0;
	char found_song[256] = { 0 };

	/* Read song_id and transpose from ordered source */
	{
		const char *key;
		unsigned fhd;
		const char *sid, *db_tr;

		key = source_ordered_key_at("gig.songs", id, idx);
		if (!key)
			return respond_error(fd, 404, "Song not found");
		fhd = source_get_fields_hd("gig.songs");
		sid = qmap_field_get(fhd, key, "song");
		if (!sid)
			return respond_error(fd, 404, "Song not found");
		snprintf(found_song, sizeof(found_song), "%s", sid);

		db_tr = qmap_field_get(fhd, key, "transpose");
		if (!t_str[0])
			transpose = db_tr ? atoi(db_tr) : 0;
	}

	if (!found_song[0])
		return respond_error(fd, 404, "Song not found");

	char *chord_html = NULL;
	int detected_key = 0;
	song_transpose_root(
	        g_doc_root, found_song, transpose, flags, &chord_html,
	        &detected_key);

	const char *tgt_key = target_key_name(
	        detected_key, transpose, (flags & TRANSP_LATIN) ? 1 : 0);

	json_object *j_resp = json_object_new_object();
	json_object_object_add(j_resp, "index", json_object_new_int(idx));
	json_object_object_add(
	        j_resp, "chord_html",
	        json_object_new_string(chord_html ? chord_html : ""));
	json_object_object_add(
	        j_resp, "target_key", json_object_new_string(tgt_key));
	json_object_object_add(
	        j_resp, "original_key", json_object_new_int(detected_key));

	const char *json_str = json_object_to_json_string(j_resp);
	free(chord_html);
	axil_header_set(fd, "Content-Type", "application/json");
	axil_respond(fd, 200, json_str);
	json_object_put(j_resp);
	return 1;
}

/* ── Per-song row loader ─────────────────────────────────── */

static int sb_load_song_row(
        const char *song_id, int transpose, unsigned song_hd, int flags,
        sb_song_row_data_t *sd)
{
	const char *st;
	char *ch;
	int dk;

	if (qmap_pos(song_hd, song_id) == QM_MISS)
		return -1;

	st = qmap_get_field_str(song_hd, song_id, "title");
	if (!st)
		st = song_id;

	ch = NULL;
	dk = 0;
	song_transpose_root(g_doc_root, song_id, transpose, flags, &ch, &dk);

	memset(sd, 0, sizeof(*sd));
	sd->orig_key = song_get_original_key(song_id);
	snprintf(sd->title, sizeof(sd->title), "%s", st);
	snprintf(sd->song_id, sizeof(sd->song_id), "%s", song_id);
	sd->transpose = transpose;
	sd->flags = flags;

	{
		const char *_yt = qmap_get_field_str(song_hd, song_id, "yt");
		const char *_audio =
		        qmap_get_field_str(song_hd, song_id, "audio");
		const char *_pdf = qmap_get_field_str(song_hd, song_id, "pdf");
		if (_yt)
			snprintf(sd->yt, sizeof(sd->yt), "%s", _yt);
		if (_audio)
			snprintf(sd->audio, sizeof(sd->audio), "%s", _audio);
		if (_pdf)
			snprintf(sd->pdf, sizeof(sd->pdf), "%s", _pdf);
		{
			char resolved_type[512] = { 0 };
			source_resolve_ref_display_str(
			        "song.items", song_id, "type", resolved_type,
			        sizeof(resolved_type));
			if (resolved_type[0])
				snprintf(
				        sd->type, sizeof(sd->type), "%s",
				        resolved_type);
		}
	}
	sd->chord_html = ch;
	return 0;
}

/* ── Song iteration callbacks ────────────────────────────── */

struct detail_song_ctx {
	unsigned song_hd;
	int f;
};

static void detail_song_cb(
        const char *key, const char *song_id, int transpose, const char *format,
        void *user)
{
	(void)key;
	struct detail_song_ctx *c = user;
	if (sb_app_state.n_songs >= MAX_SB_SONGS)
		return;
	sb_song_row_data_t *sd = &g_sb_songs[sb_app_state.n_songs];
	if (sb_load_song_row(song_id, transpose, c->song_hd, c->f, sd) == 0) {
		if (format && format[0] && strcmp(format, "any") != 0) {
			char resolved_fmt[512] = { 0 };
			source_resolve_ref_display_str(
			        "song.types", format, "name", resolved_fmt,
			        sizeof(resolved_fmt));
			if (resolved_fmt[0])
				snprintf(
				        sd->type, sizeof(sd->type), "%s",
				        resolved_fmt);
			else
				snprintf(
				        sd->type, sizeof(sd->type), "%s",
				        format);
		}
		sb_app_state.n_songs++;
	}
}

struct edit_song_ctx {
	unsigned song_hd;
	sb_edit_row_t *songs;
	int *n_songs;
};

static void edit_song_cb(
        const char *key, const char *song_id, int transpose, const char *format,
        void *user)
{
	(void)key;
	struct edit_song_ctx *c = user;
	if (*c->n_songs >= 256)
		return;
	sb_edit_row_t *row = &c->songs[*c->n_songs];
	const char *s_title = qmap_get_field_str(c->song_hd, song_id, "title");
	if (!s_title)
		s_title = song_id;
	snprintf(row->repo_id, sizeof(row->repo_id), "%s", song_id);
	snprintf(row->title, sizeof(row->title), "%s", s_title);
	snprintf(row->transpose, sizeof(row->transpose), "%d", transpose);
	snprintf(row->format, sizeof(row->format), "%s", format);
	(*c->n_songs)++;
}

/* ── Edit data-loading helpers ──────────────────────────── */

static int sb_load_edit_songs(
        const char *sb_id, unsigned song_hd, sb_edit_row_t *songs, int max)
{
	int n = 0;
	struct edit_song_ctx ctx = { song_hd, songs, &n };
	sb_for_each_song(sb_id, edit_song_cb, &ctx);
	for (int i = 0; i < n; i++)
		songs[i].orig_key = song_get_original_key(songs[i].repo_id);
	return n;
}

static void sb_resolve_edit_format_names(sb_edit_row_t *songs, int n_songs)
{
	unsigned type_fhd = source_get_fields_hd("song.types");
	unsigned type_data_hd = source_get_data_hd("song.types");
	if (!type_fhd || !type_data_hd)
		return;
	for (int si = 0; si < n_songs; si++) {
		const char *fmt = songs[si].format;
		if (fmt[0] && strcmp(fmt, "any") != 0) {
			char nk[320];
			snprintf(nk, sizeof(nk), "%s:name", fmt);
			const char *name = qmap_get(type_fhd, nk);
			if (name && name[0])
				snprintf(
				        songs[si].format,
				        sizeof(songs[si].format), "%s", name);
		}
	}
}

int sb_load_format_options(char (*buf)[128], const char **opts, int max)
{
	return source_dataset_collect_options(
	        "song.types", "name", "any", (source_opt_buf_t *)buf, opts,
	        max);
}

/* ── HTTP handlers ──────────────────────────────────────── */

/* ── Detail handler ──────────────────────────────────────── */

static int
gig_detail_auth(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;
	unsigned sb_hd, song_hd;
	const char *title, *owner;
	char owner_buf[64] = { 0 };
	char fkey[512];
	int is_owner = 0;
	char *grp_id = NULL;
	song_viewer_prefs_t prefs;
	char page_title[256];
	bud_node *layout;
	const char *csrf_token;

	sb_hd = source_get_fields_hd("gig.items");
	if (!sb_hd)
		return server_error(fd, "No fields_hd");

	title = qmap_get_field_str(sb_hd, ctx->id, "title");
	if (!title)
		return respond_error(fd, 404, "Gig not found");

	item_owner_read(ctx->item_path, owner_buf, sizeof(owner_buf));
	owner = owner_buf;
	is_owner = item_owner_check(ctx->item_path, ctx->username);

	/* Parse query prefs + zoom */
	song_parse_viewer_prefs(fd, ctx->username, &prefs);

	/* Resolve grp ID from gig reference field */
	{
		snprintf(fkey, sizeof(fkey), "%s:grp", ctx->id);
		const char *grp_id_str = qmap_get(sb_hd, fkey);
		if (grp_id_str && grp_id_str[0])
			grp_id = strdup(grp_id_str);
	}

	snprintf(page_title, sizeof(page_title), "gig: %s", title);

	/* Open data handles we need throughout */
	song_hd = source_get_fields_hd("song.items");
	if (!song_hd)
		return respond_error(fd, 500, "Failed to open data handles");

	csrf_token = csrf_setup(fd);

	/* Load songs via ordered source first so song count is known */
	memset(&sb_app_state, 0, sizeof(sb_app_state));
	sb_app_state.active_row_pick = -1;
	{
		struct detail_song_ctx {
			unsigned song_hd;
			int f;
		} detail_ctx = { song_hd, prefs.flags };
		sb_for_each_song(ctx->id, detail_song_cb, &detail_ctx);
	}

	/* Auto-collect scoped picker if active (?replace=N or
	 * pick_q_song_id__N), otherwise collect options for the top Add Song
	 * picker. */
	if (is_owner) {
		char qs[2048] = { 0 };
		if (fd > 0)
			axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");

		hyle_bud_picker_view_collect_schema(
		        qs, sb_pick_song_schema, NULL, &g_sb_pick_state,
		        &sb_app_state.active_row_pick);
		hyle_bud_picker_view_collect_schema(
		        qs, sb_pick_fmt_schema, NULL, &g_sb_fmt_pick_state,
		        &sb_app_state.active_fmt_pick);
		/* For No-JS top add picker when search query is present
		 * (pick_q_song_id=), ensure top picker is populated */
		if (sb_app_state.active_row_pick < 0 &&
		    sb_app_state.active_fmt_pick < 0)
		{
			if (strstr(qs, "pick_q_song_id=") ||
			    strstr(qs, "pick_page_song_id="))
			{
				hyle_bud_picker_view_collect_schema(
				        qs, sb_pick_song_schema, NULL,
				        &g_sb_pick_state, NULL);
			}
		}
	}

	/* ── Populate sb_app_state with page data ────────────────── */
	sb_app_state.zoom = prefs.zoom;
	sb_app_state.latin = (prefs.flags & TRANSP_LATIN) ? 1 : 0;
	sb_app_state.show_media = prefs.show_media;
	sb_app_state.t_pref = prefs.transpose;
	sb_app_state.bemol = (prefs.flags & TRANSP_BEMOL) ? 1 : 0;
	sb_app_state.is_owner = is_owner;

	snprintf(sb_app_state.sb_id, sizeof(sb_app_state.sb_id), "%s", ctx->id);
	snprintf(
	        sb_app_state.path, sizeof(sb_app_state.path), "/gig/%s",
	        ctx->id);
	snprintf(
	        sb_app_state.title, sizeof(sb_app_state.title), "%s",
	        page_title);
	snprintf(
	        sb_app_state.user, sizeof(sb_app_state.user), "%s",
	        ctx->username ? ctx->username : "");
	snprintf(
	        sb_app_state.csrf_token, sizeof(sb_app_state.csrf_token), "%s",
	        csrf_token);
	snprintf(
	        sb_app_state.grp_id, sizeof(sb_app_state.grp_id), "%s",
	        grp_id ? grp_id : "");
	snprintf(sb_app_state.owner, sizeof(sb_app_state.owner), "%s", owner);

	snprintf(
	        sb_app_state.pick_q, sizeof(sb_app_state.pick_q), "%s",
	        g_sb_pick_state.entries[0].q ? g_sb_pick_state.entries[0].q
	                                     : "");
	sb_app_state.pick_page = g_sb_pick_state.entries[0].page;

	snprintf(
	        sb_app_state.pick_fmt_q, sizeof(sb_app_state.pick_fmt_q), "%s",
	        g_sb_fmt_pick_state.entries[0].q
	                ? g_sb_fmt_pick_state.entries[0].q
	                : "");
	sb_app_state.pick_fmt_page = g_sb_fmt_pick_state.entries[0].page;

	/* ── Build page through isomorphic entry point ────────────── */
	layout = bud_app_render();

	/* ── JSON state blob for WASM init (inside <script> tag) ──── */
	{
		char *state_json = sb_emit_state_json();
		site_ui_respond_page(
		        fd, page_title, sb_app_state.path,
		        site_ui_module_icon("gig"), sb_app_state.user,
		        state_json, "gig_detail", layout);
		free(state_json);
	}

	/* Free allocated chord data after render */
	for (int i = 0; i < sb_app_state.n_songs; i++) {
		free(g_sb_songs[i].chord_html);
		g_sb_songs[i].chord_html = NULL;
	}
	free(grp_id);

	return 0;
}

static int gig_detail_handler(int fd, char *body)
{
	return with_module_item_access(
	        fd, body, "gig", 0, "Gig not found", NULL, gig_detail_auth,
	        NULL);
}

/* ── Edit GET handler ───────────────────────────────────── */

static int gig_edit_auth(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;

	unsigned fields_hd = source_get_fields_hd("gig.items");
	const char *title;
	const char *grp_id;
	unsigned song_hd;
	char song_source[16];
	if (!fields_hd)
		return server_error(fd, "No fields_hd");

	title = qmap_get_field_str(fields_hd, ctx->id, "title");
	if (!title)
		title = "";

	grp_id = qmap_get_field_str(fields_hd, ctx->id, "grp");
	song_hd = source_get_fields_hd("song.items");

	/* Read song_source from meta; default to repertoire when a grp
	 * is assigned */
	song_source[0] = '\0';
	{
		gig_cache_t sm;
		gig_meta_read(ctx->item_path, &sm);
		strncpy(song_source, sm.song_source, sizeof(song_source) - 1);
	}
	if (grp_id && !song_source[0])
		strcpy(song_source, "repertoire");

	/* Load current songs via ordered source + compute original keys */
	sb_edit_row_t songs[256];
	int n_songs = sb_load_edit_songs(ctx->id, song_hd, songs, 256);

	/* Map format values from type ID slugs to display names */
	sb_resolve_edit_format_names(songs, n_songs);

	/* Read format options from song.types */
	char format_buf[128][128];
	const char *format_opts[128];
	int n_format_opts =
	        sb_load_format_options(format_buf, format_opts, 128);

	/* Collect grp picker view: options window, pinned selection and
	 * draft overlays from the query string. */
	pick_view_t edit_pv;
	gig_cache_t grp_rec;
	memset(&grp_rec, 0, sizeof(grp_rec));
	if (grp_id && grp_id[0])
		snprintf(grp_rec.grp, sizeof(grp_rec.grp), "%s", grp_id);

	static const hyle_schema_desc_t grp_field_schema[] = {
		{ .key = "grp",
		  .qm_type = BUD_QM_STR,
		  .type = HYLE_FIELD_REFERENCE,
		  .ref_source = "grp.items",
		  .offset = offsetof(gig_cache_t, grp),
		  .size = sizeof(grp_rec.grp),
		  .writable = 1 },
		{ 0 }
	};
	char qs[2048] = { 0 };
	if (fd > 0)
		axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");

	hyle_bud_picker_view_collect_schema(
	        qs, grp_field_schema, &grp_rec, &edit_pv, NULL);

	/* Check if any row's song picker or format picker is active using
	 * unified multi-field auto-collector */
	static const hyle_schema_desc_t row_candidate_schema[] = {
		{ .key = "song",
		  .qm_type = BUD_QM_STR,
		  .type = HYLE_FIELD_REFERENCE,
		  .ref_source = "song.items",
		  .writable = 1 },
		{ .key = "fmt",
		  .qm_type = BUD_QM_STR,
		  .type = HYLE_FIELD_REFERENCE,
		  .ref_source = "song.types",
		  .writable = 1 },
		{ 0 }
	};
	pick_view_t active_row_pv;
	memset(&active_row_pv, 0, sizeof(active_row_pv));
	int active_field_idx = -1;
	int active_scope = -1;

	hyle_bud_picker_view_collect_auto_fields_schema(
	        qs, row_candidate_schema, &active_row_pv, &active_field_idx,
	        &active_scope);

	int active_edit_row = (active_field_idx == 0) ? active_scope : -1;
	int active_edit_fmt_row = (active_field_idx == 1) ? active_scope : -1;

	pick_view_t add_pv;
	memset(&add_pv, 0, sizeof(add_pv));

	/* Load song picks for omnisearch add-picker only when no row is active
	 */
	if (active_edit_row < 0 && active_edit_fmt_row < 0)
		sb_load_edit_song_picks(fd, &add_pv);

	const char *csrf_token = csrf_setup(fd);

	char action[256];
	char cancel_href[256];
	snprintf(action, sizeof(action), "/gig/%s/edit", ctx->id);
	snprintf(cancel_href, sizeof(cancel_href), "/gig/%s", ctx->id);

	bud_node *form = sb_render_edit_form(
	        action, csrf_token, title, ctx->id, grp_rec.grp, &edit_pv,
	        cancel_href, n_songs, songs, n_format_opts, format_opts,
	        song_source, active_edit_row,
	        active_edit_row >= 0 ? &active_row_pv : NULL,
	        active_edit_fmt_row,
	        active_edit_fmt_row >= 0 ? &active_row_pv : NULL,
	        (active_edit_row < 0 && active_edit_fmt_row < 0) ? &add_pv
	                                                         : NULL);

	return site_ui_respond_edit_page(
	        fd, ctx->username, "gig", site_ui_module_icon("gig"), title,
	        ctx->id, form);
}

static int gig_edit_get_handler(int fd, char *body)
{
	return with_module_item_access(
	        fd, body, "gig", ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	        "Gig not found", NULL, gig_edit_auth, NULL);
}

/* ── Add GET handler ─────────────────────────────────────── */

static int gig_add_get_handler(int fd, char *body)
{
	(void)body;
	const char *user = require_user(fd);
	if (!user)
		return 1;

	const char *csrf_token = csrf_setup(fd);

	char qs[2048] = { 0 };
	if (fd > 0)
		axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");

	pick_view_t add_pv;
	hyle_bud_picker_view_collect_schema(
	        qs, gig_fields, NULL, &add_pv, NULL);

	bud_node *form = sb_render_add_form(csrf_token, NULL, &add_pv);

	return site_ui_respond_add_page(
	        fd, user, "gig", site_ui_module_icon("gig"), form);
}

/* ── Edit POST handler ───────────────────────────────────── */

static const source_ordered_field_sync_t gig_song_sync_fields[] = {
	{ .form_field_prefix = "song",
	  .schema_field_name = "song",
	  .default_value = "",
	  .is_primary_key = 1 },
	{ .form_field_prefix = "key",
	  .schema_field_name = "transpose",
	  .default_value = "0",
	  .is_primary_key = 0 },
	{ .form_field_prefix = "fmt",
	  .schema_field_name = "format",
	  .default_value = "any",
	  .is_primary_key = 0 }
};

static void sb_save_edit_songs_from_form(const char *gig_id)
{
	source_ordered_sync_form(
	        "gig.songs", gig_id, "amount", "remove", gig_song_sync_fields,
	        3);
}

static int
gig_edit_post_authorized(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	char old_grp[128] = { 0 };

	/* Capture the grp reference before it is rewritten so both the
	 * old and the new group can be re-synced below. */
	{
		unsigned ohd = source_get_fields_hd("gig.items");
		const char *og =
		        ohd ? qmap_get_field_str(ohd, ctx->id, "grp") : NULL;
		if (og && og[0])
			snprintf(old_grp, sizeof(old_grp), "%s", og);
	}

	/* Hydrate standard fields */
	{
		unsigned dh = source_parse_form("gig.items");
		if (dh) {
			const char *new_grp = qmap_get(dh, "grp");
			if (new_grp && new_grp[0]) {
				if (!source_item_exists("grp.items", new_grp) ||
				    !module_item_owner_check(
				            fd, "grp", new_grp, ctx->username))
				{
					qmap_close(dh);
					return respond_error(
					        fd, 403,
					        "You don't own this group");
				}
				module_item_group_record(fd, "gig", ctx->id, new_grp);
			}

			source_update_item(fd, "gig.items", ctx->id, dh);
			qmap_close(dh);
		}
	}

	/* Write songs via ordered source */
	sb_save_edit_songs_from_form(ctx->id);

	/* Auto-repertoire: re-sync the old grp (songs may have left it)
	 * and, on reassignment, the new one. */
	gig_sync_repertoire(ctx->id);
	if (old_grp[0]) {
		unsigned nhd = source_get_fields_hd("gig.items");
		const char *ng =
		        nhd ? qmap_get_field_str(nhd, ctx->id, "grp") : NULL;
		if (!ng || !ng[0] || strcmp(ng, old_grp) != 0)
			rep_rebuild(old_grp);
	}

	return redirect_to_item(fd, "gig", ctx->id);
}

static int gig_edit_post_handler(int fd, char *body)
{
	return with_module_item_access(
	        fd, body, "gig",
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP | ICTX_CSRF_MPFD,
	        "Gig not found", "You don't own this gig",
	        gig_edit_post_authorized, NULL);
}

void xy_install(void)
{
	char doc_root[256] = { 0 };
	resolve_doc_root(0, doc_root, sizeof(doc_root));
	strncpy(g_doc_root, doc_root, sizeof(g_doc_root) - 1);

	xy_load("./mods/index/index");
	xy_load("./mods/mpfd/mpfd");
	xy_load("./mods/song/song");
	xy_load("./mods/source/source");
	xy_load("./mods/grp/grp");

	source_setup(
	        "gig.items", NULL, sizeof(gig_cache_t), "var/gig", gig_fields,
	        SB_FIELD_COUNT, 0, &gig_list_view);

	/* Register ordered source for gig songs (data.txt persistence) */
	{
		static const hyle_field_t sb_song_fields[] = {
			{ "song", HYLE_FIELD_REFERENCE, 1, "song.items", NULL,
			  1, 0, 0, 0, 0, NULL },
			{ "transpose", HYLE_FIELD_INT, 1, NULL, NULL, 0, 0, 0,
			  0, 0, NULL },
			{ "format", HYLE_FIELD_STRING, 1, NULL, NULL, 1, 0, 0,
			  0, 16, NULL },
		};
		source_register_ordered(&(source_ordered_def_t){
		        .source_id = "gig.songs",
		        .fields = sb_song_fields,
		        .field_count = 3,
		        .partition_field = "sb",
		        .record_id = 0,
		        .flags = SOURCE_AUTO_RECORD,
		        .load_fn = source_dsv_load,
		        .save_fn = source_dsv_save,
		        .persist_user = g_doc_root,
		});
	}

	index_open("Gig", "gig.items", NULL, NULL, NULL, NULL, NULL, NULL);

	standard_item_handlers_t handlers = {
		.detail = gig_detail_handler,
		.add_get = gig_add_get_handler,
		.add_post = handle_sb_add,
		.edit_get = gig_edit_get_handler,
		.edit_post = gig_edit_post_handler,
	};
	register_standard_item_handlers("gig", &handlers);
	axil_register_handler("POST:/gig/:id/randomize", handle_sb_randomize);
	axil_register_handler("POST:/gig/:id/transpose", handle_sb_transpose);
	axil_register_handler("POST:/api/gig/:id/songs", handle_sb_song_add);
	axil_register_handler(
	        "POST:/api/gig/:id/song/:n/remove", handle_sb_song_remove);
	axil_register_handler(
	        "POST:/api/gig/:id/song/:n/replace", handle_sb_song_replace);
	axil_register_handler(
	        "GET:/api/gig/:id/transpose", api_sb_transpose_get);

	/* Backfill / heal: ensure any gigs that have a grp in their metadata
	 * file on disk are correctly indexed in gig.items in memory. */
	{
		unsigned gh = source_get_fields_hd("gig.items");
		unsigned dh = source_get_data_hd("gig.items");
		if (gh && dh) {
			uint32_t cur = qmap_iter(dh, NULL, 0);
			const void *k, *v;
			while (qmap_next(&k, &v, cur)) {
				const char *gig_id = (const char *)k;
				const char *grp_in_mem =
				        qmap_get_field_str(gh, gig_id, "grp");
				char item_path[512];
				if (item_path_build(
				            0, "gig", gig_id, item_path,
				            sizeof(item_path)) == 0)
				{
					gig_cache_t meta;
					gig_meta_read(item_path, &meta);
					if (meta.grp[0] &&
					    (!grp_in_mem ||
					     strcmp(grp_in_mem, meta.grp) != 0))
					{
						unsigned row_dh = qmap_open(
						        NULL, "row_data",
						        QM_STR, QM_STR, 0x1F,
						        0);
						qmap_put(
						        row_dh, "grp",
						        meta.grp);
						source_update_item(
						        0, "gig.items", gig_id,
						        row_dh);
						qmap_close(row_dh);
					}
				}
			}
			qmap_fin(cur);
		}
	}

	/* Auto-repertoire: sanitize every grp's stored partition (prune
	 * non-pinned rows) and sync in-memory state. */
	{
		unsigned gh = source_get_fields_hd("grp.items");
		unsigned dh = source_get_data_hd("grp.items");
		if (gh && dh) {
			uint32_t cur = qmap_iter(dh, NULL, 0);
			const void *k, *v;
			while (qmap_next(&k, &v, cur))
				rep_rebuild((const char *)k);
			qmap_fin(cur);
		}
	}
}
