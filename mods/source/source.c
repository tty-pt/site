#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <limits.h>
#include <pwd.h>

#include "../auth/auth.h"
#include <ttypt/axil.h>
#include <ttypt/ndx-mod.h>
#include <ttypt/qmap.h>
#include "bud/bud.h"
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

static void resolve_ref_append(
        char *out,
        size_t *rpos,
        size_t out_sz,
        const source_def_t *target,
        const char *token)
{
	const char *target_id = NULL;
	const char *display = NULL;
	uint32_t pos;
	size_t dlen;

	pos = (uint32_t)strtoul(token, NULL, 10);
	if (target->fields_hd)
		target_id = qmap_get_key(target->fields_hd, pos);
	if (!target_id)
		target_id = token;

	display =
	        qmap_field_get(target->fields_hd, target_id, target->key_field);
	if (!display || !display[0])
		display = target_id;
	if (!display || !display[0])
		return;

	if (*rpos > 0 && *rpos < out_sz - 1) {
		out[(*rpos)++] = '\n';
	}

	dlen = strlen(display);
	if (*rpos + dlen < out_sz) {
		memcpy(out + *rpos, display, dlen);
		*rpos += dlen;
	}
}

NDX_LISTENER(int, source_resolve_ref_display_str,
	const char *, dataset_id,
	const char *, item_id,
	const char *, field_name,
	char *, out, size_t, out_sz)
{
	const source_def_t *def;
	const source_field_t *f = NULL;
	const char *val;
	const source_def_t *target;

	if (out && out_sz > 0)
		out[0] = '\0';
	if (!dataset_id || !item_id || !field_name || !out || out_sz == 0)
		return -1;

	def = source_find(dataset_id);
	if (!def || !def->fields_hd)
		return -1;

	for (size_t i = 0; i < def->field_count; i++) {
		if (strcmp(def->fields[i].name, field_name) == 0) {
			f = &def->fields[i];
			break;
		}
	}
	if (!f || f->type != SOURCE_FIELD_MULTI_REFERENCE || !f->target_source)
		return -1;

	val = qmap_field_get(def->fields_hd, item_id, field_name);
	if (!val || !val[0])
		return 0;

	target = source_find(f->target_source);
	if (!target || !target->fields_hd || !target->key_field)
		return -1;

	size_t rpos = 0;
	const char *p = val;
	const char *start = p;

	while (*p) {
		if (*p == '\n') {
			char id_buf[256];
			size_t len = (size_t)(p - start);
			if (len >= sizeof(id_buf))
				len = sizeof(id_buf) - 1;
			memcpy(id_buf, start, len);
			id_buf[len] = '\0';

			resolve_ref_append(out, &rpos, out_sz, target, id_buf);
			p++;
			start = p;
		} else {
			p++;
		}
	}
	if (p > start) {
		char id_buf[256];
		size_t len = (size_t)(p - start);
		if (len >= sizeof(id_buf))
			len = sizeof(id_buf) - 1;
		memcpy(id_buf, start, len);
		id_buf[len] = '\0';

		resolve_ref_append(out, &rpos, out_sz, target, id_buf);
	}
	if (rpos < out_sz)
		out[rpos] = '\0';
	else if (out_sz > 0)
		out[out_sz - 1] = '\0';

	return 0;
}

NDX_LISTENER(int, source_resolve_meta_display,
	const char *, dataset_id,
	const char *, item_id,
	const bud_field_desc_t *, fields,
	int, count,
	void *, state)
{
	int resolved = 0;

	if (!dataset_id || !item_id || !fields || !state)
		return -1;

	for (int i = 0; i < count; i++) {
		if (fields[i].kind != SF_REF_DISPLAY)
			continue;
		if (!fields[i].key || !fields[i].offset || !fields[i].size)
			continue;
		char buf[4096] = { 0 };
		if (source_resolve_ref_display_str(
		            dataset_id,
		            item_id,
		            fields[i].key,
		            buf,
		            sizeof(buf)) == 0 &&
		    buf[0])
		{
			snprintf(
			        (char *)state + fields[i].offset,
			        fields[i].size,
			        "%s",
			        buf);
			resolved++;
		}
	}
	return resolved > 0 ? 0 : -1;
}

