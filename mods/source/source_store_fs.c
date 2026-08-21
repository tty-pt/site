#include "source_store.h"
#include "source.h"
#include "source_internal.h"
#include "../common/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <ttypt/axil.h>
#include <ttypt/qmap.h>
#include "hyle/hyle.h"
#include "hyle/source.h"

/* Filesystem adapter — owns opendir/mkdir/slurp/write/delete for item
 * sources (COMPLY.md §9.4). Generic source stays FS-free and delegates
 * here via def->store.ops. Ordered sources already use DSV hyle
 * callbacks; this adapter covers var/ directory item sources. */

static int fs_load(source_store_t *store, const source_def_t *def,
                   const char *id, unsigned *row_out)
{
	(void)store;
	if (!def || !id)
		return -1;
	if (!is_safe_id(id))
		return -1;
	const char *items_path = def->items_path
	        ? def->items_path
	        : (const char *)store->user;
	if (!items_path || !items_path[0])
		return -1;
	char doc_root[256] = { 0 };
	const char *root = resolve_doc_root(0, doc_root, sizeof(doc_root));
	char item_path[PATH_MAX];
	snprintf(item_path, sizeof(item_path), "%s/%s/%s", root,
	        items_path, id);
	struct stat st;
	if (lstat(item_path, &st) != 0 || !S_ISDIR(st.st_mode))
		return -1;
	const char *names[64];
	const char *values[64];
	size_t k = 0;
	char *bufs[64];
	size_t nb = 0;
	char id_norm[256];
	axil_slugify(id, strnlen(id, sizeof(id_norm)), id_norm, sizeof(id_norm));
	names[k] = "id";
	values[k] = strcmp(id, id_norm) != 0 ? id_norm : id;
	k++;
	for (size_t i = 0; i < def->field_count && k < 64; i++) {
		const source_field_t *f = &def->fields[i];
		if (strcmp(f->name, "id") == 0)
			continue;
		if (!f->file)
			continue;
		char file_path[PATH_MAX + 256];
		snprintf(file_path, sizeof(file_path), "%s/%s", item_path,
		        f->file);
		char *data = slurp_file(file_path);
		if (data) {
			source_internal_process_multi_ref(f, def->id, &data);
			names[k] = f->name;
			values[k] = data;
			k++;
			bufs[nb++] = data;
		}
	}
	int rc = hyle_source_put(def->id, id, names, values, k);
	for (size_t i = 0; i < nb; i++)
		free(bufs[i]);
	if (row_out)
		*row_out = 0;
	return rc;
}

static int fs_scan(source_store_t *store, const source_def_t *def)
{
	const char *items_path = def->items_path
	        ? def->items_path
	        : (const char *)store->user;
	if (!items_path || !items_path[0])
		return 0;
	char doc_root[256] = { 0 };
	const char *root = resolve_doc_root(0, doc_root, sizeof(doc_root));
	char full[PATH_MAX];
	snprintf(full, sizeof(full), "%s/%s", root, items_path);
	DIR *dir = opendir(full);
	if (!dir)
		return 0;
	struct dirent *entry;
	while ((entry = readdir(dir))) {
		if (entry->d_name[0] == '.')
			continue;
		unsigned row_unused = 0;
		fs_load(store, def, entry->d_name, &row_unused);
	}
	closedir(dir);
	return 0;
}

