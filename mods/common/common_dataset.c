#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/auth.h>
#include <ttypt/ndc.h>
#include <ttypt/ndx-mod.h>
#include <ttypt/qmap.h>

#include "common_internal.h"

#define MAX_DATASETS 64

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

static dataset_access_result_t dataset_access_allowed(
        dataset_def_t *def, int fd, const char *username)
{
	switch (def->access_policy) {
	case DATASET_ACCESS_PUBLIC:
		return DATASET_ACCESS_RESULT_ALLOW;
	case DATASET_ACCESS_LOGIN:
		return (username && username[0])
		               ? DATASET_ACCESS_RESULT_ALLOW
		               : DATASET_ACCESS_RESULT_UNAUTHORIZED;
	case DATASET_ACCESS_CALLBACK:
		if (!def->access_cb)
			return DATASET_ACCESS_RESULT_FORBIDDEN;
		return def->access_cb(fd, username, def->user);
	}
	return DATASET_ACCESS_RESULT_FORBIDDEN;
}

static int dataset_fields_json_build(
        const dataset_def_t *def, char **out_json)
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
		    json_array_kv_bool(fields, "writable", field->writable) != 0 ||
		    json_array_end_object(fields) != 0)
			return -1;
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
 * This is considered stable-enough-for-now.
 */
static int dataset_rows_json_build(
        const dataset_def_t *def, char **out_json)
{
	json_array_t *rows;
	unsigned cur;
	const void *key;
	const void *value;
	char *row_json;
	char *rows_json;
	json_object_t *row;

	rows = json_array_new(0);
	if (!rows)
		return -1;

	cur = qmap_iter(def->source_hd, NULL, 0);
	while (qmap_next(&key, &value, cur)) {
		row = json_object_new(0);
		if (!row)
			return -1;
		if (!def->row_json_cb ||
		    def->row_json_cb(
		            row,
		            (const char *)key,
		            value,
		            def->user) != 0) {
			json_object_free(row);
			return -1;
		}

		row_json = json_object_finish(row);
		if (!row_json)
			return -1;
		if (json_array_append_raw(rows, row_json) != 0) {
			free(row_json);
			return -1;
		}
		free(row_json);
	}

	rows_json = json_array_finish(rows);
	if (!rows_json)
		return -1;

	*out_json = rows_json;
	return 0;
}

