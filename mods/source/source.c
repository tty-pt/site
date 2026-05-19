#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>

#include "../auth/auth.h"
#include <ttypt/ndc.h>
#include <ttypt/ndx-mod.h>
#include <ttypt/qmap.h>
#include "hyle/hyle.h"
#include "hyle/source.h"
#include "../mpfd/mpfd.h"
#include "../common/common.h"

#define SOURCE_IMPL
#include "source.h"

static int source_scan_item(int fd, const source_def_t *def, const char *id);
static hyle_field_type_t source_to_hyle_type(source_field_type_t t);
static int ref_normalize(
	const char *dataset_id,
	const char *field_name,
	char **data,
	size_t *len);
NDX_HOOK_DECL(int, source_after_update,
	int, fd, const char *, dataset_id,
	const char *, id, unsigned, data_handle);

static int source_field_is_multi_reference(source_field_type_t type)
{
	return type == SOURCE_FIELD_MULTI_REFERENCE;
}

/*
 * source_find returns the source_def_t stored as the libhyle user pointer.
 * All registration stores a heap-allocated copy of the def there.
 */
NDX_LISTENER(source_def_t *, source_find, const char *, dataset_id)
{
	if (!dataset_id || !dataset_id[0])
		return NULL;
	return (source_def_t *)hyle_source_get_user(dataset_id);
}

static int source_scan_item(int fd, const source_def_t *def, const char *id)
{
	char doc_root[256] = { 0 };
	get_doc_root(fd, doc_root, sizeof(doc_root));
	const char *root = (fd == 0 && !doc_root[0]) ? "." : doc_root;

	char item_path[PATH_MAX];
	snprintf(
		item_path,
		sizeof(item_path),
		"%s/%s/%s",
		root,
		def->items_path,
		id);

	struct stat st;
	if (lstat(item_path, &st) != 0 || !S_ISDIR(st.st_mode))
		return -1;

	/* Write "id" field first */
	qmap_field_put(def->fields_hd, id, "id", id);

	/* Normalize id field to URL-safe form */
	{
		char id_norm[256];
		ndc_slugify(id, strlen(id), id_norm, sizeof(id_norm));
		if (strcmp(id, id_norm) != 0)
			qmap_field_put(def->fields_hd, id, "id", id_norm);
	}

	for (size_t i = 0; i < def->field_count; i++) {
		if (strcmp(def->fields[i].name, "id") == 0)
			continue;
		if (!def->fields[i].file)
			continue;

		char file_path[PATH_MAX + 256];
		snprintf(
			file_path,
			sizeof(file_path),
			"%s/%s",
			item_path,
			def->fields[i].file);

		char *data = slurp_file(file_path);
		if (data) {
			size_t len = strlen(data);
			ref_normalize(
				def->id, def->fields[i].name, &data, &len);
			qmap_field_put(
				def->fields_hd, id, def->fields[i].name, data);
		}
		free(data);
	}

	qmap_put(def->source_hd, id, "");

	return 0;
}

NDX_LISTENER(int, source_delete_item,
	int, fd,
	const source_def_t *, def,
	const char *, item_id)
{
	if (!def || !item_id)
		return -1;

	char doc_root[256] = { 0 };
	get_doc_root(fd, doc_root, sizeof(doc_root));
	const char *root = (fd == 0 && !doc_root[0]) ? "." : doc_root;

	char item_path[PATH_MAX];
	snprintf(
		item_path,
		sizeof(item_path),
		"%s/%s/%s",
		root,
		def->items_path,
		item_id);

	item_remove_path_recursive(item_path);
	qmap_del(def->fields_hd, item_id);
	qmap_del(def->source_hd, item_id);
	return 0;
}

static int source_scan_items(source_def_t *def)
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
		source_scan_item(0, def, entry->d_name);
	}

	closedir(dir);
	return 0;
}

