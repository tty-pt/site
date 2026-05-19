#include <stdio.h>
#include <limits.h>
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ttypt/qmap.h>
#include <ttypt/ndx-mod.h>
#include <ttypt/ndc.h>

#include "./../common/common.h"
#include "./../source/source.h"
#include "./../mpfd/mpfd.h"
#include "./../auth/auth.h"
#include "./../ssr/ssr.h"

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
int item_read_owner(const char *item_path, char *out, size_t outlen);
int item_unlink_owner(const char *item_path);

static char modules_json[256 * MAX_MODULES], *modules_json_end = modules_json;

static size_t modules_rem = sizeof(modules_json), modules_count = 0;

static unsigned module_hd;

/* Per-module cleanup callbacks and hd lookup */
static char module_names[MAX_MODULES][256];
static char module_titles[MAX_MODULES][256];
static unsigned module_hds[MAX_MODULES];
static unsigned module_flags_arr[MAX_MODULES];
static void (*module_cleanups[MAX_MODULES])(const char *id);
static size_t module_slot_count = 0;

int index_update_json(const char *id, const char *title, unsigned flags)
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
	module_flags_arr[modules_count] = flags;

	ndc_json_escape(id, id_esc, sizeof(id_esc));
	ndc_json_escape(title, title_esc, sizeof(title_esc));

	offset = snprintf(
	        modules_json_end,
	        modules_rem,
	        "%c{"
	        "\"id\":\"%s\","
	        "\"title\":\"%s\","
	        "\"flags\":%u}",
	        (modules_count ? ',' : '['),
	        id_esc,
	        title_esc,
	        flags);

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

	ndc_env_get(fd, uri, "DOCUMENT_URI");
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

	{
		char csrf_submitted[33] = { 0 };
		mpfd_get(
		        "csrf_token",
		        csrf_submitted,
		        sizeof(csrf_submitted) - 1);
		if (csrf_validate(fd, csrf_submitted))
			return respond_error(fd, 403, "Forbidden");
	}

	title_len = mpfd_get("title", title, sizeof(title) - 1);
	if (title_len <= 0)
		return bad_request(fd, "Missing title");

	ndc_slugify(title, (size_t)title_len, id, sizeof(id));
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

