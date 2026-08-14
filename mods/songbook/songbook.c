#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>
#include <sys/stat.h>

#include <ttypt/axil.h>
#include <ttypt/xy.h>
#include <ttypt/xy-mod.h>
#include <ttypt/qmap.h>

#include <hyle/source.h>

#include "../common/common.h"
#include "../source/source.h"

#include "../auth/auth.h"
#include "../mpfd/mpfd.h"
#include "../song/song.h"
#include "fields.h"
#include "../index/index.h"

#define SONGBOOK_ITEMS_PATH "items/songbook/items"

static char g_doc_root[256] = ".";

#define SB_OWNED_HANDLER(name, forbidden_msg, extra_flags)                     \
	static int handle_sb_##name(int fd, char *body)                        \
	{                                                                      \
		return with_item_access(                                       \
		        fd, body, SONGBOOK_ITEMS_PATH,                         \
		        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP | extra_flags,   \
		        "Songbook not found", forbidden_msg,                   \
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

SB_OWNED_HANDLER(transpose, "You don't own this songbook", ICTX_CSRF_MPFD)
SB_OWNED_HANDLER(randomize, "You don't own this songbook", ICTX_CSRF_MPFD)
SB_OWNED_HANDLER(song_add, "Forbidden", ICTX_CSRF_QUERY)
SB_OWNED_HANDLER(song_remove, "Forbidden", ICTX_CSRF_QUERY)

static void sb_append_song(
        const char *source_id, const char *pval, const char *song,
        const char *transpose, const char *format)
{
	const char *n[3] = { "song", "transpose", "format" };
	const char *v[3] = { song, transpose, format };
	hyle_source_ordered_append(source_id, pval, n, v, 3);
}

typedef void (*sb_song_cb)(
        const char *key, const char *song_id, int transpose, const char *format,
        void *user);

static void sb_for_each_song(const char *sb_id, sb_song_cb cb, void *user)
{
	int total = hyle_source_ordered_count("songbook.songs", sb_id);
	unsigned fhd = hyle_source_get_fields_hd("songbook.songs");
	for (int i = 0; i < total; i++) {
		const char *key =
		        hyle_source_ordered_key_at("songbook.songs", sb_id, i);
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

static void songbook_meta_read(const char *item_path, songbook_cache_t *meta)
{
	source_meta_read(
	        item_path, songbook_fields, SB_FIELD_COUNT, meta,
	        sizeof(*meta));
}

static int
songbook_meta_write(const char *item_path, const songbook_cache_t *meta)
{
	return source_meta_write(
	        item_path, songbook_fields, SB_FIELD_COUNT, meta);
}

/* Get a random repertoire entry for the given type from the
 * songbook's choir. sb_id is the songbook item ID. */
static int get_random_repertoire_by_type(
        const char *sb_id, const char *type, char *out_id, size_t out_len)
{
	/* Read choir from songbook metadata file */
	char sb_item_path[PATH_MAX];
	snprintf(
	        sb_item_path, sizeof(sb_item_path),
	        "%s/items/songbook/items/%s", g_doc_root, sb_id);
	songbook_cache_t meta;
	songbook_meta_read(sb_item_path, &meta);
	if (!meta.choir[0])
		return -1;

	int total = hyle_source_ordered_count("choir.songs", meta.choir);
	unsigned fhd = hyle_source_get_fields_hd("choir.songs");
	if (!fhd || total == 0)
		return -1;

	/* Collect matching entry IDs (null-terminated packed) */
	char ids[4096] = { 0 };
	size_t id_pos = 0;
	size_t match_count = 0;

	for (int i = 0; i < total; i++) {
		const char *eid = hyle_source_ordered_key_at(
		        "choir.songs", meta.choir, i);
		if (!eid)
			continue;

		const char *fmt = qmap_field_get(fhd, eid, "format");
		const char *ftype = (fmt && fmt[0]) ? fmt : "any";

		if (strcmp(ftype, type) == 0 || strcmp(type, "any") == 0) {
			const char *rs = qmap_field_get(fhd, eid, "song");
			if (rs && id_pos + strlen(rs) + 1 < sizeof(ids)) {
				strcpy(ids + id_pos, rs);
				id_pos += strlen(rs) + 1;
				match_count++;
			}
		}
	}

	if (match_count == 0)
		return -1;

	/* Pick random */
	int pick = rand() % match_count;
	const char *p = ids;
	for (int i = 0; i < pick; i++)
		p += strlen(p) + 1;

	strncpy(out_id, p, out_len - 1);
	out_id[out_len - 1] = '\0';
	return 0;
}

/* POST /songbook/:id/transpose - Transpose single song by index */
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

	mpfd_get("n", n_str, sizeof(n_str) - 1);
	mpfd_get("t", t_str, sizeof(t_str) - 1);
	if (!n_str[0])
		return bad_request(fd, "Missing n");
	idx = atoi(n_str);

	key = hyle_source_ordered_key_at("songbook.songs", ctx->id, idx);
	if (!key)
		return respond_error(fd, 404, "Song not found in songbook");
	char location[256];

	names[0] = "transpose";
	vals[0] = t_str;
	hyle_source_put("songbook.songs", key, names, vals, 1);
	hyle_source_ordered_save("songbook.songs", ctx->id);

	snprintf(location, sizeof(location), "/songbook/%s", ctx->id);
	return axil_redirect(fd, location);
}

/* POST /songbook/:id/randomize - Randomize song by index */
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

	mpfd_get("n", n_str, sizeof(n_str) - 1);
	if (!n_str[0])
		return bad_request(fd, "Missing n");
	idx = atoi(n_str);

	key = hyle_source_ordered_key_at("songbook.songs", ctx->id, idx);
	if (!key)
		return respond_error(fd, 404, "Song not found in songbook");
	fhd = hyle_source_get_fields_hd("songbook.songs");
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
	hyle_source_put("songbook.songs", key, s_names, s_vals, 3);
	hyle_source_ordered_save("songbook.songs", ctx->id);

	snprintf(location, sizeof(location), "/songbook/%s", ctx->id);
	return axil_redirect(fd, location);
}

static void resolve_song_id(char *s_id, size_t s_id_sz)
{
	source_def_t *song_def = source_find("song.items");
	source_def_t *repo_def = source_find("choir.songs");
	if (song_def && repo_def &&
	    qmap_pos(song_def->fields_hd, s_id) == QM_MISS)
	{
		uint32_t rp = qmap_pos(repo_def->fields_hd, s_id);
		if (rp != QM_MISS) {
			const char *rs = qmap_field_get(
			        repo_def->fields_hd, s_id, "song");
			if (rs)
				snprintf(s_id, s_id_sz, "%s", rs);
		}
	}
}

/* POST /api/songbook/:id/songs - Add a song to the songbook */
static int handle_sb_song_add_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	char s_id[128] = { 0 };
	char fmt_val[64] = "any";

	if (axil_query_param("song_id", s_id, sizeof(s_id) - 1) <= 0)
		return bad_request(fd, "Missing song_id");
	datalist_extract_id(s_id, s_id, sizeof(s_id));

	resolve_song_id(s_id, sizeof(s_id));

	axil_query_param("format", fmt_val, sizeof(fmt_val) - 1);
	if (!fmt_val[0])
		snprintf(fmt_val, sizeof(fmt_val), "any");

	sb_append_song("songbook.songs", ctx->id, s_id, "0", fmt_val);

	return redirect_to_item(fd, "songbook", ctx->id);
}

/* POST /api/songbook/:id/song/:n/remove - Remove a song from the songbook */
static int handle_sb_song_remove_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	char n_str[16] = { 0 };
	int idx;

	axil_query_param("n", n_str, sizeof(n_str) - 1);
	idx = atoi(n_str);

	hyle_source_ordered_remove_at("songbook.songs", ctx->id, idx);

	return redirect_to_item(fd, "songbook", ctx->id);
}

