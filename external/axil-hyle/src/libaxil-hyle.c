#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>

#include <ttypt/axil.h>
#include <ttypt/auth.h>
#include <ttypt/qmap.h>
#include <json-c/json.h>
#include <hyle/hyle.h>
#include <hyle/source.h>
#include <hyle-source/hyle_source.h>
#include <hyle-bud/hyle-bud.h>
#include <ttypt/axil-hyle.h>

#define SOURCE_API_DEFAULT_PAGE 1
#define SOURCE_API_DEFAULT_PER_PAGE 25
#define SOURCE_API_MAX_PER_PAGE 100
#define SOURCE_API_MAX_PAGE (UINT32_MAX / SOURCE_API_MAX_PER_PAGE)

#define PICK_MAX_OPTS 128
#define PICK_MAX_SEL 256
#define PICK_DEFAULT_PER_PAGE 15
#define PICK_MAX_SCROLL_PAGES 10
#define PICK_PANEL_SZ 65536
#define PICK_ROWS_SZ 32768
#define PICK_VALUES_SZ 8192

static __thread char pick_panel_buf[PICK_PANEL_SZ];
static __thread char pick_values_buf[PICK_VALUES_SZ];
static __thread char pick_rows_buf[PICK_ROWS_SZ];
static __thread char pick_opt_ids[PICK_MAX_OPTS][64];
static __thread char pick_opt_labels[PICK_MAX_OPTS][256];
static __thread char pick_sel_ids[PICK_MAX_SEL][64];
static __thread char pick_sel_labels[PICK_MAX_SEL][256];

/* XY / Auth / Ownership declarations */
XY_DECL(int, csrf_validate, int, fd, const char *, submitted);
XY_DECL(int, item_owner_record, const char *, item_path, const char *, username);
XY_DECL(int, item_owner_check, const char *, item_path, const char *, username);
XY_DECL(const char *, resolve_doc_root, int, fd, char *, buf, size_t, len);
XY_DECL(int, source_after_update, int, fd, const char *, dataset_id, const char *, id, unsigned, data_handle);

/* ── HTTP Response Helpers ───────────────────────────────────────── */

static int respond_json(int fd, int status, const char *msg)
{
	axil_header_set(fd, "Content-Type", "application/json");
	axil_respond(fd, status, msg);
	return 1;
}

static int respond_json_error(int fd, int status, const char *msg)
{
	char buf[512];
	snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg ? msg : "Error");
	return respond_json(fd, status, buf);
}

static int bad_request(int fd, const char *msg)
{
	return respond_json_error(fd, 400, msg ? msg : "Bad request");
}

static int not_found(int fd, const char *msg)
{
	return respond_json_error(fd, 404, msg ? msg : "Not found");
}

static int server_error(int fd, const char *msg)
{
	return respond_json_error(fd, 500, msg ? msg : "Internal server error");
}

static int respond_422_close(int fd, const char *json_str)
{
	axil_header_set(fd, "Connection", "close");
	return respond_json(fd, 422, json_str);
}

static int is_safe_id(const char *id)
{
	if (!id || !id[0])
		return 0;
	if (strcmp(id, ".") == 0 || strcmp(id, "..") == 0)
		return 0;
	for (const char *p = id; *p; p++) {
		if (*p == '/' || *p == '\\' || *p == ':' || (unsigned char)*p < 0x20)
			return 0;
	}
	return 1;
}

static char *pick_json_escape(const char *s)
{
	size_t n = s ? strlen(s) : 0;
	char *esc = malloc(n * 6 + 8);
	if (!esc)
		return NULL;
	axil_json_escape(s ? s : "", esc, n * 6 + 8);
	return esc;
}

static int pick_respond_jsonf(int fd, const char *fmt, ...)
{
	va_list ap;
	int needed;
	char *json;

	va_start(ap, fmt);
	needed = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (needed < 0)
		return server_error(fd, "Envelope failed");
	json = malloc((size_t)needed + 1);
	if (!json)
		return server_error(fd, "Out of memory");
	va_start(ap, fmt);
	vsnprintf(json, (size_t)needed + 1, fmt, ap);
	va_end(ap);
	respond_json(fd, 200, json);
	free(json);
	return 1;
}

