#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx-mod.h>
#include <ttypt/ndx.h>
#include <ttypt/qmap.h>

#include "../index/index.h"
#include "../common/common.h"
#include "../source/source.h"

#include "../auth/auth.h"

#include "../song/song.h"

#define CHOIR_SONGS_PATH "items/choir/items"
static unsigned index_hd = 0;

typedef struct {
	char id[64];
	char title[256];
	char format[2048];
	char owner[32];
} choir_item_t;

typedef struct {
	char id[64];
	uint32_t song;
	char transpose[16];
	char format[64];
	uint32_t choir;
} choir_repertoire_item_t;

static uint32_t choir_record_id;
static uint32_t choir_repertoire_record_id;

static const qmap_record_field_t choir_record_fields[] = {
	{ "id",
	  QM_STR,
	  offsetof(choir_item_t, id),
	  sizeof(((choir_item_t *)0)->id) },
	{ "title",
	  QM_STR,
	  offsetof(choir_item_t, title),
	  sizeof(((choir_item_t *)0)->title) },
	{ "format",
	  QM_STR,
	  offsetof(choir_item_t, format),
	  sizeof(((choir_item_t *)0)->format) },
	{ "owner",
	  QM_STR,
	  offsetof(choir_item_t, owner),
	  sizeof(((choir_item_t *)0)->owner) },
};

static const qmap_record_field_t choir_repertoire_record_fields_basic[] = {
	{ "id",
	  QM_STR,
	  offsetof(choir_repertoire_item_t, id),
	  sizeof(((choir_repertoire_item_t *)0)->id),
	  0,
	  0 },
	{ "song",
	  QM_STR,
	  offsetof(choir_repertoire_item_t, song),
	  sizeof(uint32_t),
	  0,
	  0 },
	{ "transpose",
	  QM_STR,
	  offsetof(choir_repertoire_item_t, transpose),
	  sizeof(((choir_repertoire_item_t *)0)->transpose),
	  0,
	  0 },
	{ "format",
	  QM_STR,
	  offsetof(choir_repertoire_item_t, format),
	  sizeof(((choir_repertoire_item_t *)0)->format),
	  0,
	  0 },
	{ "choir",
	  QM_STR,
	  offsetof(choir_repertoire_item_t, choir),
	  sizeof(uint32_t),
	  0,
	  0 },
	{ "in_songbooks", QM_STR, 0, 0, 0, 0 },
};
#define CHOIR_REPERTOIRE_FIELD_COUNT                                           \
	(sizeof(choir_repertoire_record_fields_basic) /                        \
	 sizeof(choir_repertoire_record_fields_basic[0]))

static const source_field_t choir_items_fields[] = {
	{ "id", NULL, SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "title", "title", SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "format", "format", SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "owner", "owner", SOURCE_FIELD_STRING, 0, NULL, NULL },
	{ "songbooks",
	  NULL,
	  SOURCE_FIELD_INVERSE,
	  0,
	  "songbook.items",
	  "choir" },
	{ "repertoire",
	  NULL,
	  SOURCE_FIELD_INVERSE,
	  0,
	  "choir.repertoire",
	  "choir" },
};

static const source_field_t choir_repertoire_fields[] = {
	{ "id", NULL, SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "song",
	  "song",
	  SOURCE_FIELD_REFERENCE,
	  1,
	  "song.items",
	  "in_choir_repertoire" },
	{ "transpose", "transpose", SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "format", "format", SOURCE_FIELD_STRING, 1, NULL, NULL },
	{ "choir",
	  "choir",
	  SOURCE_FIELD_REFERENCE,
	  1,
	  "choir.items",
	  "repertoire" },
	{ "in_songbooks",
	  NULL,
	  SOURCE_FIELD_INVERSE,
	  0,
	  "songbook.item_songs",
	  "song" },
};

static const source_def_t choir_items_def = {
	.id = "choir.items",
	.key_field = "id",
	.items_path = "items/choir/items",
	.access_policy = SOURCE_ACCESS_PUBLIC,
	.fields = choir_items_fields,
	.field_count =
	        sizeof(choir_items_fields) / sizeof(choir_items_fields[0]),
};

static const source_def_t choir_repertoire_def = {
	.id = "choir.repertoire",
	.key_field = "id",
	.items_path = "items/choir/repertoire",
	.access_policy = SOURCE_ACCESS_PUBLIC,
	.fields = choir_repertoire_fields,
	.field_count = sizeof(choir_repertoire_fields) /
	               sizeof(choir_repertoire_fields[0]),
};