static void
source_ensure_entity(const char *ref_source, const char *display_name)
{
	source_def_t *target;
	char slug[64];
	uint32_t stid;
	size_t struct_size;
	void *buf;

	if (!ref_source || !display_name || !display_name[0])
		return;
	target = source_find(ref_source);
	if (!target || !target->fields_hd)
		return;
	axil_slugify(display_name, strlen(display_name), slug, sizeof(slug));
	if (!slug[0])
		return;
	if (qmap_get(target->fields_hd, slug))
		return;
	stid = qmap_record_type_id(target->record_id);
	struct_size = stid != QM_MISS ? qmap_type_len(stid) : 0;
	buf = struct_size > 0 ? calloc(1, struct_size) : NULL;
	if (buf) {
		qmap_put(target->fields_hd, slug, buf);
		free(buf);
	}
	if (target->key_field)
		qmap_field_put(
		        target->fields_hd,
		        slug,
		        target->key_field,
		        display_name);
	if (target->source_hd)
		qmap_put(target->source_hd, slug, "");
	if (!(target->flags & SOURCE_FLAG_VOLATILE) && target->items_path &&
	    target->items_path[0])
	{
		char dir[PATH_MAX];
		char npath[PATH_MAX];
		FILE *fp;
		const char *fname =
		        target->key_field ? target->key_field : "name";

		snprintf(dir, sizeof(dir), "%s/%s", target->items_path, slug);
		mkdir(dir, 0755);
		snprintf(
		        npath,
		        sizeof(npath),
		        "%s/%s/%s",
		        target->items_path,
		        slug,
		        fname);
		fp = fopen(npath, "w");
		if (fp) {
			fwrite(display_name, 1, strlen(display_name), fp);
			fclose(fp);
		}
	}
}

static int source_scan_item(int fd, const source_def_t *def, const char *id)
{
	char doc_root[256] = { 0 };
	const char *root = resolve_doc_root(fd, doc_root, sizeof(doc_root));

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
		axil_slugify(id, strlen(id), id_norm, sizeof(id_norm));
		if (strcmp(id, id_norm) != 0)
			qmap_field_put(def->fields_hd, id, "id", id_norm);
	}

	/* Register the item in source_hd so reference fields can resolve by ID */
	if (def->source_hd)
		qmap_put(def->source_hd, id, "");

	int owner_read = 0;

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
			if (def->fields[i].type ==
			            SOURCE_FIELD_MULTI_REFERENCE &&
			    def->fields[i].target_source)
			{
				char ebuf[8192];
				snprintf(ebuf, sizeof(ebuf), "%s", data);
				char *etok, *esv;
				etok = strtok_r(ebuf, "\r\n", &esv);
				while (etok) {
					str_trim(etok);
					if (etok[0])
						source_ensure_entity(
						        def->fields[i]
						                .target_source,
						        etok);
					etok = strtok_r(NULL, "\r\n", &esv);
				}
			}
			size_t len = strlen(data);
			ref_normalize(
			        def->id, def->fields[i].name, &data, &len);
			qmap_field_put(
			        def->fields_hd, id, def->fields[i].name, data);
			if (strcmp(def->fields[i].name, "owner") == 0 && data[0])
				owner_read = 1;
		}
		free(data);
	}

	/* Fallback: if schema has an "owner" field but no owner file was
	 * written (e.g. running as root — item_record_ownership uses chown
	 * instead of writing a file), resolve from filesystem directory
	 * owner via getpwuid. */
	{
		int has_owner = 0;
		for (size_t i = 0; i < def->field_count; i++) {
			if (strcmp(def->fields[i].name, "owner") == 0) {
				has_owner = 1;
				break;
			}
		}
		if (has_owner && !owner_read) {
			struct passwd *pw = getpwuid(st.st_uid);
			if (pw && pw->pw_name)
				qmap_field_put(
				        def->fields_hd,
				        id,
				        "owner",
				        pw->pw_name);
		}
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
	const char *root = resolve_doc_root(fd, doc_root, sizeof(doc_root));

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

struct clear_inv_ctx {
	const source_def_t *def;
	uint32_t item_pos;
};

static int clear_inv_refs_cb(const source_def_t *target, void *user)
{
	struct clear_inv_ctx *ctx = user;
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

		uint32_t inv_buf[256];
		size_t count = qmap_inv_get(
		        target->fields_hd,
		        f->name,
		        ctx->item_pos,
		        inv_buf,
		        256);
		for (size_t j = 0; j < count; j++) {
			const char *ref_key =
			        qmap_get_key(target->fields_hd, inv_buf[j]);
			if (!ref_key)
				continue;
			char key[512];
			snprintf(key, sizeof(key), "%s:%s", ref_key, f->name);
			qmap_del(target->fields_hd, key);
		}
	}
	return 0;
}