static int handle_sb_add(int fd, char *body)
{
	char id[256] = { 0 };
	char choir[128] = { 0 };
	if (index_add_item(fd, body, id, sizeof(id)) != 0)
		return 1;

	int choir_len = mpfd_get("choir", choir, sizeof(choir) - 1);
	if (choir_len > 0) {
		choir[choir_len] = '\0';
		char sb_item_path[512];
		if (item_path_build(
		            fd, "songbook", id, sb_item_path,
		            sizeof(sb_item_path)) != 0)
			return server_error(
			        fd, "Failed to resolve songbook path");

		songbook_cache_t meta;
		songbook_meta_read(sb_item_path, &meta);
		snprintf(meta.choir, sizeof(meta.choir), "%s", choir);
		int meta_wr = songbook_meta_write(sb_item_path, &meta);
		if (meta_wr != 0)
			return server_error(
			        fd, "Failed to write songbook metadata");

		source_refresh_row(fd, "songbook.items", id);

		/* Pre-populate with one random song per choir format type */
		{
			char choir_item_path[PATH_MAX];
			char format_path[PATH_MAX];
			FILE *ffp;

			item_path_build(
			        0, "choir", choir, choir_item_path,
			        sizeof(choir_item_path));
			item_child_path(
			        choir_item_path, "format", format_path,
			        sizeof(format_path));
			ffp = fopen(format_path, "r");
			if (ffp) {
				char type[128];
				while (fgets(type, sizeof(type), ffp)) {
					size_t tlen = strlen(type);
					while (tlen > 0 &&
					       (type[tlen - 1] == '\n' ||
					        type[tlen - 1] == '\r'))
						type[--tlen] = '\0';
					if (tlen == 0)
						continue;
					char song_id[256] = { 0 };
					if (get_random_repertoire_by_type(
					            id, type, song_id,
					            sizeof(song_id)) == 0)
						sb_append_song(
						        "songbook.songs", id,
						        song_id, "0", type);
				}
				fclose(ffp);
			}
		}
		hyle_source_ordered_save("songbook.songs", id);
	} else {
		source_def_t *sb_def = source_find("songbook.items");
		if (sb_def) {
			unsigned dh = qmap_open(
			        NULL, "row_data", QM_STR, QM_STR, 0x1F, 0);
			qmap_put(dh, "choir", "");
			source_update_item(fd, "songbook.items", id, dh);
			qmap_close(dh);
		}
	}

	source_refresh_row(fd, "songbook.items", id);

	char location[512];
	snprintf(location, sizeof(location), "/songbook/%s", id);
	return axil_redirect(fd, location);
}

