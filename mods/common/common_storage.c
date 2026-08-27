#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ttypt/axil.h>
#include <ttypt/xy-mod.h>

#include "common_internal.h"

static int remove_path_recursive(const char *path);

XY_IMPL(int, is_safe_id, const char *, id)
{
	const char *p;
	if (!id || !id[0])
		return 0;
	for (p = id; *p; p++) {
		char c = *p;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || c == '_' || c == '-')
			continue;
		return 0;
	}
	return 1;
}

XY_IMPL(int, read_meta_file,
	const char *, item_path,
	const char *, name,
	char *, buf,
	size_t, sz)
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

XY_IMPL(int, write_meta_file,
	const char *, item_path,
	const char *, name,
	const char *, buf,
	size_t, sz)
{
	char p[PATH_MAX];

	snprintf(p, sizeof(p), "%s/%s", item_path, name);
	return write_file_path(p, buf, sz);
}

XY_IMPL(int, meta_fields_read,
	const char *, item_path,
	meta_field_t *, fields,
	size_t, count)
{
	if (!item_path || !fields)
		return -1;

	for (size_t i = 0; i < count; i++) {
		if (!fields[i].name || !fields[i].buf || fields[i].sz == 0)
			continue;
		fields[i].buf[0] = '\0';
		read_meta_file(
		        item_path, fields[i].name, fields[i].buf, fields[i].sz);
	}

	return 0;
}

XY_IMPL(int, meta_fields_write,
	const char *, item_path,
	const meta_field_t *, fields,
	size_t, count)
{
	if (!item_path || !fields)
		return -1;

	for (size_t i = 0; i < count; i++) {
		if (!fields[i].name || !fields[i].buf)
			continue;
		if (write_meta_file(
		            item_path, fields[i].name, fields[i].buf,
		            strlen(fields[i].buf)) != 0)
			return -1;
	}

	return 0;
}

XY_IMPL(int, write_file_path,
	const char *, path,
	const char *, buf,
	size_t, sz)
{
	char tmp_path[PATH_MAX];
	int fd;
	ssize_t written;

	if (!path || !path[0])
		return -1;

	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", path, (int)getpid());

	fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
	if (fd < 0)
		return -1;

	if (sz > 0) {
		written = write(fd, buf, sz);
		if (written < 0 || (size_t)written != sz) {
			close(fd);
			unlink(tmp_path);
			return -1;
		}
	}

	if (fsync(fd) != 0) {
		close(fd);
		unlink(tmp_path);
		return -1;
	}

	if (close(fd) != 0) {
		unlink(tmp_path);
		return -1;
	}

	if (rename(tmp_path, path) != 0) {
		unlink(tmp_path);
		return -1;
	}

	return 0;
}

XY_IMPL(int, ensure_dir_path, const char *, path)
{
	if (mkdir(path, 0755) == 0 || errno == EEXIST)
		return 0;
	return -1;
}

XY_IMPL(int, user_path_build,
	const char *, username,
	const char *, suffix,
	char *, out,
	size_t, outlen)
{
	if (!username || !username[0] || !suffix || !suffix[0] || !out ||
	    outlen == 0)
		return -1;
	if (username[0] == '/' || username[0] == '\\' || username[0] == '.')
		return -1;
	if (strcmp(username, ".") == 0 || strcmp(username, "..") == 0)
		return -1;
	snprintf(out, outlen, "./home/%s/%s", username, suffix);
	return 0;
}

XY_IMPL(int, user_pref_read,
	const char *, username,
	const char *, name,
	char *, out,
	size_t, out_sz)
{
	char s[PATH_MAX], p[PATH_MAX];
	char *c;

	if (!username || !username[0] || !name || !name[0] || !out || out_sz == 0)
		return -1;

	snprintf(s, sizeof(s), ".tty/%s", name);
	if (user_path_build(username, s, p, sizeof(p)) != 0)
		return -1;

	c = slurp_file(p);
	if (!c) {
		out[0] = '\0';
		return -1;
	}
	snprintf(out, out_sz, "%s", c);
	free(c);
	return 0;
}

XY_IMPL(int, user_pref_write,
	const char *, username,
	const char *, name,
	const char *, val)
{
	char d[PATH_MAX], s[PATH_MAX], p[PATH_MAX];

	if (!username || !username[0] || !name || !name[0])
		return -1;

	user_path_build(username, ".tty", d, sizeof(d));
	ensure_dir_path(d);

	snprintf(s, sizeof(s), ".tty/%s", name);
	user_path_build(username, s, p, sizeof(p));
	return write_file_path(p, val, val ? strlen(val) : 0);
}

XY_IMPL(int, write_item_child_file,
	const char *, item_path,
	const char *, name,
	const char *, buf,
	size_t, sz)
{
	char p[PATH_MAX];

	if (item_child_path(item_path, name, p, sizeof(p)) != 0)
		return -1;
	return write_file_path(p, buf, sz);
}

#define SLURP_MAX (10 * 1024 * 1024)

