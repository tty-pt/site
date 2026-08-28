#include <stdio.h>
#include <limits.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ttypt/qmap.h>
#include <ttypt/xy-mod.h>
#include <ttypt/axil.h>

#include "./../common/common.h"
#include "./../source/source.h"
#include "./../mpfd/mpfd.h"
#include "./../auth/auth.h"

#define INDEX_IMPL
#include "index.h"

#define MAX_MODULES 64

static int index_add_get_handler(int fd, char *body);
static int index_generic_edit_get_handler(int fd, char *body);
static int index_delete_get_handler(int fd, char *body);
static int index_delete_handler(int fd, char *body);
int index_add_item(int fd, char *body, char *id_out, size_t id_len);

static char modules_json[256 * MAX_MODULES], *modules_json_end = modules_json;

static size_t modules_rem = sizeof(modules_json), modules_count = 0;

static unsigned module_hd;

static char module_names[MAX_MODULES][256];
static char module_titles[MAX_MODULES][256];
static unsigned module_hds[MAX_MODULES];
static void (*module_cleanups[MAX_MODULES])(const char *id);
static size_t module_slot_count = 0;

#include "ux/all.c"
#include "list_fill.c"

int index_update_json(const char *id, const char *title)
{
	long offset;
	char id_esc[512], title_esc[512];

	if (modules_count >= MAX_MODULES)
		return -1;

	snprintf(
	        module_names[modules_count], sizeof(module_names[0]), "%s", id);
	snprintf(
	        module_titles[modules_count], sizeof(module_titles[0]), "%s",
	        title);

	axil_json_escape(id, id_esc, sizeof(id_esc));
	axil_json_escape(title, title_esc, sizeof(title_esc));

	offset = snprintf(
	        modules_json_end, modules_rem,
	        "%c{"
	        "\"id\":\"%s\","
	        "\"title\":\"%s\"}",
	        (modules_count ? ',' : '['), id_esc, title_esc);

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

	axil_env_get(fd, uri, sizeof(uri), "DOCUMENT_URI");
	module = strchr(uri + 1, '/');
	if (module)
		*module = '\0';
	module = uri + 1;
	return module;
}

/*
 * XY hook: create an index item from a parsed multipart form on fd/body.
 * Performs auth check, creates the item directory, writes the title file,
 * records ownership, and updates the in-memory index.
 * Writes the generated id into id_out (up to id_len bytes).
 * On error, sends the error response itself and returns non-zero.
 * On success returns 0 and id_out is populated — caller must redirect.
 */
XY_IMPL(int, index_add_item,
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

	title_len = mpfd_get("title", title, sizeof(title));
	if (title_len <= 0)
		return bad_request(fd, "Missing title");

	axil_slugify(title, (size_t)title_len, id, sizeof(id));
	module = index_name(fd);

	if (item_path_build(fd, module, id, path, sizeof(path)) != 0)
		return server_error(fd, "Failed to resolve item path");

	int r = mkdir(path, 0755);
	if (r == -1 && errno == EEXIST)
		return respond_error(
		        fd, 409, "An item with that title already exists");
	if (r == -1)
		return respond_error(
		        fd, 403, "You don't have permissions for that");

	if (module_item_owner_record(fd, module, id, username) != 0) {
		item_remove_path_recursive(path);
		return server_error(fd, "Failed to record ownership");
	}

	if (write_meta_file(path, "title", title, (size_t)title_len) != 0) {
		item_remove_path_recursive(path);
		return respond_error(
		        fd, 403, "You don't have permissions for that");
	}

	{
		char dataset_id[512];
		snprintf(dataset_id, sizeof(dataset_id), "%s.items", module);
		if (source_refresh_row(fd, dataset_id, id) != 0) {
			item_remove_path_recursive(path);
			return server_error(fd, "Failed to refresh item");
		}
	}

	snprintf(id_out, id_len, "%s", id);
	return 0;
}

