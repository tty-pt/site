#include <stdio.h>
#include <limits.h>
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ttypt/qmap.h>
#include <ttypt/ndx-mod.h>
#include <ttypt/axil.h>

#include "./../common/common.h"
#include "./../source/source.h"
#include "./../mpfd/mpfd.h"
#include "./../auth/auth.h"

typedef void (*index_cleanup_fn)(const char *id);
typedef size_t (*index_format_fn)(
        const char *id, const char *val, char *out, size_t out_sz);
typedef int (*index_detail_handler_fn)(int fd, char *body);
typedef int (*index_handler_fn)(int fd, char *body);

#define MAX_MODULES 64

static int index_add_get_handler(int fd, char *body);
static int index_delete_get_handler(int fd, char *body);
static int index_delete_handler(int fd, char *body);
int index_add_item(int fd, char *body, char *id_out, size_t id_len);
int item_record_ownership(const char *item_path, const char *username);

static char modules_json[256 * MAX_MODULES], *modules_json_end = modules_json;

static size_t modules_rem = sizeof(modules_json), modules_count = 0;

static unsigned module_hd;

static char module_names[MAX_MODULES][256];
static char module_titles[MAX_MODULES][256];
static unsigned module_hds[MAX_MODULES];
static void (*module_cleanups[MAX_MODULES])(const char *id);
static size_t module_slot_count = 0;

#include "ux/all.c"

int index_update_json(const char *id, const char *title)
{
	long offset;
	char id_esc[512], title_esc[512];

	if (modules_count >= MAX_MODULES)
		return -1;

	snprintf(
	        module_names[modules_count], sizeof(module_names[0]), "%s", id);
	snprintf(
	        module_titles[modules_count],
	        sizeof(module_titles[0]),
	        "%s",
	        title);

	axil_json_escape(id, id_esc, sizeof(id_esc));
	axil_json_escape(title, title_esc, sizeof(title_esc));

	offset = snprintf(
	        modules_json_end,
	        modules_rem,
	        "%c{"
	        "\"id\":\"%s\","
	        "\"title\":\"%s\"}",
	        (modules_count ? ',' : '['),
	        id_esc,
	        title_esc);

	if (offset < 0)
		return -1;

	modules_json_end += offset;
	modules_rem -= offset;
	modules_json_end[0] = ']';
	modules_json_end[1] = '\0';

	modules_count++;

	return 0;
}

static const char *index_name(int fd)
{
	static __thread char uri[256];
	char *module;

	axil_env_get(fd, uri, "DOCUMENT_URI");
	module = strchr(uri + 1, '/');
	if (module)
		*module = '\0';
	module = uri + 1;
	return module;
}

/*
 * NDX hook: create an index item from a parsed multipart form on fd/body.
 * Performs auth check, creates the item directory, writes the title file,
 * records ownership, and updates the in-memory index.
 * Writes the generated id into id_out (up to id_len bytes).
 * On error, sends the error response itself and returns non-zero.
 * On success returns 0 and id_out is populated — caller must redirect.
 */
NDX_LISTENER(int, index_add_item,
	int, fd,
	char *, body,
	char *, id_out,
	size_t, id_len)
{
	char title[256], id[256], path[1024];
	int parse_result, title_len;
	const char *module;
	unsigned hd;

	const char *username = get_request_user(fd);
	if (require_login(fd, username))
		return 1;

	parse_result = mpfd_parse(fd, body);
	if (parse_result == -1)
		return respond_error(fd, 415, "Expected multipart/form-data");

	if (csrf_check_mpfd(fd))
		return 1;

	title_len = mpfd_get("title", title, sizeof(title) - 1);
	if (title_len <= 0)
		return bad_request(fd, "Missing title");

	axil_slugify(title, (size_t)title_len, id, sizeof(id));
	module = index_name(fd);

	if (item_path_build(fd, module, id, path, sizeof(path)) != 0)
		return server_error(fd, "Failed to resolve item path");

	int r = mkdir(path, 0755);
	if (r == -1 && errno != EEXIST)
		return respond_error(
		        fd, 403, "You don't have permissions for that");

	item_record_ownership(path, username);

	if (write_meta_file(path, "title", title, (size_t)title_len) != 0)
		return respond_error(
		        fd, 403, "You don't have permissions for that");

	{
		char dataset_id[512];
		snprintf(dataset_id, sizeof(dataset_id), "%s.items", module);
		source_refresh_row(fd, dataset_id, id);
	}

	snprintf(id_out, id_len, "%s", id);
	return 0;
}

