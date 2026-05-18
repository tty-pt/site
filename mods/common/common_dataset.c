#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include <ttypt/auth.h>
#include <ttypt/ndc.h>
#include <ttypt/ndx-mod.h>
#include <ttypt/qmap.h>
#include "../mpfd/mpfd.h"

#include "common_internal.h"

#define MAX_DATASETS 64

/* internal error codes for dataset_write_init */
#define DATASET_INIT_ERR_NO_BODY 1001
#define DATASET_INIT_ERR_BAD_CSRF 1002

/*
 * Thread-safety: registration (dataset_register) is assumed to happen during
 * single-threaded startup only. If concurrent registration or read access is
 * needed later, these globals must be protected by a mutex.
 */
static dataset_def_t dataset_defs[MAX_DATASETS];
static size_t dataset_count = 0;


static int dataset_scan_item(const dataset_def_t *def, const char *id);

static int dataset_field_is_multi_reference(dataset_field_type_t type)
{
	return type == DATASET_FIELD_MULTI_REFERENCE;
}

static char *dataset_multiref_json_from_text(const char *text)
{
	json_array_t *arr;
	char *copy;
	char *line;
	char *saveptr;
	char *json;

	arr = json_array_new(0);
	if (!arr)
		return NULL;

	if (!text || !text[0]) {
		json = json_array_finish(arr);
		if (!json)
			return strdup("[]");
		return json;
	}

	copy = strdup(text);
	if (!copy) {
		json_array_free(arr);
		return NULL;
	}

	line = strtok_r(copy, "\r\n", &saveptr);
	while (line) {
		char escaped[1024];
		char raw[1060];

		if (line[0]) {
			if (json_escape(
			            line,
			            escaped,
			            sizeof(escaped) - 1) != 0)
			{
				free(copy);
				json_array_free(arr);
				return NULL;
			}
			snprintf(raw, sizeof(raw), "\"%s\"", escaped);
			if (json_array_append_raw(arr, raw) != 0) {
				free(copy);
				json_array_free(arr);
				return NULL;
			}
		}
		line = strtok_r(NULL, "\r\n", &saveptr);
	}

	free(copy);
	json = json_array_finish(arr);
	if (!json)
		return strdup("[]");
	return json;
}

static int dataset_json_object_add_field(
        json_object_t *obj, const dataset_field_t *field, const char *content)
{
	char *arr_json;
	int rc;

	if (!obj || !field)
		return -1;

	if (!dataset_field_is_multi_reference(field->type))
		return json_object_kv_str(obj, field->name, content ? content : "");

	arr_json = dataset_multiref_json_from_text(content);
	if (!arr_json)
		return -1;
	rc = json_object_kv_raw(obj, field->name, arr_json);
	free(arr_json);
	return rc;
}


static int dataset_def_index(const char *dataset_id)
{
	size_t i;

	if (!dataset_id || !dataset_id[0])
		return -1;

	for (i = 0; i < dataset_count; i++)
		if (strcmp(dataset_defs[i].id, dataset_id) == 0)
			return (int)i;

	return -1;
}


NDX_LISTENER(dataset_def_t *, dataset_find, const char *, dataset_id)
{
	int idx;

	idx = dataset_def_index(dataset_id);
	if (idx < 0)
		return NULL;

	return &dataset_defs[idx];
}


static dataset_access_result_t
dataset_access_allowed(const dataset_def_t *def, int fd, const char *username)
{
	switch (def->access_policy) {
	case DATASET_ACCESS_PUBLIC:
		return DATASET_ACCESS_RESULT_ALLOW;
	case DATASET_ACCESS_LOGIN:
		return (username && username[0])
		               ? DATASET_ACCESS_RESULT_ALLOW
		               : DATASET_ACCESS_RESULT_UNAUTHORIZED;
	}
	return DATASET_ACCESS_RESULT_FORBIDDEN;
}


static char *dataset_slurp_file(const char *path, size_t *out_len)
{
	FILE *fp = fopen(path, "r");
	if (!fp)
		return NULL;

	fseek(fp, 0, SEEK_END);
	long len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	char *buf = malloc((size_t)len + 1);
	if (!buf) {
		fclose(fp);
		return NULL;
	}

	size_t read = fread(buf, 1, (size_t)len, fp);
	buf[read] = '\0';
	fclose(fp);

	if (out_len)
		*out_len = read;
	return buf;
}

