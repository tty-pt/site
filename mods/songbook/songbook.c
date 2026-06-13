#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>
#include <sys/stat.h>

#include <ttypt/axil.h>
#include <ttypt/ndx.h>
#include <ttypt/ndx-mod.h>
#include <ttypt/qmap.h>

#include "../common/common.h"
#include "../source/source.h"

#include "../auth/auth.h"
#include "../mpfd/mpfd.h"
#include "../song/song.h"
#include "fields.h"
#include "../index/index.h"

#define SONGBOOK_ITEMS_PATH "items/songbook/items"
static unsigned songbook_meta_qtype = 0;
static char g_doc_root[256] = ".";

static void songbook_meta_read(const char *item_path, songbook_cache_t *meta)
{
	source_meta_read(
	        item_path,
	        songbook_fields,
	        SB_FIELD_COUNT,
	        meta,
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
	        sb_item_path,
	        sizeof(sb_item_path),
	        "%s/items/songbook/items/%s",
	        g_doc_root,
	        sb_id);
	songbook_cache_t meta;
	songbook_meta_read(sb_item_path, &meta);
	if (!meta.choir[0])
		return -1;

	source_def_t *choir_def = source_find("choir.items");
	source_def_t *repo_def = source_find("choir.repertoire");
	if (!repo_def || !repo_def->fields_hd || !choir_def ||
	    !choir_def->fields_hd)
		return -1;

	uint32_t choir_pos = qmap_pos(choir_def->fields_hd, meta.choir);
	if (choir_pos == UINT32_MAX)
		return -1;

	uint32_t buf[4096];
	size_t n = qmap_inv_get(
	        repo_def->fields_hd, "choir", choir_pos, buf, 4096);
	if (n == 0)
		return -1;

	/* Collect matching entry IDs (null-terminated packed) */
	char ids[4096] = { 0 };
	size_t id_pos = 0;
	size_t match_count = 0;

	for (size_t i = 0; i < n; i++) {
		const char *eid = qmap_get_key(repo_def->fields_hd, buf[i]);
		if (!eid)
			continue;

		const char *fmt =
		        qmap_field_get(repo_def->fields_hd, eid, "format");
		const char *ftype = (fmt && fmt[0]) ? fmt : "any";

		if (strcmp(ftype, type) == 0 || strcmp(type, "any") == 0) {
			size_t elen = strlen(eid);
			if (id_pos + elen + 1 < sizeof(ids)) {
				memcpy(ids + id_pos, eid, elen + 1);
				id_pos += elen + 1;
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

	mpfd_parse(fd, body);
	if (csrf_check_mpfd(fd))
		return 1;

	char n_str[16] = { 0 };
	char t_str[16] = { 0 };
	mpfd_get("n", n_str, sizeof(n_str) - 1);
	mpfd_get("t", t_str, sizeof(t_str) - 1);

	if (!n_str[0])
		return bad_request(fd, "Missing n");

	source_def_t *sis_def = source_find("songbook.item_songs");
	source_def_t *sb_def = source_find("songbook.items");
	if (!sis_def || !sb_def)
		return server_error(fd, "Source not found");

	uint32_t sb_pos = qmap_pos(sb_def->fields_hd, ctx->id);
	int idx = atoi(n_str);
	if (sb_pos != UINT32_MAX) {
		uint32_t buf[4096];
		size_t n = qmap_inv_get(
		        sis_def->fields_hd, "songbook", sb_pos, buf, 4096);
		if (idx >= 0 && (size_t)idx < n) {
			const char *eid =
			        qmap_get_key(sis_def->fields_hd, buf[idx]);
			if (eid) {
				unsigned dh = qmap_open(
				        NULL,
				        "row_data",
				        QM_STR,
				        QM_STR,
				        0x1F,
				        0);
				qmap_put(dh, "transpose", t_str);
				source_update_item(
				        fd, "songbook.item_songs", eid, dh);
				qmap_close(dh);
			}
		}
	}

	char location[256];
	snprintf(location, sizeof(location), "/songbook/%s", ctx->id);
	return axil_redirect(fd, location);
}

static int handle_sb_transpose(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        SONGBOOK_ITEMS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	        "Songbook not found",
	        "You don't own this songbook",
	        handle_sb_transpose_authorized,
	        NULL);
}

/* POST /songbook/:id/randomize - Randomize song by index */
static int handle_sb_randomize_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;

	mpfd_parse(fd, body);
	if (csrf_check_mpfd(fd))
		return 1;

	char n_str[16] = { 0 };
	mpfd_get("n", n_str, sizeof(n_str) - 1);

	if (!n_str[0])
		return bad_request(fd, "Missing n");

	source_def_t *sis_def = source_find("songbook.item_songs");
	source_def_t *sb_def = source_find("songbook.items");
	if (!sis_def || !sb_def)
		return server_error(fd, "Source not found");

	uint32_t sb_pos = qmap_pos(sb_def->fields_hd, ctx->id);
	int idx = atoi(n_str);
	if (sb_pos != UINT32_MAX) {
		uint32_t buf[4096];
		size_t n = qmap_inv_get(
		        sis_def->fields_hd, "songbook", sb_pos, buf, 4096);
		if (idx >= 0 && (size_t)idx < n) {
			const char *eid =
			        qmap_get_key(sis_def->fields_hd, buf[idx]);
			if (!eid)
				goto done;

			/* Get current format type */
			const char *fmt = qmap_field_get(
			        sis_def->fields_hd, eid, "format");
			const char *ftype = (fmt && fmt[0]) ? fmt : "any";

			/* Pick random repertoire entry of same type */
			char new_repo_id[128] = { 0 };
			if (get_random_repertoire_by_type(
			            ctx->id,
			            ftype,
			            new_repo_id,
			            sizeof(new_repo_id)) == 0)
			{
				unsigned dh = qmap_open(
				        NULL,
				        "row_data",
				        QM_STR,
				        QM_STR,
				        0x1F,
				        0);
				qmap_put(dh, "song", new_repo_id);
				source_update_item(
				        fd, "songbook.item_songs", eid, dh);
				qmap_close(dh);
			}
		}
	}
done:
	char location[256];
	snprintf(location, sizeof(location), "/songbook/%s", ctx->id);
	return axil_redirect(fd, location);
}

static int handle_sb_randomize(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        SONGBOOK_ITEMS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	        "Songbook not found",
	        "You don't own this songbook",
	        handle_sb_randomize_authorized,
	        NULL);
}