static int choir_repertoire_path(
        const char *doc_root, const char *entry_id, char *buf, size_t buf_sz)
{
	return snprintf(
	        buf,
	        buf_sz,
	        "%s/items/choir/repertoire/%s",
	        (doc_root && doc_root[0]) ? doc_root : ".",
	        entry_id);
}

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

	char entry_id[64];
	choir_repertoire_entry_id(ctx->id, s_id, entry_id, sizeof(entry_id));

	unsigned dh = source_parse_form("choir.repertoire");
	if (dh == 0)
		return server_error(fd, "OOM");
	qmap_put(dh, "song", s_id);
	qmap_put(dh, "transpose", "0");
	qmap_put(dh, "choir", ctx->id);
	if (!qmap_get(dh, "format"))
		qmap_put(dh, "format", "any");
	source_update_item(fd, "choir.repertoire", entry_id, dh);
	qmap_close(dh);

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
	ndc_query_parse(body);
	{
		char csrf_submitted[33] = { 0 };
		ndc_query_param(
		        "csrf_token", csrf_submitted, sizeof(csrf_submitted));
		if (csrf_validate(fd, csrf_submitted))
			return respond_error(fd, 403, "Forbidden");
	}
	ndc_query_param("key", k_s, sizeof(k_s) - 1);

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
	ndc_query_parse(body);
	{
		char csrf_submitted[33] = { 0 };
		ndc_query_param(
		        "csrf_token", csrf_submitted, sizeof(csrf_submitted));
		if (csrf_validate(fd, csrf_submitted))
			return respond_error(fd, 403, "Forbidden");
	}

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
	return ndc_redirect(fd, loc);
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

void ndx_install(void)
{
	ndx_load("./mods/common/common");
	ndx_load("./mods/index/index");
	ndx_load("./mods/auth/auth");
	ndx_load("./mods/mpfd/mpfd");
	ndx_load("./mods/song/song");
	ndc_register_handler(
	        "GET:/choir/:id/song/:song_id", handle_choir_song_view);
	ndc_register_handler(
	        "POST:/api/choir/:id/songs", handle_choir_song_add);
	ndc_register_handler(
	        "POST:/api/choir/:id/song/:song_id/key", handle_choir_song_key);
	ndc_register_handler(
	        "DELETE:/api/choir/:id/song/:song_id",
	        handle_choir_song_delete);
	ndc_register_handler(
	        "POST:/api/choir/:id/song/:song_id/remove",
	        handle_choir_song_delete);
	{
		choir_record_id = qmap_record_register(
		        "choir",
		        sizeof(choir_item_t),
		        choir_record_fields,
		        sizeof(choir_record_fields) /
		                sizeof(choir_record_fields[0]));
	}

	/* Register sources first — libhyle creates both handles */
	{
		source_def_t def = choir_items_def;
		def.record_id = choir_record_id;
		source_register(&def);
	}

	{
		source_def_t *song_def = source_find("song.items");
		uint32_t song_rec_id = song_def ? song_def->record_id : 0;
		source_def_t *choir_items_src = source_find("choir.items");
		uint32_t choir_rec_id =
		        choir_items_src ? choir_items_src->record_id : 0;

		qmap_record_field_t cr_fields[CHOIR_REPERTOIRE_FIELD_COUNT];
		memcpy(cr_fields,
		       choir_repertoire_record_fields_basic,
		       sizeof(cr_fields));
		cr_fields[1].type = QM_REFERENCE;
		cr_fields[1].target_record = song_rec_id;
		cr_fields[1].inverse = "in_choir_repertoire";
		cr_fields[4].type = QM_REFERENCE;
		cr_fields[4].target_record = choir_rec_id;
		cr_fields[4].inverse = "repertoire";

		choir_repertoire_record_id = qmap_record_register(
		        "choir_repertoire",
		        sizeof(choir_repertoire_item_t),
		        cr_fields,
		        CHOIR_REPERTOIRE_FIELD_COUNT);

		source_def_t def = choir_repertoire_def;
		def.record_id = choir_repertoire_record_id;
		source_register(&def);
	}

	index_hd = source_get_data_hd("choir.items");
	index_open(
	        "Choir",
	        1,
	        index_hd,
	        NULL,
	        core_get,
	        NULL,
	        NULL,
	        NULL);

	/* Ensure repertoire storage directory exists */
	{
		char doc_root[256] = { 0 };
		get_doc_root(0, doc_root, sizeof(doc_root));
		const char *root = doc_root[0] ? doc_root : ".";
		char rep_path[PATH_MAX];
		snprintf(
		        rep_path,
		        sizeof(rep_path),
		        "%s/items/choir/repertoire",
		        root);
		mkdir(rep_path, 0755);
	}
}