/* ── Schema & data querying (native-only: axil, source, qmap) ── */

static int idx_schema_collect(
        const char *dataset_id,
        const char *select_csv,
        col_t *cols,
        int max_cols)
{
	unsigned schema_hd;
	int n = 0;
	uint32_t cur;
	const void *key;
	const void *val;

	schema_hd = source_get_schema_hd(dataset_id);
	if (!schema_hd)
		return 0;

	if (select_csv && select_csv[0]) {
		char copy[256];
		char *tok;
		char *rest;

		strncpy(copy, select_csv, sizeof(copy) - 1);
		copy[sizeof(copy) - 1] = '\0';
		rest = copy;
		while ((tok = strtok_r(rest, ",", &rest)) && n < max_cols) {
			strncpy(cols[n].key, tok, sizeof(cols[n].key) - 1);
			cols[n].key[sizeof(cols[n].key) - 1] = '\0';
			col_tok_label(
			        cols[n].label, sizeof(cols[n].label), tok);
			if (cols[n].label[0] >= 'a')
				cols[n].label[0] -= 32;
			val = qmap_get(schema_hd, tok);
			cols[n].type = 0;
			cols[n].target_source[0] = '\0';
			cols[n].target_hd = 0;
			if (val && ((const char *)val)[0] == '{') {
				int t;
				char ts[64] = "";
				int m =
				        sscanf((const char *)val,
				               "{\"t\":%d,\"s\":\"%63[^\"]\"",
				               &t,
				               ts);
				if (m >= 1)
					cols[n].type = t;
				if (m >= 2 && ts[0]) {
					strncpy(cols[n].target_source,
					        ts,
					        sizeof(cols[n].target_source) -
					                1);
				}
			}
			n++;
		}
	} else {
		cur = qmap_iter(schema_hd, NULL, 0);
		while (n < max_cols && qmap_next(&key, &val, cur)) {
			strncpy(cols[n].key,
			        (const char *)key,
			        sizeof(cols[n].key) - 1);
			cols[n].key[sizeof(cols[n].key) - 1] = '\0';
			col_tok_label(
			        cols[n].label,
			        sizeof(cols[n].label),
			        (const char *)key);
			if (cols[n].label[0] >= 'a')
				cols[n].label[0] -= 32;
			cols[n].type = 0;
			n++;
		}
		qmap_fin(cur);
	}
	return n;
}