static int idx_render_list_bud(
        int fd, const char *module, const char *query_str, const char *username)
{
	char dataset_id[256];
	list_state_t state;
	bud_node *layout;
	char title[128];
	char path[256];
	char *extra_head = NULL;
	int rc;

	memset(&state, 0, sizeof(state));
	snprintf(
	        state.module, sizeof(state.module), "%s", module ? module : "");
	snprintf(
	        state.username, sizeof(state.username), "%s",
	        username ? username : "");
	snprintf(dataset_id, sizeof(dataset_id), "%s.items", module);

	/* Filler parses params, whitelists the qs into state.query, queries
	 * and slices. On query failure it leaves nids/total at 0 with the
	 * column metadata intact so the chrome still renders (empty page). */
	list_fill_state(&state, dataset_id, query_str, 1);

	/* One render path: serialize state -> JSON, render, respond. */
	{
		size_t json_budget =
		        8192 +
		        (size_t)state.nids * (size_t)(state.ncols * 512 + 128) +
		        (size_t)state.nopts * 256;
		char *json = malloc(json_budget);

		if (json) {
			if (list_state_to_json(&state, json, json_budget) == 0)
			{
				size_t head_len = strlen(json) + 128;
				extra_head = malloc(head_len);
				if (extra_head)
					snprintf(
					        extra_head, head_len,
					        "<script "
					        "type=\"application/json\" "
					        "id=\"bud-state\">%s</script>",
					        json);
			}
			free(json);
		}
	}

	layout = list_render(&state);
	rc = 0;
	if (layout) {
		snprintf(title, sizeof(title), "%ss", state.display_name);
		if (title[0] >= 'a')
			title[0] -= 32;
		snprintf(path, sizeof(path), "/%s/", module);
		respond_html(
		        fd, site_ui_page(
		                    title, path, site_ui_module_icon(module),
		                    username, extra_head, "list", layout));
	} else {
		axil_respond(fd, 500, "Internal Server Error");
		rc = -1;
	}
	free(extra_head);
	list_fill_free(&state);
	return rc;
}

XY_IMPL(int, index_render_list,
	int, fd,
	unsigned, hd,
	index_format_fn, fmt)
{
	char query[512] = { 0 };
	const char *username;

	(void)hd;
	(void)fmt;
	axil_env_get(fd, query, sizeof(query), "QUERY_STRING");
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

	int title_len = mpfd_get("title", title, sizeof(title));
	if (title_len <= 0)
		return bad_request(fd, "Missing title");

	axil_slugify(title, (size_t)title_len, id, sizeof(id));
	snprintf(dataset_id, sizeof(dataset_id), "%s.items", module);

	if (item_path_build(fd, module, id, items_path, sizeof(items_path)) !=
	    0)
		return server_error(fd, "Failed to resolve item path");

	if (mkdir(items_path, 0755) == -1) {
		if (errno == EEXIST)
			return respond_error(
			        fd, 409,
			        "An item with that title already exists");
		return respond_error(
		        fd, 403, "Failed to create item directory");
	}

	if (module_item_owner_record(fd, module, id, username) != 0) {
		item_remove_path_recursive(items_path);
		return server_error(fd, "Failed to record ownership");
	}

	unsigned data_handle = source_parse_form(dataset_id);
	if (!data_handle) {
		item_remove_path_recursive(items_path);
		return server_error(fd, "OOM");
	}

	if (source_update_item(fd, dataset_id, id, data_handle) != 0) {
		qmap_close(data_handle);
		item_remove_path_recursive(items_path);
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
	qmap_close(data_handle);
	if (rc != 0) {
		return server_error(fd, "Failed to update item data");
	}

	return redirect_to_item(fd, module, ctx->id);
}

static int index_generic_edit_handler(int fd, char *body)
{
	const char *module = index_name(fd);

	return with_module_item_access(
	        fd, body, module, ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP, NULL,
	        NULL, index_generic_edit_authorized, NULL);
}

XY_IMPL(unsigned, index_open,
	const char *, name,
	const char *, dataset_name,
	index_cleanup_fn, cleanup,
	index_detail_handler_fn, detail_handler,
	index_handler_fn, add_handler,
	index_handler_fn, edit_get_handler,
	index_handler_fn, edit_post_handler,
	const char *, url_slug)
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

	if (url_slug && url_slug[0])
		snprintf(id, sizeof(id), "%s", url_slug);
	else
		axil_slugify(name, strlen(name), id, sizeof(id));
	index_update_json(id, name);
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

	snprintf(buf, sizeof(buf), "GET:/%s/:id/edit", id);
	axil_register_handler(
	        buf, edit_get_handler ? edit_get_handler
	                              : index_generic_edit_get_handler);
	snprintf(buf, sizeof(buf), "POST:/%s/:id/edit", id);
	axil_register_handler(
	        buf, edit_post_handler ? edit_post_handler
	                               : index_generic_edit_handler);

	if (module_slot_count < MAX_MODULES) {
		size_t slot = module_slot_count++;
		snprintf(
		        module_names[slot], sizeof(module_names[slot]), "%s",
		        id);
		module_hds[slot] = hd;
		module_cleanups[slot] = cleanup;
	}

	qmap_put(module_hd, id, &hd);
	return hd;
}

