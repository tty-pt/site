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
			const char *rs = qmap_field_get(
			        repo_def->fields_hd, eid, "song");
			if (!rs)
				continue;
			size_t elen = strlen(rs);
			if (id_pos + elen + 1 < sizeof(ids)) {
				memcpy(ids + id_pos, rs, elen + 1);
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

	int idx = atoi(n_str);

	char dpath[PATH_MAX], tmp[PATH_MAX + 4];
	snprintf(
	        dpath,
	        sizeof(dpath),
	        "%s/items/songbook/items/%s/data.txt",
	        g_doc_root,
	        ctx->id);
	snprintf(tmp, sizeof(tmp), "%s.tmp", dpath);

	char *raw = slurp_file(dpath);
	if (!raw)
		return server_error(fd, "No data.txt");

	FILE *fp = fopen(tmp, "w");
	if (!fp) {
		free(raw);
		return server_error(fd, "Failed to write");
	}

	int line = 0;
	char *p = raw;
	while (*p) {
		char *nl = strchr(p, '\n');
		size_t llen = nl ? (size_t)(nl - p) : strlen(p);
		if (line == idx && llen > 0) {
			char buf[256];
			snprintf(buf, sizeof(buf), "%.*s", (int)llen, p);
			char *first = strchr(buf, ':');
			if (first) {
				*first++ = '\0';
				char *second = strchr(first, ':');
				if (second) {
					*second++ = '\0';
					fprintf(fp,
					        "%s:%s:%s\n",
					        buf,
					        t_str,
					        second);
					goto next_line;
				}
			}
		}
		fprintf(fp, "%.*s\n", (int)llen, p);
	next_line:
		if (nl)
			p = nl + 1;
		else
			break;
		line++;
	}

	fclose(fp);
	free(raw);
	rename(tmp, dpath);

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

	int idx = atoi(n_str);

	char dpath[PATH_MAX], tmp[PATH_MAX + 4];
	snprintf(
	        dpath,
	        sizeof(dpath),
	        "%s/items/songbook/items/%s/data.txt",
	        g_doc_root,
	        ctx->id);
	snprintf(tmp, sizeof(tmp), "%s.tmp", dpath);

	char *raw = slurp_file(dpath);
	if (!raw)
		return server_error(fd, "No data.txt");

	/* Find the format of the current song at idx */
	char fmt_str[64] = "any";
	{
		int line = 0;
		char *p = raw;
		while (*p && line <= idx) {
			char *nl = strchr(p, '\n');
			size_t llen = nl ? (size_t)(nl - p) : strlen(p);
			if (line == idx && llen > 0) {
				char *first = strchr(p, ':');
				if (first && (size_t)(first - p) < llen) {
					char *second = strchr(first + 1, ':');
					if (second &&
					    (size_t)(second - p) < llen)
					{
						size_t flen = (p + llen) -
						              (second + 1);
						if (flen >= sizeof(fmt_str))
							flen = sizeof(fmt_str) -
							       1;
						memcpy(fmt_str,
						       second + 1,
						       flen);
						fmt_str[flen] = '\0';
					}
				}
				break;
			}
			if (nl)
				p = nl + 1;
			else
				break;
			line++;
		}
	}

	/* Pick a random song of the same format */
	char new_song_id[128] = { 0 };
	if (get_random_repertoire_by_type(
	            ctx->id, fmt_str, new_song_id, sizeof(new_song_id)) != 0)
	{
		free(raw);
		return server_error(fd, "No songs found for format");
	}

	/* Rewrite data.txt with the new song at idx */
	FILE *fp = fopen(tmp, "w");
	if (!fp) {
		free(raw);
		return server_error(fd, "Failed to write");
	}

	int line = 0;
	char *p = raw;
	while (*p) {
		char *nl = strchr(p, '\n');
		size_t llen = nl ? (size_t)(nl - p) : strlen(p);
		if (line == idx && llen > 0) {
			char *first = strchr(p, ':');
			if (first && (size_t)(first - p) < llen) {
				/* Replace song_id, reset transpose to 0 */
				fprintf(fp, "%s:0:%s\n", new_song_id, fmt_str);
				goto next_line_r;
			}
		}
		fprintf(fp, "%.*s\n", (int)llen, p);
	next_line_r:
		if (nl)
			p = nl + 1;
		else
			break;
		line++;
	}

	fclose(fp);
	free(raw);
	rename(tmp, dpath);

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

	/* Resolve repertoire entry IDs to bare song IDs for compatibility */
	{
		source_def_t *song_def = source_find("song.items");
		source_def_t *repo_def = source_find("choir.repertoire");
		if (song_def && repo_def &&
		    qmap_pos(song_def->fields_hd, s_id) == QM_MISS)
		{
			uint32_t rp = qmap_pos(repo_def->fields_hd, s_id);
			if (rp != QM_MISS) {
				const char *rs = qmap_field_get(
				        repo_def->fields_hd, s_id, "song");
				if (rs)
					snprintf(s_id, sizeof(s_id), "%s", rs);
			}
		}
	}

	char fmt_val[64] = "any";
	axil_query_param("format", fmt_val, sizeof(fmt_val) - 1);
	if (!fmt_val[0])
		snprintf(fmt_val, sizeof(fmt_val), "any");

	char dpath[PATH_MAX];
	snprintf(
	        dpath,
	        sizeof(dpath),
	        "%s/items/songbook/items/%s/data.txt",
	        g_doc_root,
	        ctx->id);

	FILE *fp = fopen(dpath, "a");
	if (!fp)
		return server_error(fd, "Failed to open data.txt");
	fprintf(fp, "%s:0:%s\n", s_id, fmt_val);
	fclose(fp);

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

	char n_str[16] = { 0 };
	axil_query_param("n", n_str, sizeof(n_str) - 1);
	int idx = atoi(n_str);

	char dpath[PATH_MAX], tmp[PATH_MAX + 4];
	snprintf(
	        dpath,
	        sizeof(dpath),
	        "%s/items/songbook/items/%s/data.txt",
	        g_doc_root,
	        ctx->id);
	snprintf(tmp, sizeof(tmp), "%s.tmp", dpath);

	char *raw = slurp_file(dpath);
	if (!raw)
		return redirect_to_item(fd, "songbook", ctx->id);

	FILE *fp = fopen(tmp, "w");
	if (!fp) {
		free(raw);
		return server_error(fd, "Failed to write");
	}

	int line = 0;
	char *p = raw;
	while (*p) {
		char *nl = strchr(p, '\n');
		size_t llen = nl ? (size_t)(nl - p) : strlen(p);
		if (llen > 0 && line != idx)
			fprintf(fp, "%.*s\n", (int)llen, p);
		if (nl)
			p = nl + 1;
		else
			break;
		line++;
	}

	fclose(fp);
	free(raw);
	rename(tmp, dpath);

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

		/* Pre-populate data.txt with one random song per choir
		 * format type */
		{
			char dpath[PATH_MAX];
			snprintf(
			        dpath,
			        sizeof(dpath),
			        "%s/items/songbook/items/%s/data.txt",
			        g_doc_root,
			        id);
			FILE *fp = fopen(dpath, "w");
			if (fp) {
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
						       (type[tlen - 1] ==
						                '\n' ||
						        type[tlen - 1] == '\r'))
							type[--tlen] = '\0';
						if (tlen == 0)
							continue;
						char song_id[256] = { 0 };
						if (get_random_repertoire_by_type(
						            id,
						            type,
						            song_id,
						            sizeof(song_id)) ==
						    0)
							fprintf(fp,
							        "%s:0:%s\n",
							        song_id,
							        type);
					}
					fclose(ffp);
				}
				fclose(fp);
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
		snprintf(o->id, sizeof(o->id), "%s", rs);
		snprintf(o->title, sizeof(o->title), "%s", st);
		sb_app_state.n_song_options++;
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
	json_object_object_add(
	        j_root, "m", json_object_new_int(sb_app_state.show_media));

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
		json_object_array_add(
		        j_song, json_object_new_string(g_sb_songs[i].yt));
		json_object_array_add(
		        j_song, json_object_new_string(g_sb_songs[i].audio));
		json_object_array_add(
		        j_song, json_object_new_string(g_sb_songs[i].pdf));
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

	const char *json_str = json_object_to_json_string_ext(j_root, 0);
	int json_len = strlen(json_str);
	int req = snprintf(
	        NULL,
	        0,
	        "<script type=\"application/json\" "
	        "id=\"bud-state\">%s</script>",
	        json_str);
	char *sj = malloc(req + 1);
	if (!sj) {
		json_object_put(j_root);
		return NULL;
	}
	snprintf(
	        sj,
	        req + 1,
	        "<script type=\"application/json\" "
	        "id=\"bud-state\">%s</script>",
	        json_str);
	fprintf(stderr,
	        "DIAG sb_emit_state_json: json_len=%d buf_len=%d\n",
	        json_len,
	        req + 1);
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
	(void)axil_query_param(
	        "m", (char[4]){ 0 }, 4); /* consumed for consistency */

	if (!n_str[0])
		return bad_request(fd, "Missing n");

	int idx = atoi(n_str);
	int transpose = t_str[0] ? atoi(t_str) : 0;

	/* Read song_id and transpose from data.txt line N */
	char dpath[PATH_MAX];
	snprintf(
	        dpath,
	        sizeof(dpath),
	        "%s/items/songbook/items/%s/data.txt",
	        g_doc_root,
	        id);
	char *raw = slurp_file(dpath);
	if (!raw)
		return respond_error(fd, 404, "Songbook not found");

	char found_song[256] = { 0 };
	int line = 0;
	int found = 0;
	char *p = raw;
	while (*p && !found) {
		char *nl = strchr(p, '\n');
		size_t llen = nl ? (size_t)(nl - p) : strlen(p);
		if (line == idx && llen > 0) {
			char buf[256];
			snprintf(buf, sizeof(buf), "%.*s", (int)llen, p);
			char *first = strchr(buf, ':');
			if (first) {
				*first++ = '\0';
				char *second = strchr(first, ':');
				if (second) {
					*second++ = '\0';
					snprintf(
					        found_song,
					        sizeof(found_song),
					        "%s",
					        buf);
					if (t_str[0])
						transpose = atoi(t_str);
					else
						transpose = atoi(first);
					found = 1;
				}
			}
		}
		if (nl)
			p = nl + 1;
		else
			break;
		line++;
	}

	if (!found || !found_song[0]) {
		free(raw);
		return respond_error(fd, 404, "Song not found");
	}

	char *chord_html = NULL;
	int detected_key = 0;
	song_transpose_root(
	        g_doc_root,
	        found_song,
	        transpose,
	        flags,
	        &chord_html,
	        &detected_key);
	free(raw);

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