/* ── Query Parameter Parsing ─────────────────────────────────────── */

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

static const char *field_type_name(hyle_source_field_type_t type)
{
	switch (type) {
	case HYLE_SOURCE_FIELD_STRING:
		return "string";
	case HYLE_SOURCE_FIELD_INT:
		return "int";
	case HYLE_SOURCE_FIELD_BOOL:
		return "bool";
	case HYLE_SOURCE_FIELD_NULLABLE_STRING:
		return "nullable_string";
	case HYLE_SOURCE_FIELD_REFERENCE:
		return "reference";
	case HYLE_SOURCE_FIELD_MULTI_REFERENCE:
		return "multi_reference";
	case HYLE_SOURCE_FIELD_INVERSE:
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

static int source_parse_row_data_body(const hyle_source_def_t *def, const char *body)
{
#define PARSE_BUF_CAP (256 * 1024)
	unsigned hd = qmap_open(NULL, "row_data", QM_STR, QM_STR, 0x1F, 0);
	if (hd == 0)
		return 0;

	for (size_t i = 0; i < def->field_count; i++) {
		const hyle_source_field_t *f = &def->fields[i];
		if (!f->writable)
			continue;

		char *val = calloc(1, PARSE_BUF_CAP);
		if (!val) {
			qmap_close(hd);
			return 0;
		}
		int ret_len;

		if (f->type == HYLE_SOURCE_FIELD_MULTI_REFERENCE)
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

static hyle_source_access_result_t
source_access_allowed(const hyle_source_def_t *def, int fd, const char *username)
{
	(void)fd;
	(void)username;
	if (def->access_policy == HYLE_SOURCE_ACCESS_PUBLIC ||
	    def->access_policy == HYLE_SOURCE_ACCESS_LOGIN)
		return HYLE_SOURCE_ACCESS_RESULT_ALLOW;
	return HYLE_SOURCE_ACCESS_RESULT_FORBIDDEN;
}

enum {
	SOURCE_INIT_OK = 0,
	SOURCE_INIT_NOT_FOUND,
	SOURCE_INIT_UNAUTHORIZED,
	SOURCE_INIT_FORBIDDEN,
};

static int source_write_init(
        int fd, char *body, const hyle_source_def_t **out_def,
        const char **out_username)
{
	char dataset_id[128] = { 0 };
	axil_env_get(
	        fd, dataset_id, sizeof(dataset_id), "PATTERN_PARAM_DATASET_ID");

	const hyle_source_def_t *def = hyle_source_find(dataset_id);
	if (!def)
		return SOURCE_INIT_NOT_FOUND;

	const char *username = get_request_user(fd);
	if (!username || !username[0])
		return SOURCE_INIT_UNAUTHORIZED;

	if (source_access_allowed(def, fd, username) !=
	    HYLE_SOURCE_ACCESS_RESULT_ALLOW)
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

static json_object *source_build_fields_json(const hyle_source_def_t *def)
{
	const hyle_source_field_t *f;
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
        const hyle_source_def_t *def, const hyle_source_field_t *field,
        const char *item_id)
{
	if (!field->target_source || !field->inverse_name)
		return json_object_new_array();

	const hyle_source_def_t *target = hyle_source_find(field->target_source);
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
        const hyle_source_def_t *def, const char *qs, const char *include,
        int *out_total_rows, json_object **out_rows_ja)
{
	unsigned result_hd;
	json_object *ja;
	char inc_set[512];
	int has_include;
	uint32_t cur;
	const void *key_ptr;
	const void *val_ptr;

	result_hd = hyle_source_query_dataset(def->id, qs ? qs : "");
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
			const hyle_source_field_t *f;
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

			val = hyle_qmap_get_field_str(def->fields_hd, item_id, f->name);

			switch (f->type) {
			case HYLE_SOURCE_FIELD_STRING:
				if (val)
					json_object_object_add(
					        jo, f->name,
					        json_object_new_string(val));
				break;
			case HYLE_SOURCE_FIELD_NULLABLE_STRING:
				if (val && val[0])
					json_object_object_add(
					        jo, f->name,
					        json_object_new_string(val));
				break;
			case HYLE_SOURCE_FIELD_INT:
				if (val)
					json_object_object_add(
					        jo, f->name,
					        json_object_new_int(atoi(val)));
				break;
			case HYLE_SOURCE_FIELD_BOOL:
				if (val)
					json_object_object_add(
					        jo, f->name,
					        json_object_new_boolean(
					                strcmp(val, "1") == 0 ||
					                strcmp(val, "true") ==
					                        0));
				break;
			case HYLE_SOURCE_FIELD_REFERENCE:
				if (val)
					json_object_object_add(
					        jo, f->name,
					        json_object_new_string(val));
				break;
			case HYLE_SOURCE_FIELD_MULTI_REFERENCE: {
				json_object *arr;
				arr = source_build_string_array(val);
				json_object_object_add(
				        jo, f->name,
				        arr ? arr : json_object_new_array());
				break;
			}
			case HYLE_SOURCE_FIELD_INVERSE: {
				json_object *arr;
				arr = source_build_inverse_array(
				        def, f, item_id);
				json_object_object_add(
				        jo, f->name,
				        arr ? arr : json_object_new_array());
				break;
			}
			default:
				break;
			}
		}

		json_object_array_add(ja, jo);
	}

	qmap_fin(cur);
	*out_rows_ja = ja;
	qmap_close(result_hd);
	return 0;
}

static int source_build_json(
        const hyle_source_def_t *def, const char *qs, const char *include, int page,
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

static void source_resolve_ref_display(
        json_object *jo, const hyle_source_def_t *def,
        const hyle_source_field_t *f, const char *item_id)
{
	char name_key[512];
	char out[4096];
	const char *val;

	if (!jo || !def || !f || !item_id || !f->target_source)
		return;

	snprintf(name_key, sizeof(name_key), "%s:%s", item_id, f->name);
	val = (const char *)qmap_get(def->fields_hd, name_key);
	if (!val || !val[0])
		return;

	if (hyle_source_resolve_ref_display_str(
	            def->id, item_id, f->name, out, sizeof(out)) == 0)
	{
		json_object_object_add(
		        jo, f->name, json_object_new_string(out));
	}
}

static int source_http_get_item_json(
        int fd, const char *dataset_id, const char *item_id, char **out_json)
{
	const hyle_source_def_t *def;
	json_object *jo;
	const char *str;
	char *copy;

	if (out_json)
		*out_json = NULL;

	def = hyle_source_find(dataset_id);
	if (!def)
		return 404;

	if (def->access_policy == HYLE_SOURCE_ACCESS_LOGIN) {
		const char *user = get_request_user(fd);
		if (!user || !user[0])
			return 401;
	}

	if (!qmap_get(def->source_hd, item_id)) {
		if (hyle_source_refresh_row(fd, dataset_id, item_id) != 0)
			return 404;
	}

	if (hyle_source_build_item_json(def, item_id, &jo) != 0)
		return -1;

	for (size_t i = 0; i < def->field_count; i++) {
		const hyle_source_field_t *f = &def->fields[i];
		if (f->type == HYLE_SOURCE_FIELD_MULTI_REFERENCE &&
		    f->target_source)
			source_resolve_ref_display(jo, def, f, item_id);
	}

	str = json_object_to_json_string_ext(
	        jo, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE);
	if (!str) {
		json_object_put(jo);
		return -1;
	}

	copy = strdup(str);
	json_object_put(jo);
	if (!copy)
		return -1;

	*out_json = copy;
	return 0;
}

/* ── Route Handlers ──────────────────────────────────────────────── */

static int source_get_handler(int fd, char *body)
{
	char dataset_id[128];
	const hyle_source_def_t *def;
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

	def = hyle_source_find(dataset_id);
	if (!def)
		return respond_json_error(fd, 404, "Dataset not found");

	username = get_request_user(fd);
	if (!username || !username[0])
		return respond_json_error(fd, 401, "Unauthorized");

	if (source_access_allowed(def, fd, username) !=
	    HYLE_SOURCE_ACCESS_RESULT_ALLOW)
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

	int r = respond_json(fd, 200, json);
	free(json);
	return r;
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

	int r = respond_json(fd, 200, json);
	free(json);
	return r;
}

static int source_post_handler(int fd, char *body)
{
	const hyle_source_def_t *def;
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

	if (!qmap_get(data_hd, def->key_field))
		qmap_put(data_hd, def->key_field, id);

	char *err_json = NULL;
	if (hyle_source_validate_row(def, data_hd, &err_json)) {
		if (err_json) {
			respond_422_close(fd, err_json);
			free(err_json);
		}
		qmap_close(data_hd);
		return 0;
	}

	int rc = hyle_source_update_item(fd, def->id, id, data_hd);
	if (rc == 0) {
		source_after_update(fd, def->id, id, data_hd);
	}
	qmap_close(data_hd);

	if (rc != 0)
		return respond_json_error(fd, 500, "Create failed");

	char doc_root[256] = { 0 };
	const char *root = resolve_doc_root(fd, doc_root, sizeof(doc_root));
	char owner_path[PATH_MAX];
	snprintf(
	        owner_path, sizeof(owner_path), "%s/%s/%s", root,
	        def->items_path, id);
	if (item_owner_record(owner_path, username) != 0) {
		hyle_source_delete_item(fd, def, id);
		return respond_json_error(
		        fd, 500, "Failed to record ownership");
	}
	if (hyle_source_refresh_row(fd, def->id, id) != 0) {
		hyle_source_delete_item(fd, def, id);
		return respond_json_error(fd, 500, "Failed to refresh owner");
	}

	char resp[256];
	snprintf(resp, sizeof(resp), "{\"%s\":\"%s\"}", def->key_field, id);
	return respond_json(fd, 201, resp);
}

static int source_put_handler(int fd, char *body)
{
	const hyle_source_def_t *def;
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

	char *err_json = NULL;
	if (hyle_source_validate_row(def, data_hd, &err_json)) {
		if (err_json) {
			respond_422_close(fd, err_json);
			free(err_json);
		}
		qmap_close(data_hd);
		return 0;
	}

	int rc = hyle_source_update_item(fd, def->id, key, data_hd);
	if (rc == 0) {
		source_after_update(fd, def->id, key, data_hd);
	}
	qmap_close(data_hd);

	if (rc != 0)
		return respond_json_error(fd, 500, "Update failed");
	if (!item_exists) {
		if (item_owner_record(item_path, username) != 0) {
			hyle_source_delete_item(fd, def, key);
			return respond_json_error(
			        fd, 500, "Failed to record ownership");
		}
		if (hyle_source_refresh_row(fd, def->id, key) != 0) {
			hyle_source_delete_item(fd, def, key);
			return respond_json_error(
			        fd, 500, "Failed to refresh owner");
		}
	}

	return respond_json(fd, 200, "{\"status\":\"ok\"}");
}

struct inv_guard_ctx {
	const hyle_source_def_t *def;
	const char *item_id;
	uint32_t item_pos;
	char *err_buf;
	size_t err_cap;
};

static int inv_guard_cb(const hyle_source_def_t *target, void *user)
{
	struct inv_guard_ctx *ctx = user;
	if (target == ctx->def)
		return 0;
	if (!target->record_id || !target->fields_hd)
		return 0;

	for (size_t i = 0; i < target->field_count; i++) {
		const hyle_source_field_t *f = &target->fields[i];
		if (f->type != HYLE_SOURCE_FIELD_REFERENCE &&
		    f->type != HYLE_SOURCE_FIELD_MULTI_REFERENCE)
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

	const hyle_source_def_t *def = hyle_source_find(dataset_id);
	if (!def)
		return respond_json_error(fd, 404, "Dataset not found");

	const char *username = get_request_user(fd);
	if (!username || !username[0])
		return respond_json_error(fd, 401, "Unauthorized");

	if (source_access_allowed(def, fd, username) !=
	    HYLE_SOURCE_ACCESS_RESULT_ALLOW)
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
			hyle_source_for_each(inv_guard_cb, &ctx);
			if (err_buf[0])
				return respond_json(fd, 409, err_buf);
		}
	}

	hyle_source_clear_inverse_refs(fd, dataset_id, key);

	int rc = hyle_source_delete_item(fd, def, key);
	if (rc != 0)
		return respond_json_error(fd, 500, "Delete failed");

	return respond_json(fd, 200, "{\"status\":\"ok\"}");
}

/* ── Picker Fragment Route: GET /pick/:id/options ────────────────── */

static int pick_options_handler(int fd, char *body)
{
	(void)body;
	char dataset[192];
	char qs[2048];
	char key[192];
	char label[256];
	char sel_raw[2048];
	char buf[16];
	hyle_option_t opts[PICK_MAX_OPTS];
	hyle_option_t sel[PICK_MAX_SEL];
	int nopts = 0, nsel = 0, total = 0;
	int multi = 0;
	int is_append = 0;
	int page = 0;
	int per_page = PICK_DEFAULT_PER_PAGE;
	char q_buf[256] = { 0 };

	axil_env_get(fd, dataset, sizeof(dataset), "PATTERN_PARAM_ID");
	if (!dataset[0])
		return bad_request(fd, "Missing dataset");

	const char *user = get_request_user(fd);
	if (!user || !user[0])
		return respond_json_error(fd, 401, "Unauthorized");

	axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");

	key[0] = '\0';
	hyle_bud_query_param(qs, "key", key, sizeof(key));
	if (!key[0] || strlen(key) > 96)
		return bad_request(fd, "Missing key");

	label[0] = '\0';
	hyle_bud_query_param(qs, "label", label, sizeof(label));
	sel_raw[0] = '\0';
	hyle_bud_query_param(qs, "sel", sel_raw, sizeof(sel_raw));
	buf[0] = '\0';
	hyle_bud_query_param(qs, "multi", buf, sizeof(buf));
	multi = (buf[0] == '1');

	char pname[256];
	snprintf(pname, sizeof(pname), "pick_q_%s", key);
	hyle_bud_query_param(qs, pname, q_buf, sizeof(q_buf));

	buf[0] = '\0';
	snprintf(pname, sizeof(pname), "pick_page_%s", key);
	hyle_bud_query_param(qs, pname, buf, sizeof(buf));
	if (buf[0]) {
		page = atoi(buf);
		if (page < 0)
			page = 0;
		if (page > 10000)
			page = 10000;
	}

	buf[0] = '\0';
	hyle_bud_query_param(qs, "more", buf, sizeof(buf));
	is_append = (buf[0] == '1');
	if (is_append) {
		buf[0] = '\0';
		hyle_bud_query_param(qs, "page", buf, sizeof(buf));
		if (buf[0]) {
			page = atoi(buf);
			if (page < 0)
				page = 0;
			if (page > 10000)
				page = 10000;
		}
	}

	if (!hyle_source_find(dataset))
		return not_found(fd, "Unknown dataset");

	nsel = hyle_source_resolve_tokens(
	        dataset, sel_raw, sel, PICK_MAX_SEL, pick_sel_ids,
	        pick_sel_labels);

	nopts = hyle_source_resolve_options(
	        dataset, q_buf, page, per_page, opts, PICK_MAX_OPTS, &total,
	        pick_opt_ids, pick_opt_labels);

	if (is_append) {
		hyle_bud_picker_desc_t d;
		char *rows_esc;
		int eof;

		if (page >= PICK_MAX_SCROLL_PAGES &&
		    (page + 1) * per_page < total)
		{
			return pick_respond_jsonf(
			        fd, "{\"rows\":\"<div class=\\\"hyle-picker-"
			            "refine\\\">Too many results \xe2\x80\x94 "
			            "refine your search.</div>\",\"eof\":1}");
		}

		memset(&d, 0, sizeof(d));
		d.key = key;
		d.label = label;
		d.source = dataset;
		d.multi = multi;
		d.q = q_buf;
		d.page = page;
		d.per_page = per_page;
		d.total = total;
		d.page_opts = opts;
		d.npage = nopts;
		d.sel = sel;
		d.nsel = nsel;

		hyle_bud_picker_rows(&d, pick_rows_buf, sizeof(pick_rows_buf));
		eof = (page + 1) * per_page >= total;

		rows_esc = pick_json_escape(pick_rows_buf);
		if (!rows_esc)
			return server_error(fd, "Out of memory");
		int rc = pick_respond_jsonf(
		        fd, "{\"rows\":\"%s\",\"eof\":%d}", rows_esc,
		        eof ? 1 : 0);
		free(rows_esc);
		return rc;
	}

	{
		hyle_bud_picker_desc_t d;
		char *panel_esc;
		char *values_esc;

		memset(&d, 0, sizeof(d));
		d.key = key;
		d.label = label;
		d.source = dataset;
		d.multi = multi;
		d.q = q_buf;
		d.page = page;
		d.per_page = per_page;
		d.total = total;
		d.page_opts = opts;
		d.npage = nopts;
		d.sel = sel;
		d.nsel = nsel;

		hyle_bud_picker_slots(
		        &d, pick_panel_buf, sizeof(pick_panel_buf),
		        pick_values_buf, sizeof(pick_values_buf));

		panel_esc = pick_json_escape(pick_panel_buf);
		values_esc = pick_json_escape(pick_values_buf);
		if (!panel_esc || !values_esc) {
			free(panel_esc);
			free(values_esc);
			return server_error(fd, "Out of memory");
		}
		int rc = pick_respond_jsonf(
		        fd, "{\"slots\":{\"panel\":\"%s\",\"values\":\"%s\"}}",
		        panel_esc, values_esc);
		free(panel_esc);
		free(values_esc);
		return rc;
	}
}

static int check_partition_owner(
        int fd, const char *dataset_id, const char *key, const char *username)
{
	if (!username || !username[0])
		return 0;
	char module[64] = { 0 };
	const char *dot = strchr(dataset_id, '.');
	if (dot) {
		snprintf(
		        module, sizeof(module), "%.*s",
		        (int)(dot - dataset_id), dataset_id);
	} else {
		snprintf(module, sizeof(module), "%s", dataset_id);
	}
	char doc_root[256] = { 0 };
	const char *root = resolve_doc_root(fd, doc_root, sizeof(doc_root));
	char item_path[PATH_MAX];
	snprintf(
	        item_path, sizeof(item_path), "%s/var/%s/%s", root, module,
	        key);
	struct stat st;
	if (stat(item_path, &st) == 0 && S_ISDIR(st.st_mode)) {
		return item_owner_check(item_path, username);
	}
	return 1;
}

static int source_get_ordered_handler(int fd, char *body)
{
	char dataset_id[128] = { 0 };
	char key[128] = { 0 };
	(void)body;

	axil_env_get(
	        fd, dataset_id, sizeof(dataset_id), "PATTERN_PARAM_DATASET_ID");
	axil_env_get(fd, key, sizeof(key), "PATTERN_PARAM_KEY");

	if (!dataset_id[0] || !key[0])
		return respond_json_error(fd, 400, "Missing parameters");

	unsigned fhd = hyle_source_get_fields_hd(dataset_id);
	if (!fhd)
		return respond_json_error(fd, 404, "Dataset not found");

	int total = hyle_source_ordered_count(dataset_id, key);
	if (total < 0)
		total = 0;

	size_t nfields = hyle_source_get_field_count(dataset_id);

	struct json_object *root = json_object_new_object();
	struct json_object *items_arr = json_object_new_array();

	json_object_object_add(root, "total", json_object_new_int(total));

	for (int i = 0; i < total; i++) {
		const char *item_key =
		        hyle_source_ordered_key_at(dataset_id, key, i);
		if (!item_key)
			continue;
		struct json_object *row = json_object_new_object();
		json_object_object_add(row, "_index", json_object_new_int(i));
		json_object_object_add(
		        row, "_key", json_object_new_string(item_key));

		for (size_t j = 0; j < nfields; j++) {
			const char *fname =
			        hyle_source_get_field_name(dataset_id, j);
			if (!fname)
				continue;
			const char *fval = qmap_field_get(fhd, item_key, fname);
			json_object_object_add(
			        row, fname,
			        json_object_new_string(fval ? fval : ""));
		}
		json_object_array_add(items_arr, row);
	}

	json_object_object_add(root, "items", items_arr);

	const char *json_str = json_object_to_json_string(root);
	char *resp = strdup(json_str ? json_str : "{}");
	json_object_put(root);

	if (!resp)
		return respond_json_error(fd, 500, "Out of memory");

	int rc = respond_json(fd, 200, resp);
	free(resp);
	return rc;
}

static int source_post_ordered_handler(int fd, char *body)
{
	char dataset_id[128] = { 0 };
	char key[128] = { 0 };

	axil_env_get(
	        fd, dataset_id, sizeof(dataset_id), "PATTERN_PARAM_DATASET_ID");
	axil_env_get(fd, key, sizeof(key), "PATTERN_PARAM_KEY");

	if (!dataset_id[0] || !key[0])
		return respond_json_error(fd, 400, "Missing parameters");

	unsigned fhd = hyle_source_get_fields_hd(dataset_id);
	if (!fhd)
		return respond_json_error(fd, 404, "Dataset not found");

	const char *username = get_request_user(fd);
	if (!username || !username[0])
		return respond_json_error(fd, 401, "Unauthorized");

	if (!check_partition_owner(fd, dataset_id, key, username))
		return respond_json_error(fd, 403, "Forbidden");

	if (body && body[0])
		axil_query_parse(body);

	size_t nfields = hyle_source_get_field_count(dataset_id);
	const char *names[64];
	const char *vals[64];
	char val_bufs[64][256];
	size_t count = 0;

	for (size_t j = 0; j < nfields && count < 64; j++) {
		const char *fname = hyle_source_get_field_name(dataset_id, j);
		if (!fname ||
		    hyle_source_get_field_type(dataset_id, j) ==
		            HYLE_FIELD_INVERSE)
			continue;
		val_bufs[count][0] = '\0';
		axil_query_param(
		        fname, val_bufs[count], sizeof(val_bufs[count]) - 1);
		names[count] = fname;
		vals[count] = val_bufs[count];
		count++;
	}

	if (hyle_source_ordered_append(dataset_id, key, names, vals, count) !=
	    0)
		return respond_json_error(fd, 500, "Failed to append item");

	hyle_source_ordered_save(dataset_id, key);

	int total = hyle_source_ordered_count(dataset_id, key);
	char resp[128];
	snprintf(
	        resp, sizeof(resp), "{\"ok\":true,\"index\":%d}",
	        total > 0 ? total - 1 : 0);
	return respond_json(fd, 201, resp);
}

static int source_put_ordered_handler(int fd, char *body)
{
	char dataset_id[128] = { 0 };
	char key[128] = { 0 };
	char n_str[32] = { 0 };

	axil_env_get(
	        fd, dataset_id, sizeof(dataset_id), "PATTERN_PARAM_DATASET_ID");
	axil_env_get(fd, key, sizeof(key), "PATTERN_PARAM_KEY");
	axil_env_get(fd, n_str, sizeof(n_str), "PATTERN_PARAM_N");

	if (!dataset_id[0] || !key[0] || !n_str[0])
		return respond_json_error(fd, 400, "Missing parameters");

	unsigned fhd = hyle_source_get_fields_hd(dataset_id);
	if (!fhd)
		return respond_json_error(fd, 404, "Dataset not found");

	const char *username = get_request_user(fd);
	if (!username || !username[0])
		return respond_json_error(fd, 401, "Unauthorized");

	if (!check_partition_owner(fd, dataset_id, key, username))
		return respond_json_error(fd, 403, "Forbidden");

	int idx = atoi(n_str);
	int total = hyle_source_ordered_count(dataset_id, key);
	if (idx < 0 || idx >= total)
		return respond_json_error(fd, 404, "Index out of bounds");

	const char *item_key = hyle_source_ordered_key_at(dataset_id, key, idx);
	if (!item_key)
		return respond_json_error(fd, 404, "Item not found");

	if (body && body[0])
		axil_query_parse(body);

	size_t nfields = hyle_source_get_field_count(dataset_id);
	const char *names[64];
	const char *vals[64];
	char val_bufs[64][256];
	size_t count = 0;

	for (size_t j = 0; j < nfields && count < 64; j++) {
		const char *fname = hyle_source_get_field_name(dataset_id, j);
		if (!fname ||
		    hyle_source_get_field_type(dataset_id, j) ==
		            HYLE_FIELD_INVERSE)
			continue;
		val_bufs[count][0] = '\0';
		if (axil_query_param(
		            fname, val_bufs[count],
		            sizeof(val_bufs[count]) - 1) > 0)
		{
			names[count] = fname;
			vals[count] = val_bufs[count];
			count++;
		}
	}

	if (count > 0) {
		hyle_source_put(dataset_id, item_key, names, vals, count);
		hyle_source_ordered_save(dataset_id, key);
	}

	char resp[128];
	snprintf(resp, sizeof(resp), "{\"ok\":true,\"index\":%d}", idx);
	return respond_json(fd, 200, resp);
}

static int source_delete_ordered_handler(int fd, char *body)
{
	(void)body;
	char dataset_id[128] = { 0 };
	char key[128] = { 0 };
	char n_str[32] = { 0 };

	axil_env_get(
	        fd, dataset_id, sizeof(dataset_id), "PATTERN_PARAM_DATASET_ID");
	axil_env_get(fd, key, sizeof(key), "PATTERN_PARAM_KEY");
	axil_env_get(fd, n_str, sizeof(n_str), "PATTERN_PARAM_N");

	if (!dataset_id[0] || !key[0] || !n_str[0])
		return respond_json_error(fd, 400, "Missing parameters");

	unsigned fhd = hyle_source_get_fields_hd(dataset_id);
	if (!fhd)
		return respond_json_error(fd, 404, "Dataset not found");

	const char *username = get_request_user(fd);
	if (!username || !username[0])
		return respond_json_error(fd, 401, "Unauthorized");

	if (!check_partition_owner(fd, dataset_id, key, username))
		return respond_json_error(fd, 403, "Forbidden");

	int idx = atoi(n_str);
	int total = hyle_source_ordered_count(dataset_id, key);
	if (idx < 0 || idx >= total)
		return respond_json_error(fd, 404, "Index out of bounds");

	hyle_source_ordered_remove_at(dataset_id, key, idx);
	hyle_source_ordered_save(dataset_id, key);

	return respond_json(fd, 200, "{\"ok\":true}");
}

/* ── Route Installation ──────────────────────────────────────────── */

void axil_hyle_install_routes(void)
{
	axil_register_handler(
	        "GET:/api/dataset/:dataset_id", source_get_handler);
	axil_register_handler(
	        "GET:/api/dataset/:dataset_id/:key", source_get_item_handler);
	axil_register_handler(
	        "GET:/api/dataset/:dataset_id/:key/ordered",
	        source_get_ordered_handler);
	axil_register_handler(
	        "POST:/api/dataset/:dataset_id", source_post_handler);
	axil_register_handler(
	        "POST:/api/dataset/:dataset_id/:key/ordered",
	        source_post_ordered_handler);
	axil_register_handler(
	        "PUT:/api/dataset/:dataset_id/:key", source_put_handler);
	axil_register_handler(
	        "PUT:/api/dataset/:dataset_id/:key/ordered/:n",
	        source_put_ordered_handler);
	axil_register_handler(
	        "DELETE:/api/dataset/:dataset_id/:key", source_delete_handler);
	axil_register_handler(
	        "DELETE:/api/dataset/:dataset_id/:key/ordered/:n",
	        source_delete_ordered_handler);
	axil_register_handler(
	        "GET:/pick/:id/options", pick_options_handler);
}
