#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>
#include <sys/stat.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx.h>
#include <ttypt/ndx-mod.h>
#include <ttypt/qmap.h>

#include "../common/common.h"
#include "../source/source.h"

#include "../auth/auth.h"
#include "../mpfd/mpfd.h"
#include "../index/index.h"

#define SONGBOOK_ITEMS_PATH "items/songbook/items"
static unsigned index_hd = 0;
static unsigned songbook_meta_qtype = 0;
static char g_doc_root[256] = ".";

typedef struct {
	char title[256];
	char choir[128];
} songbook_meta_t;

typedef struct {
	char id[64];
	char title[256];
	uint32_t choir;
	char owner[32];
} songbook_item_t;

typedef struct {
	char id[64];
	uint32_t song; /* QM_REFERENCE → choir.repertoire */
	char transpose[16];
	char format[64];
	uint32_t songbook; /* QM_REFERENCE → songbook.items */
} songbook_item_song_t;

static uint32_t songbook_record_id;

static const qmap_record_field_t songbook_record_fields_basic[] = {
	{ "id",
	  QM_STR,
	  offsetof(songbook_item_t, id),
	  sizeof(((songbook_item_t *)0)->id) },
	{ "title",
	  QM_STR,
	  offsetof(songbook_item_t, title),
	  sizeof(((songbook_item_t *)0)->title) },
	{ "choir", QM_STR, offsetof(songbook_item_t, choir), sizeof(uint32_t) },
	{ "owner",
	  QM_STR,
	  offsetof(songbook_item_t, owner),
	  sizeof(((songbook_item_t *)0)->owner) },
};
#define SONGBOOK_FIELD_COUNT                                                   \
	(sizeof(songbook_record_fields_basic) /                                \
	 sizeof(songbook_record_fields_basic[0]))

static const source_field_t songbook_items_fields[] = {
	{ "id", NULL, SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "title", "title", SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "choir",
	  "choir",
	  SOURCE_FIELD_REFERENCE,
	  1,
	  "choir.items",
	  "songbooks" },
	{ "item_songs",
	  NULL,
	  SOURCE_FIELD_INVERSE,
	  0,
	  "songbook.item_songs",
	  "songbook" },
	{ "owner", "owner", SOURCE_FIELD_STRING, 0, NULL, NULL },
};

static const source_field_t songbook_item_songs_fields[] = {
	{ "id", NULL, SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "song",
	  "song",
	  SOURCE_FIELD_REFERENCE,
	  1,
	  "choir.repertoire",
	  "in_songbooks" },
	{ "transpose", "transpose", SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "format", "format", SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "songbook",
	  "songbook",
	  SOURCE_FIELD_REFERENCE,
	  1,
	  "songbook.items",
	  "item_songs" },
};

static const source_def_t songbook_item_songs_def = {
	.id = "songbook.item_songs",
	.key_field = "id",
	.items_path = "items/songbook/item_songs",
	.access_policy = SOURCE_ACCESS_PUBLIC,
	.fields = songbook_item_songs_fields,
	.field_count = sizeof(songbook_item_songs_fields) /
	               sizeof(songbook_item_songs_fields[0]),
	.flags = QM_AINDEX,
};

static const source_def_t songbook_items_def = {
	.id = "songbook.items",
	.key_field = "id",
	.items_path = "items/songbook/items",
	.access_policy = SOURCE_ACCESS_PUBLIC,
	.fields = songbook_items_fields,
	.field_count = sizeof(songbook_items_fields) /
	               sizeof(songbook_items_fields[0]),
};

static void songbook_meta_read(const char *item_path, songbook_meta_t *meta)
{
	meta_field_t fields[] = {
		{ "title", meta->title, sizeof(meta->title) },
		{ "choir", meta->choir, sizeof(meta->choir) },
	};

	memset(meta, 0, sizeof(*meta));
	META_READ(item_path, fields);
}

static int
songbook_meta_write(const char *item_path, const songbook_meta_t *meta)
{
	meta_field_t fields[] = {
		{ "title", (char *)meta->title, sizeof(meta->title) },
		{ "choir", (char *)meta->choir, sizeof(meta->choir) },
	};

	return META_WRITE(item_path, fields);
}