#include "ux/detail.c"
#include "ux/add.c"
#include "ux/edit.c"

static void
sb_load_song_options(unsigned choir_fhd, const char *choir_id, unsigned song_hd)
{
	if (!choir_id || !choir_id[0] || !song_hd)
		return;
	uint32_t cp = qmap_pos(choir_fhd, choir_id);
	if (cp == QM_MISS)
		return;

	int total = hyle_source_ordered_count("choir.songs", choir_id);
	unsigned fhd = hyle_source_get_fields_hd("choir.songs");
	if (!fhd)
		return;

	for (int j = 0; j < total && sb_app_state.n_song_options < MAX_SB_OPTS;
	     j++)
	{
		const char *key =
		        hyle_source_ordered_key_at("choir.songs", choir_id, j);
		if (!key)
			continue;
		const char *rs = qmap_field_get(fhd, key, "song");
		if (!rs)
			continue;
		const char *st = qmap_get_field_str(song_hd, rs, "title");
		if (!st)
			st = rs;
		sb_song_option_t *o =
		        &g_sb_options[sb_app_state.n_song_options];
		snprintf(o->id, sizeof(o->id), "%s", rs);
		snprintf(o->title, sizeof(o->title), "%s", st);
		sb_app_state.n_song_options++;
	}
}

static char *sb_emit_state_json(void)
{
	json_object *j_root = json_object_new_object();

	source_overlay_from_desc(
	        j_root, &sb_app_state, songbook_app_fields, BUD_OVERLAY_INT,
	        BUD_OVERLAY_STR);

	json_object_object_add(
	        j_root, "songs",
	        source_overlay_array(
	                g_sb_songs, sb_app_state.n_songs, sizeof(g_sb_songs[0]),
	                sb_song_row_fields, BUD_OVERLAY_INT, BUD_OVERLAY_STR));
	json_object_object_add(
	        j_root, "opts",
	        source_overlay_array(
	                g_sb_options, sb_app_state.n_song_options,
	                sizeof(g_sb_options[0]), sb_song_option_fields,
	                BUD_OVERLAY_INT, BUD_OVERLAY_STR));

	const char *json_str = json_object_to_json_string_ext(j_root, 0);
	int req = snprintf(
	        NULL, 0,
	        "<script type=\"application/json\" "
	        "id=\"bud-state\">%s</script>",
	        json_str);
	char *sj = malloc(req + 1);
	if (!sj) {
		json_object_put(j_root);
		return NULL;
	}
	snprintf(
	        sj, req + 1,
	        "<script type=\"application/json\" "
	        "id=\"bud-state\">%s</script>",
	        json_str);
	json_object_put(j_root);
	return sj;
}