XY_IMPL(int, core_get, int, fd, char *, body)
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
		        fd, site_ui_page(
		                    "tty.pt", "/", site_ui_module_icon(NULL),
		                    username, NULL, NULL, layout));
	}
	axil_respond(fd, 500, "Internal Server Error");
	return 0;
}

static int index_add_get_handler(int fd, char *body)
{
	(void)body;
	const char *user = require_user(fd);
	if (!user)
		return 1;

	const char *module = index_name(fd);
	char dataset_id[128];
	snprintf(dataset_id, sizeof(dataset_id), "%s.items", module);

	int count = 0;
	const source_desc_t *defs = source_get_desc(dataset_id, &count);
	if (!defs)
		return core_get(fd, body);

	const char *csrf_token = csrf_setup(fd);
	char action[128], cancel_href[128];
	snprintf(action, sizeof(action), "/%s/add", module);
	snprintf(cancel_href, sizeof(cancel_href), "/%s/", module);

	pick_view_t pv;
	int active_scope = -1;
	char qs[4096] = { 0 };
	if (fd > 0)
		axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");
	hyle_bud_picker_view_collect_schema(qs, defs, NULL, &pv, &active_scope);

	bud_node *form = site_ui_form_from_desc(
	        action, cancel_href, "Add", defs, NULL, csrf_token, &pv, NULL);

	return site_ui_respond_add_page(
	        fd, user, module, site_ui_module_icon(module), form);
}

static int
index_generic_edit_auth(int fd, char *body, const item_ctx_t *ctx, void *user)
{
	(void)body;
	(void)user;
	const char *module = index_name(fd);
	char dataset_id[128];
	snprintf(dataset_id, sizeof(dataset_id), "%s.items", module);

	int count = 0;
	const source_desc_t *defs = source_get_desc(dataset_id, &count);
	if (!defs)
		return respond_error(fd, 404, "Module schema not found");

	size_t rec_sz = source_get_record_size(dataset_id);
	if (rec_sz == 0 || rec_sz > 65536)
		rec_sz = 4096;

	char *record = calloc(1, rec_sz);
	if (!record)
		return server_error(fd, "OOM");

	source_meta_read(ctx->item_path, defs, count, record, rec_sz);
	source_resolve_meta_display(dataset_id, ctx->id, defs, count, record);

	char title[256] = { 0 };
	read_meta_file(ctx->item_path, "title", title, sizeof(title));

	char *vstr_val = NULL;
	for (int i = 0; i < count; i++) {
		if (defs[i].qm_type == BUD_QM_VSTR && defs[i].file) {
			char vstr_path[PATH_MAX];
			item_child_path(
			        ctx->item_path, defs[i].file, vstr_path,
			        sizeof(vstr_path));
			vstr_val = slurp_file(vstr_path);
			break;
		}
	}

	const char *csrf_token = csrf_setup(fd);
	char action[256], cancel_href[256];
	snprintf(action, sizeof(action), "/%s/%s/edit", module, ctx->id);
	snprintf(cancel_href, sizeof(cancel_href), "/%s/%s", module, ctx->id);

	pick_view_t pv;
	int active_scope = -1;
	char qs[4096] = { 0 };
	if (fd > 0)
		axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");
	hyle_bud_picker_view_collect_schema(qs, defs, record, &pv, &active_scope);

	bud_node *form = site_ui_form_from_desc(
	        action, cancel_href, "Save Changes", defs, record, csrf_token,
	        &pv, vstr_val);

	if (vstr_val)
		free(vstr_val);
	free(record);

	return site_ui_respond_edit_page(
	        fd, ctx->username, module, site_ui_module_icon(module),
	        title[0] ? title : ctx->id, ctx->id, form);
}

static int index_generic_edit_get_handler(int fd, char *body)
{
	const char *module = index_name(fd);
	return with_module_item_access(
	        fd, body, module, ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	        "Not found", NULL, index_generic_edit_auth, NULL);
}