XY_IMPL(char *, slurp_file, const char *, path)
{
	FILE *fp = fopen(path, "r");
	struct stat st;
	char *buf;
	size_t got;

	if (!fp)
		return NULL;
	if (fstat(fileno(fp), &st) != 0 || !S_ISREG(st.st_mode)) {
		fclose(fp);
		return strdup("");
	}
	if (st.st_size <= 0 || (size_t)st.st_size > SLURP_MAX) {
		fclose(fp);
		return (st.st_size <= 0) ? strdup("") : NULL;
	}
	buf = malloc((size_t)st.st_size + 1);
	if (!buf) {
		fclose(fp);
		return NULL;
	}
	got = fread(buf, 1, (size_t)st.st_size, fp);
	fclose(fp);
	buf[got] = '\0';
	return buf;
}

XY_IMPL(int, get_doc_root, int, fd, char *, buf, size_t, len)
{
	if (fd > 0 && axil_env_get(fd, buf, len, "DOCUMENT_ROOT") == 0 &&
	    buf[0])
		return 0;

	snprintf(buf, len, ".");
	return 0;
}

XY_IMPL(const char *, resolve_doc_root, int, fd, char *, buf, size_t, len)
{
	get_doc_root(fd, buf, len);
	if (fd == 0 && (!buf[0] || buf[0] == '.'))
		return ".";
	return buf;
}

XY_IMPL(int, item_child_path,
	const char *, item_path,
	const char *, name,
	char *, out,
	size_t, outlen)
{
	int n = snprintf(out, outlen, "%s/%s", item_path, name);

	if (n < 0 || (size_t)n >= outlen)
		return -1;
	return 0;
}

static int remove_path_recursive(const char *path)
{
	struct stat st;
	DIR *dir;
	struct dirent *entry;
	int rc = 0;

	if (lstat(path, &st) != 0) {
		return -1;
	}
	if (!S_ISDIR(st.st_mode)) {
		return unlink(path);
	}

	dir = opendir(path);
	if (!dir) {
		return -1;
	}

	while ((entry = readdir(dir)) != NULL) {
		char child[PATH_MAX];

		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;
		if (snprintf(
		            child, sizeof(child), "%s/%s", path,
		            entry->d_name) >= (int)sizeof(child))
		{
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

static int is_safe_var_subpath(const char *path)
{
	char resolved[PATH_MAX];
	char var_root[PATH_MAX];

	if (!path || !path[0])
		return 0;

	if (!realpath(path, resolved))
		return 0;

	if (!realpath("var", var_root) && !realpath("./var", var_root)) {
		/* Fallback if var doesn't resolve directly */
		return (strstr(resolved, "/var/") != NULL);
	}

	size_t vlen = strlen(var_root);
	if (strncmp(resolved, var_root, vlen) != 0)
		return 0;

	if (resolved[vlen] != '/' && resolved[vlen] != '\0')
		return 0;

	return 1;
}

XY_IMPL(int, item_remove_path_recursive, const char *, item_path)
{
	if (!item_path || !item_path[0])
		return -1;
	if (!is_safe_var_subpath(item_path))
		return -1;
	return remove_path_recursive(item_path);
}

XY_IMPL(int, module_path_build,
	const char *, doc_root,
	const char *, module,
	char *, out,
	size_t, outlen)
{
	const char *root = (doc_root && doc_root[0]) ? doc_root : ".";
	int n;

	if (!out || outlen == 0 || !is_safe_id(module))
		return -1;
	n = snprintf(out, outlen, "%s/var/%s", root, module);

	if (n < 0 || (size_t)n >= outlen)
		return -1;
	return 0;
}

XY_IMPL(int, module_items_path_build,
	const char *, doc_root,
	const char *, module,
	char *, out,
	size_t, outlen)
{
	const char *root = (doc_root && doc_root[0]) ? doc_root : ".";
	int n;

	if (!out || outlen == 0 || !is_safe_id(module))
		return -1;
	n = snprintf(out, outlen, "%s/var/%s", root, module);

	if (n < 0 || (size_t)n >= outlen)
		return -1;
	return 0;
}

XY_IMPL(int, item_path_build_root,
	const char *, doc_root,
	const char *, module,
	const char *, id,
	char *, out,
	size_t, outlen)
{
	const char *root;
	int n;

	if (!out || outlen == 0 || !is_safe_id(module) || !is_safe_id(id))
		return -1;
	root = (doc_root && doc_root[0]) ? doc_root : ".";
	n = snprintf(out, outlen, "%s/var/%s/%s", root, module, id);

	if (n < 0 || (size_t)n >= outlen)
		return -1;
	return 0;
}

XY_IMPL(int, item_path_build,
	int, fd,
	const char *, module,
	const char *, id,
	char *, out,
	size_t, outlen)
{
	char doc_root[256] = { 0 };

	get_doc_root(fd, doc_root, sizeof(doc_root));
	return item_path_build_root(doc_root, module, id, out, outlen);
}

XY_IMPL(int, datalist_extract_id,
	const char *, in,
	char *, id_out,
	size_t, outlen)
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

XY_IMPL(int, build_owner_path,
	const char *, ip,
	char *, out,
	size_t, len)
{
	int n;

	if (!ip || !ip[0] || !out || len == 0)
		return -1;
	n = snprintf(out, len, "%s/owner", ip);
	if (n < 0 || (size_t)n >= len) {
		out[0] = '\0';
		return -1;
	}
	return 0;
}