static void songbook_cleanup(const char *id)
{
	(void)id;
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
	songbook_meta_t meta;
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
	{
		char csrf_submitted[33] = { 0 };
		mpfd_get(
		        "csrf_token",
		        csrf_submitted,
		        sizeof(csrf_submitted) - 1);
		if (csrf_validate(fd, csrf_submitted))
			return respond_error(fd, 403, "Forbidden");
	}

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
	return ndc_redirect(fd, location);
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
	{
		char csrf_submitted[33] = { 0 };
		mpfd_get(
		        "csrf_token",
		        csrf_submitted,
		        sizeof(csrf_submitted) - 1);
		if (csrf_validate(fd, csrf_submitted))
			return respond_error(fd, 403, "Forbidden");
	}

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
	return ndc_redirect(fd, location);
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

	ndc_query_parse(body);
	{
		char csrf_submitted[33] = { 0 };
		ndc_query_param(
		        "csrf_token", csrf_submitted, sizeof(csrf_submitted));
		if (csrf_validate(fd, csrf_submitted))
			return respond_error(fd, 403, "Forbidden");
	}
	int s_len = ndc_query_param("song_id", s_id, sizeof(s_id) - 1);
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
	source_update_item(fd, "songbook.item_songs", NULL, dh);
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

	ndc_query_parse(body);
	{
		char csrf_submitted[33] = { 0 };
		ndc_query_param(
		        "csrf_token", csrf_submitted, sizeof(csrf_submitted));
		if (csrf_validate(fd, csrf_submitted))
			return respond_error(fd, 403, "Forbidden");
	}

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
	ndc_query_param("n", n_str, sizeof(n_str) - 1);
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

static int songbook_details_handler(int fd, char *body)
{
	(void)body;
	char id[128] = { 0 };
	char sb_path[512];

	ndc_env_get(fd, id, "PATTERN_PARAM_ID");
	if (!id[0])
		return respond_error(fd, 404, "Songbook not found");

	if (item_path_build(fd, "songbook", id, sb_path, sizeof(sb_path)) != 0)
		return respond_error(fd, 404, "Songbook not found");

	songbook_meta_t meta;
	songbook_meta_read(sb_path, &meta);
	if (!meta.title[0])
		return respond_error(fd, 404, "Songbook not found");

	char location[256];
	snprintf(location, sizeof(location), "/songbook/%s", id);
	ndc_header_set(fd, "Location", location);
	ndc_respond(fd, 303, "");
	return 0;
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

		songbook_meta_t meta;
		songbook_meta_read(sb_item_path, &meta);
		snprintf(meta.choir, sizeof(meta.choir), "%s", choir);
		if (songbook_meta_write(sb_item_path, &meta) != 0)
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
	return ndc_redirect(fd, location);
}

static int songbook_details_handler(int fd, char *body);

void ndx_install(void)
{
	char doc_root[256] = { 0 };
	get_doc_root(0, doc_root, sizeof(doc_root));
	if (doc_root[0])
		strncpy(g_doc_root, doc_root, sizeof(g_doc_root) - 1);

	ndx_load("./mods/common/common");
	ndx_load("./mods/index/index");
	ndx_load("./mods/mpfd/mpfd");
	ndx_load("./mods/auth/auth");
	ndx_load("./mods/song/song");
	ndx_load("./mods/choir/choir");

	songbook_record_id = 0;

	/* Register songbook item record — 4 fields (no songs inline) */
	{
		source_def_t *choir_def = source_find("choir.items");
		uint32_t choir_rec_id = choir_def ? choir_def->record_id : 0;
		qmap_record_field_t sb_fields[4];
		memcpy(sb_fields,
		       songbook_record_fields_basic,
		       sizeof(sb_fields));
		sb_fields[2].type = QM_REFERENCE;
		sb_fields[2].target_record = choir_rec_id;
		sb_fields[2].inverse = "songbooks";
		songbook_record_id = qmap_record_register(
		        "songbook", sizeof(songbook_item_t), sb_fields, 4);
	}

	/* Register songbook item_song record — 5 fields,
	 * song→choir.repertoire, songbook→songbook.items */
	uint32_t sis_rec_id = 0;
	{
		source_def_t *repo_def = source_find("choir.repertoire");
		uint32_t repo_rec_id = repo_def ? repo_def->record_id : 0;
		uint32_t sb_rec_id = songbook_record_id;

		static const qmap_record_field_t sis_fields_basic[] = {
			{ "id",
			  QM_STR,
			  offsetof(songbook_item_song_t, id),
			  sizeof(((songbook_item_song_t *)0)->id) },
			{ "song",
			  QM_STR,
			  offsetof(songbook_item_song_t, song),
			  sizeof(uint32_t) },
			{ "transpose",
			  QM_STR,
			  offsetof(songbook_item_song_t, transpose),
			  sizeof(((songbook_item_song_t *)0)->transpose) },
			{ "format",
			  QM_STR,
			  offsetof(songbook_item_song_t, format),
			  sizeof(((songbook_item_song_t *)0)->format) },
			{ "songbook",
			  QM_STR,
			  offsetof(songbook_item_song_t, songbook),
			  sizeof(uint32_t) },
		};
		qmap_record_field_t sis_fields[5];
		memcpy(sis_fields, sis_fields_basic, sizeof(sis_fields));
		sis_fields[1].type = QM_REFERENCE;
		sis_fields[1].target_record = repo_rec_id;
		sis_fields[1].inverse = "in_songbooks";
		sis_fields[4].type = QM_REFERENCE;
		sis_fields[4].target_record = sb_rec_id;
		sis_fields[4].inverse = "item_songs";
		sis_rec_id = qmap_record_register(
		        "songbook_item_song",
		        sizeof(songbook_item_song_t),
		        sis_fields,
		        5);
	}

	/* Register sources first — libhyle creates both handles */
	{
		source_def_t def = songbook_items_def;
		def.record_id = songbook_record_id;
		source_register(&def);
	}
	{
		source_def_t def = songbook_item_songs_def;
		def.record_id = sis_rec_id;
		source_register(&def);
	}

	index_hd = source_get_data_hd("songbook.items");
	index_open(
	        "Songbook",
	        1,
	        index_hd,
	        NULL,
	        core_get,
	        handle_sb_add,
	        NULL,
	        NULL);

	ndc_register_handler(
	        "POST:/songbook/:id/randomize", handle_sb_randomize);
	ndc_register_handler(
	        "POST:/songbook/:id/transpose", handle_sb_transpose);
	ndc_register_handler(
	        "POST:/api/songbook/:id/songs", handle_sb_song_add);
	ndc_register_handler(
	        "POST:/api/songbook/:id/song/:n/remove", handle_sb_song_remove);

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