/* ── Error response helper (HTML page instead of raw JSON) ─ */

static int songbook_respond_error(int fd, const char *msg)
{
	bud_node *p = lx_el("p", lx_text(msg)).data.node;
	char *html = site_ui_page("Error", NULL, NULL, p);
	axil_header_set(fd, "Content-Type", "text/html; charset=utf-8");
	axil_respond(fd, 500, html);
	free(html);
	return 0;
}

/* ── HTTP handlers ──────────────────────────────────────── */

/* ── Detail handler ──────────────────────────────────────── */

static int songbook_detail_handler(int fd, char *body)
{
	(void)body;
	char id[128] = { 0 };
	const char *user = get_request_user(fd);
	unsigned sb_hd, repo_hd, song_hd, choir_fhd;
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
	int t = 0, f = TRANSP_HTML, show_media = 0;
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
		char tmp[16] = { 0 };
		axil_query_param("m", tmp, sizeof(tmp));
		if (tmp[0] && tmp[0] == '1')
			show_media = 1;
	}
	{
		char qs[1024] = { 0 };
		axil_env_get(fd, qs, "QUERY_STRING");
		if (!qs[0] && user && user[0]) {
			char *v;
			v = song_get_pref(user, "chords-bemol");
			if (v) {
				if (atoi(v))
					f |= TRANSP_BEMOL;
				free(v);
			}
			v = song_get_pref(user, "chords-latin");
			if (v) {
				if (atoi(v))
					f |= TRANSP_LATIN;
				free(v);
			}
			v = song_get_pref(user, "chords-media");
			if (v) {
				if (atoi(v))
					show_media = 1;
				free(v);
			}
		}
	}

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
	if (!repo_hd || !song_hd)
		return songbook_respond_error(
		        fd, "Failed to open data handles");

	/* Add-song options */
	memset(&sb_app_state, 0, sizeof(sb_app_state));
	sb_load_song_options(choir_fhd, choir_id, repo_hd, song_hd);

	/* Load songs from data.txt */
	{
		char dpath[PATH_MAX];
		snprintf(
		        dpath,
		        sizeof(dpath),
		        "%s/items/songbook/items/%s/data.txt",
		        g_doc_root,
		        id);
		struct stat st;
		if (stat(dpath, &st) == 0 && S_ISREG(st.st_mode)) {
			char *raw = slurp_file(dpath);
			if (raw) {
				char *p = raw;
				while (*p &&
				       sb_app_state.n_songs < MAX_SB_SONGS)
				{
					char *nl = strchr(p, '\n');
					size_t llen = nl ? (size_t)(nl - p)
					                 : strlen(p);
					if (llen > 0 && llen < 255 &&
					    p[0] != '#')
					{
						char buf[256];
						snprintf(
						        buf,
						        sizeof(buf),
						        "%.*s",
						        (int)llen,
						        p);
						char *tr_str = strchr(buf, ':');
						if (tr_str) {
							*tr_str++ = '\0';
							char *fmt_str = strchr(
							        tr_str, ':');
							if (fmt_str) {
								*fmt_str++ =
								        '\0';
								const char *song_id =
								        buf;
								int transpose = atoi(
								        tr_str);
								if (qmap_pos(
								            song_hd,
								            song_id) !=
								    QM_MISS)
								{
									const char *st = qmap_get_field_str(
									        song_hd,
									        song_id,
									        "title");
									if (!st)
										st = song_id;
									int ok = song_get_original_key(
									        song_id);
									char *ch =
									        NULL;
									int dk =
									        0;
									song_transpose_root(
									        g_doc_root,
									        song_id,
									        transpose,
									        f,
									        &ch,
									        &dk);
									sb_song_row_data_t *sd =
									        &g_sb_songs
									                [sb_app_state
									                         .n_songs];
									memset(sd,
									       0,
									       sizeof(*sd));
									snprintf(
									        sd->title,
									        sizeof(sd->title),
									        "%s",
									        st);
									snprintf(
									        sd->song_id,
									        sizeof(sd->song_id),
									        "%s",
									        song_id);
									sd->orig_key =
									        ok;
									sd->transpose =
									        transpose;
									sd->flags =
									        f;
									{
										const char *_yt = qmap_get_field_str(
										        song_hd,
										        song_id,
										        "yt");
										const char *_audio = qmap_get_field_str(
										        song_hd,
										        song_id,
										        "audio");
										const char *_pdf = qmap_get_field_str(
										        song_hd,
										        song_id,
										        "pdf");
										if (_yt)
											snprintf(
											        sd->yt,
											        sizeof(sd->yt),
											        "%s",
											        _yt);
										if (_audio)
											snprintf(
											        sd->audio,
											        sizeof(sd->audio),
											        "%s",
											        _audio);
										if (_pdf)
											snprintf(
											        sd->pdf,
											        sizeof(sd->pdf),
											        "%s",
											        _pdf);
									}
									sd->chord_html =
									        ch;
									sb_app_state
									        .n_songs++;
								}
							}
						}
					}
					if (nl)
						p = nl + 1;
					else
						break;
				}
				free(raw);
			}
		}
	}

	/* ── Populate sb_app_state with page data ────────────────────── */
	sb_app_state.zoom = zoom;
	sb_app_state.bemol = (f & TRANSP_BEMOL) ? 1 : 0;
	sb_app_state.latin = (f & TRANSP_LATIN) ? 1 : 0;
	sb_app_state.show_media = show_media;
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

