#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "../auth/auth.h"
#include <ttypt/ndc.h>
#include <ttypt/qmap.h>
#include "../mpfd/mpfd.h"
#include "../common/common.h"
#include "source.h"

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

static int respond_json_error(int fd, int status, const char *msg)
{
	char buf[512];
	snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg);
	return respond_json(fd, status, buf);
}

static const char *field_type_name(source_field_type_t type)
{
	switch (type) {
	case SOURCE_FIELD_STRING:
		return "string";
	case DATASET_FIELD_INT:
		return "int";
	case DATASET_FIELD_BOOL:
		return "bool";
	case DATASET_FIELD_NULLABLE_STRING:
		return "nullable_string";
	case SOURCE_FIELD_REFERENCE:
		return "reference";
	case SOURCE_FIELD_MULTI_REFERENCE:
		return "multi_reference";
	case SOURCE_FIELD_INVERSE:
		return "inverse";
	default:
		return "string";
	}
}

static int source_collect_query_values(
	const char *body, const char *name, char *buf, size_t buf_len)
{
	char *copy = strdup(body ? body : "");
	if (!copy)
		return -1;

	char *tok;
	char *saveptr;
	int found = 0;
	buf[0] = '\0';

	tok = strtok_r(copy, "&", &saveptr);
	while (tok) {
		char *eq = strchr(tok, '=');
		if (eq) {
			*eq = '\0';
			size_t klen = strlen(tok);
			char kdec[256];
			ndc_url_decode(tok, klen, kdec, sizeof(kdec));
			if (strcmp(kdec, name) == 0) {
				size_t vlen = strlen(eq + 1);
				char vdec[1024];
				ndc_url_decode(eq + 1, vlen, vdec, sizeof(vdec));
				size_t cur = strlen(buf);
				if (found) {
					if (cur + 1 < buf_len) {
						buf[cur] = '\n';
						cur++;
					}
				}
				size_t vdec_len = strlen(vdec);
				if (cur + vdec_len < buf_len) {
					memcpy(buf + cur, vdec, vdec_len);
					buf[cur + vdec_len] = '\0';
				}
				found = 1;
			}
		}
		tok = strtok_r(NULL, "&", &saveptr);
	}
	free(copy);
	return found ? (int)strlen(buf) : -1;
}

