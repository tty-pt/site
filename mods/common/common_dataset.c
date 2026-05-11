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

typedef struct {
	char *fields_json;
	char *rows_json;
} dataset_json_parts_t;

static const char *dataset_field_type_name(dataset_field_type_t type)
{
	switch (type) {
	case DATASET_FIELD_STRING:
		return "string";
	case DATASET_FIELD_INT:
		return "int";
	case DATASET_FIELD_BOOL:
		return "bool";
	case DATASET_FIELD_NULLABLE_STRING:
		return "nullable_string";
	}
	return "string";
}

static dataset_def_t *dataset_find(const char *dataset_id)
{
	size_t i;

	if (!dataset_id || !dataset_id[0])
		return NULL;

	for (i = 0; i < dataset_count; i++)
		if (strcmp(dataset_defs[i].id, dataset_id) == 0)
			return &dataset_defs[i];

	return NULL;
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

static int dataset_fields_json_build(const dataset_def_t *def, char **out_json)
{
	json_array_t *fields;
	char *json;
	size_t i;

	fields = json_array_new(0);
	if (!fields)
		return -1;

	for (i = 0; i < def->field_count; i++) {
		const dataset_field_t *field = &def->fields[i];
		if (json_array_begin_object(fields) != 0 ||
		    json_array_kv_str(fields, "name", field->name) != 0 ||
		    json_array_kv_str(
		            fields,
		            "type",
		            dataset_field_type_name(field->type)) != 0 ||
		    json_array_kv_bool(fields, "writable", field->writable) !=
		            0 ||
		    json_array_end_object(fields) != 0)
		{
			json_array_free(fields);
			return -1;
		}
	}

	json = json_array_finish(fields);
	if (!json)
		return -1;

	*out_json = json;
	return 0;
}

static int dataset_key_field_valid(const dataset_def_t *def)
{
	size_t i;

	for (i = 0; i < def->field_count; i++)
		if (strcmp(def->fields[i].name, def->key_field) == 0)
			return 1;

	return 0;
}

/*
 * Builds the rows array for a dataset.
 * Note: Row order is currently determined by qmap iteration order,
 * which generally follows insertion order for small maps or hash order.
/*
 * Build rows JSON from stored data.
 * If include is specified, also read file-based fields from disk.
 */
static int dataset_rows_json_build(
        const dataset_def_t *def, const char *include, char **out_json)
{
	json_array_t *rows;
	unsigned cur;
	const void *key;
	const void *value;
	char *rows_json;

	rows = json_array_new(0);
	if (!rows)
		return -1;

	char doc_root[256] = { 0 };
	get_doc_root(0, doc_root, sizeof(doc_root));
	const char *root = doc_root[0] ? doc_root : ".";

	cur = qmap_iter(def->source_hd, NULL, 0);
	while (qmap_next(&key, &value, cur)) {
		const char *id = (const char *)key;
		const char *json_val = (const char *)value;

		if (!json_val || !json_val[0])
			continue;

		char *row_json = NULL;
		if (include && include[0]) {
			char item_path[PATH_MAX];
			snprintf(
			        item_path,
			        sizeof(item_path),
			        "%s/%s/%s",
			        root,
			        def->items_path,
			        id);

			char base_json[8192] = { 0 };
			strncpy(base_json, json_val, sizeof(base_json) - 1);

			char include_copy[256];
			strncpy(include_copy,
			        include,
			        sizeof(include_copy) - 1);
			char *saveptr;
			char *field = strtok_r(include_copy, ",", &saveptr);
			while (field) {
				while (*field == ' ')
					field++;
				const dataset_field_t *f = NULL;
				for (unsigned i = 0; i < def->field_count; i++)
				{
					if (strcmp(def->fields[i].name,
					           field) == 0)
					{
						f = &def->fields[i];
						break;
					}
				}
				if (f && f->file && f->file[0]) {
					char file_path[PATH_MAX];
					snprintf(
					        file_path,
					        sizeof(file_path),
					        "%s/%s",
					        item_path,
					        f->file);
					FILE *fp = fopen(file_path, "r");
					if (fp) {
						fseek(fp, 0, SEEK_END);
						long len = ftell(fp);
						fseek(fp, 0, SEEK_SET);
						if (len > 0 && len < 4096) {
							char file_data[4096] = {
								0
							};
							size_t r =
							        fread(file_data,
							              1,
							              len,
							              fp);
							file_data[r] = '\0';

							char escaped[8192] = {
								0
							};
							char *ep = escaped;
							for (size_t i = 0;
							     file_data[i] &&
							     ep < escaped + sizeof(escaped) -
							                     4;
							     i++)
							{
								if (file_data[i] ==
								            '"' ||
								    file_data[i] ==
								            '\\')
									*ep++ = '\\';
								*ep++ = file_data
								        [i];
							}

							char new_field[128];
							snprintf(
							        new_field,
							        sizeof(new_field),
							        ",\"%s\":\"%"
							        "s\"",
							        field,
							        escaped);
							size_t base_len = strlen(
							        base_json);
							size_t field_len =
							        strlen(new_field);
							if (base_len +
							            field_len <
							    sizeof(base_json) -
							            1)
							{
								char *brace = strchr(
								        base_json,
								        '}');
								if (brace) {
									memmove(brace + field_len,
									        brace + 1,
									        strlen(brace));
									memcpy(brace,
									       new_field,
									       field_len);
								}
							}
						}
						fclose(fp);
					}
				}
				field = strtok_r(NULL, ",", &saveptr);
			}
			row_json = base_json;
		} else {
			row_json = (char *)json_val;
		}

		if (row_json && row_json[0]) {
			if (json_array_append_raw(rows, row_json) != 0) {
				json_array_free(rows);
				return -1;
			}
		}
	}

	rows_json = json_array_finish(rows);
	if (!rows_json)
		return -1;

	*out_json = rows_json;
	return 0;
}

static int dataset_json_parts_build(
        const dataset_def_t *def,
        const char *include,
        dataset_json_parts_t *parts)
{
	parts->fields_json = NULL;
	parts->rows_json = NULL;
	if (dataset_fields_json_build(def, &parts->fields_json) != 0)
		return -1;
	if (dataset_rows_json_build(def, include, &parts->rows_json) != 0) {
		free(parts->fields_json);
		parts->fields_json = NULL;
		return -1;
	}
	return 0;
}

static void dataset_json_parts_free(dataset_json_parts_t *parts)
{
	if (!parts)
		return;
	free(parts->fields_json);
	free(parts->rows_json);
	parts->fields_json = NULL;
	parts->rows_json = NULL;
}

static int dataset_json_build(
        int fd, const dataset_def_t *def, const char *include, char **out_json)
{
	(void)fd;
	json_object_t *root;
	dataset_json_parts_t parts;
	char *json;

	parts.fields_json = NULL;
	parts.rows_json = NULL;
	root = NULL;

	if (dataset_json_parts_build(def, include, &parts) != 0)
		goto oom;

	root = json_object_new(0);
	if (!root)
		goto oom;

	if (json_object_kv_str(root, "dataset", def->id) != 0 ||
	    json_object_kv_int(root, "version", 1) != 0 ||
	    json_object_kv_str(root, "keyField", def->key_field) != 0 ||
	    json_object_kv_raw(root, "fields", parts.fields_json) != 0 ||
	    json_object_kv_raw(root, "rows", parts.rows_json) != 0)
		goto oom;

	json = json_object_finish(root);
	dataset_json_parts_free(&parts);
	if (!json)
		return -1;

	*out_json = json;
	return 0;

oom:
	dataset_json_parts_free(&parts);
	json_object_free(root);
	return -1;
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

	json_object_t *obj = json_object_new(0);
	if (!obj)
		return -1;

	json_object_kv_str(obj, "id", id);

	for (size_t i = 0; i < def->field_count; i++) {
		if (!def->fields[i].file)
			continue;

		char file_path[PATH_MAX];
		snprintf(
		        file_path,
		        sizeof(file_path),
		        "%s/%s",
		        item_path,
		        def->fields[i].file);

		char *content = dataset_slurp_file(file_path, NULL);
		if (content) {
			json_object_kv_str(obj, def->fields[i].name, content);
			free(content);
		}
	}

	char *json = json_object_finish(obj);
	if (json) {
		qmap_put(def->source_hd, id, json);
		free(json);
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

		if (entry->d_type != DT_DIR)
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
	    !def->items_path || def->source_hd == 0 ||
	    !dataset_key_field_valid(def))
		return -1;

	if (dataset_count >= MAX_DATASETS || dataset_find(def->id))
		return -1;

	dataset_scan_items((dataset_def_t *)def);

	dataset_defs[dataset_count++] = *def;
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
	int rc;

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

	rc = dataset_json_build(fd, def, include, out_json);
	if (rc == -2)
		return 400;
	if (rc == -3)
		return 403;
	if (rc != 0 || !out_json || !*out_json)
		return 500;

	return 0;
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

	ndc_query_parse(body);
	char csrf[64] = { 0 };
	ndc_query_param("csrf_token", csrf, sizeof(csrf) - 1);
	if (csrf_validate(fd, csrf) != 0) {
		return DATASET_INIT_ERR_BAD_CSRF;
	}

	return 0;
}

static unsigned dataset_parse_row_data(const dataset_def_t *def)
{
	unsigned hd = qmap_open(NULL, "row_data", QM_STR, QM_STR, 0x1F, 0);
	size_t i;
	int ret_len;
	char val[1024];

	if (hd == 0)
		return 0;

	for (i = 0; i < def->field_count; i++) {
		const dataset_field_t *f = &def->fields[i];
		if (!f->writable)
			continue;

		ret_len = mpfd_get(f->name, val, sizeof(val) - 1);
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

		/* If field is in request, update file and object */
		if (val) {
			if (f->file) {
				write_item_child_file(
				        item_path, f->file, val, strlen(val));
			}
			json_object_kv_str(obj, f->name, val);
		} else {
			/* Slurp existing if available to maintain full row JSON
			 */
			if (f->file) {
				char file_path[PATH_MAX];
				snprintf(
				        file_path,
				        sizeof(file_path),
				        "%s/%s",
				        item_path,
				        f->file);
				char *content =
				        dataset_slurp_file(file_path, NULL);
				if (content) {
					json_object_kv_str(
					        obj, f->name, content);
					free(content);
				} else {
					/* Create empty file for new items with
					 * file-based fields */
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
		free(json);
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

	unsigned data_hd = dataset_parse_row_data(def);
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

	unsigned data_hd = dataset_parse_row_data(def);
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