/* POST /api/songbook/:id/songs - Add a song to the songbook */
static int handle_sb_song_add_authorized(
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

	unsigned dh = source_parse_form("songbook.item_songs");
	if (dh == 0)
		return server_error(fd, "OOM");
	qmap_put(dh, "song", s_id);
	qmap_put(dh, "transpose", "0");
	qmap_put(dh, "songbook", ctx->id);
	if (!qmap_get(dh, "format"))
		qmap_put(dh, "format", "any");
	if (source_update_item(
	            fd, "songbook.item_songs", NULL, dh) != 0) {
		qmap_close(dh);
		return server_error(fd, "Failed to add song to songbook");
	}
	qmap_close(dh);

	return redirect_to_item(fd, "songbook", ctx->id);
}

static int handle_sb_song_add(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        SONGBOOK_ITEMS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	        "Songbook not found",
	        "Forbidden",
	        handle_sb_song_add_authorized,
	        NULL);
}

/* POST /api/songbook/:id/song/:n/remove - Remove a song from the songbook */
static int handle_sb_song_remove_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;

	if (csrf_check_query(fd, body))
		return 1;

	source_def_t *sis_def = source_find("songbook.item_songs");
	source_def_t *sb_def = source_find("songbook.items");
	if (!sis_def || !sb_def)
		return server_error(fd, "Source not found");

	uint32_t sb_pos = qmap_pos(sb_def->fields_hd, ctx->id);
	if (sb_pos == UINT32_MAX)
		return redirect_to_item(fd, "songbook", ctx->id);

	uint32_t buf[4096];
	size_t n =
	        qmap_inv_get(sis_def->fields_hd, "songbook", sb_pos, buf, 4096);

	char n_str[16] = { 0 };
	axil_query_param("n", n_str, sizeof(n_str) - 1);
	int idx = atoi(n_str);

	if (idx >= 0 && (size_t)idx < n) {
		const char *eid = qmap_get_key(sis_def->fields_hd, buf[idx]);
		if (eid)
			source_delete_item(fd, sis_def, eid);
	}

	return redirect_to_item(fd, "songbook", ctx->id);
}

static int handle_sb_song_remove(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        SONGBOOK_ITEMS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	        "Songbook not found",
	        "Forbidden",
	        handle_sb_song_remove_authorized,
	        NULL);
}