static unsigned source_parse_row_data_body(
	const source_def_t *def, const char *body)
{
	unsigned hd = qmap_open(NULL, "row_data", QM_STR, QM_STR, 0x1F, 0);
	if (hd == 0)
		return 0;

	for (size_t i = 0; i < def->field_count; i++) {
		const source_field_t *f = &def->fields[i];
		if (!f->writable)
			continue;

		char val[4096] = {0};
		int ret_len;

		if (f->type == SOURCE_FIELD_MULTI_REFERENCE)
			ret_len = source_collect_query_values(body, f->name,
				val, sizeof(val));
		else
			ret_len = ndc_query_param(f->name, val, sizeof(val) - 1);

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

static source_access_result_t source_access_allowed(
	const source_def_t *def, int fd, const char *username)
{
	(void)fd;
	if (def->access_policy == SOURCE_ACCESS_PUBLIC ||
	    def->access_policy == DATASET_ACCESS_LOGIN)
		return DATASET_ACCESS_RESULT_ALLOW;
	return DATASET_ACCESS_RESULT_FORBIDDEN;
}

enum {
	SOURCE_INIT_OK = 0,
	SOURCE_INIT_NOT_FOUND,
	SOURCE_INIT_UNAUTHORIZED,
	SOURCE_INIT_FORBIDDEN,
};

static int source_write_init(int fd, char *body,
	const source_def_t **out_def, const char **out_username)
{
	char dataset_id[128] = {0};
	ndc_env_get(fd, dataset_id, "PATTERN_PARAM_DATASET_ID");

	const source_def_t *def = source_find(dataset_id);
	if (!def)
		return SOURCE_INIT_NOT_FOUND;

	const char *username = get_request_user(fd);
	if (!username || !username[0])
		return SOURCE_INIT_UNAUTHORIZED;

	if (source_access_allowed(def, fd, username)
	    != DATASET_ACCESS_RESULT_ALLOW)
		return SOURCE_INIT_FORBIDDEN;

	if (body && body[0]) {
		ndc_query_parse(body);
		char csrf[33] = {0};
		ndc_query_param("csrf_token", csrf, sizeof(csrf));
		if (csrf_validate(fd, csrf) != 0)
			return SOURCE_INIT_FORBIDDEN;
	}

	if (out_def)
		*out_def = def;
	if (out_username)
		*out_username = username;
	return SOURCE_INIT_OK;
}

static char *source_build_fields_json(const source_def_t *def)
{
	json_array_t *ja = json_array_new(0);
	if (!ja)
		return NULL;

	for (size_t i = 0; i < def->field_count; i++) {
		const source_field_t *f = &def->fields[i];
		json_object_t *jo = json_object_new(0);
		if (!jo)
			continue;

		json_object_kv_str(jo, "name", f->name);
		json_object_kv_str(jo, "type", field_type_name(f->type));
		json_object_kv_bool(jo, "writable", f->writable ? 1 : 0);

		if (f->file)
			json_object_kv_str(jo, "file", f->file);
		if (f->target_source)
			json_object_kv_str(jo, "target_source",
				f->target_source);
		if (f->inverse_name)
			json_object_kv_str(jo, "inverse_name",
				f->inverse_name);

		if (f->required)
			json_object_kv_bool(jo, "required", 1);
		if (f->min) {
			char mbuf[32];
			snprintf(mbuf, sizeof(mbuf), "%lld",
				(long long)f->min);
			json_object_kv_str(jo, "min", mbuf);
		}
		if (f->max) {
			char mbuf[32];
			snprintf(mbuf, sizeof(mbuf), "%lld",
				(long long)f->max);
			json_object_kv_str(jo, "max", mbuf);
		}
		if (f->min_length) {
			char mbuf[32];
			snprintf(mbuf, sizeof(mbuf), "%llu",
				(unsigned long long)f->min_length);
			json_object_kv_str(jo, "minLength", mbuf);
		}
		if (f->max_length) {
			char mbuf[32];
			snprintf(mbuf, sizeof(mbuf), "%llu",
				(unsigned long long)f->max_length);
			json_object_kv_str(jo, "maxLength", mbuf);
		}
		if (f->pattern)
			json_object_kv_str(jo, "pattern",
				f->pattern);

		char *field_json = json_object_finish(jo);
		if (field_json) {
			json_array_append_raw(ja, field_json);
			free(field_json);
		}
	}
	return json_array_finish(ja);
}

/* -----------------------------------------------------------------------
 * Core JSON builders
 * ----------------------------------------------------------------------- */

static char *source_build_string_array(const char *input)
{
	json_array_t *ja = json_array_new(0);
	if (!ja)
		return NULL;

	if (input && input[0]) {
		char *copy = strdup(input);
		if (!copy) {
			char *r = json_array_finish(ja);
			free(r);
			return NULL;
		}
		char *tok;
		char *saveptr;
		tok = strtok_r(copy, "\n", &saveptr);
		while (tok) {
			if (tok[0]) {
				char esc[4096];
				ndc_json_escape(tok, esc, sizeof(esc));
				char buf[4100];
				int n = snprintf(buf, sizeof(buf),
					"\"%s\"", esc);
				if (n > 0 && (size_t)n < sizeof(buf))
					json_array_append_raw(ja, buf);
			}
			tok = strtok_r(NULL, "\n", &saveptr);
		}
		free(copy);
	}
	return json_array_finish(ja);
}

static char *source_build_inverse_array(
	const source_def_t *def,
	const source_field_t *field,
	const char *item_id)
{
	if (!field->target_source || !field->inverse_name)
		return strdup("[]");

	const source_def_t *target = source_find(field->target_source);
	if (!target || !target->fields_hd)
		return strdup("[]");

	uint32_t pos = qmap_pos(def->fields_hd, item_id);
	if (pos == UINT32_MAX)
		return strdup("[]");

	uint32_t inv_buf[256];
	size_t count = qmap_inv_get(target->fields_hd,
		field->inverse_name, pos, inv_buf, 256);

	json_array_t *ja = json_array_new(0);
	if (!ja)
		return strdup("[]");

	for (size_t i = 0; i < count; i++) {
		const char *key = qmap_get_key(target->fields_hd, inv_buf[i]);
		if (key) {
			char esc[256];
			ndc_json_escape(key, esc, sizeof(esc));
			char buf[260];
			snprintf(buf, sizeof(buf), "\"%s\"", esc);
			json_array_append_raw(ja, buf);
		}
	}
	return json_array_finish(ja);
}

static int source_build_rows_json(
	const source_def_t *def,
	const char *qs,
	const char *include,
	int *out_total_rows,
	char **out_rows_json)
{
	unsigned result_hd;
	json_array_t *ja;
	char inc_set[512];
	int has_include;
	uint32_t cur;
	const void *key_ptr;
	const void *val_ptr;

	result_hd = source_query(def->id, qs ? qs : "");
	if (!result_hd) {
		*out_total_rows = 0;
		*out_rows_json = strdup("[]");
		return *out_rows_json ? 0 : -1;
	}

	{
		const char *ts = qmap_get(result_hd, "__total__");
		*out_total_rows = ts ? atoi(ts) : 0;
	}

	ja = json_array_new(0);
	if (!ja) {
		qmap_close(result_hd);
		return -1;
	}

	memset(inc_set, 0, sizeof(inc_set));
	has_include = (include && include[0]);
	if (has_include)
		snprintf(inc_set, sizeof(inc_set), ",id,%s,", include);

	cur = qmap_iter(result_hd, NULL, 0);

	while (qmap_next(&key_ptr, &val_ptr, cur)) {
		const char *item_id = (const char *)key_ptr;
		json_object_t *jo;
		size_t i;

		if (strcmp(item_id, "__total__") == 0)
			continue;

		jo = json_object_new(0);
		if (!jo)
			continue;

		json_object_kv_str(jo, "id", item_id);

		for (i = 0; i < def->field_count; i++) {
			const source_field_t *f;
			const char *val;

			f = &def->fields[i];
			if (strcmp(f->name, "id") == 0)
				continue;
			if (has_include &&
			    !strstr(inc_set, f->name))
				continue;

			val = qmap_field_get(
				def->fields_hd, item_id, f->name);

			switch (f->type) {
			case SOURCE_FIELD_STRING:
				if (val)
					json_object_kv_str(jo,
						f->name, val);
				break;
			case DATASET_FIELD_NULLABLE_STRING:
				if (val && val[0])
					json_object_kv_str(jo,
						f->name, val);
				break;
			case DATASET_FIELD_INT:
				if (val)
					json_object_kv_int(jo,
						f->name, atoi(val));
				break;
			case DATASET_FIELD_BOOL:
				if (val)
					json_object_kv_bool(jo, f->name,
						strcmp(val, "1") == 0 ||
						strcmp(val, "true") == 0);
				break;
			case SOURCE_FIELD_REFERENCE:
				if (val)
					json_object_kv_str(jo,
						f->name, val);
				break;
			case SOURCE_FIELD_MULTI_REFERENCE: {
				char *arr;
				arr = source_build_string_array(val);
				json_object_kv_raw(jo, f->name,
					arr ? arr : "[]");
				free(arr);
				break;
			}
			case SOURCE_FIELD_INVERSE: {
				char *arr;
				arr = source_build_inverse_array(
					def, f, item_id);
				json_object_kv_raw(jo, f->name,
					arr ? arr : "[]");
				free(arr);
				break;
			}
			}
		}

		{
			char *row_json = json_object_finish(jo);
			if (row_json) {
				json_array_append_raw(ja, row_json);
				free(row_json);
			}
		}
	}

	qmap_fin(cur);
	*out_rows_json = json_array_finish(ja);
	qmap_close(result_hd);
	return 0;
}

static int source_build_item_json(
	const source_def_t *def,
	const char *item_id,
	char **out_json)
{
	json_object_t *jo = json_object_new(0);
	if (!jo)
		return -1;

	json_object_kv_str(jo, "id", item_id);

	for (size_t i = 0; i < def->field_count; i++) {
		const source_field_t *f = &def->fields[i];
		if (strcmp(f->name, "id") == 0)
			continue;

		const char *val =
			qmap_field_get(def->fields_hd, item_id, f->name);

		switch (f->type) {
		case SOURCE_FIELD_STRING:
			if (val)
				json_object_kv_str(jo, f->name, val);
			break;
		case DATASET_FIELD_NULLABLE_STRING:
			if (val && val[0])
				json_object_kv_str(jo, f->name, val);
			break;
		case DATASET_FIELD_INT:
			if (val)
				json_object_kv_int(jo, f->name, atoi(val));
			break;
		case DATASET_FIELD_BOOL:
			if (val)
				json_object_kv_bool(jo, f->name,
					strcmp(val, "1") == 0 ||
					strcmp(val, "true") == 0);
			break;
		case SOURCE_FIELD_REFERENCE:
			if (val)
				json_object_kv_str(jo, f->name, val);
			break;
		case SOURCE_FIELD_MULTI_REFERENCE: {
			char *arr = source_build_string_array(val);
			json_object_kv_raw(jo, f->name,
				arr ? arr : "[]");
			free(arr);
			break;
		}
		case SOURCE_FIELD_INVERSE: {
			char *arr = source_build_inverse_array(
				def, f, item_id);
			json_object_kv_raw(jo, f->name,
				arr ? arr : "[]");
			free(arr);
			break;
		}
		}
	}

	*out_json = json_object_finish(jo);
	return *out_json ? 0 : -1;
}

static int source_build_json(
	const source_def_t *def,
	const char *qs,
	const char *include,
	int page, int per_page,
	char **out_json)
{
	char *fields_str;
	int total;
	char *rows_str;
	int rc;

	fields_str = source_build_fields_json(def);
	total = 0;
	rows_str = NULL;

	rc = source_build_rows_json(def, qs, include,
		&total, &rows_str);
	if (rc != 0 || !rows_str) {
		free(fields_str);
		return -1;
	}

	{
		int total_pages;
		json_object_t *pjo;
		char *pagination_str;
		json_object_t *jo;

		total_pages = per_page > 0
			? (total + per_page - 1) / per_page : 1;

		pjo = json_object_new(0);
		json_object_kv_int(pjo, "page", page);
		json_object_kv_int(pjo, "per_page", per_page);
		json_object_kv_int(pjo, "total", total);
		json_object_kv_int(pjo, "total_pages", total_pages);
		pagination_str = json_object_finish(pjo);

		jo = json_object_new(0);
		json_object_kv_str(jo, "dataset", def->id);
		json_object_kv_int(jo, "version", 0);
		json_object_kv_str(jo, "keyField", def->key_field);
		json_object_kv_raw(jo, "fields",
			fields_str ? fields_str : "[]");
		json_object_kv_raw(jo, "rows",
			rows_str ? rows_str : "[]");
		json_object_kv_raw(jo, "pagination",
			pagination_str ? pagination_str : "{}");
		*out_json = json_object_finish(jo);

		free(pagination_str);
	}

	free(fields_str);
	free(rows_str);
	return *out_json ? 0 : -1;
}

/* -----------------------------------------------------------------------
 * HTTP handlers
 * ----------------------------------------------------------------------- */

static int source_get_handler(int fd, char *body)
{
	char dataset_id[128];
	const source_def_t *def;
	const char *username;
	char qs[512];
	int page;
	int per_page;
	char include[256];
	char qs_copy[512];
	char *json;

	(void)body;

	memset(dataset_id, 0, sizeof(dataset_id));
	ndc_env_get(fd, dataset_id, "PATTERN_PARAM_DATASET_ID");

	def = source_find(dataset_id);
	if (!def)
		return respond_json_error(fd, 404, "Dataset not found");

	username = get_request_user(fd);
	if (!username || !username[0])
		return respond_json_error(fd, 401, "Unauthorized");

	if (source_access_allowed(def, fd, username)
	    != DATASET_ACCESS_RESULT_ALLOW)
		return respond_json_error(fd, 403, "Forbidden");

	memset(qs, 0, sizeof(qs));
	ndc_env_get(fd, qs, "QUERY_STRING");

	page = 1;
	per_page = 0;
	memset(include, 0, sizeof(include));
	memset(qs_copy, 0, sizeof(qs_copy));

	if (qs[0]) {
		strncpy(qs_copy, qs, sizeof(qs_copy) - 1);
		ndc_query_parse(qs);
		{
			char buf[64];
			if (ndc_query_param("page", buf, sizeof(buf)) > 0)
				page = atoi(buf);
			if (ndc_query_param("per_page", buf, sizeof(buf)) > 0)
				per_page = atoi(buf);
			ndc_query_param("include", include, sizeof(include));
		}
	}

	if (page < 1)
		page = 1;

	json = NULL;
	if (source_build_json(def, qs_copy, include,
		page, per_page, &json) != 0)
		return respond_json_error(fd, 500,
			"Failed to render dataset");

	{
		int r = respond_json(fd, 200, json);
		free(json);
		return r;
	}
}

static int source_post_handler(int fd, char *body)
{
	const source_def_t *def;
	const char *username;

	int init = source_write_init(fd, body, &def, &username);
	switch (init) {
	case SOURCE_INIT_NOT_FOUND:
		return respond_json_error(fd, 404, "Dataset not found");
	case SOURCE_INIT_UNAUTHORIZED:
		return respond_json_error(fd, 401, "Unauthorized");
	case SOURCE_INIT_FORBIDDEN:
		return respond_json_error(fd, 403, "Forbidden");
	case SOURCE_INIT_OK:
		break;
	default:
		return respond_json_error(fd, 400, "Bad request");
	}

	/* Parse key field value from body (already parsed by write_init) */
	char id_buf[128] = {0};
	ndc_query_param(def->key_field, id_buf, sizeof(id_buf));
	const char *id = id_buf[0] ? id_buf : NULL;

	/* Auto-generate ID if missing */
	char auto_key[32] = {0};
	if (!id) {
		static uint32_t http_auto_seq = 0;
		snprintf(auto_key, sizeof(auto_key), "%u",
			++http_auto_seq);
		id = auto_key;
	}

	unsigned data_hd = source_parse_row_data_body(def, body);
	if (data_hd == 0)
		return respond_json_error(fd, 500,
			"Failed to parse row data");

	/* Ensure key field is in data handle */
	if (!qmap_get(data_hd, def->key_field))
		qmap_put(data_hd, def->key_field, id);

	int rc = source_update_item(fd, def->id, id, data_hd);
	qmap_close(data_hd);

	if (rc == SOURCE_ERR_VALIDATION)
		return 0;
	if (rc != 0)
		return respond_json_error(fd, 500, "Create failed");

	char resp[256];
	snprintf(resp, sizeof(resp),
		"{\"%s\":\"%s\"}", def->key_field, id);
	return respond_json(fd, 201, resp);
}

static int source_put_handler(int fd, char *body)
{
	const source_def_t *def;
	const char *username;

	int init = source_write_init(fd, body, &def, &username);
	switch (init) {
	case SOURCE_INIT_NOT_FOUND:
		return respond_json_error(fd, 404, "Dataset not found");
	case SOURCE_INIT_UNAUTHORIZED:
		return respond_json_error(fd, 401, "Unauthorized");
	case SOURCE_INIT_FORBIDDEN:
		return respond_json_error(fd, 403, "Forbidden");
	case SOURCE_INIT_OK:
		break;
	default:
		return respond_json_error(fd, 400, "Bad request");
	}

	char key[128] = {0};
	ndc_env_get(fd, key, "PATTERN_PARAM_KEY");
	if (!key[0])
		return respond_json_error(fd, 400, "Missing key");

	unsigned data_hd = source_parse_row_data_body(def, body);
	if (data_hd == 0)
		return respond_json_error(fd, 500,
			"Failed to parse row data");

	int rc = source_update_item(fd, def->id, key, data_hd);
	qmap_close(data_hd);

	if (rc == SOURCE_ERR_VALIDATION)
		return 0;
	if (rc != 0)
		return respond_json_error(fd, 500, "Update failed");

	return respond_json(fd, 200, "{\"status\":\"ok\"}");
}

struct inv_guard_ctx {
	const source_def_t *def;
	const char *item_id;
	uint32_t item_pos;
	char *err_buf;
	size_t err_cap;
};

static int inv_guard_cb(const source_def_t *target, void *user)
{
	struct inv_guard_ctx *ctx = user;
	if (target == ctx->def)
		return 0;
	if (!target->record_id || !target->fields_hd)
		return 0;

	for (size_t i = 0; i < target->field_count; i++) {
		const source_field_t *f = &target->fields[i];
		if (f->type != SOURCE_FIELD_REFERENCE &&
		    f->type != SOURCE_FIELD_MULTI_REFERENCE)
			continue;
		if (!f->target_source)
			continue;
		if (strcmp(f->target_source, ctx->def->id) != 0)
			continue;

		uint32_t inv_buf[16];
		size_t count = qmap_inv_get(target->fields_hd,
			f->name, ctx->item_pos, inv_buf, 16);
		if (count > 0) {
			snprintf(ctx->err_buf, ctx->err_cap,
				"Cannot delete '%s': "
				"referenced by %zu item(s) in %s",
				ctx->item_id, count, target->id);
			return 1;
		}
	}
	return 0;
}

static int source_delete_handler(int fd, char *body)
{
	char dataset_id[128] = {0};
	ndc_env_get(fd, dataset_id, "PATTERN_PARAM_DATASET_ID");

	const source_def_t *def = source_find(dataset_id);
	if (!def)
		return respond_json_error(fd, 404, "Dataset not found");

	const char *username = get_request_user(fd);
	if (!username || !username[0])
		return respond_json_error(fd, 401, "Unauthorized");

	if (source_access_allowed(def, fd, username)
	    != DATASET_ACCESS_RESULT_ALLOW) {
		return respond_json_error(fd, 403, "Forbidden");
	}

	if (body && body[0]) {
		ndc_query_parse(body);
		char csrf[33] = {0};
		ndc_query_param("csrf_token", csrf, sizeof(csrf));
		if (csrf_validate(fd, csrf) != 0)
			return respond_json_error(fd, 403, "Forbidden");
	}

	char key[128] = {0};
	ndc_env_get(fd, key, "PATTERN_PARAM_KEY");
	if (!key[0])
		return respond_json_error(fd, 400, "Missing key");

	/* Inverse reference guard */
	if (def->record_id > 0 && def->fields_hd) {
		uint32_t pos = qmap_pos(def->fields_hd, key);
		if (pos != UINT32_MAX) {
			char err_buf[512] = {0};
			struct inv_guard_ctx ctx = {
				.def = def,
				.item_id = key,
				.item_pos = pos,
				.err_buf = err_buf,
				.err_cap = sizeof(err_buf),
			};
			source_for_each(inv_guard_cb, &ctx);
			if (err_buf[0])
				return respond_json(fd, 409, err_buf);
		}
	}

	int rc = source_delete_item(fd, def, key);
	if (rc != 0)
		return respond_json_error(fd, 500, "Delete failed");

	return respond_json(fd, 200, "{\"status\":\"ok\"}");
}

/* -----------------------------------------------------------------------
 * Exported functions
 * ----------------------------------------------------------------------- */

int source_http_get_json(int fd, const char *dataset_id,
	const char *include, char **out_json)
{
	if (out_json)
		*out_json = NULL;

	const source_def_t *def = source_find(dataset_id);
	if (!def)
		return 404;

	const char *username = get_request_user(fd);
	if (!username || !username[0])
		return 401;

	switch (source_access_allowed(def, fd, username)) {
	case DATASET_ACCESS_RESULT_ALLOW:
		break;
	case DATASET_ACCESS_RESULT_UNAUTHORIZED:
		return 401;
	case DATASET_ACCESS_RESULT_FORBIDDEN:
		return 403;
	}

	return source_build_json(def, "", include, 1, 0, out_json)
		== 0 ? 0 : 500;
}

int source_http_get_item_json(int fd, const char *dataset_id,
	const char *id, char **out_json)
{
	if (out_json)
		*out_json = NULL;

	const source_def_t *def = source_find(dataset_id);
	if (!def || !id || !id[0])
		return 404;

	const char *username = get_request_user(fd);
	if (!username || !username[0]) {
		if (out_json)
			*out_json = NULL;
		return 401;
	}

	switch (source_access_allowed(def, fd, username)) {
	case DATASET_ACCESS_RESULT_ALLOW:
		break;
	case DATASET_ACCESS_RESULT_UNAUTHORIZED:
		if (out_json)
			*out_json = NULL;
		return 401;
	case DATASET_ACCESS_RESULT_FORBIDDEN:
		if (out_json)
			*out_json = NULL;
		return 403;
	}

	if (!qmap_get(def->source_hd, id)) {
		if (source_refresh_row(fd, dataset_id, id) != 0)
			return 404;
	}

	return source_build_item_json(def, id, out_json) == 0 ? 0 : 500;
}

void source_install_routes(void)
{
	ndc_register_handler(
		"GET:/api/dataset/:dataset_id", source_get_handler);
	ndc_register_handler(
		"POST:/api/dataset/:dataset_id", source_post_handler);
	ndc_register_handler(
		"PUT:/api/dataset/:dataset_id/:key", source_put_handler);
	ndc_register_handler(
		"DELETE:/api/dataset/:dataset_id/:key",
		source_delete_handler);
}
