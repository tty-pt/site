#include <stdio.h>
#include <limits.h>
#include <dirent.h>
#include <errno.h>
#include <iconv.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ttypt/qmap.h>
#include <ttypt/ndx-mod.h>
#include <ttypt/ndc.h>

#include "./../common/common.h"
#include "./../proxy/proxy.h"
#include "./../mpfd/mpfd.h"
#include "./../auth/auth.h"

typedef void (*index_cleanup_fn)(const char *id);

#define MAX_MODULES 64

static int index_add_get_handler(int fd, char *body);
static int index_delete_get_handler(int fd, char *body);
static int index_delete_handler(int fd, char *body);
int index_add_item(int fd, char *body, char *id_out, size_t id_len);

static char modules_header[2 * 256 * MAX_MODULES];

static char modules_json[256 * MAX_MODULES],
	    *modules_json_end = modules_json;

static size_t modules_rem = sizeof(modules_json),
	      modules_count = 0;

static unsigned module_hd;
static iconv_t cd;

/* Per-module cleanup callbacks and hd lookup */
static char  module_names[MAX_MODULES][256];
static unsigned module_hds[MAX_MODULES];
static void (*module_cleanups[MAX_MODULES])(const char *id);
static size_t module_slot_count = 0;

NDX_LISTENER(int, index_id,
		char *, result,
		size_t, result_len,
		const char *, title,
		size_t, title_len)
{
	size_t i, j, written;
	char *o = result;

	written = result_len;
	iconv(cd, (char **) &title, &title_len,
			&result, &result_len);
	written -= result_len;

	for (i = 0; i < written; i++) {
		register char c = *o;
		if (c == ' ') {
			*o = '_';
			o++;
		} else if (c >= 'A' && c <= 'Z') {
			*o = *o + 32;
			o++;
		} else if ((c >= 'a' && c <= 'z')
				|| (c >= '0' && c <= '9'))
			o++;
	}
	*o = '\0';
	return 0;
}

int index_update_json(
		const char * id,
		const char * title,
		unsigned flags)
{
	long offset;
	char module_json[256];

	offset = snprintf(modules_json_end, modules_rem, "%c{"
			"\"id\":\"%s\","
			"\"title\":\"%s\","
			"\"flags\":\"%u\"}",
			(modules_count ? ',' : '['),
			id, title, flags);

	if (offset < 0)
		return -1;

	modules_json_end += offset;
	modules_rem -= offset;
	modules_json_end[0] = ']';
	modules_json_end[1] = '\0';

	memset(modules_header, 0, sizeof(modules_header));
	b64_encode(modules_json,
			modules_header,
			sizeof(modules_header));

	modules_count++;

	return 0;
}

static const char *index_name(int fd)
{
	static char uri[256];
	char *module;

	ndc_env_get(fd, uri, "DOCUMENT_URI");
	module = strchr(uri + 1, '/');
	if (module)
		*module = '\0';
	module = uri + 1;
	return module;
}

static int index_add_handler(
		int fd,
		char *body)
{
	char id[256] = {0};
	const char *module;
	char path[512];

	if (index_add_item(fd, body, id, sizeof(id)) != 0)
		return 1;

	module = index_name(fd);
	snprintf(path, sizeof(path), "/%s/%s", module, id);
	return redirect(fd, path);
}

/*
 * NDX hook: create an index item from a parsed multipart form on fd/body.
 * Performs auth check, creates the item directory, writes the title file,
 * records ownership, and updates the in-memory index.
 * Writes the generated id into id_out (up to id_len bytes).
 * On error, sends the error response itself and returns non-zero.
 * On success returns 0 and id_out is populated — caller must redirect.
 */
NDX_LISTENER(int, index_add_item, int, fd, char *, body, char *, id_out, size_t, id_len)
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

	title_len = mpfd_get("title", title, sizeof(title) - 1);
	if (title_len <= 0)
		return bad_request(fd, "Missing title");

	index_id(id, sizeof(id), title, title_len);
	module = index_name(fd);

	if (item_path_build(fd, module, id, path, sizeof(path)) != 0)
		return server_error(fd, "Failed to resolve item path");

	int r = mkdir(path, 0755);
	if (r == -1 && errno != EEXIST)
		return respond_error(fd, 403, "You don't have permissions for that");

	item_record_ownership(path, username);

	if (write_meta_file(path, "title", title, (size_t)title_len) != 0)
		return respond_error(fd, 403, "You don't have permissions for that");

	hd = *(unsigned *) qmap_get(module_hd, module);
	qmap_put(hd, id, title);

	snprintf(id_out, id_len, "%s", id);
	return 0;
}