static int handle_sb_add(int fd, char *body)
{
	char id[256] = { 0 };
	if (index_add_item(fd, body, id, sizeof(id)) != 0)
		return 1;

	char choir[128] = { 0 };
	int choir_len = mpfd_get("choir", choir, sizeof(choir) - 1);
	if (choir_len > 0) {
		choir[choir_len] = '\0';
		char sb_item_path[512];
		if (item_path_build(
		            fd,
		            "songbook",
		            id,
		            sb_item_path,
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

		/* Pre-populate with one random repertoire entry per choir
		 * format type */
		source_def_t *sis_def = source_find("songbook.item_songs");
		if (sis_def) {
			char choir_item_path[PATH_MAX];
			item_path_build(
			        0,
			        "choir",
			        choir,
			        choir_item_path,
			        sizeof(choir_item_path));
			char format_path[PATH_MAX];
			item_child_path(
			        choir_item_path,
			        "format",
			        format_path,
			        sizeof(format_path));
			FILE *ffp = fopen(format_path, "r");
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
					char repo_id[256] = { 0 };
					if (get_random_repertoire_by_type(
					            id,
					            type,
					            repo_id,
					            sizeof(repo_id)) == 0)
					{
						unsigned dh = qmap_open(
						        NULL,
						        "row_data",
						        QM_STR,
						        QM_STR,
						        0x1F,
						        0);
						qmap_put(dh, "song", repo_id);
						qmap_put(dh, "transpose", "0");
						qmap_put(dh, "format", type);
						qmap_put(dh, "songbook", id);
						source_update_item(
						        fd,
						        "songbook.item_songs",
						        NULL,
						        dh);
						qmap_close(dh);
					}
				}
				fclose(ffp);
			}
		}
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

#include "ux/all.c"

static void sb_load_song_options(
        unsigned choir_fhd,
        const char *choir_id,
        unsigned repo_hd,
        unsigned song_hd)
{
	if (!choir_id || !choir_id[0] || !repo_hd || !song_hd)
		return;
	uint32_t cp = qmap_pos(choir_fhd, choir_id);
	if (cp == QM_MISS)
		return;
	uint32_t buf[512];
	size_t n = qmap_inv_get(repo_hd, "choir", cp, buf, 512);
	for (size_t j = 0; j < n && sb_app_state.n_song_options < MAX_SB_OPTS;
	     j++)
	{
		const char *eid = qmap_get_key(repo_hd, buf[j]);
		if (!eid)
			continue;
		const char *rs = qmap_field_get(repo_hd, eid, "song");
		if (!rs)
			continue;
		const char *st = qmap_get_field_str(song_hd, rs, "title");
		if (!st)
			st = rs;
		sb_song_option_t *o =
		        &g_sb_options[sb_app_state.n_song_options];
		snprintf(o->id, sizeof(o->id), "%s", eid);
		snprintf(o->title, sizeof(o->title), "%s", st);
		sb_app_state.n_song_options++;
	}
}

static void sb_load_song_list(
        unsigned sis_hd,
        uint32_t sb_pos,
        unsigned repo_hd,
        unsigned song_hd,
        int flags)
{
	uint32_t buf[512];
	size_t n = qmap_inv_get(sis_hd, "songbook", sb_pos, buf, 512);
	for (size_t i = 0; i < n; i++) {
		const char *eid = qmap_get_key(sis_hd, buf[i]);
		if (!eid)
			continue;
		const char *sr = qmap_field_get(sis_hd, eid, "song");
		const char *ts = qmap_field_get(sis_hd, eid, "transpose");
		if (!sr)
			continue;
		int tp = ts ? atoi(ts) : 0;
		const char *st = NULL;
		const char *rs = NULL;
		int ok = 0;
		if (repo_hd)
			rs = qmap_field_get(repo_hd, sr, "song");
		if (rs && song_hd) {
			st = qmap_get_field_str(song_hd, rs, "title");
			ok = song_get_original_key(rs);
		}
		if (!st)
			st = sr;
		char *ch = NULL;
		int dk = 0;
		if (rs)
			song_transpose_root(
			        g_doc_root, rs, tp, flags, &ch, &dk);
		if (sb_app_state.n_songs < MAX_SB_SONGS) {
			sb_song_row_data_t *sd =
			        &g_sb_songs[sb_app_state.n_songs];
			memset(sd, 0, sizeof(*sd));
			snprintf(sd->title, sizeof(sd->title), "%s", st);
			if (rs)
				snprintf(
				        sd->song_id,
				        sizeof(sd->song_id),
				        "%s",
				        rs);
			sd->orig_key = ok;
			sd->transpose = tp;
			sd->flags = flags;
			sd->chord_html = ch;
			sb_app_state.n_songs++;
		} else if (ch) {
			free(ch);
		}
	}
}

static char *sb_emit_state_json(void)
{
	json_object *j_root = json_object_new_object();

	json_object_object_add(
	        j_root, "id", json_object_new_string(sb_app_state.sb_id));
	json_object_object_add(
	        j_root, "zoom", json_object_new_int(sb_app_state.zoom));
	json_object_object_add(
	        j_root, "b", json_object_new_int(sb_app_state.bemol));
	json_object_object_add(
	        j_root, "l", json_object_new_int(sb_app_state.latin));
	json_object_object_add(
	        j_root, "owner", json_object_new_int(sb_app_state.is_owner));
	json_object_object_add(
	        j_root, "title", json_object_new_string(sb_app_state.title));
	json_object_object_add(
	        j_root, "user", json_object_new_string(sb_app_state.user));
	json_object_object_add(
	        j_root, "choir", json_object_new_string(sb_app_state.choir_id));
	json_object_object_add(
	        j_root,
	        "csrf",
	        json_object_new_string(sb_app_state.csrf_token));
	json_object_object_add(
	        j_root,
	        "owner_name",
	        json_object_new_string(sb_app_state.owner));
	json_object_object_add(
	        j_root, "n", json_object_new_int(sb_app_state.n_songs));

	json_object *j_songs = json_object_new_array();
	for (int i = 0; i < sb_app_state.n_songs; i++) {
		json_object *j_song = json_object_new_array();
		json_object_array_add(
		        j_song, json_object_new_string(g_sb_songs[i].title));
		json_object_array_add(
		        j_song, json_object_new_string(g_sb_songs[i].song_id));
		json_object_array_add(
		        j_song, json_object_new_int(g_sb_songs[i].orig_key));
		json_object_array_add(
		        j_song, json_object_new_int(g_sb_songs[i].transpose));
		json_object_array_add(
		        j_song, json_object_new_int(g_sb_songs[i].flags));
		json_object_array_add(j_songs, j_song);
	}
	json_object_object_add(j_root, "songs", j_songs);

	json_object *j_opts = json_object_new_array();
	for (int j = 0; j < sb_app_state.n_song_options; j++) {
		json_object *j_opt = json_object_new_array();
		json_object_array_add(
		        j_opt, json_object_new_string(g_sb_options[j].id));
		json_object_array_add(
		        j_opt, json_object_new_string(g_sb_options[j].title));
		json_object_array_add(j_opts, j_opt);
	}
	json_object_object_add(j_root, "opts", j_opts);

	const char *json_str = json_object_to_json_string(j_root);
	char *sj = malloc(65536);
	snprintf(
	        sj,
	        65536,
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
	axil_env_get(fd, id, "PATTERN_PARAM_ID");
	if (!id[0])
		return bad_request(fd, "Missing ID");

	char qs[1024] = { 0 };
	axil_env_get(fd, qs, "QUERY_STRING");
	if (!qs[0])
		return bad_request(fd, "Missing query string");

	axil_query_parse(qs);
	char n_str[16] = { 0 }, t_str[16] = { 0 };
	int flags = TRANSP_HTML;
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

	if (!n_str[0])
		return bad_request(fd, "Missing n");

	int idx = atoi(n_str);
	int transpose = t_str[0] ? atoi(t_str) : 0;

	unsigned sb_hd = source_get_fields_hd("songbook.items");
	unsigned sis_hd = source_get_fields_hd("songbook.item_songs");
	unsigned repo_hd = source_get_fields_hd("choir.repertoire");

	if (!sb_hd || !sis_hd || !repo_hd)
		return server_error(fd, "Source not found");

	uint32_t sb_pos = qmap_pos(sb_hd, id);
	if (sb_pos == QM_MISS)
		return respond_error(fd, 404, "Songbook not found");

	uint32_t buf[4096];
	size_t n = qmap_inv_get(sis_hd, "songbook", sb_pos, buf, 4096);
	if (idx < 0 || (size_t)idx >= n)
		return respond_error(fd, 404, "Song not found");

	const char *eid = qmap_get_key(sis_hd, buf[idx]);
	if (!eid)
		return respond_error(fd, 404, "Entry not found");

	const char *song_ref = qmap_field_get(sis_hd, eid, "song");
	if (!song_ref)
		return respond_error(fd, 404, "Song ref not found");

	const char *repo_song = NULL;
	if (repo_hd)
		repo_song = qmap_field_get(repo_hd, song_ref, "song");

	if (!repo_song)
		return respond_error(fd, 404, "Song not found in repertoire");

	char *chord_html = NULL;
	int detected_key = 0;
	song_transpose_root(
	        g_doc_root,
	        repo_song,
	        transpose,
	        flags,
	        &chord_html,
	        &detected_key);

	const char *tgt_key = target_key_name(detected_key, transpose, flags);

	json_object *j_resp = json_object_new_object();
	json_object_object_add(j_resp, "index", json_object_new_int(idx));
	json_object_object_add(
	        j_resp,
	        "chord_html",
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

/* ── HTTP handlers ──────────────────────────────────────── */

/* ── Detail handler ──────────────────────────────────────── */

static int songbook_detail_handler(int fd, char *body)
{
	(void)body;
	char id[128] = { 0 };
	const char *user = get_request_user(fd);
	unsigned sb_hd, sis_hd, repo_hd, song_hd, choir_fhd;
	const char *title, *owner;
	char fkey[512];
	bud_node *layout;
	int is_owner = 0;
	char page_title[256];
	char *choir_id = NULL;
	const char *csrf_token = csrf_setup(fd);

	axil_env_get(fd, id, "PATTERN_PARAM_ID");
	if (!id[0])
		return bad_request(fd, "Missing ID");

	sb_hd = source_get_fields_hd("songbook.items");
	if (!sb_hd)
		return server_error(fd, "No fields_hd");

	title = qmap_get_field_str(sb_hd, id, "title");
	if (!title)
		return respond_error(fd, 404, "Songbook not found");

	owner = qmap_get_field_str(sb_hd, id, "owner");
	if (!owner)
		owner = "";

	is_owner = (user && user[0] && strcmp(user, owner) == 0);

	/* Parse query prefs */
	int t = 0, f = 0;
	{
		char qs[1024] = { 0 };
		axil_env_get(fd, qs, "QUERY_STRING");
		if (qs[0])
			axil_query_parse(qs);
	}
	{
		char tmp[16] = { 0 };
		axil_query_param("t", tmp, sizeof(tmp));
		if (tmp[0])
			t = atoi(tmp);
	}
	{
		char tmp[16] = { 0 };
		axil_query_param("b", tmp, sizeof(tmp));
		if (tmp[0] && tmp[0] == '1')
			f |= TRANSP_BEMOL;
	}
	{
		char tmp[16] = { 0 };
		axil_query_param("l", tmp, sizeof(tmp));
		if (tmp[0] && tmp[0] == '1')
			f |= TRANSP_LATIN;
	}
	{
		char qs[1024] = { 0 };
		axil_env_get(fd, qs, "QUERY_STRING");
		if (!qs[0] && user && user[0]) {
			char p[PATH_MAX];
			char *v;
			if (user_path_build(
			            user, ".tty/chords-bemol", p, sizeof(p)) ==
			    0)
			{
				v = slurp_file(p);
				if (v) {
					if (atoi(v))
						f |= TRANSP_BEMOL;
					free(v);
				}
			}
			if (user_path_build(
			            user, ".tty/chords-latin", p, sizeof(p)) ==
			    0)
			{
				v = slurp_file(p);
				if (v) {
					if (atoi(v))
						f |= TRANSP_LATIN;
					free(v);
				}
			}
		}
	}

	f |= TRANSP_HTML;

	int zoom = 100;
	if (user && user[0]) {
		zoom = song_get_viewer_zoom(user);
		if (zoom < 70 || zoom > 170)
			zoom = 100;
	}

	/* Resolve choir ID from songbook reference field */
	{
		snprintf(fkey, sizeof(fkey), "%s:choir", id);
		const uint32_t *choir_ref =
		        (const uint32_t *)qmap_get(sb_hd, fkey);
		if (choir_ref && *choir_ref != QM_MISS) {
			choir_fhd = source_get_fields_hd("choir.items");
			if (choir_fhd) {
				const char *cid =
				        qmap_get_key(choir_fhd, *choir_ref);
				if (cid && cid[0])
					choir_id = strdup(cid);
			}
		}
	}

	snprintf(page_title, sizeof(page_title), "songbook: %s", title);

	/* Open data handles we need throughout */
	repo_hd = source_get_fields_hd("choir.repertoire");
	song_hd = source_get_fields_hd("song.items");

	/* Add-song options */
	memset(&sb_app_state, 0, sizeof(sb_app_state));
	sb_load_song_options(choir_fhd, choir_id, repo_hd, song_hd);

	/* Song list */
	sis_hd = source_get_fields_hd("songbook.item_songs");
	if (sis_hd) {
		uint32_t sb_pos = qmap_pos(sb_hd, id);
		if (sb_pos != QM_MISS) {
			uint32_t inv_buf[512];
			size_t n = qmap_inv_get(
			        sis_hd, "songbook", sb_pos, inv_buf, 512);

			/* On-access migration from legacy data.txt
			 * (song_id:transpose:format) — runs whenever
			 * data.txt exists, deleting partial entries first */
			if (choir_id) {
				char ipath[PATH_MAX];
				char dpath[PATH_MAX];
				if (item_path_build(
				            fd,
				            "songbook",
				            id,
				            ipath,
				            sizeof(ipath)) == 0 &&
				    item_child_path(
				            ipath,
				            "data.txt",
				            dpath,
				            sizeof(dpath)) == 0)
				{
					char *raw = slurp_file(dpath);
					if (raw) {
						if (n > 0) {
							source_def_t *sis_def =
							        source_find(
							                "songbo"
							                "ok."
							                "item_"
							                "song"
							                "s");
							if (sis_def) {
								for (size_t i =
								             0;
								     i < n;
								     i++)
								{
									const char *eid = qmap_get_key(
									        sis_hd,
									        inv_buf[i]);
									if (eid)
										source_delete_item(
										        fd,
										        sis_def,
										        eid);
								}
							}
						}

						uint32_t choir_pos = qmap_pos(
						        choir_fhd, choir_id);
						if (choir_pos != QM_MISS) {
							uint32_t repo_buf[512];
							size_t n_repo =
							        qmap_inv_get(
							                repo_hd,
							                "choir",
							                choir_pos,
							                repo_buf,
							                512);
							char *p = raw;
							while (*p) {
								char *nl = strchr(
								        p,
								        '\n');
								size_t llen =
								        nl ? (size_t)(nl -
								                      p)
								           : strlen(p);
								if (llen > 0 &&
								    p[0] != '#')
								{
									char buf[256];
									char entry_id
									        [1024];
									snprintf(
									        buf,
									        sizeof(buf),
									        "%.*s",
									        (int)llen,
									        p);
									char *tr_str = strchr(
									        buf,
									        ':');
									if (tr_str)
									{
										*tr_str++ =
										        '\0';
										char *fmt_str = strchr(
										        tr_str,
										        ':');
										if (fmt_str)
										{
											*fmt_str++ =
											        '\0';
											const char *song_id =
											        buf;
											int transpose =
											        atoi(tr_str);
											const char *repo_id =
											        NULL;
											for (size_t i =
											             0;
											     i <
											     n_repo;
											     i++)
											{
												const char *eid = qmap_get_key(
												        repo_hd,
												        repo_buf[i]);
												if (!eid)
													continue;
												const char *rs = qmap_field_get(
												        repo_hd,
												        eid,
												        "song");
												if (rs &&
												    strcmp(rs,
												           song_id) ==
												            0)
												{
													repo_id =
													        eid;
													break;
												}
											}
											if (!repo_id)
											{
												snprintf(
												        entry_id,
												        sizeof(entry_id),
												        "%s_%s",
												        choir_id,
												        song_id);
												unsigned rdh = source_parse_form(
												        "choir.repertoire");
												if (rdh)
												{
													qmap_put(
													        rdh,
													        "song",
													        song_id);
													qmap_put(
													        rdh,
													        "transpose",
													        "0");
													qmap_put(
													        rdh,
													        "choir",
													        choir_id);
													qmap_put(
													        rdh,
													        "format",
													        fmt_str[0]
													                ? fmt_str
													                : "any");
													source_update_item(
													        fd,
													        "choir.repertoire",
													        entry_id,
													        rdh);
													qmap_close(
													        rdh);
													repo_id =
													        entry_id;
												}
											}
											if (repo_id)
											{
												unsigned dh = source_parse_form(
												        "songbook.item_songs");
												if (dh)
												{
													char t_str
													        [16];
													snprintf(
													        t_str,
													        sizeof(t_str),
													        "%d",
													        transpose);
													qmap_put(
													        dh,
													        "song",
													        repo_id);
													qmap_put(
													        dh,
													        "transpose",
													        t_str);
													qmap_put(
													        dh,
													        "format",
													        fmt_str[0]
													                ? fmt_str
													                : "any");
													qmap_put(
													        dh,
													        "songbook",
													        id);
													source_update_item(
													        fd,
													        "songbook.item_songs",
													        NULL,
													        dh);
													qmap_close(
													        dh);
												}
											}
										}
									}
								}
								if (nl)
									p = nl +
									    1;
								else
									break;
							}
						}
						free(raw);
						source_refresh_row(
						        fd,
						        "songbook."
						        "items",
						        id);
					}
				}
			}
			sb_load_song_list(sis_hd, sb_pos, repo_hd, song_hd, f);
		}
	}

	/* ── Populate sb_app_state with page data ────────────────────── */
	sb_app_state.zoom = zoom;
	sb_app_state.bemol = (f & TRANSP_BEMOL) ? 1 : 0;
	sb_app_state.latin = (f & TRANSP_LATIN) ? 1 : 0;
	sb_app_state.is_owner = is_owner;
	snprintf(sb_app_state.sb_id, sizeof(sb_app_state.sb_id), "%s", id);
	snprintf(
	        sb_app_state.title,
	        sizeof(sb_app_state.title),
	        "%s",
	        page_title);
	snprintf(
	        sb_app_state.user,
	        sizeof(sb_app_state.user),
	        "%s",
	        user ? user : "");
	snprintf(
	        sb_app_state.csrf_token,
	        sizeof(sb_app_state.csrf_token),
	        "%s",
	        csrf_token);
	snprintf(
	        sb_app_state.choir_id,
	        sizeof(sb_app_state.choir_id),
	        "%s",
	        choir_id ? choir_id : "");
	snprintf(sb_app_state.owner, sizeof(sb_app_state.owner), "%s", owner);

	/* ── Build page through isomorphic entry point ────────────────── */
	layout = bud_app_render();

	/* ── JSON state blob for WASM init (inside <script> tag) ──────── */
	{
		char *state_json = sb_emit_state_json();
		site_ui_respond_page(
		        fd, page_title, state_json, "songbook", layout);
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

/* ── Edit GET handler ────────────────────────────────────── */

/* ── Edit GET handler ───────────────────────────────────── */

static int songbook_edit_get_handler(int fd, char *body)
{
	(void)body;
	char id[128] = { 0 };
	const char *user;
	char item_path_buf[512];
	unsigned fields_hd;
	const char *title;
	char fkey[256];

	if (check_item_access(
	            fd,
	            "songbook",
	            id,
	            sizeof(id),
	            &user,
	            item_path_buf,
	            sizeof(item_path_buf)))
		return 1;

	fields_hd = source_get_fields_hd("songbook.items");
	if (!fields_hd)
		return server_error(fd, "No fields_hd");

	title = qmap_get_field_str(fields_hd, id, "title");
	if (!title)
		title = "";

	/* Resolve choir ID from songbook reference field */
	unsigned choir_fhd = 0;
	const char *choir_id = NULL;
	{
		snprintf(fkey, sizeof(fkey), "%s:choir", id);
		const uint32_t *choir_ref =
		        (const uint32_t *)qmap_get(fields_hd, fkey);
		if (choir_ref && *choir_ref != QM_MISS) {
			choir_fhd = source_get_fields_hd("choir.items");
			if (choir_fhd) {
				const char *cid =
				        qmap_get_key(choir_fhd, *choir_ref);
				if (cid && cid[0])
					choir_id = cid;
			}
		}
	}

	unsigned sis_hd = source_get_fields_hd("songbook.item_songs");
	unsigned repo_hd = source_get_fields_hd("choir.repertoire");
	unsigned song_hd = source_get_fields_hd("song.items");

	/* Load current songs */
	sb_edit_row_t songs[256];
	int n_songs = 0;

	uint32_t sb_pos = qmap_pos(fields_hd, id);
	if (sb_pos != UINT32_MAX && sis_hd) {
		uint32_t inv_buf[512];
		size_t n =
		        qmap_inv_get(sis_hd, "songbook", sb_pos, inv_buf, 512);
		for (size_t i = 0; i < n && n_songs < 256; i++) {
			const char *eid = qmap_get_key(sis_hd, inv_buf[i]);
			if (!eid)
				continue;

			const char *song_ref =
			        qmap_field_get(sis_hd, eid, "song");
			const char *t_str =
			        qmap_field_get(sis_hd, eid, "transpose");
			const char *fmt = qmap_field_get(sis_hd, eid, "format");

			if (!song_ref)
				continue;
			if (!fmt)
				fmt = "any";

			const char *s_title = NULL;
			if (repo_hd && song_hd) {
				const char *repo_song = qmap_field_get(
				        repo_hd, song_ref, "song");
				if (repo_song) {
					snprintf(
					        fkey,
					        sizeof(fkey),
					        "%s:title",
					        repo_song);
					s_title = qmap_get(song_hd, fkey);
				}
			}
			if (!s_title)
				s_title = song_ref;

			songs[n_songs].repo_id = song_ref;
			songs[n_songs].title = s_title;
			songs[n_songs].transpose = t_str ? t_str : "";
			songs[n_songs].format = fmt;
			n_songs++;
		}
	}

	/* Load repertoire options for the song select dropdowns */
	sb_repo_opt_t options[512];
	int n_options = 0;

	if (choir_id && choir_fhd && repo_hd && song_hd) {
		uint32_t choir_pos = qmap_pos(choir_fhd, choir_id);
		if (choir_pos != UINT32_MAX) {
			uint32_t repo_buf[512];
			size_t n_repo = qmap_inv_get(
			        repo_hd, "choir", choir_pos, repo_buf, 512);
			for (size_t i = 0; i < n_repo && n_options < 512; i++) {
				const char *eid =
				        qmap_get_key(repo_hd, repo_buf[i]);
				if (!eid)
					continue;

				const char *repo_s =
				        qmap_field_get(repo_hd, eid, "song");
				if (!repo_s)
					continue;

				snprintf(
				        fkey, sizeof(fkey), "%s:title", repo_s);
				const char *s_title = qmap_get(song_hd, fkey);
				if (!s_title)
					s_title = repo_s;

				options[n_options].id = eid;
				options[n_options].title = s_title;
				n_options++;
			}
		}
	}

	const char *csrf_token = csrf_setup(fd);

	char action[256];
	char cancel_href[256];
	snprintf(action, sizeof(action), "/songbook/%s/edit", id);
	snprintf(cancel_href, sizeof(cancel_href), "/songbook/%s", id);

	bud_node *form = sb_render_edit_form(
	        action,
	        csrf_token,
	        title,
	        cancel_href,
	        n_songs,
	        songs,
	        n_options,
	        options);

	return site_ui_respond_edit_page(
	        fd, user, "songbook", "\xf0\x9f\x93\x95", title, id, form);
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

	mpfd_parse(fd, body);
	if (csrf_check_mpfd(fd))
		return 1;

	/* Update title */
	{
		char title[256] = { 0 };
		int title_len = mpfd_get("title", title, sizeof(title) - 1);
		if (title_len > 0) {
			unsigned dh = source_parse_form("songbook.items");
			if (dh) {
				qmap_put(dh, "title", title);
				source_update_item(
				        fd, "songbook.items", ctx->id, dh);
				qmap_close(dh);
			}
		}
	}

	/* Delete existing songs before recreating from form data */
	{
		source_def_t *sis_def = source_find("songbook.item_songs");
		source_def_t *sb_def = source_find("songbook.items");
		if (sis_def && sb_def) {
			uint32_t del_pos = qmap_pos(sb_def->fields_hd, ctx->id);
			if (del_pos != UINT32_MAX) {
				uint32_t del_buf[4096];
				size_t n_del = qmap_inv_get(
				        sis_def->fields_hd,
				        "songbook",
				        del_pos,
				        del_buf,
				        4096);
				for (size_t i = 0; i < n_del; i++) {
					const char *eid = qmap_get_key(
					        sis_def->fields_hd, del_buf[i]);
					if (eid)
						source_delete_item(
						        fd, sis_def, eid);
				}
			}
		}
	}

	/* Process song rows */
	{
		char amount_str[16] = { 0 };
		int amount = 0;
		if (mpfd_get("amount", amount_str, sizeof(amount_str) - 1) > 0)
			amount = atoi(amount_str);

		for (int i = 0; i < amount; i++) {
			char song_field[32], key_field[32], fmt_field[32];
			snprintf(song_field, sizeof(song_field), "song_%d", i);
			snprintf(key_field, sizeof(key_field), "key_%d", i);
			snprintf(fmt_field, sizeof(fmt_field), "fmt_%d", i);

			char song_val[256] = { 0 };
			if (mpfd_get(
			            song_field,
			            song_val,
			            sizeof(song_val) - 1) <= 0)
				continue;

			/* Extract repo ID from "Title [repo_id]" format */
			char *repo_id = strrchr(song_val, '[');
			if (repo_id) {
				repo_id++;
				char *end = strrchr(repo_id, ']');
				if (end)
					*end = '\0';
			} else {
				continue;
			}

			char key_val[16] = { 0 };
			mpfd_get(key_field, key_val, sizeof(key_val) - 1);
			char fmt_val[64] = { 0 };
			mpfd_get(fmt_field, fmt_val, sizeof(fmt_val) - 1);

			unsigned dh = source_parse_form("songbook.item_songs");
			if (dh == 0)
				continue;
			qmap_put(dh, "song", repo_id);
			qmap_put(dh, "transpose", key_val[0] ? key_val : "0");
			qmap_put(dh, "songbook", ctx->id);
			qmap_put(dh, "format", fmt_val[0] ? fmt_val : "any");
			source_update_item(fd, "songbook.item_songs", NULL, dh);
			qmap_close(dh);
		}
	}

	return redirect_to_item(fd, "songbook", ctx->id);
}

static int songbook_edit_post_handler(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        SONGBOOK_ITEMS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	        "Songbook not found",
	        "Forbidden",
	        songbook_edit_post_authorized,
	        NULL);
}

void ndx_install(void)
{
	char doc_root[256] = { 0 };
	resolve_doc_root(0, doc_root, sizeof(doc_root));
	strncpy(g_doc_root, doc_root, sizeof(g_doc_root) - 1);

	ndx_load("./mods/index/index");
	ndx_load("./mods/mpfd/mpfd");
	ndx_load("./mods/song/song");
	ndx_load("./mods/source/source");
	ndx_load("./mods/choir/choir");

	source_setup(
	        "songbook.items",
	        NULL,
	        sizeof(songbook_cache_t),
	        "items/songbook/items",
	        songbook_fields,
	        SB_FIELD_COUNT,
	        0);
	source_setup(
	        "songbook.item_songs",
	        NULL,
	        sizeof(songbook_item_song_cache_t),
	        "items/songbook/item_songs",
	        songbook_item_song_fields,
	        SB_ITEM_SONG_FIELD_COUNT,
	        QM_AINDEX);

	index_open(
	        "Songbook",
	        "songbook.items",
	        NULL,
	        songbook_detail_handler,
	        handle_sb_add,
	        songbook_edit_get_handler,
	        songbook_edit_post_handler);

	axil_register_handler("GET:/songbook/add", songbook_add_get_handler);
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

	/* Ensure item_songs storage directory exists */
	{
		char is_path[PATH_MAX];
		snprintf(
		        is_path,
		        sizeof(is_path),
		        "%s/items/songbook/item_songs",
		        g_doc_root);
		mkdir(is_path, 0755);
	}
}