static int dataset_json_parts_build(
        const dataset_def_t *def, dataset_json_parts_t *parts)
{
	parts->fields_json = NULL;
	parts->rows_json = NULL;
	if (dataset_fields_json_build(def, &parts->fields_json) != 0)
		return -1;
	if (dataset_rows_json_build(def, &parts->rows_json) != 0)
		return -1;
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

static const dataset_relation_t *dataset_relation_find(
        const dataset_def_t *def, const char *name)
{
	size_t i;

	if (!def || !name || !name[0])
		return NULL;
	for (i = 0; i < def->relation_count; i++)
		if (strcmp(def->relations[i].name, name) == 0)
			return &def->relations[i];
	return NULL;
}

static int dataset_includes_json_build(
        int fd, const dataset_def_t *def, const char *include,
        char **out_json);

static int dataset_json_build(
        int fd, const dataset_def_t *def, const char *include, char **out_json)
{
	json_object_t *root;
	dataset_json_parts_t parts;
	char *includes_json;
	char *json;

	parts.fields_json = NULL;
	parts.rows_json = NULL;
	includes_json = NULL;
	root = NULL;

	if (dataset_json_parts_build(def, &parts) != 0)
		goto oom;
	if (include && include[0] &&
	    dataset_includes_json_build(fd, def, include, &includes_json) != 0)
		goto oom;

	root = json_object_new(0);
	if (!root)
		goto oom;

	if (json_object_kv_str(root, "dataset", def->id) != 0 ||
	    json_object_kv_int(root, "version", 1) != 0 ||
	    json_object_kv_str(root, "keyField", def->key_field) != 0 ||
	    json_object_kv_raw(root, "fields", parts.fields_json) != 0 ||
	    json_object_kv_raw(root, "rows", parts.rows_json) != 0 ||
	    (includes_json &&
	     json_object_kv_raw(root, "includes", includes_json) != 0))
		goto oom;

	json = json_object_finish(root);
	dataset_json_parts_free(&parts);
	free(includes_json);
	if (!json)
		return -1;

	*out_json = json;
	return 0;

oom:
	dataset_json_parts_free(&parts);
	free(includes_json);
	json_object_free(root);
	return -1;
}

static int dataset_includes_json_build(
        int fd, const dataset_def_t *def, const char *include,
        char **out_json)
{
	char include_buf[512];
	char *token;
	json_object_t *includes;

	includes = json_object_new(0);
	if (!includes)
		return -1;
	snprintf(include_buf, sizeof(include_buf), "%s", include);
	token = strtok(include_buf, ",");
	while (token) {
		const dataset_relation_t *rel;
		dataset_def_t *target_def;
		char *target_json;
		char *next_token;

		while (*token == ' ')
			token++;
		rel = dataset_relation_find(def, token);
		if (!rel) {
			json_object_free(includes);
			return -2;
		}

		target_def = dataset_find(rel->target_dataset_id);
		if (!target_def ||
		    dataset_json_build(fd, target_def, NULL, &target_json) != 0) {
			json_object_free(includes);
			return -1;
		}

		if (json_object_kv_raw(includes, rel->name, target_json) != 0) {
			free(target_json);
			json_object_free(includes);
			return -1;
		}
		free(target_json);

		next_token = strtok(NULL, ",");
		token = next_token;
	}

	*out_json = json_object_finish(includes);
	return *out_json ? 0 : -1;
}

NDX_LISTENER(int, dataset_register, const dataset_def_t *, def)
{
	if (!def || !def->id || !def->id[0] || !def->key_field ||
	    !def->key_field[0] || !def->fields || def->field_count == 0 ||
	    !def->row_json_cb || def->source_hd == 0 ||
	    !dataset_key_field_valid(def))
		return -1;

	if (dataset_count >= MAX_DATASETS || dataset_find(def->id))
		return -1;

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

	if (dataset_json_build(fd, def, include, out_json) == -2)
		return 400;
	if (!out_json || !*out_json)
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

	rc = dataset_get_json(fd, dataset_id, include[0] ? include : NULL, &json);
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

static int dataset_write_init(int fd, char *body, const dataset_def_t **out_def, const char **out_username)
{
	char dataset_id[256] = { 0 };
	ndc_env_get(fd, dataset_id, "PATTERN_PARAM_DATASET_ID");

	*out_def = dataset_find(dataset_id);
	if (!*out_def) {
		return 404;
	}

	*out_username = get_request_user(fd);
	if (dataset_access_allowed(*out_def, fd, *out_username) !=
	    DATASET_ACCESS_RESULT_ALLOW) {
		return (*out_username && **out_username) ? 403 : 401;
	}

	/* Check content type for write operations */
	char ct[256] = { 0 };
	ndc_env_get(fd, ct, "HTTP_CONTENT_TYPE");
	if (ct[0] && !strstr(ct, "application/x-www-form-urlencoded")) {
		return 415;
	}

	if (!body || !body[0]) {
		return 400;
	}

	ndc_query_parse(body);
	char csrf[64] = { 0 };
	ndc_query_param("csrf_token", csrf, sizeof(csrf) - 1);
	if (csrf_validate(fd, csrf) != 0) {
		return 403;
	}

	return 0;
}

static unsigned dataset_parse_row_data(const dataset_def_t *def)
{
	unsigned hd = qmap_open(NULL, "row_data", QM_STR, QM_STR, 0x1F, 0);
	size_t i;
	char val[1024];

	if (hd == 0)
		return 0;

	for (i = 0; i < def->field_count; i++) {
		const dataset_field_t *f = &def->fields[i];
		if (!f->writable)
			continue;

		if (ndc_query_param(f->name, val, sizeof(val) - 1) >= 0) {
			qmap_put(hd, f->name, val);
		}
	}
	return hd;
}

static int dataset_post_handler(int fd, char *body)
{
	const dataset_def_t *def;
	const char *username;
	int rc = dataset_write_init(fd, body, &def, &username);
	if (rc != 0)
		return (rc == 404) ? respond_error(fd, 404, "Not found") :
		       (rc == 401) ? respond_error(fd, 401, "Unauthorized") :
		       (rc == 403) ? respond_error(fd, 403, "Forbidden") :
		       (rc == 415) ? respond_error(fd, 415, "Unsupported Media Type") :
		                     respond_error(fd, 400, "Bad request");

	if (!def->create_cb)
		return respond_error(fd, 405, "Method not allowed");

	unsigned data_hd = dataset_parse_row_data(def);
	if (data_hd == 0)
		return server_error(fd, "OOM");

	char new_key[256] = { 0 };
	rc = def->create_cb(
	        fd, username, data_hd, def->user, new_key, sizeof(new_key));
	qmap_close(data_hd);

	if (rc == 0) {
		char out[512];
		snprintf(out, sizeof(out), "{\"%s\":\"%s\"}", def->key_field, new_key);
		return respond_json(fd, 201, out);
	}
	return (rc > 0) ? rc : server_error(fd, "Create failed");
}

static int dataset_put_handler(int fd, char *body)
{
	const dataset_def_t *def;
	const char *username;
	char key[256] = { 0 };
	int rc = dataset_write_init(fd, body, &def, &username);
	if (rc != 0)
		return (rc == 404) ? respond_error(fd, 404, "Not found") :
		       (rc == 401) ? respond_error(fd, 401, "Unauthorized") :
		       (rc == 403) ? respond_error(fd, 403, "Forbidden") :
		       (rc == 415) ? respond_error(fd, 415, "Unsupported Media Type") :
		                     respond_error(fd, 400, "Bad request");

	if (!def->update_cb)
		return respond_error(fd, 405, "Method not allowed");

	ndc_env_get(fd, key, "PATTERN_PARAM_KEY");
	if (!key[0])
		return respond_error(fd, 400, "Missing key");

	unsigned data_hd = dataset_parse_row_data(def);
	if (data_hd == 0)
		return server_error(fd, "OOM");

	rc = def->update_cb(fd, username, key, data_hd, def->user);
	qmap_close(data_hd);

	if (rc == 0)
		return respond_json(fd, 200, "{\"status\":\"ok\"}");
	return (rc > 0) ? rc : server_error(fd, "Update failed");
}

static int dataset_delete_handler(int fd, char *body)
{
	const dataset_def_t *def;
	const char *username;
	char key[256] = { 0 };
	int rc = dataset_write_init(fd, body, &def, &username);
	if (rc != 0 && rc != 400) // body might be empty for DELETE
		return (rc == 404) ? respond_error(fd, 404, "Not found") :
		       (rc == 401) ? respond_error(fd, 401, "Unauthorized") :
		       (rc == 403) ? respond_error(fd, 403, "Forbidden") :
		       (rc == 415) ? respond_error(fd, 415, "Unsupported Media Type") :
		                     respond_error(fd, 400, "Bad request");

	if (rc == 400) {
		return respond_error(fd, 400, "Missing CSRF token in body");
	}

	if (!def->delete_cb)
		return respond_error(fd, 405, "Method not allowed");

	ndc_env_get(fd, key, "PATTERN_PARAM_KEY");
	if (!key[0])
		return respond_error(fd, 400, "Missing key");

	rc = def->delete_cb(fd, username, key, def->user);
	if (rc == 0)
		return respond_json(fd, 200, "{\"status\":\"ok\"}");
	return (rc > 0) ? rc : server_error(fd, "Delete failed");
}

void dataset_install_routes(void)
{
	ndc_register_handler("GET:/api/dataset/:dataset_id", dataset_get_handler);
	ndc_register_handler("POST:/api/dataset/:dataset_id", dataset_post_handler);
	ndc_register_handler(
	        "PUT:/api/dataset/:dataset_id/:key", dataset_put_handler);
	ndc_register_handler(
	        "DELETE:/api/dataset/:dataset_id/:key", dataset_delete_handler);
}