static int
index_page(unsigned fd, unsigned hd, char *path, char *title)
{
	register size_t total = 0;
	unsigned cur = qmap_iter(hd, NULL, 0);
	const void *key, *val;
	char *body, *s;
	int ret;

	// count body size
	while (qmap_next(&key, &val, cur)) {
		register size_t klen, vlen;
		klen = qmap_len(QM_STR, key);
		vlen = qmap_len(QM_STR, val);
		total += klen + vlen + 3;
	}

	// alloc it
	total++;
	body = malloc(total);
	s = body;

	// store it
	cur = qmap_iter(hd, NULL, 0);
	while (qmap_next(&key, &val, cur)) {
		s += sprintf(s, "%s %s\r\n",
				(char *) key,
				(char *) val);
	}

	// send it
	*s = '\0';
	proxy_init("POST", path);
	proxy_add_standard_headers(fd, modules_header);
	ret = proxy_body(fd, body, total);
	free(body);
	return ret;
}

static int index_list_handler(
		int fd,
		char *body)
{
	char path[1024];
	unsigned hd;
	const char *module;

	module = index_name(fd);

	hd = *(unsigned *) qmap_get(module_hd, module);

	ndc_env_get(fd, path, "DOCUMENT_URI");

	return index_page(fd, hd, path,
			(char *) module);
}

NDX_LISTENER(unsigned, index_open,
		const char *, name,
		unsigned, mask,
		unsigned, flags,
		index_cleanup_fn, cleanup)
{
	unsigned hd = qmap_open(NULL, "hd", QM_STR, QM_STR,
			mask ? mask : 0x3FF, QM_SORTED);

	struct dirent *entry;
	char buf[PATH_MAX / 2];
	char id[256] = { 0 };
	DIR *dir;

	index_id(id, sizeof(id), name, strlen(name));
	index_update_json(id, name, flags);

	if (!(flags & 1))
		return 0;

	if (module_path_build(".", id, buf, sizeof(buf)) != 0)
		return QM_MISS;
	mkdir(buf, 0755);
	if (module_items_path_build(".", id, buf, sizeof(buf)) != 0)
		return QM_MISS;
	mkdir(buf, 0755);

	dir = opendir(buf);
	if (!dir) {
		perror("opendir");
		return QM_MISS;
	}

	while ((entry = readdir(dir)) != NULL) {
		char title[256] = { 0 };
		char item_path[PATH_MAX];

		if (item_path_build_root(".", id, entry->d_name,
				item_path, sizeof(item_path)) != 0)
			continue;

		if (read_meta_file(item_path, "title", title, sizeof(title)) != 0)
			continue;

		qmap_put(hd, entry->d_name, title);
	}

	closedir(dir);

	snprintf(buf, sizeof(buf), "POST:/%s/add", id);
	ndc_register_handler(buf, index_add_handler);

	snprintf(buf, sizeof(buf), "GET:/%s/add", id);
	ndc_register_handler(buf, index_add_get_handler);

	snprintf(buf, sizeof(buf), "GET:/%s", id);
	ndc_register_handler(buf, index_list_handler);

	snprintf(buf, sizeof(buf), "GET:/%s/:id/delete", id);
	ndc_register_handler(buf, index_delete_get_handler);

	snprintf(buf, sizeof(buf), "POST:/%s/:id/delete", id);
	ndc_register_handler(buf, index_delete_handler);

	if (module_slot_count < MAX_MODULES) {
		size_t slot = module_slot_count++;
		snprintf(module_names[slot], sizeof(module_names[slot]), "%s", id);
		module_hds[slot] = hd;
		module_cleanups[slot] = cleanup;
	}

	qmap_put(module_hd, id, &hd);
	return hd;
}

NDX_LISTENER(unsigned, index_put,
		unsigned, hd,
		char *, key,
		char *, value)
{
	return qmap_put(hd, key, value);
}

static int
index_del(unsigned hd, char *key)
{
	qmap_del(hd, key);
	return 0;
}

static unsigned
index_get(unsigned hd, char *value, size_t len, char *key)
{
	const void *val = qmap_get(hd, key);
	size_t rlen = qmap_len(QM_STR, val);

	if (rlen > len)
		return 1;

	memcpy(value, val, rlen);
	return 0;
}