static const char *idx_resolve_refs(const col_t *col, const char *raw)
{
	static char buf[4096];
	static char last_target[64] = "";
	static char display_field[64] = "";
	const char *df;

	if (!raw || !raw[0] || !col->target_hd)
		return raw;

	if (strcmp(last_target, col->target_source) != 0) {
		unsigned shd = source_get_schema_hd(col->target_source);
		display_field[0] = '\0';
		if (shd) {
			uint32_t ccur;
			const void *ckey;
			const void *cval;
			ccur = qmap_iter(shd, NULL, 0);
			while (qmap_next(&ckey, &cval, ccur)) {
				const char *fn = (const char *)ckey;
				if (strcmp(fn, "id") == 0)
					continue;
				strncpy(display_field,
				        fn,
				        sizeof(display_field) - 1);
				break;
			}
			qmap_fin(ccur);
		}
		strncpy(last_target,
		        col->target_source,
		        sizeof(last_target) - 1);
	}

	df = display_field[0] ? display_field : NULL;
	if (!df)
		return raw;

	buf[0] = '\0';
	{
		const char *p = raw;
		while (*p) {
			const char *nl = strchr(p, '\n');
			size_t llen = nl ? (size_t)(nl - p) : strlen(p);
			if (llen > 0) {
				char num[32];
				size_t cplen = llen < sizeof(num) - 1
				                       ? llen
				                       : sizeof(num) - 1;
				memcpy(num, p, cplen);
				num[cplen] = '\0';
				const char *slug = NULL;
				const char *name = NULL;
				/* Try position lookup first */
				if (num[0] >= '0' && num[0] <= '9') {
					uint32_t pos = (uint32_t)atoi(num);
					slug = qmap_get_key(
					        col->target_hd, pos);
				}
				/* If not a position or not found, treat as raw
				 * slug */
				if (!slug)
					slug = num;
				if (slug) {
					char name_key[320];
					snprintf(
					        name_key,
					        sizeof(name_key),
					        "%s:%s",
					        slug,
					        df);
					name = (const char *)qmap_get(
					        col->target_hd, name_key);
					if (buf[0])
						strncat(buf,
						        ", ",
						        sizeof(buf) -
						                strlen(buf) -
						                1);
					strncat(buf,
					        name ? name : slug,
					        sizeof(buf) - strlen(buf) - 1);
				}
			}
			if (!nl)
				break;
			p = nl + 1;
		}
	}

	if (!buf[0])
		return raw;

	return buf;
}

static int idx_render_list_bud(
        int fd, const char *module, const char *query_str, const char *username)
{
	char dataset_id[256];
	const char *select_csv;
	col_t cols[32];
	int ncols;
	unsigned result_hd;
	unsigned fields_hd;
	const char *total_str;
	uint32_t total = 0;
	uint32_t page = 1;
	uint32_t per_page = 10;
	char page_buf[64] = { 0 };
	char per_page_buf[64] = { 0 };
	char sort_field[64];
	int sort_asc;
	const char *ids[1024];
	int nids = 0;
	uint32_t cur;
	const void *key;
	const void *val;
	uint32_t offset;
	uint32_t disp_count;
	int i, j, rc;

	snprintf(dataset_id, sizeof(dataset_id), "%s.items", module);
	select_csv = idx_select_fields_for(module);

	int has_page = 0;
	idx_query_param(query_str, "page", page_buf, sizeof(page_buf));
	if (page_buf[0]) {
		page = (uint32_t)atoi(page_buf);
		has_page = 1;
	}
	idx_query_param(
	        query_str, "per_page", per_page_buf, sizeof(per_page_buf));
	if (per_page_buf[0])
		per_page = (uint32_t)atoi(per_page_buf);
	idx_parse_sort(query_str, sort_field, sizeof(sort_field), &sort_asc);

	ncols = idx_schema_collect(dataset_id, select_csv, cols, 32);

	result_hd = source_query(dataset_id, query_str);
	if (!result_hd)
		goto empty_page;

	total_str = (const char *)qmap_get(result_hd, "__total__");
	if (total_str)
		total = (uint32_t)atoi(total_str);

	cur = qmap_iter(result_hd, NULL, 0);
	while (nids < 1024 && qmap_next(&key, &val, cur)) {
		const char *ks = (const char *)key;
		if (strcmp(ks, "__total__") == 0)
			continue;
		ids[nids] = strdup(ks);
		if (!ids[nids])
			break;
		nids++;
	}
	qmap_fin(cur);

	offset = 0;
	disp_count = (uint32_t)nids;
	if (!has_page) {
		offset = (page - 1) * per_page;
		if (offset > (uint32_t)nids)
			offset = 0;
		disp_count = per_page;
		if (offset + disp_count > (uint32_t)nids)
			disp_count = (uint32_t)nids - offset;
	}

	{
		const char **disp_ids = ids + offset;
		int disp_nids = (int)disp_count;
		const char **values;
		bud_node *layout;

		fields_hd = source_get_fields_hd(dataset_id);

		for (i = 0; i < ncols; i++) {
			if (cols[i].target_source[0] && !cols[i].target_hd)
				cols[i].target_hd = source_get_fields_hd(
				        cols[i].target_source);
		}

		values = malloc((size_t)disp_nids * ncols * sizeof(char *));
		if (!values) {
			for (i = 0; i < nids; i++)
				free((void *)ids[i]);
			return respond_error(fd, 500, "OOM");
		}

		for (i = 0; i < disp_nids; i++) {
			for (j = 0; j < ncols; j++) {
				char fkey[256];
				snprintf(
				        fkey,
				        sizeof(fkey),
				        "%s:%s",
				        disp_ids[i],
				        cols[j].key);
				const char *fval =
				        (const char *)qmap_get(fields_hd, fkey);
				if (!fval)
					fval = "";
				if (j > 0 &&
				    cols[j].type ==
				            SOURCE_FIELD_MULTI_REFERENCE &&
				    cols[j].target_hd)
				{
					fval = idx_resolve_refs(&cols[j], fval);
				}
				values[i * ncols + j] = strdup(fval);
			}
		}

		layout = idx_list_layout(
		        module,
		        query_str,
		        username,
		        (int)page,
		        (int)per_page,
		        (int)total,
		        cols,
		        ncols,
		        disp_ids,
		        disp_nids,
		        values,
		        sort_field,
		        sort_asc,
		        has_page);

		rc = 0;
		if (layout) {
			respond_html(
			        fd, site_ui_page(module, NULL, NULL, layout));
		} else {
			axil_respond(fd, 500, "Internal Server Error");
		}

		for (i = 0; i < disp_nids; i++)
			for (j = 0; j < ncols; j++)
				free((void *)values[i * ncols + j]);
		free(values);
		for (i = 0; i < nids; i++)
			free((void *)ids[i]);
		return rc;
	}

empty_page: {
	bud_node *layout = idx_list_empty_layout(module, username);
	if (layout) {
		respond_html(fd, site_ui_page(module, NULL, NULL, layout));
	} else {
		axil_respond(fd, 500, "Internal Server Error");
	}
}
	return 0;
}

