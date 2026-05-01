#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx-mod.h>

#include "common_internal.h"

static int remove_path_recursive(const char *path);

NDX_LISTENER(int, read_meta_file,
	const char *, item_path, const char *, name,
	char *, buf, size_t, sz)
{
	char p[PATH_MAX];
	FILE *mfp;

	snprintf(p, sizeof(p), "%s/%s", item_path, name);
	mfp = fopen(p, "r");
	if (!mfp)
		return -1;
	if (fgets(buf, (int)sz - 1, mfp)) {
		size_t l = strlen(buf);
		if (l > 0 && buf[l - 1] == '\n')
			buf[l - 1] = '\0';
	}
	fclose(mfp);
	return 0;
}

NDX_LISTENER(int, write_meta_file,
	const char *, item_path, const char *, name,
	const char *, buf, size_t, sz)
{
	char p[PATH_MAX];

	snprintf(p, sizeof(p), "%s/%s", item_path, name);
	return write_file_path(p, buf, sz);
}

NDX_LISTENER(int, meta_fields_read,
	const char *, item_path, meta_field_t *, fields, size_t, count)
{
	if (!item_path || !fields)
		return -1;

	for (size_t i = 0; i < count; i++) {
		if (!fields[i].name || !fields[i].buf || fields[i].sz == 0)
			continue;
		fields[i].buf[0] = '\0';
		read_meta_file(item_path, fields[i].name, fields[i].buf,
			fields[i].sz);
	}

	return 0;
}

NDX_LISTENER(int, meta_fields_write,
	const char *, item_path, const meta_field_t *, fields, size_t, count)
{
	if (!item_path || !fields)
		return -1;

	for (size_t i = 0; i < count; i++) {
		if (!fields[i].name || !fields[i].buf)
			continue;
		if (write_meta_file(item_path, fields[i].name, fields[i].buf,
				strlen(fields[i].buf)) != 0)
			return -1;
	}

	return 0;
}

NDX_LISTENER(int, write_file_path,
	const char *, path, const char *, buf, size_t, sz)
{
	FILE *fp = fopen(path, "w");

	if (!fp)
		return -1;
	if (sz > 0 && fwrite(buf, 1, sz, fp) != sz) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	return 0;
}

NDX_LISTENER(int, ensure_dir_path, const char *, path)
{
	if (mkdir(path, 0755) == 0 || errno == EEXIST)
		return 0;
	return -1;
}

NDX_LISTENER(int, user_path_build,
	const char *, username, const char *, suffix,
	char *, out, size_t, outlen)
{
	if (!username || !username[0] || !suffix || !suffix[0] ||
			!out || outlen == 0)
		return -1;
	snprintf(out, outlen, "./home/%s/%s", username, suffix);
	return 0;
}

NDX_LISTENER(int, write_item_child_file,
	const char *, item_path, const char *, name,
	const char *, buf, size_t, sz)
{
	char p[PATH_MAX];

	if (item_child_path(item_path, name, p, sizeof(p)) != 0)
		return -1;
	return write_file_path(p, buf, sz);
}

NDX_LISTENER(char *, slurp_file, const char *, path)
{
	FILE *fp = fopen(path, "r");
	long fsize;
	char *buf;
	size_t got;

	if (!fp)
		return NULL;
	fseek(fp, 0, SEEK_END);
	fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (fsize <= 0) {
		fclose(fp);
		return strdup("");
	}
	buf = malloc((size_t)fsize + 1);
	if (!buf) {
		fclose(fp);
		return NULL;
	}
	got = fread(buf, 1, (size_t)fsize, fp);
	fclose(fp);
	buf[got] = '\0';
	return buf;
}

NDX_LISTENER(char *, slurp_item_child_file,
	const char *, item_path, const char *, name)
{
	char path[PATH_MAX];

	if (item_child_path(item_path, name, path, sizeof(path)) != 0)
		return NULL;
	return slurp_file(path);
}

