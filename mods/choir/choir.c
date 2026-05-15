#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx-mod.h>
#include <ttypt/ndx.h>
#include <ttypt/qmap.h>

#include "../index/index.h"
#include "../common/common.h"
#include "../auth/auth.h"
#include "../mpfd/mpfd.h"
#include "../song/song.h"
#include "../ssr/ssr.h"

#define CHOIR_REPERTOIRE_IMPL
#include "choir.h"
#undef CHOIR_REPERTOIRE_IMPL

#define CHOIR_SONGS_PATH "items/choir/items"
static unsigned index_hd = 0;
static unsigned choir_index_hd = 0;
static unsigned choir_meta_qtype = 0;

typedef struct {
	char title[256];
	char format[2048];
} choir_meta_t;

static const char *CHOIR_DEFAULT_FORMATS =
        "entrada\naleluia\nofertorio\nsanto\ncomunhao\nacao_de_"
        "gracas\nsaida\nany";

static void choir_meta_read(const char *item_path, choir_meta_t *meta)
{
	meta_field_t fields[] = {
		{ "title", meta->title, sizeof(meta->title) },
	};
	memset(meta, 0, sizeof(*meta));
	meta_fields_read(item_path, fields, 1);
	char *format = slurp_item_child_file(item_path, "format");
	if (format) {
		snprintf(meta->format, sizeof(meta->format), "%s", format);
		size_t len = strlen(meta->format);
		while (len > 0 && (meta->format[len - 1] == '\n' ||
		                   meta->format[len - 1] == '\r'))
			meta->format[--len] = '\0';
		free(format);
	} else {
		strcpy(meta->format, CHOIR_DEFAULT_FORMATS);
	}
}

static int choir_meta_write(const char *item_path, const choir_meta_t *meta)
{
	meta_field_t f[] = { { "title", (char *)meta->title, 0 } };
	meta_fields_write(item_path, f, 1);
	return write_item_child_file(
	        item_path, "format", meta->format, strlen(meta->format));
}

static int choir_index_write_file(const char *root)
{
	char path[PATH_MAX], tmp[PATH_MAX];
	snprintf(path, sizeof(path), "%s/items/choir/index.tsv", root);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE *fp = fopen(tmp, "w");
	if (!fp)
		return -1;
	unsigned c = qmap_iter(choir_index_hd, NULL, 0);
	const void *k, *v;
	while (qmap_next(&k, &v, c)) {
		const choir_meta_t *m = (const choir_meta_t *)v;
		char title[256], fmt[2048];
		char *p;
		snprintf(title, sizeof(title), "%s", m->title);
		snprintf(fmt, sizeof(fmt), "%s", m->format);
		index_field_clean(title);
		/* Replace newlines in format with '|' for TSV storage */
		for (p = fmt; *p; p++) {
			if (*p == '\n' || *p == '\r')
				*p = '|';
		}
		fprintf(fp, "%s\t%s\t%s\n", (const char *)k, title, fmt);
	}
	if (fclose(fp) != 0) {
		unlink(tmp);
		return -1;
	}
	return rename(tmp, path);
}

static void choir_index_put_meta(const char *id, const choir_meta_t *meta)
{
	qmap_put(choir_index_hd, id, meta);
}

#include "repertoire_impl.inc"

static int handle_choir_edit_authorized(
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
	choir_meta_t meta;
	choir_meta_read(ctx->item_path, &meta);
	int t_len = mpfd_get("title", meta.title, sizeof(meta.title) - 1);
	int f_len = mpfd_get("format", meta.format, sizeof(meta.format) - 1);
	if (t_len > 0)
		meta.title[t_len] = '\0';
	if (f_len > 0)
		meta.format[f_len] = '\0';
	if ((t_len > 0 || f_len > 0) &&
	    choir_meta_write(ctx->item_path, &meta) != 0)
		return server_error(fd, "Failed to write choir metadata");
	if (t_len > 0)
		qmap_put(choir_index_hd, (char *)ctx->id, &meta);
	if (t_len > 0 || f_len > 0)
		choir_index_put_meta(ctx->id, &meta);

	dataset_refresh_row("choir.items", ctx->id);

	return redirect_to_item(fd, "choir", ctx->id);
}

static int handle_choir_edit(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        CHOIR_SONGS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	        NULL,
	        NULL,
	        handle_choir_edit_authorized,
	        NULL);
}

