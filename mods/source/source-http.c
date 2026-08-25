#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>

#include "../auth/auth.h"
#include <ttypt/axil.h>
#include <json-c/json.h>
#include <ttypt/qmap.h>
#include "../mpfd/mpfd.h"
#include "../common/common.h"
#include "source.h"

#define SOURCE_API_DEFAULT_PAGE 1
#define SOURCE_API_DEFAULT_PER_PAGE 25
#define SOURCE_API_MAX_PER_PAGE 100
#define SOURCE_API_MAX_PAGE (UINT32_MAX / SOURCE_API_MAX_PER_PAGE)

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

static int respond_json_error(int fd, int status, const char *msg)
{
	char buf[512];
	snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg);
	return respond_json(fd, status, buf);
}

static int source_query_int(
        const char *value, size_t value_len, int fallback, int max,
        int clamp_overflow)
{
	char decoded[BUFSIZ];
	char *end;
	long parsed;

	if (value_len >= sizeof(decoded))
		return clamp_overflow ? max : fallback;
	memcpy(decoded, value, value_len);
	decoded[value_len] = '\0';
	axil_url_decode(decoded, value_len, decoded, sizeof(decoded));

	errno = 0;
	end = NULL;
	parsed = strtol(decoded, &end, 10);
	if (!decoded[0] || !end || *end || parsed <= 0)
		return fallback;
	if (errno == ERANGE || parsed > max)
		return clamp_overflow ? max : fallback;
	return (int)parsed;
}