NDX_LISTENER(int, source_refresh_row,
	int, fd, const char *, dataset_id, const char *, id)
{
	source_def_t *def = source_find(dataset_id);
	if (!def || !id || !id[0])
		return -1;
	return source_scan_item(fd, def, id);
}

static unsigned source_parse_row_data(const source_def_t *def);

NDX_LISTENER(unsigned, source_parse_form, const char *, dataset_id)
{
	const source_def_t *def = source_find(dataset_id);
	if (!def)
		return 0;
	return source_parse_row_data(def);
}

NDX_LISTENER(unsigned, source_row_qtype, uint32_t, record_id)
{
	if (record_id > 0)
		return qmap_record_type_id(record_id);
	return QM_STR;
}

NDX_LISTENER(int, source_register, const source_def_t *, def)
{
	size_t          n;
	size_t          i;
	hyle_field_t   *hf;
	source_def_t   *copy;
	unsigned        fields_hd;

	if (!def || !def->id || !def->id[0] || !def->key_field ||
	    !def->key_field[0] || !def->fields || def->field_count == 0 ||
	    !def->items_path)
		return -1;

	if (source_find(def->id))
		return -1;

	/* Convert source_field_t[] → hyle_field_t[] (owned by libhyle) */
	n  = def->field_count;
	hf = malloc(n * sizeof(hyle_field_t));
	if (!hf)
		return -1;

	for (i = 0; i < n; i++) {
		const source_field_t *sf = &def->fields[i];
		hf[i].name         = sf->name;
		hf[i].type         = source_to_hyle_type(sf->type);
		hf[i].writable     = sf->writable;
		hf[i].target_source = sf->target_source;
		hf[i].inverse_name = sf->inverse_name;
		hf[i].required     = sf->required;
		hf[i].min          = sf->min;
		hf[i].max          = sf->max;
		hf[i].min_length   = sf->min_length;
		hf[i].max_length   = sf->max_length;
		hf[i].pattern      = sf->pattern;
	}

	/*
	 * Allocate a copy of the def to store as the libhyle user pointer.
	 * libhyle creates both handles — retrieve them via accessors after
	 * registration.
	 */
	copy = malloc(sizeof(*copy));
	if (!copy) {
		free(hf);
		return -1;
	}
	*copy = *def;

	fields_hd = hyle_source_register(
		def->id,
		hf, n,
		def->record_id,
		def->flags | QM_SORTED,
		copy);

	if (!fields_hd) {
		free(hf);
		free(copy);
		return -1;
	}

	copy->source_hd = hyle_source_get_row_hd(def->id);
	copy->fields_hd = fields_hd;

	/* Set target_hd for reference fields using typed record layout */
	if (def->record_id > 0) {
		for (i = 0; i < n; i++) {
			const source_field_t *sf = &def->fields[i];
			source_def_t *target;

			if ((sf->type != SOURCE_FIELD_REFERENCE &&
			     sf->type != SOURCE_FIELD_MULTI_REFERENCE) ||
			    !sf->target_source)
				continue;
			target = source_find(sf->target_source);
			if (target && target->fields_hd)
				qmap_record_field_set_target_hd(
					def->record_id,
					sf->name,
					target->fields_hd);
		}
	}

	/* Scan filesystem items into the qmaps */
	source_scan_items(copy);

	return 0;
}