NDX_LISTENER(int, source_clear_inverse_refs,
	const char *, dataset_id,
	const char *, item_id)
{
	const source_def_t *def = source_find(dataset_id);
	if (!def || !def->fields_hd)
		return 0;

	uint32_t del_pos = qmap_pos(def->fields_hd, item_id);
	if (del_pos == UINT32_MAX)
		return 0;

	struct clear_inv_ctx ctx = { .def = def, .item_pos = del_pos };
	size_t n = hyle_source_count();
	for (size_t i = 0; i < n; i++) {
		const char *sid = hyle_source_id_at(i);
		source_def_t *target;
		if (!sid)
			continue;
		target = (source_def_t *)hyle_source_get_user(sid);
		if (!target)
			continue;
		clear_inv_refs_cb(target, &ctx);
	}
	return 0;
}

static int source_scan_items(source_def_t *def)
{
	char doc_root[256] = { 0 };
	const char *root = resolve_doc_root(0, doc_root, sizeof(doc_root));

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

NDX_LISTENER(int, source_register, const source_def_t *, def)
{
	size_t n;
	size_t i;
	hyle_field_t *hf;
	source_def_t *copy;
	unsigned fields_hd;

	if (!def || !def->id || !def->id[0] || !def->key_field ||
	    !def->key_field[0] || !def->fields || def->field_count == 0 ||
	    !def->items_path)
		return -1;

	if (source_find(def->id))
		return -1;

	/* Convert source_field_t[] → hyle_field_t[] (owned by libhyle) */
	n = def->field_count;
	hf = malloc(n * sizeof(hyle_field_t));
	if (!hf)
		return -1;

	for (i = 0; i < n; i++) {
		const source_field_t *sf = &def->fields[i];
		hf[i].name = sf->name;
		hf[i].type = source_to_hyle_type(sf->type);
		hf[i].writable = sf->writable;
		hf[i].target_source = sf->target_source;
		hf[i].inverse_name = sf->inverse_name;
		hf[i].required = sf->required;
		hf[i].min = sf->min;
		hf[i].max = sf->max;
		hf[i].min_length = sf->min_length;
		hf[i].max_length = sf->max_length;
		hf[i].pattern = sf->pattern;
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
	        def->id, hf, n, def->record_id, def->flags | QM_SORTED, copy);

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
			if (target && target->source_hd)
				qmap_record_field_set_target_hd(
				        def->record_id,
				        sf->name,
				        target->source_hd);
		}
	}

	/* Scan filesystem items into the qmaps */
	if (!(def->flags & SOURCE_FLAG_VOLATILE))
		source_scan_items(copy);

	return 0;
}