static int fs_put(source_store_t *store, const source_def_t *def,
                  const char *id, unsigned data_handle)
{
	(void)store;
	if (!def || !id)
		return -1;
	if (!is_safe_id(id))
		return -1;
	const char *items_path = def->items_path
	        ? def->items_path
	        : (const char *)store->user;
	if (!items_path || !items_path[0])
		return -1;
	char doc_root[256] = { 0 };
	const char *root = resolve_doc_root(0, doc_root, sizeof(doc_root));
	char item_path[PATH_MAX];
	snprintf(item_path, sizeof(item_path), "%s/%s/%s", root,
	        items_path, id);
	if (mkdir(item_path, 0755) != 0 && errno != EEXIST)
		return -1;
	for (size_t i = 0; i < def->field_count; i++) {
		const source_field_t *f = &def->fields[i];
		if (strcmp(f->name, "owner") == 0)
			continue;
		if (!f->file)
			continue;
		const char *val = qmap_get(data_handle, f->name);
		if (val) {
			if (write_item_child_file(item_path, f->file, val,
			                          strlen(val)) != 0)
				return -1;
		} else {
			char file_path[PATH_MAX + 256];
			snprintf(file_path, sizeof(file_path), "%s/%s",
			        item_path, f->file);
			char *content = slurp_file(file_path);
			if (content) {
				free(content);
			} else if (f->type !=
			           SOURCE_FIELD_MULTI_REFERENCE) {
				FILE *fp = fopen(file_path, "w");
				if (fp)
					fclose(fp);
			}
		}
	}
	return 0;
}

static int fs_put_field(source_store_t *store, const source_def_t *def,
                        const char *id, const char *field,
                        const char *value)
{
	(void)def;
	const char *items_path = def->items_path
	        ? def->items_path
	        : (const char *)store->user;
	if (!items_path || !items_path[0] || !id || !field)
		return -1;
	if (!is_safe_id(id))
		return -1;
	char doc_root[256] = { 0 };
	const char *root = resolve_doc_root(0, doc_root, sizeof(doc_root));
	char dir[PATH_MAX];
	snprintf(dir, sizeof(dir), "%s/%s/%s", root, items_path, id);
	size_t vlen = value ? strlen(value) : 0;
	return write_item_child_file(dir, field, value ? value : "",
	                             vlen);
}

static int fs_del(source_store_t *store, const source_def_t *def,
                  const char *id)
{
	(void)store;
	const char *items_path = def->items_path
	        ? def->items_path
	        : (const char *)store->user;
	if (!items_path || !items_path[0] || !id)
		return -1;
	if (!is_safe_id(id))
		return -1;
	char doc_root[256] = { 0 };
	const char *root = resolve_doc_root(0, doc_root, sizeof(doc_root));
	char item_path[PATH_MAX];
	snprintf(item_path, sizeof(item_path), "%s/%s/%s", root,
	        items_path, id);
	item_remove_path_recursive(item_path);
	return 0;
}

static const source_store_ops_t fs_ops = {
	.scan = fs_scan,
	.load = fs_load,
	.put = fs_put,
	.put_field = fs_put_field,
	.del = fs_del,
};

const source_store_ops_t *source_store_fs_ops(void) { return &fs_ops; }

source_store_t source_store_fs(const char *items_path)
{
	source_store_t s = { &fs_ops, (void *)items_path };
	return s;
}

/* Volatile store — no filesystem I/O. */
static int mem_scan(source_store_t *s, const source_def_t *d) { (void)s; (void)d; return 0; }
static int mem_load(source_store_t *s, const source_def_t *d, const char *i, unsigned *r) { (void)s; (void)d; (void)i; (void)r; return 0; }
static int mem_put(source_store_t *s, const source_def_t *d, const char *i, unsigned h) { (void)s; (void)d; (void)i; (void)h; return 0; }
static int mem_put_field(source_store_t *s, const source_def_t *d, const char *i, const char *f, const char *v) { (void)s; (void)d; (void)i; (void)f; (void)v; return 0; }
static int mem_del(source_store_t *s, const source_def_t *d, const char *i) { (void)s; (void)d; (void)i; return 0; }

static const source_store_ops_t mem_ops = {
	.scan = mem_scan,
	.load = mem_load,
	.put = mem_put,
	.put_field = mem_put_field,
	.del = mem_del,
};

const source_store_ops_t *source_store_mem_ops(void) { return &mem_ops; }
source_store_t source_store_mem(void) { source_store_t s = { &mem_ops, NULL }; return s; }