NDX_LISTENER(int, get_doc_root, int, fd, char *, buf, size_t, len)
{
	buf[0] = '\0';
	ndc_env_get(fd, buf, "DOCUMENT_ROOT");
	if (!buf[0])
		snprintf(buf, len, ".");
	return 0;
}

NDX_LISTENER(int, item_dir_exists, const char *, item_path)
{
	struct stat st;

	return stat(item_path, &st) == 0 && S_ISDIR(st.st_mode);
}

NDX_LISTENER(int, item_child_path,
	const char *, item_path, const char *, name,
	char *, out, size_t, outlen)
{
	int n = snprintf(out, outlen, "%s/%s", item_path, name);

	if (n < 0 || (size_t)n >= outlen)
		return -1;
	return 0;
}

static int
remove_path_recursive(const char *path)
{
	struct stat st;
	DIR *dir;
	struct dirent *entry;
	int rc = 0;

	if (lstat(path, &st) != 0)
		return -1;
	if (!S_ISDIR(st.st_mode))
		return unlink(path);

	dir = opendir(path);
	if (!dir)
		return -1;

	while ((entry = readdir(dir)) != NULL) {
		char child[PATH_MAX];

		if (strcmp(entry->d_name, ".") == 0 ||
				strcmp(entry->d_name, "..") == 0)
			continue;
		if (snprintf(child, sizeof(child), "%s/%s",
				path, entry->d_name) >= (int)sizeof(child)) {
			rc = -1;
			break;
		}
		if (remove_path_recursive(child) != 0) {
			rc = -1;
			break;
		}
	}
	closedir(dir);

	if (rc != 0)
		return -1;
	return rmdir(path);
}

NDX_LISTENER(int, item_remove_path_recursive, const char *, item_path)
{
	if (!item_path || !item_path[0])
		return -1;
	return remove_path_recursive(item_path);
}

NDX_LISTENER(int, module_path_build,
	const char *, doc_root, const char *, module,
	char *, out, size_t, outlen)
{
	const char *root = (doc_root && doc_root[0]) ? doc_root : ".";
	int n = snprintf(out, outlen, "%s/items/%s", root, module);

	if (n < 0 || (size_t)n >= outlen)
		return -1;
	return 0;
}

NDX_LISTENER(int, module_items_path_build,
	const char *, doc_root, const char *, module,
	char *, out, size_t, outlen)
{
	const char *root = (doc_root && doc_root[0]) ? doc_root : ".";
	int n = snprintf(out, outlen, "%s/items/%s/items", root, module);

	if (n < 0 || (size_t)n >= outlen)
		return -1;
	return 0;
}

NDX_LISTENER(int, item_path_build_root,
	const char *, doc_root, const char *, module, const char *, id,
	char *, out, size_t, outlen)
{
	const char *root = (doc_root && doc_root[0]) ? doc_root : ".";
	int n = snprintf(out, outlen, "%s/items/%s/items/%s",
		root, module, id);

	if (n < 0 || (size_t)n >= outlen)
		return -1;
	return 0;
}

NDX_LISTENER(int, item_path_build,
	int, fd, const char *, module, const char *, id,
	char *, out, size_t, outlen)
{
	char doc_root[256] = {0};

	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	return item_path_build_root(doc_root, module, id, out, outlen);
}

NDX_LISTENER(int, datalist_extract_id,
	const char *, in, char *, id_out, size_t, outlen)
{
	const char *lb;
	const char *rb;
	size_t n;

	if (!in || !id_out || outlen == 0)
		return -1;

	lb = strrchr(in, '[');
	rb = strrchr(in, ']');
	if (!lb || !rb || rb <= lb + 1) {
		if (in != id_out)
			snprintf(id_out, outlen, "%s", in);
		return -1;
	}

	n = (size_t)(rb - lb - 1);
	if (n >= outlen)
		n = outlen - 1;
	memmove(id_out, lb + 1, n);
	id_out[n] = '\0';
	return 0;
}

NDX_LISTENER(int, index_field_clean, char *, s)
{
	for (; s && *s; s++)
		if (*s == '\t' || *s == '\n' || *s == '\r')
			*s = ' ';
	return 0;
}