/* ── POST /songbook/:id/migrate — run data.txt migration ── */

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

	unsigned repo_hd = source_get_fields_hd("choir.repertoire");
	unsigned song_hd = source_get_fields_hd("song.items");

	/* Load current songs from data.txt */
	sb_edit_row_t songs[256];
	int n_songs = 0;

	{
		char dpath[PATH_MAX];
		snprintf(
		        dpath,
		        sizeof(dpath),
		        "%s/items/songbook/items/%s/data.txt",
		        g_doc_root,
		        id);
		struct stat st;
		if (stat(dpath, &st) == 0 && S_ISREG(st.st_mode)) {
			char *raw = slurp_file(dpath);
			if (raw) {
				char *p = raw;
				while (*p && n_songs < 256) {
					char *nl = strchr(p, '\n');
					size_t llen = nl ? (size_t)(nl - p)
					                 : strlen(p);
					if (llen > 0 && p[0] != '#') {
						char buf[256];
						snprintf(
						        buf,
						        sizeof(buf),
						        "%.*s",
						        (int)llen,
						        p);
						char *first = strchr(buf, ':');
						if (first) {
							*first++ = '\0';
							char *second = strchr(
							        first, ':');
							if (second) {
								*second++ =
								        '\0';
								const char *sid =
								        buf;
								const char *s_title = qmap_get_field_str(
								        song_hd,
								        sid,
								        "titl"
								        "e");
								if (!s_title)
									s_title =
									        sid;
								songs[n_songs]
								        .repo_id =
								        sid;
								songs[n_songs]
								        .title =
								        s_title;
								songs[n_songs]
								        .transpose =
								        first;
								songs[n_songs]
								        .format =
								        second;
								n_songs++;
							}
						}
					}
					if (nl)
						p = nl + 1;
					else
						break;
				}
				free(raw);
			}
		}
	}

	/* Load song options for the select dropdowns (use song IDs) */
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

				options[n_options].id = repo_s;
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

	/* Write songs to data.txt */
	{
		char dpath[PATH_MAX], tmp[PATH_MAX + 4];
		snprintf(
		        dpath,
		        sizeof(dpath),
		        "%s/items/songbook/items/%s/data.txt",
		        g_doc_root,
		        ctx->id);
		snprintf(tmp, sizeof(tmp), "%s.tmp", dpath);

		char amount_str[16] = { 0 };
		int amount = 0;
		if (mpfd_get("amount", amount_str, sizeof(amount_str) - 1) > 0)
			amount = atoi(amount_str);

		FILE *fp = fopen(tmp, "w");
		if (!fp)
			return server_error(fd, "Failed to write data.txt");

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

			/* Extract song ID from "Title [song_id]" format */
			char *sid = strrchr(song_val, '[');
			if (sid) {
				sid++;
				char *end = strrchr(sid, ']');
				if (end)
					*end = '\0';
			} else {
				continue;
			}

			char key_val[16] = { 0 };
			mpfd_get(key_field, key_val, sizeof(key_val) - 1);
			char fmt_val[64] = { 0 };
			mpfd_get(fmt_field, fmt_val, sizeof(fmt_val) - 1);

			fprintf(fp,
			        "%s:%s:%s\n",
			        sid,
			        key_val[0] ? key_val : "0",
			        fmt_val[0] ? fmt_val : "any");
		}

		fclose(fp);
		rename(tmp, dpath);
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
}