NDX_LISTENER(int, index_render_list,
	int, fd,
	unsigned, hd,
	index_format_fn, fmt)
{
	char query[512] = { 0 };
	const char *username;

	(void)hd;
	(void)fmt;
	axil_env_get(fd, query, "QUERY_STRING");
	username = get_request_user(fd);
	return idx_render_list_bud(
	        fd, index_name(fd), query, username ? username : "");
}

static int index_list_handler(int fd, char *body)
{
	const char *module;
	unsigned hd;

	(void)body;
	module = index_name(fd);
	hd = *(unsigned *)qmap_get(module_hd, module);
	return index_render_list(fd, hd, NULL);
}

static int index_generic_add_handler(int fd, char *body)
{
	char id[256] = { 0 };
	char title[256] = { 0 };
	const char *module = index_name(fd);
	char items_path[512];
	char dataset_id[512];

	const char *username = get_request_user(fd);
	if (!username || !username[0])
		return respond_error(fd, 401, "Unauthorized");

	if (mpfd_parse(fd, body) == -1)
		return respond_error(fd, 415, "Expected multipart/form-data");
	if (csrf_check_mpfd(fd))
		return 1;

	int title_len = mpfd_get("title", title, sizeof(title) - 1);
	if (title_len <= 0)
		return bad_request(fd, "Missing title");

	axil_slugify(title, (size_t)title_len, id, sizeof(id));
	snprintf(dataset_id, sizeof(dataset_id), "%s.items", module);

	if (item_path_build(fd, module, id, items_path, sizeof(items_path)) !=
	    0)
		return server_error(fd, "Failed to resolve item path");

	if (mkdir(items_path, 0755) == -1 && errno != EEXIST)
		return respond_error(
		        fd, 403, "Failed to create item directory");

	item_record_ownership(items_path, username);
	write_meta_file(items_path, "title", title, (size_t)title_len);

	unsigned data_handle = source_parse_form(dataset_id);
	if (!data_handle)
		return server_error(fd, "OOM");

	if (source_update_item(fd, dataset_id, id, data_handle) != 0) {
		qmap_close(data_handle);
		return server_error(fd, "Failed to save item data");
	}
	qmap_close(data_handle);

	return redirect_to_item(fd, module, id);
}