static unsigned source_parse_row_data(const source_def_t *def)
{
	unsigned hd;
	size_t   i;
	int      ret_len;
	char     val[1024];

	hd = qmap_open(NULL, "row_data", QM_STR, QM_STR, 0x1F, 0);
	if (hd == 0)
		return 0;

	for (i = 0; i < def->field_count; i++) {
		const source_field_t *f = &def->fields[i];
		if (!f->writable)
			continue;

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

static hyle_field_type_t source_to_hyle_type(source_field_type_t t)
{
	switch (t) {
	case SOURCE_FIELD_STRING: return HYLE_FIELD_STRING;
	case DATASET_FIELD_INT: return HYLE_FIELD_INT;
	case DATASET_FIELD_BOOL: return HYLE_FIELD_BOOL;
	case DATASET_FIELD_NULLABLE_STRING: return HYLE_FIELD_NULLABLE_STRING;
	case SOURCE_FIELD_REFERENCE: return HYLE_FIELD_REFERENCE;
	case SOURCE_FIELD_MULTI_REFERENCE: return HYLE_FIELD_MULTI_REFERENCE;
	case SOURCE_FIELD_INVERSE: return HYLE_FIELD_INVERSE;
	default: return HYLE_FIELD_STRING;
	}
}

static int source_validate_row(int fd, const source_def_t *def,
	unsigned data_handle)
{
	size_t            n;
	hyle_field_t     *hfields;
	const char      **values;
	size_t            i;
	int               rc;
	hyle_purify_error_t *errs;
	size_t            nerr;

	n       = def->field_count;
	hfields = malloc(n * sizeof(hyle_field_t));
	values  = malloc(n * sizeof(const char *));
	if (!hfields || !values) {
		free(hfields);
		free(values);
		return 0;
	}

	for (i = 0; i < n; i++) {
		const source_field_t *sf = &def->fields[i];
		hfields[i].name         = sf->name;
		hfields[i].type         = source_to_hyle_type(sf->type);
		hfields[i].writable     = sf->writable;
		hfields[i].target_source = sf->target_source;
		hfields[i].inverse_name = sf->inverse_name;
		hfields[i].required     = sf->required;
		hfields[i].min          = sf->min;
		hfields[i].max          = sf->max;
		hfields[i].min_length   = sf->min_length;
		hfields[i].max_length   = sf->max_length;
		hfields[i].pattern      = sf->pattern;
		values[i] = qmap_get(data_handle, sf->name);
	}

	rc = hyle_purify_row(hfields, n, values, &errs, &nerr);

	free(hfields);
	free(values);

	if (rc == 0)
		return 0;

	/* Build JSON error body */
	char   buf[4096];
	size_t pos;
	size_t j;

	pos = 0;
	pos += snprintf(buf + pos, sizeof(buf) - pos,
		"{\"errors\":[");
	for (j = 0; j < nerr; j++) {
		if (j > 0)
			pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
		pos += snprintf(buf + pos, sizeof(buf) - pos,
			"{\"field\":\"");
		ndc_json_escape(errs[j].field,
			buf + pos, sizeof(buf) - pos);
		pos += strlen(buf + pos);
		pos += snprintf(buf + pos, sizeof(buf) - pos,
			"\",\"rule\":\"");
		ndc_json_escape(errs[j].rule,
			buf + pos, sizeof(buf) - pos);
		pos += strlen(buf + pos);
		pos += snprintf(buf + pos, sizeof(buf) - pos,
			"\",\"message\":\"");
		ndc_json_escape(errs[j].message,
			buf + pos, sizeof(buf) - pos);
		pos += strlen(buf + pos);
		pos += snprintf(buf + pos, sizeof(buf) - pos, "\"}");
	}
	snprintf(buf + pos, sizeof(buf) - pos, "]}");

	hyle_purify_errors_free(errs, nerr);

	ndc_header_set(fd, "Connection", "close");
	ndc_header_set(fd, "Content-Type", "application/json");
	ndc_respond(fd, 422, buf);
	return 1;
}

NDX_LISTENER(int, source_update_item,
	int, fd,
	const char *, dataset_id,
	const char *, id,
	unsigned, data_handle)
{
	const source_def_t *def = source_find(dataset_id);
	if (!def)
		return -1;
	if (source_validate_row(fd, def, data_handle))
		return SOURCE_ERR_VALIDATION;
	char auto_key_buf[32];
	if (!id || !id[0]) {
		static uint32_t source_auto_seq = 0;
		snprintf(
			auto_key_buf,
			sizeof(auto_key_buf),
			"%u",
			++source_auto_seq);
		id = auto_key_buf;
	}

	char doc_root[256] = { 0 };
	get_doc_root(fd, doc_root, sizeof(doc_root));
	const char *root = (fd == 0 && !doc_root[0]) ? "." : doc_root;

	char item_path[PATH_MAX];
	snprintf(
		item_path,
		sizeof(item_path),
		"%s/%s/%s",
		root,
		def->items_path,
		id);
	mkdir(item_path, 0755);

	for (size_t i = 0; i < def->field_count; i++) {
		const source_field_t *f = &def->fields[i];
		const char *val = qmap_get(data_handle, f->name);

		if (val) {
			if (f->file) {
				write_item_child_file(
					item_path, f->file, val, strlen(val));
			}
		} else {
			if (f->file) {
				char file_path[PATH_MAX + 256];
				snprintf(
					file_path,
					sizeof(file_path),
					"%s/%s",
					item_path,
					f->file);
				char *content = slurp_file(file_path);
				if (content) {
					free(content);
				} else if (!source_field_is_multi_reference(
					f->type))
				{
					FILE *fp = fopen(file_path, "w");
					if (fp)
						fclose(fp);
				}
			}
		}
	}

	int result = source_scan_item(fd, def, id);
	if (result == 0)
		source_after_update(fd, dataset_id, id, data_handle);
	return result;
}

#define MAX_REF_REGISTRATIONS 32

typedef struct {
	const char *dataset_id;
	const char *field_name;
} ref_reg_t;

static ref_reg_t ref_regs[MAX_REF_REGISTRATIONS];
static size_t    ref_reg_count = 0;

NDX_LISTENER(int, ref_field_register,
	const char *, dataset_id,
	const char *, field_name)
{
	if (!dataset_id || !field_name)
		return -1;
	if (ref_reg_count >= MAX_REF_REGISTRATIONS)
		return -1;
	for (size_t i = 0; i < ref_reg_count; i++) {
		if (strcmp(ref_regs[i].dataset_id, dataset_id) == 0 &&
		    strcmp(ref_regs[i].field_name, field_name) == 0)
			return 0;
	}
	ref_regs[ref_reg_count].dataset_id = dataset_id;
	ref_regs[ref_reg_count].field_name = field_name;
	ref_reg_count++;
	return 0;
}

int ref_normalize(
	const char *dataset_id,
	const char *field_name,
	char **data,
	size_t *len)
{
	if (!dataset_id || !field_name || !data || !*data || !(*data)[0])
		return 0;

	int found = 0;
	for (size_t i = 0; i < ref_reg_count; i++) {
		if (strcmp(ref_regs[i].dataset_id, dataset_id) == 0 &&
		    strcmp(ref_regs[i].field_name, field_name) == 0)
		{
			found = 1;
			break;
		}
	}
	if (!found)
		return 0;

	char copy[8192];
	char result[8192] = { 0 };

	snprintf(copy, sizeof(copy), "%s", *data);
	char    *tok;
	char    *saveptr;
	tok = strtok_r(copy, "\r\n", &saveptr);
	int first = 1;

	while (tok) {
		str_trim(tok);
		if (tok[0]) {
			char   id_norm[256];
			size_t rlen;
			ndc_slugify(tok, strlen(tok), id_norm, sizeof(id_norm));
			rlen = strlen(result);
			snprintf(
				result + rlen,
				sizeof(result) - rlen,
				"%s%s",
				first ? "" : "\n",
				id_norm);
			first = 0;
		}
		tok = strtok_r(NULL, "\r\n", &saveptr);
	}

	if (result[0]) {
		free(*data);
		*data = strdup(result);
		if (*data && len)
			*len = strlen(*data);
	}

	return 0;
}

/* Functions implemented in source-http.c */
void source_install_routes(void);
int source_http_get_json(int fd, const char *dataset_id,
	const char *include, char **out_json);
int source_http_get_item_json(int fd, const char *dataset_id,
	const char *id, char **out_json);

NDX_LISTENER(int, source_for_each, source_each_cb_t, cb, void *, user)
{
	size_t count;
	size_t i;

	if (!cb)
		return -1;
	count = hyle_source_count();
	for (i = 0; i < count; i++) {
		const char   *sid = hyle_source_id_at(i);
		source_def_t *def;

		if (!sid)
			continue;
		def = (source_def_t *)hyle_source_get_user(sid);
		if (!def)
			continue;
		if (cb(def, user) != 0)
			return 1;
	}
	return 0;
}

NDX_LISTENER(unsigned, source_query,
	const char *, dataset_id,
	const char *, query_str)
{
	hyle_query_t   query;
	char          *qs_copy = NULL;
	hyle_row_set_t output;
	size_t         total;
	char           tbuf[16];

	if (!dataset_id || !hyle_source_get_user(dataset_id))
		return 0;

	memset(&query, 0, sizeof(query));

	if (query_str && query_str[0]) {
		qs_copy = strdup(query_str);
		if (!qs_copy)
			return 0;
		hyle_parse_query(qs_copy, &query);
	}

	memset(&output, 0, sizeof(output));
	total = 0;

	if (hyle_source_query(dataset_id, &query, &output, &total) != 0) {
		hyle_query_clear(&query);
		free(qs_copy);
		return 0;
	}

	snprintf(tbuf, sizeof(tbuf), "%zu", total);
	qmap_put(output.row_hd, "__total__", tbuf);

	hyle_query_clear(&query);
	free(qs_copy);
	return output.row_hd;
}

NDX_LISTENER(int, source_get_json,
	int, fd,
	const char *, dataset_id,
	const char *, include,
	char **, out_json)
{
	return source_http_get_json(fd, dataset_id, include, out_json);
}

NDX_LISTENER(int, source_get_item_json,
	int, fd,
	const char *, dataset_id,
	const char *, id,
	char **, out_json)
{
	return source_http_get_item_json(fd, dataset_id, id, out_json);
}

NDX_LISTENER(unsigned, source_get_data_hd, const char *, dataset_id)
{
	return hyle_source_get_row_hd(dataset_id);
}

NDX_LISTENER(unsigned, source_get_fields_hd, const char *, dataset_id)
{
	return hyle_source_get_fields_hd(dataset_id);
}

static unsigned source_build_schema_hd(const source_def_t *def)
{
	unsigned              hd;
	size_t                i;
	char                  buf[512];
	const source_field_t *f;

	hd = qmap_open(NULL, NULL, QM_STR, QM_STR, 0x3FF, 0);
	if (!hd)
		return 0;

	for (i = 0; i < def->field_count; i++) {
		f = &def->fields[i];
		if (f->type == SOURCE_FIELD_REFERENCE ||
		    f->type == SOURCE_FIELD_MULTI_REFERENCE) {
			snprintf(buf, sizeof(buf),
				"{\"t\":%d,\"s\":\"%s\"}",
				(int)f->type,
				f->target_source ? f->target_source : "");
		} else if (f->type == SOURCE_FIELD_INVERSE) {
			snprintf(buf, sizeof(buf),
				"{\"t\":%d,\"s\":\"%s\",\"i\":\"%s\"}",
				(int)f->type,
				f->target_source ? f->target_source : "",
				f->inverse_name ? f->inverse_name : "");
		} else {
			snprintf(buf, sizeof(buf),
				"{\"t\":%d}", (int)f->type);
		}
		qmap_put(hd, f->name, buf);
	}

	return hd;
}

NDX_LISTENER(unsigned, source_get_schema_hd, const char *, dataset_id)
{
	const source_def_t *def = source_find(dataset_id);
	if (!def)
		return 0;
	return source_build_schema_hd(def);
}

MODULE_API void ndx_install(void)
{
	source_install_routes();
}