static unsigned source_parse_row_data(const source_def_t *def)
{
	unsigned hd;
	size_t i;
	int ret_len;
	char val[1024];

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
			        axil_query_param(f->name, val, sizeof(val) - 1);
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
	case SOURCE_FIELD_STRING:
		return HYLE_FIELD_STRING;
	case DATASET_FIELD_INT:
		return HYLE_FIELD_INT;
	case DATASET_FIELD_BOOL:
		return HYLE_FIELD_BOOL;
	case DATASET_FIELD_NULLABLE_STRING:
		return HYLE_FIELD_NULLABLE_STRING;
	case SOURCE_FIELD_REFERENCE:
		return HYLE_FIELD_REFERENCE;
	case SOURCE_FIELD_MULTI_REFERENCE:
		return HYLE_FIELD_MULTI_REFERENCE;
	case SOURCE_FIELD_INVERSE:
		return HYLE_FIELD_INVERSE;
	default:
		return HYLE_FIELD_STRING;
	}
}

static int
source_validate_row(int fd, const source_def_t *def, unsigned data_handle)
{
	size_t n;
	hyle_field_t *hfields;
	const char **values;
	size_t i;
	int rc;
	hyle_purify_error_t *errs;
	size_t nerr;

	n = def->field_count;
	hfields = malloc(n * sizeof(hyle_field_t));
	values = malloc(n * sizeof(const char *));
	if (!hfields || !values) {
		free(hfields);
		free(values);
		return 0;
	}

	for (i = 0; i < n; i++) {
		const source_field_t *sf = &def->fields[i];
		hfields[i].name = sf->name;
		hfields[i].type = source_to_hyle_type(sf->type);
		hfields[i].writable = sf->writable;
		hfields[i].target_source = sf->target_source;
		hfields[i].inverse_name = sf->inverse_name;
		hfields[i].required = sf->required;
		hfields[i].min = sf->min;
		hfields[i].max = sf->max;
		hfields[i].min_length = sf->min_length;
		hfields[i].max_length = sf->max_length;
		hfields[i].pattern = sf->pattern;
		values[i] = qmap_get(data_handle, sf->name);
	}

	rc = hyle_purify_row(hfields, n, values, &errs, &nerr);

	free(hfields);
	free(values);

	if (rc == 0)
		return 0;

	/* Build JSON error body */
	json_object *j_errors = json_object_new_array();
	for (size_t j = 0; j < nerr; j++) {
		json_object *j_err = json_object_new_object();
		json_object_object_add(
		        j_err, "field", json_object_new_string(errs[j].field));
		json_object_object_add(
		        j_err, "rule", json_object_new_string(errs[j].rule));
		json_object_object_add(
		        j_err,
		        "message",
		        json_object_new_string(errs[j].message));
		json_object_array_add(j_errors, j_err);
	}
	json_object *j_root = json_object_new_object();
	json_object_object_add(j_root, "errors", j_errors);

	hyle_purify_errors_free(errs, nerr);

	const char *json_str = json_object_to_json_string(j_root);
	axil_header_set(fd, "Connection", "close");
	axil_header_set(fd, "Content-Type", "application/json");
	axil_respond(fd, 422, json_str);
	json_object_put(j_root);
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
	const char *root = resolve_doc_root(fd, doc_root, sizeof(doc_root));

	char item_path[PATH_MAX];
	snprintf(
	        item_path,
	        sizeof(item_path),
	        "%s/%s/%s",
	        root,
	        def->items_path,
	        id);
	if (mkdir(item_path, 0755) != 0 && errno != EEXIST)
		return -1;

	for (size_t i = 0; i < def->field_count; i++) {
		const source_field_t *f = &def->fields[i];
		const char *val = qmap_get(data_handle, f->name);

		if (val) {
			if (f->file) {
				if (write_item_child_file(
				            item_path,
				            f->file,
				            val,
				            strlen(val)) != 0)
					return -1;
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
					if (strcmp(f->name, "owner") == 0 &&
					    geteuid() == 0)
						continue;
					FILE *fp = fopen(file_path, "w");
					if (fp)
						fclose(fp);
				}
			}
		}
	}

	for (size_t i = 0; i < def->field_count; i++) {
		const source_field_t *f = &def->fields[i];
		if (f->type != SOURCE_FIELD_MULTI_REFERENCE ||
		    !f->target_source)
			continue;
		const char *val = qmap_get(data_handle, f->name);
		if (!val || !val[0])
			continue;
		char buf[8192];
		snprintf(buf, sizeof(buf), "%s", val);
		char *tok, *sv;
		tok = strtok_r(buf, "\r\n", &sv);
		while (tok) {
			str_trim(tok);
			if (tok[0])
				source_ensure_entity(f->target_source, tok);
			tok = strtok_r(NULL, "\r\n", &sv);
		}
	}

	int result = source_scan_item(fd, def, id);

	if (result == 0) {
		for (size_t i = 0; i < def->field_count; i++) {
			const source_field_t *f = &def->fields[i];
			if (f->type != SOURCE_FIELD_MULTI_REFERENCE ||
			    !f->target_source || !f->file)
				continue;
			char display[8192] = { 0 };
			if (source_resolve_ref_display_str(
			            def->id,
			            id,
			            f->name,
			            display,
			            sizeof(display)) != 0)
				continue;
			if (!display[0])
				continue;
			write_item_child_file(
			        item_path, f->file, display, strlen(display));
		}
	}

	if (result == 0) {
		for (size_t i = 0; i < def->field_count; i++) {
			const source_field_t *f = &def->fields[i];
			if (f->type != SOURCE_FIELD_REFERENCE &&
			    f->type != SOURCE_FIELD_MULTI_REFERENCE)
				continue;
			const char *val = qmap_get(data_handle, f->name);
			if (!val || !val[0])
				continue;
			const char *resolved =
			        qmap_field_get(def->fields_hd, id, f->name);
			if (resolved)
				continue;
			char msg[512];
			snprintf(
			        msg,
			        sizeof(msg),
			        "Referenced %s '%s' not found in %s",
			        f->name,
			        val,
			        f->target_source ? f->target_source
			                         : "(unknown)");
			json_object *j_root = json_object_new_object();
			json_object_object_add(
			        j_root,
			        "error",
			        json_object_new_string("Reference not found"));
			json_object *j_errors = json_object_new_array();
			json_object *j_err = json_object_new_object();
			json_object_object_add(
			        j_err,
			        "field",
			        json_object_new_string(f->name));
			json_object_object_add(
			        j_err, "message", json_object_new_string(msg));
			json_object_array_add(j_errors, j_err);
			json_object_object_add(j_root, "errors", j_errors);
			const char *json_str =
			        json_object_to_json_string(j_root);
			axil_header_set(fd, "Connection", "close");
			axil_header_set(fd, "Content-Type", "application/json");
			axil_respond(fd, 422, json_str);
			json_object_put(j_root);
			result = SOURCE_ERR_VALIDATION;
			break;
		}
	}

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
static size_t ref_reg_count = 0;

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
	char *tok;
	char *saveptr;
	tok = strtok_r(copy, "\r\n", &saveptr);
	int first = 1;

	while (tok) {
		str_trim(tok);
		if (tok[0]) {
			char id_norm[256];
			size_t rlen;
			axil_slugify(
			        tok, strlen(tok), id_norm, sizeof(id_norm));
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
int source_http_build_state_json(
        const char *dataset_id,
        const char *item_id,
        const source_state_field_t *specs,
        json_object **out);
int source_http_state_overlay(json_object *jo, const source_state_kv_t *kvs);

NDX_LISTENER(int, source_for_each, source_each_cb_t, cb, void *, user)
{
	size_t count;
	size_t i;

	if (!cb)
		return -1;
	count = hyle_source_count();
	for (i = 0; i < count; i++) {
		const char *sid = hyle_source_id_at(i);
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

NDX_LISTENER(int, source_build_state_json,
	const char *, dataset_id,
	const char *, item_id,
	const source_state_field_t *, specs,
	json_object **, out)
{
	return source_http_build_state_json(dataset_id, item_id, specs, out);
}

NDX_LISTENER(int, source_state_overlay,
	json_object *, jo,
	const source_state_kv_t *, kvs)
{
	return source_http_state_overlay(jo, kvs);
}

NDX_LISTENER(int, source_overlay_from_desc,
	json_object *, jo,
	const void *, state,
	const bud_field_desc_t *, fields,
	int, int_kind,
	int, str_kind)
{
	source_state_kv_t kvs[32];
	int n = 0;
	for (const bud_field_desc_t *f = fields; f->key && n < 31; f++) {
		if (f->kind == int_kind) {
			kvs[n].key = f->key;
			kvs[n].is_int = 1;
			kvs[n].int_val =
			        *(int *)((const char *)state + f->offset);
			kvs[n].str_val = NULL;
			n++;
		} else if (f->kind == str_kind) {
			kvs[n].key = f->key;
			kvs[n].is_int = 0;
			kvs[n].int_val = 0;
			kvs[n].str_val =
			        (const char *)((const char *)state + f->offset);
			n++;
		}
	}
	kvs[n].key = NULL;
	source_state_overlay(jo, kvs);
	return 0;
}

NDX_LISTENER(unsigned, source_query,
	const char *, dataset_id,
	const char *, query_str)
{
	hyle_query_t query;
	char *qs_copy = NULL;
	hyle_row_set_t output;
	size_t total;
	char tbuf[16];

	if (!dataset_id || !hyle_source_get_user(dataset_id))
		return 0;

	memset(&query, 0, sizeof(query));

	if (query_str && query_str[0]) {
		qs_copy = strdup(query_str);
		if (!qs_copy)
			return 0;
		hyle_parse_query(qs_copy, &query);

		const source_def_t *sdef = source_find(dataset_id);
		if (sdef) {
			for (unsigned fi = 0; fi < query.filter_count; fi++) {
				hyle_field_filter_t *f = &query.filters[fi];
				for (size_t sj = 0; sj < sdef->field_count;
				     sj++)
				{
					if (strcmp(sdef->fields[sj].name,
					           f->field) == 0 &&
					    sdef->fields[sj].type ==
					            SOURCE_FIELD_MULTI_REFERENCE)
					{
						char slug[256];
						axil_slugify(
						        f->value,
						        strlen(f->value),
						        slug,
						        sizeof(slug));
						f->value = strdup(slug);
						break;
					}
				}
			}
		}
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
	unsigned hd;
	size_t i;
	char buf[512];
	const source_field_t *f;

	hd = qmap_open(NULL, NULL, QM_STR, QM_STR, 0x3FF, 0);
	if (!hd)
		return 0;

	for (i = 0; i < def->field_count; i++) {
		f = &def->fields[i];
		if (f->type == SOURCE_FIELD_REFERENCE ||
		    f->type == SOURCE_FIELD_MULTI_REFERENCE)
		{
			snprintf(
			        buf,
			        sizeof(buf),
			        "{\"t\":%d,\"s\":\"%s\"}",
			        (int)f->type,
			        f->target_source ? f->target_source : "");
		} else if (f->type == SOURCE_FIELD_INVERSE) {
			snprintf(
			        buf,
			        sizeof(buf),
			        "{\"t\":%d,\"s\":\"%s\",\"i\":\"%s\"}",
			        (int)f->type,
			        f->target_source ? f->target_source : "",
			        f->inverse_name ? f->inverse_name : "");
		} else {
			snprintf(buf, sizeof(buf), "{\"t\":%d}", (int)f->type);
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

/* ── Unified field schema generators ─────────────────────────── */

static int
impl_source_def_to_qmap(const bud_field_desc_t *defs, int count, void *out)
{
	qmap_record_field_t *qf = (qmap_record_field_t *)out;
	int n = 0;
	int i;
	for (i = 0; i < count; i++) {
		const bud_field_desc_t *d = &defs[i];
		if (!d->key || d->kind >= 3)
			continue;
		qf[n].name = d->key;
		qf[n].type = (uint32_t)d->qm_type;
		qf[n].offset = d->offset;
		qf[n].max_size = d->size;
		qf[n].target_record = 0;
		qf[n].target_hd = 0;
		qf[n].inverse = d->ref_inverse;
		n++;
	}
	return n;
}

NDX_LISTENER(int, source_def_to_qmap,
    const bud_field_desc_t *, defs, int, count, void *, out)
{
	return impl_source_def_to_qmap(defs, count, out);
}

static int impl_source_def_to_source_fields(
        const bud_field_desc_t *defs, int count, void *out)
{
	source_field_t *sf = (source_field_t *)out;
	int n = 0;
	int i;
	for (i = 0; i < count; i++) {
		const bud_field_desc_t *d = &defs[i];
		if (d->kind == SOURCE_FIELD_KIND_INVERSE) {
			if (!d->key || !d->ref_source || !d->ref_inverse)
				continue;
			sf[n].name = d->key;
			sf[n].file = NULL;
			sf[n].type = SOURCE_FIELD_INVERSE;
			sf[n].writable = 0;
			sf[n].target_source = d->ref_source;
			sf[n].inverse_name = d->ref_inverse;
			sf[n].required = 0;
			sf[n].min = 0;
			sf[n].max = 0;
			sf[n].min_length = 0;
			sf[n].max_length = 0;
			sf[n].pattern = NULL;
			n++;
			continue;
		}
		if (!d->key || d->kind >= 3)
			continue;
		sf[n].name = d->key;
		sf[n].file = d->file ? d->file : d->key;
		sf[n].type = (source_field_type_t)d->source_type;
		sf[n].writable = d->writable;
		sf[n].target_source = d->ref_source;
		sf[n].inverse_name = d->ref_inverse;
		sf[n].required = d->required;
		sf[n].min = 0;
		sf[n].max = 0;
		sf[n].min_length = d->min_length;
		sf[n].max_length = 0;
		sf[n].pattern = NULL;
		n++;
	}
	return n;
}

NDX_LISTENER(int, source_def_to_source_fields,
    const bud_field_desc_t *, defs, int, count, void *, out)
{
	return impl_source_def_to_source_fields(defs, count, out);
}

static int impl_source_def_to_meta_fields(
        const bud_field_desc_t *defs, int count, const void *record, void *out)
{
	meta_field_t *mf = (meta_field_t *)out;
	int n = 0;
	int i;
	for (i = 0; i < count; i++) {
		const bud_field_desc_t *d = &defs[i];
		if (!d->key || d->kind >= 3 || !d->in_meta)
			continue;
		mf[n].name = d->key;
		mf[n].buf = (char *)record + d->offset;
		mf[n].sz = d->size;
		n++;
	}
	return n;
}

NDX_LISTENER(int, source_def_to_meta_fields,
    const bud_field_desc_t *, defs, int, count,
    const void *, record, void *, out)
{
	return impl_source_def_to_meta_fields(defs, count, record, out);
}

static int impl_source_build_state_specs(
        const bud_field_desc_t *fields,
        source_state_field_t *specs,
        int max_specs)
{
	int i = 0;
	for (const bud_field_desc_t *f = fields; f->key && i < max_specs - 1;
	     f++)
	{
		if (f->kind == SF_EXCLUDE || f->kind == SF_REF_DISPLAY) {
			specs[i].name = f->key;
			specs[i].kind = (source_state_kind_t)f->kind;
			i++;
		}
	}
	specs[i].name = NULL;
	specs[i].kind = 0;
	return i;
}

NDX_LISTENER(int, source_build_state_specs,
	const bud_field_desc_t *, fields,
	source_state_field_t *, specs,
	int, max_specs)
{
	return impl_source_build_state_specs(fields, specs, max_specs);
}

NDX_LISTENER(int, source_meta_read,
	const char *, path,
	const bud_field_desc_t *, fields,
	int, count,
	void *, record,
	size_t, record_size)
{
	meta_field_t f[(size_t)count];
	int n = impl_source_def_to_meta_fields(fields, count, record, f);
	memset(record, 0, record_size);
	return meta_fields_read(path, f, (size_t)n);
}

NDX_LISTENER(int, source_meta_write,
	const char *, path,
	const bud_field_desc_t *, fields,
	int, count,
	const void *, record)
{
	meta_field_t f[(size_t)count];
	int n = impl_source_def_to_meta_fields(fields, count, record, f);
	return meta_fields_write(path, f, (size_t)n);
}

static void impl_source_patch_qmap_targets(
        qmap_record_field_t *qf, int n, const bud_field_desc_t *defs, int count)
{
	int i;
	for (i = 0; i < n && i < count; i++) {
		if (defs[i].ref_source && qf[i].target_record == 0) {
			source_def_t *src = source_find(defs[i].ref_source);
			if (src)
				qf[i].target_record = src->record_id;
		}
	}
}

NDX_LISTENER(size_t, source_inv_keys,
	const char *, dataset_id,
	const char *, field,
	uint32_t, target_pos,
	const char **, keys,
	size_t, max)
{
	unsigned fhd;
	uint32_t buf[4096];
	size_t n, i, j;

	fhd = source_get_fields_hd(dataset_id);
	if (!fhd)
		return 0;

	n = qmap_inv_get(fhd, field, target_pos, buf, 4096);
	for (i = 0, j = 0; i < n && j < max; i++) {
		const char *k = qmap_get_key(fhd, buf[i]);
		if (k)
			keys[j++] = k;
	}
	return j;
}

NDX_LISTENER(const char *, source_inv_key_at,
	const char *, dataset_id,
	const char *, field,
	uint32_t, target_pos,
	size_t, index)
{
	unsigned fhd;
	uint32_t buf[4096];
	size_t n;

	fhd = source_get_fields_hd(dataset_id);
	if (!fhd)
		return NULL;

	n = qmap_inv_get(fhd, field, target_pos, buf, 4096);
	if (index >= n)
		return NULL;
	return qmap_get_key(fhd, buf[index]);
}

NDX_LISTENER(const char *, qmap_get_field_str,
	unsigned, hd,
	const char *, id,
	const char *, field)
{
	static __thread char key[512];

	snprintf(key, sizeof(key), "%s:%s", id, field);
	return qmap_get(hd, key);
}

NDX_LISTENER(uint32_t, source_setup,
	const char *, source_id,
	const char *, key_field,
	size_t, record_size,
	const char *, items_path,
	const bud_field_desc_t *, defs,
	int, field_count,
	unsigned, flags)
{
	char record_name[256];
	const char *p;
	char *q, *kf;
	qmap_record_field_t qf[(size_t)field_count];
	int n_qf, n_sf;
	uint32_t record_id;
	source_field_t *sf;

	snprintf(record_name, sizeof(record_name), "%s", source_id);
	for (p = source_id, q = record_name; *p; p++, q++) {
		if (*p == '.')
			*q = '_';
		else
			*q = *p;
	}
	*q = '\0';

	kf = (char *)(key_field ? key_field : "id");

	n_qf = impl_source_def_to_qmap(defs, field_count, qf);
	impl_source_patch_qmap_targets(qf, n_qf, defs, field_count);
	record_id = qmap_record_register(
	        record_name, record_size, qf, (size_t)n_qf);

	sf = calloc((size_t)field_count, sizeof(source_field_t));
	if (!sf)
		return 0;
	n_sf = impl_source_def_to_source_fields(defs, field_count, sf);
	source_register(&(source_def_t){
	        .id = source_id,
	        .key_field = kf,
	        .items_path = items_path,
	        .access_policy = SOURCE_ACCESS_PUBLIC,
	        .fields = sf,
	        .field_count = (size_t)n_sf,
	        .record_id = record_id,
	        .flags = flags,
	});
	return record_id;
}