static int index_generic_edit_authorized(
        int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)user;
	char dataset_id[512];
	char items_path[512];
	const char *module = index_name(fd);

	if (mpfd_parse(fd, body) == -1) {
		return respond_error(fd, 415, "Expected multipart/form-data");
	}
	if (csrf_check_mpfd(fd))
		return 1;

	snprintf(dataset_id, sizeof(dataset_id), "%s.items", module);

	unsigned data_handle = source_parse_form(dataset_id);
	if (!data_handle)
		return server_error(fd, "OOM");

	int rc = source_update_item(fd, dataset_id, ctx->id, data_handle);
	if (rc != 0) {
		qmap_close(data_handle);
		return server_error(fd, "Failed to update item data");
	}

	qmap_close(data_handle);

	/* Update title in meta file if provided */
	char title[256];
	int title_len = mpfd_get("title", title, sizeof(title) - 1);
	if (title_len > 0) {
		if (item_path_build(
		            fd,
		            module,
		            ctx->id,
		            items_path,
		            sizeof(items_path)) == 0)
		{
			write_meta_file(
			        items_path, "title", title, strlen(title));
		}
	}

	return redirect_to_item(fd, module, ctx->id);
}

static int index_generic_edit_handler(int fd, char *body)
{
	const char *module = index_name(fd);
	char items_path[512];
	snprintf(items_path, sizeof(items_path), "items/%s/items", module);

	return with_item_access(
	        fd,
	        body,
	        items_path,
	        ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	        NULL,
	        NULL,
	        index_generic_edit_authorized,
	        NULL);
}

NDX_LISTENER(unsigned, index_open,
	const char *, name,
	const char *, dataset_name,
	index_cleanup_fn, cleanup,
	index_detail_handler_fn, detail_handler,
	index_handler_fn, add_handler,
	index_handler_fn, edit_get_handler,
	index_handler_fn, edit_post_handler)
{
	struct dirent *entry;
	char buf[PATH_MAX / 2];
	char id[256] = { 0 };
	char doc_root[256] = { 0 };
	DIR *dir;

	resolve_doc_root(0, doc_root, sizeof(doc_root));

	unsigned hd = source_get_data_hd(dataset_name);
	if (!hd)
		return QM_MISS;

	index_update_json(id, name);
	axil_slugify(name, strlen(name), id, sizeof(id));
	if (module_path_build(doc_root, id, buf, sizeof(buf)) != 0)
		return QM_MISS;
	mkdir(buf, 0755);
	if (module_items_path_build(doc_root, id, buf, sizeof(buf)) != 0)
		return QM_MISS;
	mkdir(buf, 0755);

	dir = opendir(buf);
	if (!dir) {
		perror("opendir");
		return QM_MISS;
	}
	closedir(dir);

	snprintf(buf, sizeof(buf), "POST:/%s/add", id);
	axil_register_handler(
	        buf, add_handler ? add_handler : index_generic_add_handler);

	snprintf(buf, sizeof(buf), "GET:/%s/add", id);
	axil_register_handler(buf, index_add_get_handler);

	snprintf(buf, sizeof(buf), "GET:/%s", id);
	axil_register_handler(buf, index_list_handler);

	snprintf(buf, sizeof(buf), "GET:/%s/", id);
	axil_register_handler(buf, index_list_handler);

	if (detail_handler) {
		snprintf(buf, sizeof(buf), "GET:/%s/:id", id);
		axil_register_handler(buf, detail_handler);
	}

	snprintf(buf, sizeof(buf), "GET:/%s/:id/delete", id);
	axil_register_handler(buf, index_delete_get_handler);

	snprintf(buf, sizeof(buf), "POST:/%s/:id/delete", id);
	axil_register_handler(buf, index_delete_handler);

	if (edit_get_handler) {
		snprintf(buf, sizeof(buf), "GET:/%s/:id/edit", id);
		axil_register_handler(buf, edit_get_handler);
	}
	snprintf(buf, sizeof(buf), "POST:/%s/:id/edit", id);
	axil_register_handler(
	        buf,
	        edit_post_handler ? edit_post_handler
	                          : index_generic_edit_handler);

	if (module_slot_count < MAX_MODULES) {
		size_t slot = module_slot_count++;
		snprintf(
		        module_names[slot],
		        sizeof(module_names[slot]),
		        "%s",
		        id);
		module_hds[slot] = hd;
		module_cleanups[slot] = cleanup;
	}

	qmap_put(module_hd, id, &hd);
	return hd;
}