struct all_songs_ctx {
	struct ChoirEntryFfi *slots;
	char (*titles)[256];
	size_t count;
	size_t max;
};

static int all_songs_cb(const char *id, const char *title, void *user)
{
	struct all_songs_ctx *c = user;
	if (c->count >= c->max)
		return 1;
	snprintf(c->titles[c->count], 256, "%s", title);
	c->slots[c->count].id = id;
	c->slots[c->count].title = c->titles[c->count];
	c->count++;
	return 0;
}

#define MAX_CHOIR_SONGS 256
#define MAX_CHOIR_ALL 1024
#define MAX_CHOIR_SONGBOOKS 256

static int
choir_details_authorized(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;
	static __thread struct ChoirSongFfi song_slots[MAX_CHOIR_SONGS];
	static __thread struct ChoirEntryFfi all_song_slots[MAX_CHOIR_ALL];
	static __thread char song_titles[MAX_CHOIR_SONGS][256];
	static __thread char song_ids[MAX_CHOIR_SONGS][128];
	static __thread char song_fmts[MAX_CHOIR_SONGS][128];
	static __thread char all_titles[MAX_CHOIR_ALL][256];

	choir_meta_t meta;
	choir_meta_read(ctx->item_path, &meta);
	char owner[64] = { 0 };
	item_read_owner(ctx->item_path, owner, sizeof(owner));

	/* Load repertoire songs */
	repertoire_row_t *rows = NULL;
	size_t count = 0;
	char songs_path[PATH_MAX];
	if (item_child_path(
	            ctx->item_path, "songs", songs_path, sizeof(songs_path)) !=
	    0)
		return server_error(fd, "Failed to resolve songs path");
	repertoire_rows_load(songs_path, &rows, &count);
	if (count > MAX_CHOIR_SONGS)
		count = MAX_CHOIR_SONGS;
	for (size_t i = 0; i < count; i++) {
		song_read_title(
		        ctx->doc_root,
		        rows[i].id,
		        song_titles[i],
		        sizeof(song_titles[i]));
		snprintf(song_ids[i], sizeof(song_ids[i]), "%s", rows[i].id);
		snprintf(
		        song_fmts[i],
		        sizeof(song_fmts[i]),
		        "%s",
		        rows[i].format);
		song_slots[i].id = song_ids[i];
		song_slots[i].title = song_titles[i];
		song_slots[i].format = song_fmts[i];
		song_slots[i].preferred_key = rows[i].value;
		song_slots[i].original_key = song_get_original_key(rows[i].id);
	}
	repertoire_rows_dispose(rows);

	/* Load all songs for datalist via song_for_each */
	struct all_songs_ctx asc = {
		.slots = all_song_slots,
		.titles = all_titles,
		.count = 0,
		.max = MAX_CHOIR_ALL,
	};
	song_for_each(all_songs_cb, &asc);
	size_t all_count = asc.count;

	static __thread char s_id[128], s_query[512];
	struct ModuleEntryFfi modules_snap[64];
	size_t modules_len;
	ndc_env_get(fd, s_id, "PATTERN_PARAM_ID");
	ndc_env_get(fd, s_query, "QUERY_STRING");
	SSR_FILL_MODULES(modules_snap, modules_len);
	{
		static __thread char s_csrf[33];
		csrf_set_cookie(fd, s_csrf, sizeof(s_csrf));
		struct ChoirDetailRenderFfi req = {
			.title = meta.title,
			.owner_name = owner,
			.formats = meta.format,
			.songs = song_slots,
			.songs_len = count,
			.all_songs = all_song_slots,
			.all_songs_len = all_count,
			.id = s_id,
			.query = s_query,
			.remote_user = get_request_user(fd),
			.modules = modules_snap,
			.modules_len = modules_len,
			.csrf_token = s_csrf,
		};
		return ssr_render_choir_detail(fd, &req);
	}
}