NDX_LISTENER(int, core_get,
		int, fd,
		char *, body)
{
	(void)body;

	char path[512] = { 0 };
	char param[512] = { 0 };
	char full_path[PATH_MAX] = { 0 };
	char cookie[256] = { 0 };
	char token[64] = { 0 };
	char host[256] = { 0 };

	ndc_env_get(fd, path, "DOCUMENT_URI");
	ndc_env_get(fd, param, "QUERY_STRING");
	if (param[0])
		snprintf(full_path, sizeof(full_path), "%s?%s", path, param);
	else
		snprintf(full_path, sizeof(full_path), "%s", path);
	proxy_init("GET", full_path);
	proxy_add_standard_headers(fd, modules_header);
	(void)cookie; (void)token; (void)host;
	return proxy_head(fd);
}

NDX_LISTENER(int, core_post,
		int, fd,
		char *, body,
		size_t, len)
{
	(void)body;

	char uri[512] = { 0 };
	char param[512] = { 0 };
	char full_path[PATH_MAX] = { 0 };
	char cookie[256] = { 0 };
	char token[64] = { 0 };
	char host[256] = { 0 };

	ndc_env_get(fd, uri, "DOCUMENT_URI");
	ndc_env_get(fd, param, "QUERY_STRING");
	snprintf(full_path, sizeof(full_path), "%s?%s", uri, param);
	proxy_init("POST", full_path);
	proxy_add_standard_headers(fd, modules_header);
	(void)cookie; (void)token; (void)host;
	return proxy_body(fd, body, len);
}

static int index_add_get_handler(
		int fd,
		char *body)
{
	return core_get(fd, body);
}

/* GET /<module>/:id/delete — confirmation page */
static int index_delete_get_handler(int fd, char *body)
{
	(void)body;

	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");
	if (!id[0])
		return bad_request(fd, "Missing ID");

	const char *module = index_name(fd);

	char item_path[512];
	if (item_path_build(fd, module, id, item_path, sizeof(item_path)) != 0)
		return server_error(fd, "Failed to resolve item path");

	const char *username = get_request_user(fd);
	if (item_require_access(fd, item_path, username,
			ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
			"Not found", "Forbidden"))
		return 1;

	char title[256] = {0};
	read_meta_file(item_path, "title", title, sizeof(title));

	json_object_t *jo = json_object_new(0);
	if (!jo)
		return respond_error(fd, 500, "OOM");
	if (json_object_kv_str(jo, "id", id) != 0 ||
			json_object_kv_str(jo, "title", title) != 0) {
		json_object_free(jo);
		return respond_error(fd, 500, "OOM");
	}
	char *json = json_object_finish(jo);
	if (!json)
		return respond_error(fd, 500, "OOM");
	int rc = core_post_json(fd, json);
	free(json);
	return rc;
}

/* POST /<module>/:id/delete — perform delete */
static int index_delete_handler(int fd, char *body)
{
	(void)body;

	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");
	if (!id[0])
		return bad_request(fd, "Missing ID");

	const char *module = index_name(fd);

	char item_path[512];
	if (item_path_build(fd, module, id, item_path, sizeof(item_path)) != 0)
		return server_error(fd, "Failed to resolve item path");

	const char *username = get_request_user(fd);
	if (item_require_access(fd, item_path, username,
			ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
			"Not found", "Forbidden"))
		return 1;

	/* Remove ownership file */
	item_unlink_owner(item_path);

	/* Find module slot and call cleanup + index_del */
	unsigned hd = 0;
	for (size_t i = 0; i < module_slot_count; i++) {
		if (strcmp(module_names[i], module) == 0) {
			hd = module_hds[i];
			if (module_cleanups[i])
				module_cleanups[i](id);
			break;
		}
	}

	if (item_remove_path_recursive(item_path) != 0)
		return server_error(fd, "Failed to delete item path");

	if (hd)
		qmap_del(hd, id);

	char location[256];
	snprintf(location, sizeof(location), "/%s/", module);
	return redirect(fd, location);
}

void ndx_install(void)
{
	ndx_load("./mods/common/common");
	ndx_load("./mods/mpfd/mpfd");
	ndx_load("./mods/proxy/proxy");

	module_hd = qmap_open(NULL, NULL,
			QM_STR, QM_U32, 0x1FF, 0);

	cd = iconv_open("ASCII//TRANSLIT", "UTF-8");
	ndc_config.default_handler = core_get;
}