/* GET /api/songbook/:id/transpose - Return transposed chord HTML for
 * song at index n (ephemeral — does not persist). Used by WASM bridge. */
static int api_sb_transpose_get(int fd, char *body)
{
	(void)body;
	char id[128] = { 0 };
	char qs[1024] = { 0 };
	char n_str[16] = { 0 }, t_str[16] = { 0 };
	int flags = TRANSP_HTML;
	axil_env_get(fd, id, "PATTERN_PARAM_ID");
	if (!id[0])
		return bad_request(fd, "Missing ID");

	axil_env_get(fd, qs, "QUERY_STRING");
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

		key = hyle_source_ordered_key_at("songbook.songs", id, idx);
		if (!key)
			return respond_error(fd, 404, "Song not found");
		fhd = hyle_source_get_fields_hd("songbook.songs");
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

	const char *tgt_key = target_key_name(detected_key, transpose, flags);

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
	(void)format;
	struct detail_song_ctx *c = user;
	if (sb_app_state.n_songs >= MAX_SB_SONGS)
		return;
	sb_song_row_data_t *sd = &g_sb_songs[sb_app_state.n_songs];
	if (sb_load_song_row(song_id, transpose, c->song_hd, c->f, sd) == 0)
		sb_app_state.n_songs++;
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

struct migrate_song_ctx {
	const char *choir_id;
	int fd;
};

static void migrate_song_cb(
        const char *key, const char *song_id, int transpose, const char *format,
        void *user)
{
	(void)key;
	(void)transpose;
	struct migrate_song_ctx *c = user;

	int total = hyle_source_ordered_count("choir.songs", c->choir_id);
	int found = 0;
	unsigned fhd = hyle_source_get_fields_hd("choir.songs");
	if (fhd) {
		for (int i = 0; i < total; i++) {
			const char *k = hyle_source_ordered_key_at(
			        "choir.songs", c->choir_id, i);
			if (!k)
				continue;
			const char *sid = qmap_field_get(fhd, k, "song");
			if (sid && strcmp(sid, song_id) == 0) {
				found = 1;
				break;
			}
		}
	}

	if (!found) {
		const char *names[] = { "song", "transpose", "format" };
		const char *vals[] = { song_id, "0", format };
		hyle_source_ordered_append(
		        "choir.songs", c->choir_id, names, vals, 3);
	}
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

static int sb_load_edit_song_options(
        unsigned song_hd, unsigned choir_fhd, const char *choir_id,
        const char *song_source, sb_repo_opt_t *options, int max)
{
	int n = 0;
	int use_repertoire =
	        (song_source && strcmp(song_source, "repertoire") == 0);

	if (use_repertoire && choir_id && song_hd && choir_fhd) {
		unsigned repo_hd = source_get_fields_hd("choir.songs");
		if (repo_hd) {
			uint32_t choir_pos = qmap_pos(choir_fhd, choir_id);
			if (choir_pos != UINT32_MAX) {
				uint32_t repo_buf[512];
				size_t n_repo = qmap_inv_get(
				        repo_hd, "choir", choir_pos, repo_buf,
				        512);
				for (size_t i = 0; i < n_repo && n < max; i++) {
					const char *eid = qmap_get_key(
					        repo_hd, repo_buf[i]);
					if (!eid)
						continue;
					const char *repo_s = qmap_field_get(
					        repo_hd, eid, "song");
					if (!repo_s)
						continue;
					char fk[320];
					snprintf(
					        fk, sizeof(fk), "%s:title",
					        repo_s);
					const char *st = qmap_get(song_hd, fk);
					options[n].id = repo_s;
					options[n].title = st ? st : repo_s;
					n++;
				}
			}
		}
	} else if (song_hd) {
		unsigned song_data_hd = source_get_data_hd("song.items");
		if (song_data_hd) {
			uint32_t cur = qmap_iter(song_data_hd, NULL, 0);
			const void *sk, *sv;
			while (qmap_next(&sk, &sv, cur) && n < max) {
				const char *song_id = (const char *)sk;
				char fk[320];
				snprintf(fk, sizeof(fk), "%s:title", song_id);
				const char *st = qmap_get(song_hd, fk);
				options[n].id = song_id;
				options[n].title = st ? st : song_id;
				n++;
			}
			qmap_fin(cur);
		}
	}
	return n;
}

static int
sb_load_user_choirs(const char *user, sb_choir_opt_t *choirs, int max)
{
	int n = 0;
	source_def_t *cd = source_find("choir.items");
	if (!cd || !cd->source_hd)
		return 0;

	uint32_t cur = qmap_iter(cd->source_hd, NULL, 0);
	const void *ck, *cv;
	while (qmap_next(&ck, &cv, cur) && n < max) {
		const char *cid = (const char *)ck;
		char ok[144];
		snprintf(ok, sizeof(ok), "%s:owner", cid);
		const char *o = qmap_get(cd->fields_hd, ok);
		if (!o || strcmp(o, user) != 0)
			continue;
		choirs[n].id = cid;
		char tk[144];
		snprintf(tk, sizeof(tk), "%s:title", cid);
		const char *ct = qmap_get(cd->fields_hd, tk);
		choirs[n].title = ct ? ct : cid;
		n++;
	}
	qmap_fin(cur);
	choirs[n].id = NULL;
	return n;
}

static int sb_load_format_options(char (*buf)[128], const char **opts, int max)
{
	int n = 0;
	snprintf(buf[n], sizeof(buf[n]), "any");
	opts[n] = buf[n];
	n++;

	unsigned type_data_hd = source_get_data_hd("song.types");
	if (!type_data_hd)
		return n;

	unsigned type_fhd = source_get_fields_hd("song.types");
	uint32_t cur = qmap_iter(type_data_hd, NULL, 0);
	const void *tk, *tv;
	while (qmap_next(&tk, &tv, cur) && n < max) {
		const char *type_id = (const char *)tk;
		const char *name = NULL;
		if (type_fhd) {
			char nk[320];
			snprintf(nk, sizeof(nk), "%s:name", type_id);
			name = qmap_get(type_fhd, nk);
		}
		const char *label = name ? name : type_id;
		int dup = 0;
		for (int di = 0; di < n; di++) {
			if (strcmp(opts[di], label) == 0) {
				dup = 1;
				break;
			}
		}
		if (!dup) {
			snprintf(buf[n], sizeof(buf[n]), "%s", label);
			opts[n] = buf[n];
			n++;
		}
	}
	qmap_fin(cur);
	return n;
}

/* ── HTTP handlers ──────────────────────────────────────── */

static void sb_parse_detail_prefs(
        int fd, const char *user, int *t, int *f, int *show_media, int *zoom)
{
	char qs[1024] = { 0 };
	*t = 0;
	*f = TRANSP_HTML;
	*show_media = 0;
	*zoom = VIEWER_ZOOM_DEFAULT;

	axil_env_get(fd, qs, "QUERY_STRING");
	if (qs[0]) {
		int pf;
		parse_transpose_qs(qs, t, &pf, show_media);
		if (pf & TPARAM_BEMOL)
			*f |= TRANSP_BEMOL;
		if (pf & TPARAM_LATIN)
			*f |= TRANSP_LATIN;
		{
			char tmp[16] = { 0 };
			axil_query_param("z", tmp, sizeof(tmp));
			if (tmp[0]) {
				int zv = atoi(tmp);
				if (zv < VIEWER_ZOOM_MIN)
					zv = VIEWER_ZOOM_MIN;
				if (zv > VIEWER_ZOOM_MAX)
					zv = VIEWER_ZOOM_MAX;
				if (user && user[0])
					song_set_viewer_zoom(user, zv);
				*zoom = zv;
			} else if (user && user[0]) {
				*zoom = song_get_viewer_zoom(user);
				if (*zoom < VIEWER_ZOOM_MIN ||
				    *zoom > VIEWER_ZOOM_MAX)
					*zoom = VIEWER_ZOOM_DEFAULT;
			}
		}
	} else if (user && user[0]) {
		char *v;
		v = song_get_pref(user, "chords-bemol");
		if (v) {
			if (atoi(v))
				*f |= TRANSP_BEMOL;
			free(v);
		}
		v = song_get_pref(user, "chords-latin");
		if (v) {
			if (atoi(v))
				*f |= TRANSP_LATIN;
			free(v);
		}
		v = song_get_pref(user, "chords-media");
		if (v) {
			if (atoi(v))
				*show_media = 1;
			free(v);
		}
		*zoom = song_get_viewer_zoom(user);
		if (*zoom < VIEWER_ZOOM_MIN || *zoom > VIEWER_ZOOM_MAX)
			*zoom = VIEWER_ZOOM_DEFAULT;
	}
}

/* ── Detail handler ──────────────────────────────────────── */

static int
songbook_detail_auth(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;
	unsigned sb_hd, song_hd, choir_fhd;
	const char *title, *owner;
	char fkey[512];
	int is_owner = 0;
	char *choir_id = NULL;
	int t, f, show_media, zoom;
	char page_title[256];
	bud_node *layout;
	const char *csrf_token;

	sb_hd = source_get_fields_hd("songbook.items");
	if (!sb_hd)
		return server_error(fd, "No fields_hd");

	title = qmap_get_field_str(sb_hd, ctx->id, "title");
	if (!title)
		return respond_error(fd, 404, "Songbook not found");

	owner = qmap_get_field_str(sb_hd, ctx->id, "owner");
	if (!owner)
		owner = "";

	is_owner =
	        (ctx->username && ctx->username[0] &&
	         strcmp(ctx->username, owner) == 0);

	/* Parse query prefs + zoom */
	sb_parse_detail_prefs(fd, ctx->username, &t, &f, &show_media, &zoom);

	/* Resolve choir ID from songbook reference field */
	{
		snprintf(fkey, sizeof(fkey), "%s:choir", ctx->id);
		const char *choir_id_str = qmap_get(sb_hd, fkey);
		if (choir_id_str && choir_id_str[0])
			choir_id = strdup(choir_id_str);
	}

	choir_fhd = source_get_fields_hd("choir.items");

	snprintf(page_title, sizeof(page_title), "songbook: %s", title);

	/* Open data handles we need throughout */
	song_hd = source_get_fields_hd("song.items");
	if (!song_hd)
		return respond_error(fd, 500, "Failed to open data handles");

	csrf_token = csrf_setup(fd);

	/* Add-song options */
	memset(&sb_app_state, 0, sizeof(sb_app_state));
	sb_load_song_options(choir_fhd, choir_id, song_hd);

	/* Load songs via ordered source */
	{
		struct detail_song_ctx {
			unsigned song_hd;
			int f;
		} detail_ctx = { song_hd, f };
		sb_for_each_song(ctx->id, detail_song_cb, &detail_ctx);
	}

	/* ── Populate sb_app_state with page data ────────────────── */
	sb_app_state.zoom = zoom;
	sb_app_state.bemol = (f & TRANSP_BEMOL) ? 1 : 0;
	sb_app_state.latin = (f & TRANSP_LATIN) ? 1 : 0;
	sb_app_state.show_media = show_media;
	sb_app_state.is_owner = is_owner;

	snprintf(sb_app_state.sb_id, sizeof(sb_app_state.sb_id), "%s", ctx->id);
	snprintf(
	        sb_app_state.path, sizeof(sb_app_state.path), "/songbook/%s",
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
	        sb_app_state.choir_id, sizeof(sb_app_state.choir_id), "%s",
	        choir_id ? choir_id : "");
	snprintf(sb_app_state.owner, sizeof(sb_app_state.owner), "%s", owner);

	/* ── Build page through isomorphic entry point ────────────── */
	layout = bud_app_render();

	/* ── JSON state blob for WASM init (inside <script> tag) ──── */
	{
		char *state_json = sb_emit_state_json();
		site_ui_respond_page(
		        fd, page_title, state_json, "songbook_detail", layout);
		free(state_json);
	}

	/* Free allocated chord data after render */
	for (int i = 0; i < sb_app_state.n_songs; i++) {
		free(g_sb_songs[i].chord_html);
		g_sb_songs[i].chord_html = NULL;
	}
	free(choir_id);

	return 0;
}

static int songbook_detail_handler(int fd, char *body)
{
	return with_item_access(
	        fd, body, SONGBOOK_ITEMS_PATH, 0, "Songbook not found", NULL,
	        songbook_detail_auth, NULL);
}

/* ── Migrate songbook songs to choir repertoire ── */

static int
migrate_songbook_to_choir(int fd, const char *sb_id, const char *choir_id)
{
	struct migrate_song_ctx ctx = { choir_id, fd };
	sb_for_each_song(sb_id, migrate_song_cb, &ctx);
	hyle_source_ordered_save("choir.songs", choir_id);
	return 0;
}
/* ── Edit GET handler ───────────────────────────────────── */

static int
songbook_edit_auth(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;

	unsigned fields_hd = source_get_fields_hd("songbook.items");
	const char *title;
	const char *choir_id;
	unsigned song_hd;
	char song_source[16];
	if (!fields_hd)
		return server_error(fd, "No fields_hd");

	title = qmap_get_field_str(fields_hd, ctx->id, "title");
	if (!title)
		title = "";

	choir_id = qmap_get_field_str(fields_hd, ctx->id, "choir");
	song_hd = source_get_fields_hd("song.items");

	/* Read song_source from meta; default to repertoire when a choir
	 * is assigned */
	song_source[0] = '\0';
	{
		songbook_cache_t sm;
		songbook_meta_read(ctx->item_path, &sm);
		strncpy(song_source, sm.song_source, sizeof(song_source) - 1);
	}
	if (choir_id && !song_source[0])
		strcpy(song_source, "repertoire");

	/* Load current songs via ordered source + compute original keys */
	sb_edit_row_t songs[256];
	int n_songs = sb_load_edit_songs(ctx->id, song_hd, songs, 256);

	/* Map format values from type ID slugs to display names */
	sb_resolve_edit_format_names(songs, n_songs);

	/* Load song options for the select dropdowns */
	unsigned choir_fhd = source_get_fields_hd("choir.items");
	sb_repo_opt_t options[512];
	int n_options = sb_load_edit_song_options(
	        song_hd, choir_fhd, choir_id, song_source, options, 512);

	/* Collect choirs owned by current user */
	sb_choir_opt_t user_choirs[128];
	int n_user_choirs =
	        sb_load_user_choirs(ctx->username, user_choirs, 128);

	/* Read format options from song.types */
	char format_buf[128][128];
	const char *format_opts[128];
	int n_format_opts =
	        sb_load_format_options(format_buf, format_opts, 128);

	const char *csrf_token = csrf_setup(fd);

	char action[256];
	char cancel_href[256];
	snprintf(action, sizeof(action), "/songbook/%s/edit", ctx->id);
	snprintf(cancel_href, sizeof(cancel_href), "/songbook/%s", ctx->id);

	bud_node *form = sb_render_edit_form(
	        action, csrf_token, title, choir_id, n_user_choirs, user_choirs,
	        cancel_href, n_songs, songs, n_options, options, n_format_opts,
	        format_opts, song_source);

	return site_ui_respond_edit_page(
	        fd, ctx->username, "songbook", "ð", title, ctx->id,
	        form);
}

static int songbook_edit_get_handler(int fd, char *body)
{
	return with_item_access(
	        fd, body, SONGBOOK_ITEMS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP, "Songbook not found",
	        NULL, songbook_edit_auth, NULL);
}

/* ── Add GET handler ─────────────────────────────────────── */

static int songbook_add_get_handler(int fd, char *body)
{
	(void)body;
	const char *user = require_user(fd);
	if (!user)
		return 1;

	const char *csrf_token = csrf_setup(fd);

	char qs[512] = { 0 };
	axil_env_get(fd, qs, "QUERY_STRING");
	if (qs[0])
		axil_query_parse(qs);

	char choir_val[128] = { 0 };
	axil_query_param("choir", choir_val, sizeof(choir_val) - 1);

	bud_node *form = sb_render_add_form(csrf_token, choir_val);

	return site_ui_respond_add_page(
	        fd, user, "songbook", "\xf0\x9f\x93\x95", form);
}

/* ── Edit POST handler ───────────────────────────────────── */

static int songbook_edit_post_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;

	/* Hydrate standard fields */
	{
		unsigned dh = source_parse_form("songbook.items");
		if (dh) {
			const char *new_choir = qmap_get(dh, "choir");
			if (new_choir && new_choir[0]) {
				char choir_path[PATH_MAX];
				snprintf(
				        choir_path, sizeof(choir_path),
				        "%s/items/choir/items/%s", g_doc_root,
				        new_choir);
				if (!item_check_ownership(
				            choir_path, ctx->username))
				{
					qmap_close(dh);
					return respond_error(
					        fd, 403,
					        "You don't own this choir");
				}
			}

			char sb_path[PATH_MAX];
			snprintf(
			        sb_path, sizeof(sb_path),
			        "%s/items/songbook/items/%s", g_doc_root,
			        ctx->id);
			item_record_ownership(sb_path, ctx->username);

			source_update_item(fd, "songbook.items", ctx->id, dh);
			qmap_close(dh);
		}
	}

	/* Write songs via ordered source */
	{
		char amount_str[16] = { 0 };
		int amount = 0, i;

		if (mpfd_get("amount", amount_str, sizeof(amount_str) - 1) > 0)
			amount = atoi(amount_str);

		hyle_source_ordered_clear("songbook.songs", ctx->id);

		for (i = 0; i < amount; i++) {
			char song_field[32], key_field[32], fmt_field[32],
			        remove_field[32];
			char remove_val[8] = { 0 };
			char song_val[256] = { 0 };

			snprintf(song_field, sizeof(song_field), "song_%d", i);
			snprintf(key_field, sizeof(key_field), "key_%d", i);
			snprintf(fmt_field, sizeof(fmt_field), "fmt_%d", i);
			snprintf(
			        remove_field, sizeof(remove_field), "remove_%d",
			        i);

			if (mpfd_get(
			            remove_field, remove_val,
			            sizeof(remove_val) - 1) > 0)
				continue;

			if (mpfd_get(
			            song_field, song_val,
			            sizeof(song_val) - 1) <= 0)
				continue;

			/* Extract song ID from "Title [song_id]" format */
			char extracted[128] = { 0 };
			if (datalist_extract_id(
			            song_val, extracted, sizeof(extracted)) !=
			    0)
				continue;

			resolve_song_id(extracted, 128);

			{
				char key_val[16] = { 0 };
				char fmt_val[64] = { 0 };
				mpfd_get(
				        key_field, key_val,
				        sizeof(key_val) - 1);
				mpfd_get(
				        fmt_field, fmt_val,
				        sizeof(fmt_val) - 1);

				sb_append_song(
				        "songbook.songs", ctx->id, extracted,
				        key_val[0] ? key_val : "0",
				        fmt_val[0] ? fmt_val : "any");
			}
		}
		hyle_source_ordered_save("songbook.songs", ctx->id);
	}

	/* Migrate new songs to the choir's repertoire */
	{
		char sb_path[PATH_MAX];
		snprintf(
		        sb_path, sizeof(sb_path), "%s/items/songbook/items/%s",
		        g_doc_root, ctx->id);
		songbook_cache_t meta;
		songbook_meta_read(sb_path, &meta);
		if (meta.choir[0])
			migrate_songbook_to_choir(fd, ctx->id, meta.choir);
	}

	return redirect_to_item(fd, "songbook", ctx->id);
}

static int songbook_edit_post_handler(int fd, char *body)
{
	return with_item_access(
	        fd, body, SONGBOOK_ITEMS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP | ICTX_CSRF_MPFD,
	        "Songbook not found", "You don't own this songbook",
	        songbook_edit_post_authorized, NULL);
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
	xy_load("./mods/choir/choir");

	source_setup(
	        "songbook.items", NULL, sizeof(songbook_cache_t),
	        "items/songbook/items", songbook_fields, SB_FIELD_COUNT, 0);

	/* Register ordered source for songbook songs (data.txt persistence) */
	{
		static const hyle_field_t sb_song_fields[] = {
			{ "song", HYLE_FIELD_REFERENCE, 1, "song.items", NULL,
			  1, 0, 0, 0, 0, NULL },
			{ "transpose", HYLE_FIELD_INT, 1, NULL, NULL, 0, 0, 0,
			  0, 0, NULL },
			{ "format", HYLE_FIELD_STRING, 1, NULL, NULL, 1, 0, 0,
			  0, 16, NULL },
		};
		hyle_source_register_ordered(
		        "songbook.songs", sb_song_fields, 3, "sb", 0,
		        HYLE_AUTO_RECORD, source_dsv_load, source_dsv_save,
		        g_doc_root);
	}

	index_open("Songbook", "songbook.items", NULL, NULL, NULL, NULL, NULL);

	standard_item_handlers_t handlers = {
		.detail = songbook_detail_handler,
		.add_get = songbook_add_get_handler,
		.add_post = handle_sb_add,
		.edit_get = songbook_edit_get_handler,
		.edit_post = songbook_edit_post_handler,
	};
	register_standard_item_handlers("songbook", &handlers);
	axil_register_handler(
	        "POST:/songbook/:id/randomize", handle_sb_randomize);
	axil_register_handler(
	        "POST:/songbook/:id/transpose", handle_sb_transpose);
	axil_register_handler(
	        "POST:/api/songbook/:id/songs", handle_sb_song_add);
	axil_register_handler(
	        "POST:/api/songbook/:id/song/:n/remove", handle_sb_song_remove);
	axil_register_handler(
	        "GET:/api/songbook/:id/transpose", api_sb_transpose_get);
}