static int dataset_scan_item(const dataset_def_t *def, const char *id)
{
	char doc_root[256] = { 0 };
	get_doc_root(0, doc_root, sizeof(doc_root));
	const char *root = doc_root[0] ? doc_root : ".";

	char item_path[PATH_MAX];
	snprintf(
	        item_path,
	        sizeof(item_path),
	        "%s/%s/%s",
	        root,
	        def->items_path,
	        id);

	struct stat st;
	if (lstat(item_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		return -1;
	}

	json_object_t *obj = json_object_new(0);
	if (!obj)
		return -1;

	json_object_kv_str(obj, "id", id);

	for (size_t i = 0; i < def->field_count; i++) {
		if (!def->fields[i].file)
			continue;

		char file_path[PATH_MAX + 256];
		snprintf(
		        file_path,
		        sizeof(file_path),
		        "%s/%s",
		        item_path,
		        def->fields[i].file);

		char *content = dataset_slurp_file(file_path, NULL);
		if (content) {
			dataset_json_object_add_field(
			        obj, &def->fields[i], content);
			free(content);
		} else if (dataset_field_is_multi_reference(
		                   def->fields[i].type)) {
			dataset_json_object_add_field(obj, &def->fields[i], NULL);
		}
	}

	char *json = json_object_finish(obj);
	if (json) {
		qmap_put(def->source_hd, id, json);
	}
	return 0;
}

static int dataset_scan_items(dataset_def_t *def)
{
	char doc_root[256] = { 0 };
	get_doc_root(0, doc_root, sizeof(doc_root));
	const char *root = doc_root[0] ? doc_root : ".";

	char items_path[PATH_MAX];
	snprintf(
	        items_path, sizeof(items_path), "%s/%s", root, def->items_path);

	DIR *dir = opendir(items_path);
	if (!dir)
		return 0;

	struct dirent *entry;
	while ((entry = readdir(dir))) {
		if (entry->d_name[0] == '.')
			continue;

		dataset_scan_item(def, entry->d_name);
	}

	closedir(dir);
	return 0;
}

NDX_LISTENER(int, dataset_refresh_row, const char *, dataset_id, const char *, id)
{
	dataset_def_t *def = dataset_find(dataset_id);
	if (!def || !id || !id[0])
		return -1;
	return dataset_scan_item(def, id);
}

NDX_LISTENER(int, dataset_get_item_json,
	int, fd,
	const char *, dataset_id,
	const char *, id,
	char **, out_json)
{
	dataset_def_t *def;
	const char *username;

	if (out_json)
		*out_json = NULL;

	def = dataset_find(dataset_id);
	if (!def || !id || !id[0])
		return 404;

	username = get_request_user(fd);
	switch (dataset_access_allowed(def, fd, username)) {
	case DATASET_ACCESS_RESULT_ALLOW:
		break;
	case DATASET_ACCESS_RESULT_UNAUTHORIZED:
		return 401;
	case DATASET_ACCESS_RESULT_FORBIDDEN:
		return 403;
	}

	if (!qmap_get(def->source_hd, id)) {
		if (dataset_scan_item(def, id) != 0) {
			return 404;
		}
	}

	/* Double check existence in qmap after potential scan before calling SSR */
	if (!qmap_get(def->source_hd, id)) {
		return 404;
	}

	return ssr_dataset_get_item_json(fd, dataset_id, id, out_json);
}

static unsigned dataset_parse_row_data(const dataset_def_t *def);

NDX_LISTENER(unsigned, dataset_parse_form, const char *, dataset_id)
{
	const dataset_def_t *def = dataset_find(dataset_id);
	if (!def)
		return 0;
	return dataset_parse_row_data(def);
}

NDX_LISTENER(int, dataset_register, const dataset_def_t *, def)
{
	if (!def || !def->id || !def->id[0] || !def->key_field ||
	    !def->key_field[0] || !def->fields || def->field_count == 0 ||
	    !def->items_path || def->source_hd == 0)
		return -1;

	if (dataset_count >= MAX_DATASETS || dataset_find(def->id))
		return -1;

	dataset_scan_items((dataset_def_t *)def);

	dataset_defs[dataset_count] = *def;
	dataset_count++;
	return 0;
}

NDX_LISTENER(int, dataset_get_json,
	int, fd,
	const char *, dataset_id,
	const char *, include,
	char **, out_json)
{
	dataset_def_t *def;
	const char *username;
	dataset_access_result_t access_result;

	if (out_json)
		*out_json = NULL;

	def = dataset_find(dataset_id);
	if (!def)
		return 404;

	username = get_request_user(fd);
	access_result = dataset_access_allowed(def, fd, username);
	if (access_result == DATASET_ACCESS_RESULT_UNAUTHORIZED)
		return 401;
	if (access_result == DATASET_ACCESS_RESULT_FORBIDDEN)
		return 403;

	return ssr_dataset_get_json(fd, dataset_id, include, out_json);
}

static int dataset_get_handler(int fd, char *body)
{
	char dataset_id[256] = { 0 };
	char query[512] = { 0 };
	char include[512] = { 0 };
	char *json;
	int rc;

	(void)body;

	ndc_env_get(fd, dataset_id, "PATTERN_PARAM_DATASET_ID");
	ndc_env_get(fd, query, "QUERY_STRING");
	if (query[0]) {
		ndc_query_parse(query);
		ndc_query_param("include", include, sizeof(include) - 1);
	}

	rc = dataset_get_json(
	        fd, dataset_id, include[0] ? include : NULL, &json);
	if (rc == 0) {
		rc = respond_json(fd, 200, json);
		free(json);
		return rc;
	}
	if (rc == 404)
		return respond_error(fd, 404, "Dataset not found");
	if (rc == 401)
		return respond_error(fd, 401, "Unauthorized");
	if (rc == 403)
		return respond_error(fd, 403, "Forbidden");
	if (rc == 400)
		return respond_error(fd, 400, "Invalid include");
	return server_error(fd, "Failed to render dataset");
}

static int dataset_write_init(
        int fd,
        char *body,
        const dataset_def_t **out_def,
        const char **out_username)
{
	char dataset_id[256] = { 0 };
	ndc_env_get(fd, dataset_id, "PATTERN_PARAM_DATASET_ID");

	*out_def = dataset_find(dataset_id);
	if (!*out_def) {
		return 404;
	}

	*out_username = get_request_user(fd);
	if (dataset_access_allowed(*out_def, fd, *out_username) !=
	    DATASET_ACCESS_RESULT_ALLOW)
	{
		return (*out_username && **out_username) ? 403 : 401;
	}

	/* Check content type for write operations */
	char ct[256] = { 0 };
	ndc_env_get(fd, ct, "HTTP_CONTENT_TYPE");
	if (ct[0] && !strstr(ct, "application/x-www-form-urlencoded")) {
		return 415;
	}

	if (!body || !body[0]) {
		return DATASET_INIT_ERR_NO_BODY;
	}

	{
		char *copy;
		copy = strdup(body);
		if (!copy)
			return -1;
		ndc_query_parse(copy);
		char csrf[64] = { 0 };
		ndc_query_param("csrf_token", csrf, sizeof(csrf) - 1);
		free(copy);
		if (csrf_validate(fd, csrf) != 0) {
			return DATASET_INIT_ERR_BAD_CSRF;
		}
	}

	return 0;
}

static int dataset_hex_digit_value(int ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	return -1;
}

static size_t dataset_url_decode_component(
        const char *src, size_t src_len, char *out, size_t out_len)
{
	size_t si;
	size_t oi;

	if (!out || out_len == 0)
		return 0;

	oi = 0;
	for (si = 0; si < src_len && oi + 1 < out_len; si++) {
		if (src[si] == '+') {
			out[oi++] = ' ';
			continue;
		}
		if (src[si] == '%' && si + 2 < src_len) {
			int hi;
			int lo;

			hi = dataset_hex_digit_value(src[si + 1]);
			lo = dataset_hex_digit_value(src[si + 2]);
			if (hi >= 0 && lo >= 0) {
				out[oi++] = (char)((hi << 4) | lo);
				si += 2;
				continue;
			}
		}
		out[oi++] = src[si];
	}
	out[oi] = '\0';
	return oi;
}

static int dataset_collect_query_values(
        const char *body, const char *name, char *buf, size_t buf_len)
{
	const char *p;
	size_t name_len;
	size_t used;

	if (!body || !name || !buf || buf_len == 0)
		return -1;

	name_len = strlen(name);
	used = 0;
	p = body;
	while (*p) {
		const char *amp;
		const char *eq;
		size_t key_len;
		char key[256];
		char value[1024];
		size_t value_len;

		amp = strchr(p, '&');
		if (!amp)
			amp = p + strlen(p);
		eq = memchr(p, '=', (size_t)(amp - p));
		key_len = eq ? (size_t)(eq - p) : (size_t)(amp - p);
		if (key_len >= sizeof(key))
			key_len = sizeof(key) - 1;
		dataset_url_decode_component(p, key_len, key, sizeof(key));
		if (strcmp(key, name) == 0) {
			value_len = 0;
			if (eq && eq + 1 <= amp) {
				value_len = dataset_url_decode_component(
				        eq + 1,
				        (size_t)(amp - (eq + 1)),
				        value,
				        sizeof(value));
			}
			if (value[0]) {
				if (used && used + 1 >= buf_len)
					return -1;
				if (used)
					buf[used++] = '\n';
				if (used + value_len >= buf_len)
					return -1;
				memcpy(buf + used, value, value_len);
				used += value_len;
				buf[used] = '\0';
			}
		}
		if (*amp == '\0')
			break;
		p = amp + 1;
	}

	return used > 0 ? (int)used : -1;
}

static unsigned dataset_parse_row_data_body(
        const dataset_def_t *def, const char *body)
{
	unsigned hd;
	size_t i;
	int ret_len;
	char val[1024];

	hd = qmap_open(NULL, "row_data", QM_STR, QM_STR, 0x1F, 0);
	if (hd == 0)
		return 0;

	for (i = 0; i < def->field_count; i++) {
		const dataset_field_t *f = &def->fields[i];
		if (!f->writable)
			continue;

		if (dataset_field_is_multi_reference(f->type))
			ret_len = dataset_collect_query_values(
			        body, f->name, val, sizeof(val));
		else
			ret_len = -1;
		if (ret_len <= 0)
			ret_len = mpfd_get(f->name, val, sizeof(val) - 1);
		if (ret_len <= 0)
			ret_len =
			        ndc_query_param(f->name, val, sizeof(val) - 1);
		if (ret_len <= 0)
			continue;
		if (ret_len >= (int)(sizeof(val) - 1)) {
			qmap_close(hd);
			return 0;
		}
		qmap_put(hd, f->name, val);
	}
	return hd;
}

static unsigned dataset_parse_row_data(const dataset_def_t *def)
{
	return dataset_parse_row_data_body(def, NULL);
}

NDX_LISTENER(int, dataset_update_item, const char *, dataset_id, const char *, id, unsigned, data_hd)
{
	const dataset_def_t *def = dataset_find(dataset_id);
	if (!def || !id || !id[0])
		return -1;

	char doc_root[256] = { 0 };
	get_doc_root(0, doc_root, sizeof(doc_root));
	const char *root = doc_root[0] ? doc_root : ".";

	char item_path[PATH_MAX];
	snprintf(
	        item_path,
	        sizeof(item_path),
	        "%s/%s/%s",
	        root,
	        def->items_path,
	        id);
	mkdir(item_path, 0755);

	json_object_t *obj = json_object_new(0);
	if (!obj)
		return -1;
	json_object_kv_str(obj, "id", id);

	for (size_t i = 0; i < def->field_count; i++) {
		const dataset_field_t *f = &def->fields[i];
		const char *val = qmap_get(data_hd, f->name);

		if (val) {
			if (f->file) {
				write_item_child_file(
				        item_path, f->file, val, strlen(val));
			}
			dataset_json_object_add_field(obj, f, val);
		} else {
			if (f->file) {
				char file_path[PATH_MAX + 256];
				snprintf(
				        file_path,
				        sizeof(file_path),
				        "%s/%s",
				        item_path,
				        f->file);
				char *content =
				        dataset_slurp_file(file_path, NULL);
				if (content) {
					dataset_json_object_add_field(
					        obj, f, content);
					free(content);
				} else if (dataset_field_is_multi_reference(
				                   f->type)) {
					dataset_json_object_add_field(
					        obj, f, NULL);
				} else {
					FILE *fp = fopen(file_path, "w");
					if (fp)
						fclose(fp);
				}
			}
		}
	}

	char *json = json_object_finish(obj);
	if (json) {
		qmap_put(def->source_hd, id, json);
	}
	return 0;
}

static int dataset_post_handler(int fd, char *body)
{
	const dataset_def_t *def;
	const char *username;
	int rc = dataset_write_init(fd, body, &def, &username);
	if (rc != 0)
		return (rc == 404)   ? respond_error(fd, 404, "Not found")
		       : (rc == 401) ? respond_error(fd, 401, "Unauthorized")
		       : (rc == 403 || rc == DATASET_INIT_ERR_BAD_CSRF)
		               ? respond_error(fd, 403, "Forbidden")
		       : (rc == 415)
		               ? respond_error(
		                         fd, 415, "Unsupported Media Type")
		               : respond_error(fd, 400, "Bad request");

	unsigned data_hd = dataset_parse_row_data_body(def, body);
	if (data_hd == 0)
		return server_error(fd, "OOM");

	const char *id = qmap_get(data_hd, def->key_field);
	if (!id || !id[0]) {
		qmap_close(data_hd);
		return bad_request(fd, "Missing key field");
	}

	rc = dataset_update_item(def->id, id, data_hd);
	qmap_close(data_hd);

	if (rc == 0) {
		json_object_t *jo = json_object_new(0);
		char *out;
		if (!jo)
			return server_error(fd, "OOM");
		json_object_kv_str(jo, def->key_field, id);
		out = json_object_finish(jo);
		respond_json(fd, 201, out);
		free(out);
		return 0;
	}
	return server_error(fd, "Create failed");
}

static int dataset_put_handler(int fd, char *body)
{
	const dataset_def_t *def;
	const char *username;
	char key[256] = { 0 };
	int rc = dataset_write_init(fd, body, &def, &username);
	if (rc != 0)
		return (rc == 404)   ? respond_error(fd, 404, "Not found")
		       : (rc == 401) ? respond_error(fd, 401, "Unauthorized")
		       : (rc == 403 || rc == DATASET_INIT_ERR_BAD_CSRF)
		               ? respond_error(fd, 403, "Forbidden")
		       : (rc == 415)
		               ? respond_error(
		                         fd, 415, "Unsupported Media Type")
		               : respond_error(fd, 400, "Bad request");

	ndc_env_get(fd, key, "PATTERN_PARAM_KEY");
	if (!key[0])
		return respond_error(fd, 400, "Missing key");

	unsigned data_hd = dataset_parse_row_data_body(def, body);
	if (data_hd == 0)
		return server_error(fd, "OOM");

	rc = dataset_update_item(def->id, key, data_hd);
	qmap_close(data_hd);

	if (rc == 0)
		return respond_json(fd, 200, "{\"status\":\"ok\"}");
	return server_error(fd, "Update failed");
}

static int dataset_delete_handler(int fd, char *body)
{
	const dataset_def_t *def;
	const char *username;
	char key[256] = { 0 };
	int rc = dataset_write_init(fd, body, &def, &username);
	if (rc != 0 && rc != DATASET_INIT_ERR_NO_BODY)
		return (rc == 404)   ? respond_error(fd, 404, "Not found")
		       : (rc == 401) ? respond_error(fd, 401, "Unauthorized")
		       : (rc == 403 || rc == DATASET_INIT_ERR_BAD_CSRF)
		               ? respond_error(fd, 403, "Forbidden")
		       : (rc == 415)
		               ? respond_error(
		                         fd, 415, "Unsupported Media Type")
		               : respond_error(fd, 400, "Bad request");

	ndc_env_get(fd, key, "PATTERN_PARAM_KEY");
	if (!key[0])
		return respond_error(fd, 400, "Missing key");

	char doc_root[256] = { 0 };
	get_doc_root(0, doc_root, sizeof(doc_root));
	const char *root = doc_root[0] ? doc_root : ".";
	char item_path[PATH_MAX];
	snprintf(
	        item_path,
	        sizeof(item_path),
	        "%s/%s/%s",
	        root,
	        def->items_path,
	        key);

	item_remove_path_recursive(item_path);
	qmap_del(def->source_hd, key);

	return respond_json(fd, 200, "{\"status\":\"ok\"}");
}

void dataset_install_routes(void)
{
	ndc_register_handler(
	        "GET:/api/dataset/:dataset_id", dataset_get_handler);
	ndc_register_handler(
	        "POST:/api/dataset/:dataset_id", dataset_post_handler);
	ndc_register_handler(
	        "PUT:/api/dataset/:dataset_id/:key", dataset_put_handler);
	ndc_register_handler(
	        "DELETE:/api/dataset/:dataset_id/:key", dataset_delete_handler);
}