static int source_rewrite_query(
        const char *raw, char *out, size_t out_len, int *page, int *per_page)
{
	const char *p;
	size_t pos;
	int first;
	char pagination[64];
	int pagination_len;

	if (!out || out_len == 0 || !page || !per_page)
		return -1;
	*page = SOURCE_API_DEFAULT_PAGE;
	*per_page = SOURCE_API_DEFAULT_PER_PAGE;
	out[0] = '\0';
	pos = 0;
	first = 1;
	p = raw ? raw : "";

	while (*p) {
		const char *amp;
		const char *eq;
		size_t part_len;
		size_t key_len;
		int is_page;
		int is_per_page;

		amp = strchr(p, '&');
		part_len = amp ? (size_t)(amp - p) : strlen(p);
		eq = memchr(p, '=', part_len);
		key_len = eq ? (size_t)(eq - p) : part_len;
		is_page = key_len == strlen("page") &&
		          memcmp(p, "page", key_len) == 0;
		is_per_page = key_len == strlen("per_page") &&
		              memcmp(p, "per_page", key_len) == 0;

		if (eq && (is_page || is_per_page)) {
			const char *value;
			size_t value_len;

			value = eq + 1;
			value_len = part_len - key_len - 1;
			if (is_page)
				*page = source_query_int(
				        value, value_len,
				        SOURCE_API_DEFAULT_PAGE,
				        SOURCE_API_MAX_PAGE, 0);
			else
				*per_page = source_query_int(
				        value, value_len,
				        SOURCE_API_DEFAULT_PER_PAGE,
				        SOURCE_API_MAX_PER_PAGE, 1);
		} else if (part_len > 0) {
			if ((!first && pos + 1 >= out_len) ||
			    pos + part_len >= out_len)
				return -1;
			if (!first)
				out[pos++] = '&';
			memcpy(out + pos, p, part_len);
			pos += part_len;
			out[pos] = '\0';
			first = 0;
		}

		p += part_len;
		if (*p == '&')
			p++;
	}

	pagination_len = snprintf(
	        pagination, sizeof(pagination), "page=%d&per_page=%d", *page,
	        *per_page);
	if (pagination_len < 0 ||
	    pos + (first ? 0 : 1) + (size_t)pagination_len >= out_len)
		return -1;
	if (!first)
		out[pos++] = '&';
	memcpy(out + pos, pagination, (size_t)pagination_len + 1);
	return 0;
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
			axil_url_decode(tok, klen, kdec, sizeof(kdec));
			if (strcmp(kdec, name) == 0) {
				size_t vlen = strlen(eq + 1);
				char vdec[1024];
				axil_url_decode(
				        eq + 1, vlen, vdec, sizeof(vdec));
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

static int source_parse_row_data_body(const source_def_t *def, const char *body)
{
#define PARSE_BUF_CAP (256 * 1024)
	unsigned hd = qmap_open(NULL, "row_data", QM_STR, QM_STR, 0x1F, 0);
	if (hd == 0)
		return 0;

	for (size_t i = 0; i < def->field_count; i++) {
		const source_field_t *f = &def->fields[i];
		if (!f->writable)
			continue;

		char *val = calloc(1, PARSE_BUF_CAP);
		if (!val) {
			qmap_close(hd);
			return 0;
		}
		int ret_len;

		if (f->type == SOURCE_FIELD_MULTI_REFERENCE)
			ret_len = source_collect_query_values(
			        body, f->name, val, PARSE_BUF_CAP);
		else
			ret_len = axil_query_param(
			        f->name, val, PARSE_BUF_CAP - 1);

		if (ret_len <= 0) {
			free(val);
			continue;
		}
		if (ret_len >= (int)(PARSE_BUF_CAP - 1)) {
			free(val);
			qmap_close(hd);
			return -1;
		}
		qmap_put(hd, f->name, val);
		free(val);
	}
#undef PARSE_BUF_CAP
	return (int)hd;
}

static source_access_result_t
source_access_allowed(const source_def_t *def, int fd, const char *username)
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

static int source_write_init(
        int fd, char *body, const source_def_t **out_def,
        const char **out_username)
{
	char dataset_id[128] = { 0 };
	axil_env_get(
	        fd, dataset_id, sizeof(dataset_id), "PATTERN_PARAM_DATASET_ID");

	const source_def_t *def = source_find(dataset_id);
	if (!def)
		return SOURCE_INIT_NOT_FOUND;

	const char *username = get_request_user(fd);
	if (!username || !username[0])
		return SOURCE_INIT_UNAUTHORIZED;

	if (source_access_allowed(def, fd, username) !=
	    DATASET_ACCESS_RESULT_ALLOW)
		return SOURCE_INIT_FORBIDDEN;

	axil_query_parse(body ? body : "");
	char csrf[33] = { 0 };
	axil_query_param("csrf_token", csrf, sizeof(csrf));
	if (csrf_validate(fd, csrf) != 0)
		return SOURCE_INIT_FORBIDDEN;

	if (out_def)
		*out_def = def;
	if (out_username)
		*out_username = username;
	return SOURCE_INIT_OK;
}

static json_object *source_build_fields_json(const source_def_t *def)
{
	const source_field_t *f;
	const char *str;

	json_object *ja = json_object_new_array();
	if (!ja)
		return NULL;
	for (size_t i = 0; i < def->field_count; i++) {
		f = &def->fields[i];
		json_object *jo = json_object_new_object();
		if (!jo) {
			json_object_put(ja);
			return NULL;
		}
		json_object_object_add(
		        jo, "name", json_object_new_string(f->name));
		str = field_type_name(f->type);
		json_object_object_add(
		        jo, "type",
		        json_object_new_string(str ? str : "unknown"));
		json_object_object_add(
		        jo, "writable", json_object_new_boolean(f->writable));
		json_object_object_add(
		        jo, "required", json_object_new_boolean(f->required));
		if (f->min)
			json_object_object_add(
			        jo, "min", json_object_new_int64(f->min));
		if (f->max)
			json_object_object_add(
			        jo, "max", json_object_new_int64(f->max));
		if (f->min_length)
			json_object_object_add(
			        jo, "minLength",
			        json_object_new_int64(f->min_length));
		if (f->max_length)
			json_object_object_add(
			        jo, "maxLength",
			        json_object_new_int64(f->max_length));
		if (f->pattern)
			json_object_object_add(
			        jo, "pattern",
			        json_object_new_string(f->pattern));
		if (f->target_source)
			json_object_object_add(
			        jo, "targetSource",
			        json_object_new_string(f->target_source));
		if (f->inverse_name)
			json_object_object_add(
			        jo, "inverseName",
			        json_object_new_string(f->inverse_name));
		json_object_array_add(ja, jo);
	}
	return ja;
}

/* -----------------------------------------------------------------------
 * Core JSON builders
 * ----------------------------------------------------------------------- */

static json_object *source_build_string_array(const char *input)
{
	const char *p, *start;
	char *field_val;

	json_object *ja = json_object_new_array();
	if (!ja)
		return NULL;
	if (input && input[0]) {
		p = input;
		start = p;
		while (*p) {
			if (*p == '\n') {
				field_val = strndup(start, (size_t)(p - start));
				if (field_val) {
					json_object_array_add(
					        ja, json_object_new_string(
					                    field_val));
					free(field_val);
				}
				p++;
				start = p;
			} else {
				p++;
			}
		}
		if (p > start) {
			field_val = strndup(start, (size_t)(p - start));
			if (field_val) {
				json_object_array_add(
				        ja, json_object_new_string(field_val));
				free(field_val);
			}
		}
	}
	return ja;
}

static json_object *source_build_inverse_array(
        const source_def_t *def, const source_field_t *field,
        const char *item_id)
{
	if (!field->target_source || !field->inverse_name)
		return json_object_new_array();

	const source_def_t *target = source_find(field->target_source);
	if (!target || !target->fields_hd)
		return json_object_new_array();

	uint32_t pos = qmap_pos(def->fields_hd, item_id);
	if (pos == UINT32_MAX)
		return json_object_new_array();

	uint32_t inv_buf[256];
	size_t count = qmap_inv_get(
	        target->fields_hd, field->inverse_name, pos, inv_buf, 256);

	json_object *ja = json_object_new_array();
	if (!ja)
		return json_object_new_array();

	for (size_t i = 0; i < count; i++) {
		const char *key = qmap_get_key(target->fields_hd, inv_buf[i]);
		if (key) {
			json_object_array_add(ja, json_object_new_string(key));
		}
	}
	return ja;
}

static int source_build_rows_json(
        const source_def_t *def, const char *qs, const char *include,
        int *out_total_rows, json_object **out_rows_ja)
{
	unsigned result_hd;
	json_object *ja;
	char inc_set[512];
	int has_include;
	uint32_t cur;
	const void *key_ptr;
	const void *val_ptr;

	result_hd = source_query(def->id, qs ? qs : "");
	if (!result_hd) {
		*out_total_rows = 0;
		*out_rows_ja = json_object_new_array();
		return *out_rows_ja ? 0 : -1;
	}

	{
		const char *ts = qmap_get(result_hd, "__total__");
		*out_total_rows = ts ? atoi(ts) : 0;
	}

	ja = json_object_new_array();
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
		json_object *jo;
		size_t i;

		if (strcmp(item_id, "__total__") == 0)
			continue;

		jo = json_object_new_object();
		if (!jo)
			continue;

		json_object_object_add(
		        jo, "id", json_object_new_string(item_id));

		for (i = 0; i < def->field_count; i++) {
			const source_field_t *f;
			const char *val;

			f = &def->fields[i];
			if (strcmp(f->name, "id") == 0)
				continue;
			if (has_include) {
				char needle[256];
				snprintf(
				        needle, sizeof(needle), ",%s,",
				        f->name);
				if (!strstr(inc_set, needle))
					continue;
			}

			val = qmap_field_get(def->fields_hd, item_id, f->name);

			switch (f->type) {
			case SOURCE_FIELD_STRING:
				if (val)
					json_object_object_add(
					        jo, f->name,
					        json_object_new_string(val));
				break;
			case DATASET_FIELD_NULLABLE_STRING:
				if (val && val[0])
					json_object_object_add(
					        jo, f->name,
					        json_object_new_string(val));
				break;
			case DATASET_FIELD_INT:
				if (val)
					json_object_object_add(
					        jo, f->name,
					        json_object_new_int(atoi(val)));
				break;
			case DATASET_FIELD_BOOL:
				if (val)
					json_object_object_add(
					        jo, f->name,
					        json_object_new_boolean(
					                strcmp(val, "1") == 0 ||
					                strcmp(val, "true") ==
					                        0));
				break;
			case SOURCE_FIELD_REFERENCE:
				if (val)
					json_object_object_add(
					        jo, f->name,
					        json_object_new_string(val));
				break;
			case SOURCE_FIELD_MULTI_REFERENCE: {
				json_object *arr;
				arr = source_build_string_array(val);
				json_object_object_add(
				        jo, f->name,
				        arr ? arr : json_object_new_array());
				break;
			}
			case SOURCE_FIELD_INVERSE: {
				json_object *arr;
				arr = source_build_inverse_array(
				        def, f, item_id);
				json_object_object_add(
				        jo, f->name,
				        arr ? arr : json_object_new_array());
				break;
			}
			}
		}

		json_object_array_add(ja, jo);
	}

	qmap_fin(cur);
	*out_rows_ja = ja;
	qmap_close(result_hd);
	return 0;
}

int source_build_item_json(
        const source_def_t *def, const char *item_id, json_object **out_jo)
{
	json_object *jo = json_object_new_object();
	if (!jo)
		return -1;

	json_object_object_add(jo, "id", json_object_new_string(item_id));

	for (size_t i = 0; i < def->field_count; i++) {
		const source_field_t *f = &def->fields[i];
		if (strcmp(f->name, "id") == 0)
			continue;

		const char *val =
		        qmap_field_get(def->fields_hd, item_id, f->name);

		switch (f->type) {
		case SOURCE_FIELD_STRING:
			if (val)
				json_object_object_add(
				        jo, f->name,
				        json_object_new_string(val));
			break;
		case DATASET_FIELD_NULLABLE_STRING:
			if (val && val[0])
				json_object_object_add(
				        jo, f->name,
				        json_object_new_string(val));
			break;
		case DATASET_FIELD_INT:
			if (val)
				json_object_object_add(
				        jo, f->name,
				        json_object_new_int(atoi(val)));
			break;
		case DATASET_FIELD_BOOL:
			if (val)
				json_object_object_add(
				        jo, f->name,
				        json_object_new_boolean(
				                strcmp(val, "1") == 0 ||
				                strcmp(val, "true") == 0));
			break;
		case SOURCE_FIELD_REFERENCE:
			if (val)
				json_object_object_add(
				        jo, f->name,
				        json_object_new_string(val));
			break;
		case SOURCE_FIELD_MULTI_REFERENCE: {
			json_object *arr = source_build_string_array(val);
			json_object_object_add(
			        jo, f->name,
			        arr ? arr : json_object_new_array());
			break;
		}
		case SOURCE_FIELD_INVERSE: {
			json_object *arr =
			        source_build_inverse_array(def, f, item_id);
			json_object_object_add(
			        jo, f->name,
			        arr ? arr : json_object_new_array());
			break;
		}
		}
	}

	*out_jo = jo;
	return 0;
}

static int source_build_json(
        const source_def_t *def, const char *qs, const char *include, int page,
        int per_page, char **out_json)
{
	json_object *fields_ja;
	json_object *rows_ja;
	int total;
	int rc;

	fields_ja = source_build_fields_json(def);
	total = 0;
	rows_ja = NULL;

	rc = source_build_rows_json(def, qs, include, &total, &rows_ja);
	if (rc != 0 || !rows_ja) {
		json_object_put(fields_ja);
		return -1;
	}

	{
		int total_pages;
		json_object *pjo;
		json_object *jo;

		total_pages =
		        per_page > 0 ? (total + per_page - 1) / per_page : 1;

		pjo = json_object_new_object();
		json_object_object_add(pjo, "page", json_object_new_int(page));
		json_object_object_add(
		        pjo, "per_page", json_object_new_int(per_page));
		json_object_object_add(
		        pjo, "total", json_object_new_int(total));
		json_object_object_add(
		        pjo, "total_pages", json_object_new_int(total_pages));

		jo = json_object_new_object();
		json_object_object_add(
		        jo, "dataset", json_object_new_string(def->id));
		json_object_object_add(jo, "version", json_object_new_int(0));
		json_object_object_add(
		        jo, "keyField", json_object_new_string(def->key_field));
		json_object_object_add(
		        jo, "fields",
		        fields_ja ? fields_ja : json_object_new_array());
		json_object_object_add(
		        jo, "rows",
		        rows_ja ? rows_ja : json_object_new_array());
		json_object_object_add(jo, "pagination", pjo);

		const char *s = json_object_to_json_string(jo);
		*out_json = strdup(s ? s : "{}");
		json_object_put(jo);
	}

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
	char qs[BUFSIZ];
	int page;
	int per_page;
	char include[256];
	char qs_copy[BUFSIZ + 64];
	char *json;

	(void)body;

	memset(dataset_id, 0, sizeof(dataset_id));
	axil_env_get(
	        fd, dataset_id, sizeof(dataset_id), "PATTERN_PARAM_DATASET_ID");

	def = source_find(dataset_id);
	if (!def)
		return respond_json_error(fd, 404, "Dataset not found");

	username = get_request_user(fd);
	if (!username || !username[0])
		return respond_json_error(fd, 401, "Unauthorized");

	if (source_access_allowed(def, fd, username) !=
	    DATASET_ACCESS_RESULT_ALLOW)
		return respond_json_error(fd, 403, "Forbidden");

	memset(qs, 0, sizeof(qs));
	axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");

	memset(include, 0, sizeof(include));
	memset(qs_copy, 0, sizeof(qs_copy));

	if (source_rewrite_query(
	            qs, qs_copy, sizeof(qs_copy), &page, &per_page) != 0)
		return respond_json_error(fd, 400, "Query string too long");
	axil_query_parse(qs_copy);
	axil_query_param("include", include, sizeof(include));

	json = NULL;
	if (source_build_json(def, qs_copy, include, page, per_page, &json) !=
	    0)
		return respond_json_error(fd, 500, "Failed to render dataset");

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

	/* Resolve the key: explicit id, else slugify title/name like
	 * HTML does. 400 before slugify so "item" is never minted. */
	char id_buf[128] = { 0 };
	axil_query_param(def->key_field, id_buf, sizeof(id_buf));

	if (!id_buf[0]) {
		char src[512] = { 0 };
		axil_query_param("title", src, sizeof(src));
		if (!src[0])
			axil_query_param("name", src, sizeof(src));
		if (!src[0])
			return respond_json_error(fd, 400, "Missing id");
		axil_slugify(src, strlen(src), id_buf, sizeof(id_buf));
	}

	if (!is_safe_id(id_buf))
		return respond_json_error(fd, 400, "Invalid key");

	const char *id = id_buf;

	/* POST never overwrites: an existing item dir is a collision */
	{
		char doc_root[256] = { 0 };
		const char *root =
		        resolve_doc_root(fd, doc_root, sizeof(doc_root));
		char item_path[PATH_MAX];
		snprintf(
		        item_path, sizeof(item_path), "%s/%s/%s", root,
		        def->items_path, id);
		struct stat st;
		if (stat(item_path, &st) == 0 && S_ISDIR(st.st_mode))
			return respond_json_error(
			        fd, 409,
			        "An item with that title already exists");
	}

	int data_hd = source_parse_row_data_body(def, body);
	if (data_hd < 0)
		return respond_json_error(fd, 413, "Field value too large");
	if (data_hd == 0)
		return respond_json_error(fd, 500, "Failed to parse row data");

	/* Ensure key field is in data handle */
	if (!qmap_get(data_hd, def->key_field))
		qmap_put(data_hd, def->key_field, id);

	int rc = source_update_item(fd, def->id, id, data_hd);
	qmap_close(data_hd);

	if (rc == SOURCE_ERR_VALIDATION)
		return 0;
	if (rc != 0)
		return respond_json_error(fd, 500, "Create failed");

	char doc_root[256] = { 0 };
	const char *root = resolve_doc_root(fd, doc_root, sizeof(doc_root));
	char owner_path[PATH_MAX];
	snprintf(
	        owner_path, sizeof(owner_path), "%s/%s/%s", root,
	        def->items_path, id);
	if (item_owner_record(owner_path, username) != 0) {
		source_delete_item(fd, def, id);
		return respond_json_error(
		        fd, 500, "Failed to record ownership");
	}
	if (source_refresh_row(fd, def->id, id) != 0) {
		source_delete_item(fd, def, id);
		return respond_json_error(fd, 500, "Failed to refresh owner");
	}

	char resp[256];
	snprintf(resp, sizeof(resp), "{\"%s\":\"%s\"}", def->key_field, id);
	return respond_json(fd, 201, resp);
}

static int source_put_handler(int fd, char *body)
{
	const source_def_t *def;
	const char *username;
	char item_path[PATH_MAX] = { 0 };
	int item_exists = 0;

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

	char key[128] = { 0 };
	axil_env_get(fd, key, sizeof(key), "PATTERN_PARAM_KEY");
	if (!key[0])
		return respond_json_error(fd, 400, "Missing key");

	if (!is_safe_id(key))
		return respond_json_error(fd, 400, "Invalid key");

	/* Ownership check: require ownership for updates to existing items */
	{
		char doc_root[256] = { 0 };
		const char *root =
		        resolve_doc_root(fd, doc_root, sizeof(doc_root));
		snprintf(
		        item_path, sizeof(item_path), "%s/%s/%s", root,
		        def->items_path, key);
		struct stat st;
		if (stat(item_path, &st) == 0 && S_ISDIR(st.st_mode)) {
			item_exists = 1;
			if (!item_owner_check(item_path, username))
				return respond_json_error(fd, 403, "Forbidden");
		}
	}

	int data_hd = source_parse_row_data_body(def, body);
	if (data_hd < 0)
		return respond_json_error(fd, 413, "Field value too large");
	if (data_hd == 0)
		return respond_json_error(fd, 500, "Failed to parse row data");

	int rc = source_update_item(fd, def->id, key, data_hd);
	qmap_close(data_hd);

	if (rc == SOURCE_ERR_VALIDATION)
		return 0;
	if (rc != 0)
		return respond_json_error(fd, 500, "Update failed");
	if (!item_exists) {
		if (item_owner_record(item_path, username) != 0) {
			source_delete_item(fd, def, key);
			return respond_json_error(
			        fd, 500, "Failed to record ownership");
		}
		if (source_refresh_row(fd, def->id, key) != 0) {
			source_delete_item(fd, def, key);
			return respond_json_error(
			        fd, 500, "Failed to refresh owner");
		}
	}

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
		size_t count = qmap_inv_get(
		        target->fields_hd, f->name, ctx->item_pos, inv_buf, 16);
		if (count > 0) {
			snprintf(
			        ctx->err_buf, ctx->err_cap,
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
	char dataset_id[128] = { 0 };
	axil_env_get(
	        fd, dataset_id, sizeof(dataset_id), "PATTERN_PARAM_DATASET_ID");

	const source_def_t *def = source_find(dataset_id);
	if (!def)
		return respond_json_error(fd, 404, "Dataset not found");

	const char *username = get_request_user(fd);
	if (!username || !username[0])
		return respond_json_error(fd, 401, "Unauthorized");

	if (source_access_allowed(def, fd, username) !=
	    DATASET_ACCESS_RESULT_ALLOW)
	{
		return respond_json_error(fd, 403, "Forbidden");
	}

	axil_query_parse(body ? body : "");
	char csrf[33] = { 0 };
	axil_query_param("csrf_token", csrf, sizeof(csrf));
	if (csrf_validate(fd, csrf) != 0)
		return respond_json_error(fd, 403, "Forbidden");

	char key[128] = { 0 };
	axil_env_get(fd, key, sizeof(key), "PATTERN_PARAM_KEY");
	if (!key[0])
		return respond_json_error(fd, 400, "Missing key");

	if (!is_safe_id(key))
		return respond_json_error(fd, 400, "Invalid key");

	/* Ownership check: require ownership for deleting existing items */
	{
		char doc_root[256] = { 0 };
		const char *root =
		        resolve_doc_root(fd, doc_root, sizeof(doc_root));
		char item_path[PATH_MAX];
		snprintf(
		        item_path, sizeof(item_path), "%s/%s/%s", root,
		        def->items_path, key);
		struct stat st;
		if (stat(item_path, &st) == 0 && S_ISDIR(st.st_mode)) {
			if (!item_owner_check(item_path, username))
				return respond_json_error(fd, 403, "Forbidden");
		}
	}

	/* Inverse reference guard */
	if (def->record_id > 0 && def->fields_hd) {
		uint32_t pos = qmap_pos(def->fields_hd, key);
		if (pos != UINT32_MAX) {
			char err_buf[512] = { 0 };
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

int source_http_get_item_json(
        int fd, const char *dataset_id, const char *id, char **out_json)
{
	json_object *jo;
	const char *s;

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

	if (source_build_item_json(def, id, &jo) != 0)
		return 500;

	s = json_object_to_json_string(jo);
	*out_json = strdup(s ? s : "{}");
	json_object_put(jo);
	return *out_json ? 0 : 500;
}

/* ── State JSON builder ──────────────────────────────────────────── */

static void source_resolve_ref_display(
        json_object *jo, const source_def_t *def, const source_field_t *f,
        const char *item_id)
{
	char result[4096] = { 0 };

	if (source_resolve_ref_display_str(
	            def->id, item_id, f->name, result, sizeof(result)) != 0)
		return;
	if (!result[0])
		return;

	json_object_object_del(jo, f->name);
	json_object_object_add(jo, f->name, json_object_new_string(result));
}

int source_http_build_state_json(
        const char *dataset_id, const char *item_id,
        const source_state_field_t *specs, json_object **out)
{
	const source_def_t *def;
	json_object *jo;

	if (out)
		*out = NULL;

	def = source_find(dataset_id);
	if (!def || !item_id || !item_id[0])
		return -1;

	if (!qmap_get(def->source_hd, item_id)) {
		if (source_refresh_row(0, dataset_id, item_id) != 0)
			return -1;
	}

	if (source_build_item_json(def, item_id, &jo) != 0)
		return -1;

	for (const source_state_field_t *s = specs; s && s->name; s++) {
		switch (s->kind) {
		case SF_EXCLUDE:
			json_object_object_del(jo, s->name);
			break;
		case SF_REF_DISPLAY: {
			const source_field_t *f = NULL;
			for (size_t i = 0; i < def->field_count; i++) {
				if (strcmp(def->fields[i].name, s->name) == 0) {
					f = &def->fields[i];
					break;
				}
			}
			if (f && f->type == SOURCE_FIELD_MULTI_REFERENCE &&
			    f->target_source)
				source_resolve_ref_display(jo, def, f, item_id);
			break;
		}
		default:
			break;
		}
	}

	*out = jo;
	return 0;
}

int source_http_state_overlay(json_object *jo, const source_state_kv_t *kvs)
{
	if (!jo || !kvs)
		return 0;
	for (const source_state_kv_t *kv = kvs; kv->key; kv++) {
		if (kv->is_int)
			json_object_object_add(
			        jo, kv->key, json_object_new_int(kv->int_val));
		else
			json_object_object_add(
			        jo, kv->key,
			        json_object_new_string(
			                kv->str_val ? kv->str_val : ""));
	}
	return 0;
}

static int source_get_item_handler(int fd, char *body)
{
	char dataset_id[128] = { 0 };
	char item_id[128] = { 0 };
	char *json = NULL;
	int rc;

	(void)body;

	axil_env_get(
	        fd, dataset_id, sizeof(dataset_id), "PATTERN_PARAM_DATASET_ID");
	axil_env_get(fd, item_id, sizeof(item_id), "PATTERN_PARAM_KEY");

	rc = source_http_get_item_json(fd, dataset_id, item_id, &json);
	if (rc == 404)
		return respond_json_error(fd, 404, "Not found");
	if (rc == 401)
		return respond_json_error(fd, 401, "Unauthorized");
	if (rc == 403)
		return respond_json_error(fd, 403, "Forbidden");
	if (rc != 0 || !json)
		return respond_json_error(fd, 500, "Failed to render record");

	{
		int r = respond_json(fd, 200, json);
		free(json);
		return r;
	}
}

void source_install_routes(void)
{
	axil_register_handler(
	        "GET:/api/dataset/:dataset_id", source_get_handler);
	axil_register_handler(
	        "GET:/api/dataset/:dataset_id/:key", source_get_item_handler);
	axil_register_handler(
	        "POST:/api/dataset/:dataset_id", source_post_handler);
	axil_register_handler(
	        "PUT:/api/dataset/:dataset_id/:key", source_put_handler);
	axil_register_handler(
	        "DELETE:/api/dataset/:dataset_id/:key", source_delete_handler);
}