NDX_LISTENER(int, index_render_list,
	int, fd,
	unsigned, hd,
	index_format_fn, fmt)
{
	char path[1024];
	char query[512] = { 0 };
	const char *username;
	int ret;

	(void)hd;
	ndc_env_get(fd, path, "DOCUMENT_URI");
	ndc_env_get(fd, query, "QUERY_STRING");
	username = get_request_user(fd);
	ret = ssr_render(
	        fd, "GET", path, query, "", 0, username ? username : "");
	return ret;
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
	{
		char csrf_submitted[33] = { 0 };
		mpfd_get(
		        "csrf_token",
		        csrf_submitted,
		        sizeof(csrf_submitted) - 1);
		if (csrf_validate(fd, csrf_submitted))
			return respond_error(fd, 403, "Forbidden");
	}

	int title_len = mpfd_get("title", title, sizeof(title) - 1);
	if (title_len <= 0)
		return bad_request(fd, "Missing title");

	ndc_slugify(title, (size_t)title_len, id, sizeof(id));
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
	{
		char csrf_submitted[33] = { 0 };
		mpfd_get("csrf_token", csrf_submitted, sizeof(csrf_submitted));
		if (csrf_validate(fd, csrf_submitted))
			return respond_error(fd, 403, "Forbidden");
	}

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
	unsigned, flags,
	unsigned, hd,
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

	get_doc_root(0, doc_root, sizeof(doc_root));

	ndc_slugify(name, strlen(name), id, sizeof(id));
	index_update_json(id, name, flags);

	if (!(flags & 1))
		return 0;

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
	ndc_register_handler(
	        buf, add_handler ? add_handler : index_generic_add_handler);

	snprintf(buf, sizeof(buf), "GET:/%s/add", id);
	ndc_register_handler(buf, index_add_get_handler);

	snprintf(buf, sizeof(buf), "GET:/%s", id);
	ndc_register_handler(buf, index_list_handler);

	snprintf(buf, sizeof(buf), "GET:/%s/", id);
	ndc_register_handler(buf, index_list_handler);

	if (detail_handler) {
		snprintf(buf, sizeof(buf), "GET:/%s/:id", id);
		ndc_register_handler(buf, detail_handler);
	}

	snprintf(buf, sizeof(buf), "GET:/%s/:id/delete", id);
	ndc_register_handler(buf, index_delete_get_handler);

	snprintf(buf, sizeof(buf), "POST:/%s/:id/delete", id);
	ndc_register_handler(buf, index_delete_handler);

	if (edit_get_handler) {
		snprintf(buf, sizeof(buf), "GET:/%s/:id/edit", id);
		ndc_register_handler(buf, edit_get_handler);
	}
	snprintf(buf, sizeof(buf), "POST:/%s/:id/edit", id);
	ndc_register_handler(
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

NDX_LISTENER(unsigned, index_put, unsigned, hd, char *, key, char *, value)
{
	return qmap_put(hd, key, value);
}

NDX_LISTENER(int, core_get, int, fd, char *, body)
{
	(void)body;

	char path[512] = { 0 };
	char param[512] = { 0 };
	char full_path[PATH_MAX] = { 0 };
	ndc_env_get(fd, path, "DOCUMENT_URI");
	ndc_env_get(fd, param, "QUERY_STRING");
	snprintf(full_path, sizeof(full_path), "%s", path);
	return ssr_render(
	        fd, "GET", full_path, param, NULL, 0, get_request_user(fd));
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
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");
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

	return ssr_render_delete(fd, module, id, title);
}

/* POST /<module>/:id/delete — perform delete */
static int index_delete_handler(int fd, char *body)
{
	char id[128] = { 0 };
	char csrf_submitted[33] = { 0 };

	if (mpfd_parse(fd, body) == -1)
		return respond_error(fd, 415, "Expected multipart/form-data");
	mpfd_get("csrf_token", csrf_submitted, sizeof(csrf_submitted) - 1);
	if (csrf_validate(fd, csrf_submitted))
		return respond_error(fd, 403, "Forbidden");

	ndc_env_get(fd, id, "PATTERN_PARAM_ID");
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

	/* Remove ownership file and item directory */
	item_unlink_owner(item_path);
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

	if (hd) {
		qmap_del_all(hd, id);
	}

	char location[256];
	snprintf(location, sizeof(location), "/%s", module);
	return ndc_redirect(fd, location);
}

NDX_LISTENER(size_t, index_get_module_count, int, dummy)
{
	(void)dummy;
	return modules_count;
}

NDX_LISTENER(const char *, index_get_module_id, size_t, i)
{
	return (i < modules_count) ? module_names[i] : "";
}

NDX_LISTENER(const char *, index_get_module_title, size_t, i)
{
	return (i < modules_count) ? module_titles[i] : "";
}

NDX_LISTENER(unsigned, index_get_module_flags, size_t, i)
{
	return (i < modules_count) ? module_flags_arr[i] : 0;
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

NDX_LISTENER(int, item_read_owner,
	const char *, item_path,
	char *, out,
	size_t, outlen)
{
	if (!out || outlen == 0)
		return -1;
	out[0] = '\0';

	if (geteuid() == 0) {
		struct stat st;
		if (stat(item_path, &st) != 0)
			return -1;
		char buf[4096];
		struct passwd pw, *result = NULL;
		if (getpwuid_r(st.st_uid, &pw, buf, sizeof(buf), &result) ==
		            0 &&
		    result)
		{
			strncpy(out, result->pw_name, outlen - 1);
			out[outlen - 1] = '\0';
			return 0;
		}
		return -1;
	} else {
		char owner_path[1024];
		build_owner_path(item_path, owner_path, sizeof(owner_path));
		FILE *fp = fopen(owner_path, "r");
		if (!fp)
			return -1;
		if (fgets(out, (int)outlen, fp))
			out[strcspn(out, "\n")] = '\0';
		else
			out[0] = '\0';
		fclose(fp);
		return out[0] ? 0 : -1;
	}
}

NDX_LISTENER(int, item_unlink_owner, const char *, item_path)
{
	if (geteuid() != 0) {
		char owner_path[1024];
		build_owner_path(item_path, owner_path, sizeof(owner_path));
		unlink(owner_path);
	}
	return 0;
}

void ndx_install(void)
{
	ndx_load("./mods/common/common");
	ndx_load("./mods/mpfd/mpfd");

	module_hd = qmap_open(NULL, NULL, QM_STR, QM_U32, 0x1FF, 0);

	ndc_config.default_handler = core_get;
}