NDX_LISTENER(int, core_get, int, fd, char *, body)
{
	(void)body;
	const char *username = get_request_user(fd);
	const char *mod_names[MAX_MODULES];
	const char *mod_titles_p[MAX_MODULES];
	size_t i;
	for (i = 0; i < module_slot_count; i++) {
		mod_names[i] = module_names[i];
		mod_titles_p[i] = module_titles[i];
	}
	bud_node *layout = idx_home_layout(
	        username, mod_names, mod_titles_p, module_slot_count);

	if (layout) {
		return respond_html(
		        fd, site_ui_page("tty.pt", NULL, NULL, layout));
	}
	axil_respond(fd, 500, "Internal Server Error");
	return 0;
}

static int index_add_get_handler(int fd, char *body)
{
	return core_get(fd, body);
}

/* GET /<module>/:id/delete — confirmation page */
static int index_delete_get_handler(int fd, char *body)
{
	(void)body;

	char id[128] = { 0 };
	axil_env_get(fd, id, "PATTERN_PARAM_ID");
	if (!id[0])
		return bad_request(fd, "Missing ID");

	const char *module = index_name(fd);

	char item_path[512];
	if (item_path_build(fd, module, id, item_path, sizeof(item_path)) != 0)
		return server_error(fd, "Failed to resolve item path");

	const char *username = get_request_user(fd);
	if (item_require_access(
	            fd,
	            item_path,
	            username,
	            ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	            "Not found",
	            "Forbidden"))
		return 1;

	char title[256] = { 0 };
	read_meta_file(item_path, "title", title, sizeof(title));

	const char *csrf_token = csrf_setup(fd);

	bud_node *form = site_ui_delete_confirm(module, id, title, csrf_token);
	char page_title[512];
	snprintf(
	        page_title,
	        sizeof(page_title),
	        "Delete %s",
	        title[0] ? title : id);

	char href_path[256];
	snprintf(href_path, sizeof(href_path), "/%s/%s/delete", module, id);

	return site_ui_respond_form_page(
	        fd, username, page_title, href_path, "🗑️", module, form);
}