/* GET /<module>/:id/delete — confirmation page */
static int index_delete_get_handler(int fd, char *body)
{
	(void)body;

	char id[128] = { 0 };
	axil_env_get(fd, id, sizeof(id), "PATTERN_PARAM_ID");
	if (!id[0])
		return bad_request(fd, "Missing ID");

	const char *module = index_name(fd);

	char item_path[512];
	if (item_path_build(fd, module, id, item_path, sizeof(item_path)) != 0)
		return server_error(fd, "Failed to resolve item path");

	const char *username = get_request_user(fd);
	if (item_require_access(
	            fd, item_path, username,
	            ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP, "Not found",
	            "Forbidden"))
		return 1;

	char title[256] = { 0 };
	read_meta_file(item_path, "title", title, sizeof(title));

	const char *csrf_token = csrf_setup(fd);

	bud_node *form = site_ui_delete_confirm(module, id, title, csrf_token);
	char page_title[512];
	snprintf(
	        page_title, sizeof(page_title), "Delete %s",
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

	axil_env_get(fd, id, sizeof(id), "PATTERN_PARAM_ID");
	if (!id[0])
		return bad_request(fd, "Missing ID");

	const char *module = index_name(fd);

	char item_path[512];
	if (item_path_build(fd, module, id, item_path, sizeof(item_path)) != 0)
		return server_error(fd, "Failed to resolve item path");

	const char *username = get_request_user(fd);
	if (item_require_access(
	            fd, item_path, username,
	            ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP, "Not found",
	            "Forbidden"))
		return 1;

	/* Clear inverse references in other datasets before purging qmap */
	{
		char dset[256];
		snprintf(dset, sizeof(dset), "%s.items", module);
		source_clear_inverse_refs(fd, dset, id);
	}

	/* Find module slot and call module-specific cleanup */
	{
		size_t i;
		for (i = 0; i < module_slot_count; i++) {
			char simple_name[257];
			snprintf(
			        simple_name, sizeof(simple_name), "%s",
			        module_names[i]);
			char *dot = strchr(simple_name, '.');
			if (dot)
				*dot = '\0';
			if (strcmp(module_names[i], module) == 0 ||
			    strcmp(simple_name, module) == 0)
			{
				if (module_cleanups[i])
					module_cleanups[i](id);
				break;
			}
		}
	}

	/* Delete through hyle (removes dir + qmaps + marks stoma_dirty) */
	{
		char dset[256];
		source_def_t *def;
		snprintf(dset, sizeof(dset), "%s.items", module);
		def = source_find(dset);
		if (def)
			source_delete_item(fd, def, id);
		else
			item_remove_path_recursive(item_path);
	}

	char location[256];
	snprintf(location, sizeof(location), "/%s", module);
	return axil_redirect(fd, location);
}

XY_IMPL(int, check_item_access,
	int, fd,
	const char *, module,
	char *, id, size_t, id_sz,
	const char **, user,
	char *, item_path, size_t, path_sz)
{
	*user = require_user(fd);
	if (!*user)
		return -1;

	axil_env_get(fd, id, id_sz, "PATTERN_PARAM_ID");
	if (!id[0]) {
		bad_request(fd, "Missing ID");
		return -1;
	}

	if (item_path_build(fd, module, id, item_path, path_sz) != 0) {
		server_error(fd, "Failed to resolve path");
		return -1;
	}

	if (item_require_access(
	            fd, item_path, *user, ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
	            "Not found", "Forbidden"))
		return -1;

	return 0;
}

XY_IMPL(uint32_t, index_module_init, const index_module_def_t *, def)
{
	if (!def || !def->name)
		return 0;

	char dataset_id[128];
	snprintf(dataset_id, sizeof(dataset_id), "%s.items", def->name);

	uint32_t rid = source_setup(
	        dataset_id, def->key_field, def->record_size,
	        def->items_path ? def->items_path : "", def->schema,
	        def->field_count, def->flags, def->list_view);

	index_open(
	        def->display_name ? def->display_name : def->name, dataset_id,
	        NULL, NULL, NULL, NULL, NULL, NULL);

	register_standard_item_handlers(def->name, &def->handlers);

	return rid;
}

void xy_install(void)
{
	xy_load("./mods/common/common");
	xy_load("./mods/auth/auth");
	xy_load("./mods/mpfd/mpfd");

	module_hd = qmap_open(NULL, NULL, QM_STR, QM_U32, 0x1FF, 0);

	axil_register_handler("GET:/", core_get);
	axil_config.default_handler = core_get;
}