static int handle_choir_songs_list(int fd, char *body)
{
	(void)body;
	item_ctx_t ctx;
	if (item_ctx_load(&ctx, fd, CHOIR_SONGS_PATH, 0))
		return 1;
	char p[PATH_MAX];
	item_child_path(ctx.item_path, "songs", p, sizeof(p));
	json_array_t *ja = json_array_new(0);
	repertoire_row_t *r = NULL;
	size_t c = 0;
	repertoire_rows_load(p, &r, &c);
	for (size_t i = 0; i < c; i++) {
		char t[256] = { 0 };
		song_read_title(ctx.doc_root, r[i].id, t, sizeof(t));
		json_array_begin_object(ja);
		json_array_kv_str(ja, "id", r[i].id);
		json_array_kv_str(ja, "title", t);
		json_array_kv_int(ja, "preferredKey", r[i].value);
		json_array_kv_str(ja, "format", r[i].format);
		json_array_end_object(ja);
	}
	repertoire_rows_dispose(r);
	char *json = json_array_finish(ja);
	int res = respond_json(fd, 200, json);
	free(json);
	return res;
}

static int handle_choir_song_add_auth(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	char p[PATH_MAX], s_id[128] = { 0 }, fmt[64] = { 0 };
	item_child_path(ctx->item_path, "songs", p, sizeof(p));
	ndc_query_parse(body);
	{
		char csrf_submitted[33] = { 0 };
		ndc_query_param(
		        "csrf_token", csrf_submitted, sizeof(csrf_submitted));
		if (csrf_validate(fd, csrf_submitted))
			return respond_error(fd, 403, "Forbidden");
	}
	int s_len = ndc_query_param("song_id", s_id, sizeof(s_id) - 1);
	int f_len = ndc_query_param("format", fmt, sizeof(fmt) - 1);
	if (s_len <= 0)
		return bad_request(fd, "Missing song_id");
	datalist_extract_id(s_id, s_id, sizeof(s_id));
	if (f_len <= 0)
		strcpy(fmt, "any");
	repertoire_row_t row = { 0 };
	snprintf(row.id, sizeof(row.id), "%s", s_id);
	row.value = 0;
	snprintf(row.format, sizeof(row.format), "%s", fmt);
	if (repertoire_file_append(p, &row) != 0)
		return server_error(fd, "Failed to add song");

	dataset_refresh_row("choir.items", ctx->id);

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

struct key_cb_ctx {
	const char *song_id;
	int new_key;
};
static int song_key_cb(
        int idx,
        const char *raw,
        int parsed,
        const char *sid,
        int ival,
        const char *fmt,
        void *user,
        char *out,
        size_t out_sz)
{
	(void)idx;
	(void)raw;
	(void)ival;
	struct key_cb_ctx *c = user;
	if (parsed && strcmp(sid, c->song_id) == 0) {
		snprintf(out, out_sz, "%s:%d:%s\n", sid, c->new_key, fmt);
		return REPERTOIRE_LINE_REPLACE;
	}
	return REPERTOIRE_LINE_KEEP;
}

static int handle_choir_song_key_auth(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	char p[PATH_MAX], k_s[32] = { 0 };
	item_child_path(ctx->item_path, "songs", p, sizeof(p));
	ndc_query_parse(body);
	{
		char csrf_submitted[33] = { 0 };
		ndc_query_param(
		        "csrf_token", csrf_submitted, sizeof(csrf_submitted));
		if (csrf_validate(fd, csrf_submitted))
			return respond_error(fd, 403, "Forbidden");
	}
	int k_l = ndc_query_param("key", k_s, sizeof(k_s) - 1);
	struct key_cb_ctx cbc = { .song_id = ctx->song_id,
		                  .new_key = (k_l > 0) ? atoi(k_s) : 0 };
	repertoire_file_rewrite(p, song_key_cb, &cbc);

	dataset_refresh_row("choir.items", ctx->id);

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

static int song_del_cb(
        int idx,
        const char *raw,
        int p,
        const char *sid,
        int v,
        const char *f,
        void *u,
        char *o,
        size_t os)
{
	(void)idx;
	(void)raw;
	(void)v;
	(void)f;
	(void)o;
	(void)os;
	return (p && strcmp(sid, (char *)u) == 0) ? REPERTOIRE_LINE_SKIP
	                                          : REPERTOIRE_LINE_KEEP;
}

static int handle_choir_song_del_auth(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	char p[PATH_MAX];
	ndc_query_parse(body);
	{
		char csrf_submitted[33] = { 0 };
		ndc_query_param(
		        "csrf_token", csrf_submitted, sizeof(csrf_submitted));
		if (csrf_validate(fd, csrf_submitted))
			return respond_error(fd, 403, "Forbidden");
	}
	item_child_path(ctx->item_path, "songs", p, sizeof(p));
	repertoire_file_rewrite(p, song_del_cb, (void *)ctx->song_id);

	dataset_refresh_row("choir.items", ctx->id);

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
	char p[PATH_MAX];
	item_child_path(ctx->item_path, "songs", p, sizeof(p));
	int pk = 0;
	repertoire_row_t *r = NULL;
	size_t c = 0;
	repertoire_rows_load(p, &r, &c);
	for (size_t i = 0; i < c; i++) {
		if (strcmp(r[i].id, ctx->song_id) == 0) {
			pk = r[i].value;
			break;
		}
	}
	repertoire_rows_dispose(r);
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

static int handle_choir_edit_get_auth(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;
	choir_meta_t meta;
	choir_meta_read(ctx->item_path, &meta);
	form_body_t *fb = form_body_new(0);
	form_body_add(fb, "id", ctx->id);
	form_body_add(fb, "title", meta.title);
	form_body_add(fb, "format", meta.format);
	return core_post_form(fd, fb);
}

static int handle_choir_edit_get(int fd, char *body)
{
	return with_item_access(
	        fd,
	        body,
	        CHOIR_SONGS_PATH,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	        NULL,
	        NULL,
	        handle_choir_edit_get_auth,
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
	        "GET:/api/choir/:id/songs", handle_choir_songs_list);
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
	ndc_register_handler("POST:/api/choir/:id/edit", handle_choir_edit);
	index_hd = index_open("Choir", 0, 1, NULL, NULL, NULL, NULL, NULL);
	{
		char doc_root[256] = { 0 };
		get_doc_root(0, doc_root, sizeof(doc_root));
		const char *root = doc_root[0] ? doc_root : ".";
		choir_meta_qtype = qmap_reg(sizeof(choir_meta_t));
		choir_index_hd = qmap_open(
		        NULL,
		        "choir_idx",
		        QM_STR,
		        choir_meta_qtype,
		        0x3FF,
		        QM_SORTED);
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/items/choir/index.tsv", root);
		FILE *fp = fopen(path, "r");
		if (fp) {
			char line[4096];
			while (fgets(line, sizeof(line), fp)) {
				char *id = line;
				char *nl = strpbrk(line, "\r\n");
				if (nl)
					*nl = '\0';
				char *val = strchr(id, '\t');
				if (!val)
					continue;
				*val++ = '\0';
				choir_meta_t meta;
				memset(&meta, 0, sizeof(meta));
				char *fmt = strchr(val, '\t');
				if (fmt) {
					*fmt++ = '\0';
					/* Restore newlines from '|' */
					char *p;
					snprintf(
					        meta.format,
					        sizeof(meta.format),
					        "%s",
					        fmt);
					for (p = meta.format; *p; p++) {
						if (*p == '|')
							*p = '\n';
					}
				}
				snprintf(
				        meta.title,
				        sizeof(meta.title),
				        "%s",
				        val);
				qmap_put(choir_index_hd, id, &meta);
			}
			fclose(fp);
		} else {
			/* Rebuild from filesystem */
			char p[PATH_MAX];
			if (module_items_path_build(
			            root, "choir", p, sizeof(p)) == 0)
			{
				DIR *d = opendir(p);
				if (d) {
					struct dirent *e;
					while ((e = readdir(d))) {
						char ip[PATH_MAX];
						choir_meta_t meta;
						if (e->d_name[0] == '.')
							continue;
						if (item_path_build_root(
						            root,
						            "choir",
						            e->d_name,
						            ip,
						            sizeof(ip)) != 0)
							continue;
						choir_meta_read(ip, &meta);
						choir_index_put_meta(
						        e->d_name, &meta);
					}
					closedir(d);
					choir_index_write_file(root);
				}
			}
		}
	}

	{
		static const dataset_field_t fields[] = {
			{ "id", NULL, DATASET_FIELD_STRING, 0 },
			{ "title", "title", DATASET_FIELD_STRING, 1 },
			{ "format", "format", DATASET_FIELD_STRING, 1 },
			{ "songs", "songs", DATASET_FIELD_STRING, 1 },
			{ "owner", "owner", DATASET_FIELD_STRING, 0 },
		};
		dataset_def_t def = { .id = "choir.items",
			              .key_field = "id",
			              .items_path = "items/choir/items",
			              .access_policy = DATASET_ACCESS_PUBLIC,
			              .fields = fields,
			              .field_count = sizeof(fields) /
			                             sizeof(fields[0]),
			              .source_hd = index_hd };
		dataset_register(&def);
	}
}