/* POST /<module>/:id/delete — perform delete */
static int index_delete_handler(int fd, char *body)
{
	char id[128] = { 0 };

	if (mpfd_parse(fd, body) == -1)
		return respond_error(fd, 415, "Expected multipart/form-data");
	if (csrf_check_mpfd(fd))
		return 1;

	axil_env_get(fd, id, "PATTERN_PARAM_ID");
	if (!id[0])
		return bad_request(fd, "Missing ID");

	const char *module = index_name(fd);

	char item_path[512];
	if (item_path_build(fd, module, id, item_path, sizeof(item_path)) != 0)
		return server_error(fd, "Failed to resolve item path");

	const char *username = get_request_user(fd);
	if (item_require_access(
	            fd,
	            item_path,
	            username,
	            ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	            "Not found",
	            "Forbidden"))
		return 1;

	/* Remove item directory */
	int remove_rc = item_remove_path_recursive(item_path);
	if (remove_rc != 0) {
		fprintf(stderr,
		        "ERROR delete: item_remove_path_recursive failed for "
		        "%s\n",
		        item_path);
	}

	/* Find module slot and call cleanup + index_del */
	unsigned hd = 0;
	pid_t pid = getpid();
	for (size_t i = 0; i < module_slot_count; i++) {
		char simple_name[257];
		snprintf(
		        simple_name,
		        sizeof(simple_name),
		        "%s",
		        module_names[i]);
		char *dot = strchr(simple_name, '.');
		if (dot)
			*dot = '\0';

		if (strcmp(module_names[i], module) == 0 ||
		    strcmp(simple_name, module) == 0)
		{
			hd = module_hds[i];
			if (module_cleanups[i]) {
				module_cleanups[i](id);
			}
			break;
		}
	}

	/* Clear inverse references in other datasets before purging qmap */
	{
		char dset[256];
		snprintf(dset, sizeof(dset), "%s.items", module);
		source_clear_inverse_refs(dset, id);
	}

	if (hd) {
		qmap_del_all(hd, id);
	}

	/* Also clean up fields handle so detail handlers return 404 */
	{
		char dataset_id[256];
		snprintf(dataset_id, sizeof(dataset_id), "%s.items", module);
		unsigned fhd = source_get_fields_hd(dataset_id);
		if (fhd)
			qmap_del_all(fhd, id);
	}

	char location[256];
	snprintf(location, sizeof(location), "/%s", module);
	return axil_redirect(fd, location);
}

/* ------------------------------------------------------------------ */
/* Ownership helpers — item metadata management                        */
/* ------------------------------------------------------------------ */

static void build_owner_path(const char *ip, char *out, size_t len)
{
	snprintf(out, len, "%s/owner", ip);
}

NDX_LISTENER(int, item_record_ownership,
	const char *, item_path,
	const char *, username)
{
	if (geteuid() == 0) {
		int uid = auth_get_uid(username);
		if (uid >= 0)
			chown(item_path, (uid_t)uid, (gid_t)-1);
	} else {
		char owner_path[1024];
		build_owner_path(item_path, owner_path, sizeof(owner_path));
		FILE *fp = fopen(owner_path, "w");
		if (fp) {
			fwrite(username, 1, strlen(username), fp);
			fclose(fp);
		}
	}
	return 0;
}

static int item_unlink_owner(const char *item_path)
{
	if (geteuid() != 0) {
		char owner_path[1024];
		build_owner_path(item_path, owner_path, sizeof(owner_path));
		unlink(owner_path);
	}
	return 0;
}

NDX_LISTENER(int, check_item_access,
	int, fd,
	const char *, module,
	char *, id, size_t, id_sz,
	const char **, user,
	char *, item_path, size_t, path_sz)
{
	*user = require_user(fd);
	if (!*user)
		return -1;

	axil_env_get(fd, id, "PATTERN_PARAM_ID");
	if (!id[0]) {
		bad_request(fd, "Missing ID");
		return -1;
	}

	if (item_path_build(fd, module, id, item_path, path_sz) != 0) {
		server_error(fd, "Failed to resolve path");
		return -1;
	}

	if (item_require_access(
	            fd,
	            item_path,
	            *user,
	            ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	            "Not found",
	            "Forbidden"))
		return -1;

	return 0;
}

void ndx_install(void)
{
	ndx_load("./mods/common/common");
	ndx_load("./mods/auth/auth");
	ndx_load("./mods/mpfd/mpfd");

	module_hd = qmap_open(NULL, NULL, QM_STR, QM_U32, 0x1FF, 0);

	axil_register_handler("GET:/", core_get);
	axil_config.default_handler = core_get;
}
