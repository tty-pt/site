#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <iconv.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ttypt/qmap.h>
#include <ttypt/ndx-mod.h>
#include <ttypt/ndc.h>

#include "./../common/common.h"
#include "./../proxy/proxy.h"
#include "./../mpfd/mpfd.h"
#include "./../auth/auth.h"

#define MAX_MODULES 64

static int index_add_get_handler(int fd, char *body);

static char modules_header[2 * 256 * MAX_MODULES];

static char modules_json[256 * MAX_MODULES],
	    *modules_json_end = modules_json;

static size_t modules_rem = sizeof(modules_json),
	      modules_count = 0;

static unsigned module_hd;
static iconv_t cd;

NDX_DEF(int, index_id,
		char *, result,
		size_t, result_len,
		const char *, title,
		size_t, title_len)
{
	size_t i, j;
	char *o = result;

	iconv(cd, (char **) &title, &title_len,
			&result, &result_len);

	for (i = 0; i < result_len; i++) {
		register char c = *o;
		if (c == ' ') {
			*o = '_';
			o++;
		} else if (c >= 'A' && c <= 'Z') {
			*o = *o + 32;
			o++;
		} else if ((c >= 'a' && c <= 'z')
				|| (c >= '0' && c <= '9'))
			o++;
	}
	*o = '\0';
	return 0;
}

int index_update_json(
		const char * id,
		const char * title,
		unsigned flags)
{
	long offset;
	char module_json[256];

	offset = snprintf(modules_json_end, modules_rem, "%c{"
			"\"id\":\"%s\","
			"\"title\":\"%s\","
			"\"flags\":\"%u\"}",
			(modules_count ? ',' : '['),
			id, title, flags);

	if (offset < 0)
		return -1;

	modules_json_end += offset;
	modules_rem -= offset;
	modules_json_end[0] = ']';
	modules_json_end[1] = '\0';

	memset(modules_header, 0, sizeof(modules_header));
	call_b64_encode(modules_json,
			modules_header,
			sizeof(modules_header));

	modules_count++;

	return 0;
}

static const char *index_name(int fd)
{
	static char uri[256];
	char *module;

	ndc_env_get(fd, uri, "DOCUMENT_URI");
	module = strchr(uri + 1, '/');
	if (module)
		*module = '\0';
	module = uri + 1;
	return module;
}

static int index_add_handler(
		int fd,
		char *body)
{
	char title[256], id[256], path[1024], uri[256];
	int parse_result, title_len;
	const char *module;
	size_t path_len;
	unsigned hd;
	FILE *tfp;

	/* Require authenticated session */
	char cookie[256] = {0};
	char token[64] = {0};
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	const char *username = call_get_session_user(token);
	if (!username || !*username) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 401);
		ndc_body(fd, "Login required");
		return 401;
	}

	parse_result = call_mpfd_parse(fd, body);
	if (parse_result == -1) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 415);
		ndc_body(fd, "Expected multipart/form-data");
		return 415;
	}

	title_len = call_mpfd_get("title",
			title, sizeof(title) - 1);

	if (title_len <= 0) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 400);
		ndc_body(fd, "Missing title");
		return 400;
	}

	index_id(id, sizeof(id), title, title_len);
	module = index_name(fd);

	path_len = snprintf(path, sizeof(path),
			"./items/%s/items/%s", module, id);

	int r = mkdir(path, 0755);

	if (r == -1 && errno != EEXIST) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "You don't have permissions for that");
		return 403;
	}

	/* Record ownership */
	call_item_record_ownership(path, username);

	snprintf(path + path_len,
			sizeof(path) - path_len,
			"/title");

	tfp = fopen(path, "w");
	if (!tfp) {
		ndc_header(fd, "Content-Type", "text/plain");
		ndc_head(fd, 403);
		ndc_body(fd, "You don't have permissions for that");
		return 403;
	}

	fwrite(title, 1, strlen(title), tfp);
	fclose(tfp);

	hd = *(unsigned *) qmap_get(module_hd, module);
	qmap_put(hd, id, title);

	path_len = snprintf(path, sizeof(path),
			"/%s/%s", module, id);

	ndc_header(fd, "Location", path);
	ndc_header(fd, "Connection", "close");
	ndc_set_flags(fd, DF_TO_CLOSE);
	ndc_head(fd, 303);
	ndc_close(fd);
	return 0;
}

NDX_DEF(int, index_page,
		unsigned, fd,
		unsigned, hd,
		char *, path,
		char *, title)
{
	register size_t total = 0;
	unsigned cur = qmap_iter(hd, NULL, 0);
	const void *key, *val;
	char *body, *s;
	int ret;

	// count body size
	while (qmap_next(&key, &val, cur)) {
		register size_t klen, vlen;
		klen = qmap_len(QM_STR, key);
		vlen = qmap_len(QM_STR, val);
		total += klen + vlen + 3;
	}

	// alloc it
	total++;
	body = malloc(total);
	s = body;

	// store it
	cur = qmap_iter(hd, NULL, 0);
	while (qmap_next(&key, &val, cur)) {
		s += sprintf(s, "%s %s\r\n",
				(char *) key,
				(char *) val);
	}

	// send it
	*s = '\0';
	call_proxy_init("POST", path);
	call_proxy_header("X-Modules", modules_header);
	{
		char cookie[256] = { 0 };
		char token[64] = { 0 };
		ndc_env_get(fd, cookie, "HTTP_COOKIE");
		call_get_cookie(cookie, token, sizeof(token));
		const char *username = call_get_session_user(token);
		if (username && *username)
			call_proxy_header("X-Remote-User", (char *)username);
	}
	ret = call_proxy_body(fd, body, total);
	free(body);
	return ret;
}

static int index_list_handler(
		int fd,
		char *body)
{
	char title[256], id[256], path[1024], uri[256];
	int parse_result, title_len;
	size_t path_len;
	unsigned hd;
	const char *module;
	FILE *tfp;

	module = index_name(fd);

	hd = *(unsigned *) qmap_get(module_hd, module);

	ndc_env_get(fd, path, "DOCUMENT_URI");

	return index_page(fd, hd, path,
			(char *) module);
}

NDX_DEF(unsigned, index_open,
		const char *, name,
		unsigned, mask,
		unsigned, flags)
{
	unsigned hd = qmap_open(NULL, "hd", QM_STR, QM_STR,
			mask ? mask : 0x3FF, QM_SORTED);

	struct dirent *entry;
	char buf[PATH_MAX / 2];
	char id[256] = { 0 };
	DIR *dir;

	index_id(id, sizeof(id), name, strlen(name));
	index_update_json(id, name, flags);

	if (!(flags & 1))
		return 0;

	snprintf(buf, sizeof(buf), "./items/%s", id);
	mkdir(buf, 0755);
	snprintf(buf, sizeof(buf), "./items/%s/items", id);
	mkdir(buf, 0755);

	dir = opendir(buf);
	if (!dir) {
		perror("opendir");
		return QM_MISS;
	}

	while ((entry = readdir(dir)) != NULL) {
		char title_path[PATH_MAX], title[256];
		FILE *f;

		snprintf(title_path, sizeof(title_path),
				"%s/%s/title", buf,
				entry->d_name);

		f = fopen(title_path, "r");
		if (!f)
			continue;

		if (fgets(title, sizeof(title), f)) {
			title[strcspn(title, "\n")] = 0;
			qmap_put(hd, entry->d_name, title);
		}

		fclose(f);
	}

	closedir(dir);

	snprintf(buf, sizeof(buf), "POST:/%s/add", id);
	ndc_register_handler(buf, index_add_handler);

	snprintf(buf, sizeof(buf), "GET:/%s/add", id);
	ndc_register_handler(buf, index_add_get_handler);

	snprintf(buf, sizeof(buf), "GET:/%s", id);
	ndc_register_handler(buf, index_list_handler);

	qmap_put(module_hd, id, &hd);
	return hd;
}

NDX_DEF(unsigned, index_put,
		unsigned, hd,
		char *, key,
		char *, value)
{
	return qmap_put(hd, key, value);
}

NDX_DEF(int, index_del,
		unsigned, hd,
		char *, key)
{
	qmap_del(hd, key);
	return 0;
}

NDX_DEF(unsigned, index_get,
		unsigned, hd,
		char *, value,
		size_t, len,
		char *, key)
{
	const void *val = qmap_get(hd, key);
	size_t rlen = qmap_len(QM_STR, val);

	if (rlen > len)
		return 1;

	memcpy(value, val, rlen);
	return 0;
}

NDX_DEF(int, core_get,
		int, fd,
		char *, body)
{
	(void)body;

	char path[512] = { 0 };
	char cookie[256] = { 0 };
	char token[64] = { 0 };
	char host[256] = { 0 };

	ndc_env_get(fd, path, "DOCUMENT_URI");
	call_proxy_init("GET", path);
	call_proxy_header("X-Modules", modules_header);
	ndc_env_get(fd, host, "HTTP_HOST");
	if (host[0])
		call_proxy_header("X-Forwarded-Host", host);
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	{
		const char *username = call_get_session_user(token);
		if (username && *username)
			call_proxy_header("X-Remote-User", (char *)username);
	}
	return call_proxy_head(fd);
}

NDX_DEF(int, core_post,
		int, fd,
		char *, body,
		size_t, len)
{
	(void)body;

	char uri[512] = { 0 };
	char param[512] = { 0 };
	char full_path[PATH_MAX] = { 0 };
	char cookie[256] = { 0 };
	char token[64] = { 0 };
	char host[256] = { 0 };

	ndc_env_get(fd, uri, "DOCUMENT_URI");
	ndc_env_get(fd, param, "QUERY_STRING");
	snprintf(full_path, sizeof(full_path), "%s?%s", uri, param);
	call_proxy_init("POST", full_path);
	call_proxy_header("X-Modules", modules_header);
	ndc_env_get(fd, host, "HTTP_HOST");
	if (host[0])
		call_proxy_header("X-Forwarded-Host", host);
	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	call_get_cookie(cookie, token, sizeof(token));
	{
		const char *username = call_get_session_user(token);
		if (username && *username)
			call_proxy_header("X-Remote-User", (char *)username);
	}
	return call_proxy_body(fd, body, len);
}

static int index_add_get_handler(
		int fd,
		char *body)
{
	return call_core_get(fd, body);
}

void ndx_install(void)
{
	ndx_load("./mods/common/common");
	ndx_load("./mods/mpfd/mpfd");
	ndx_load("./mods/proxy/proxy");

	module_hd = qmap_open(NULL, NULL,
			QM_STR, QM_U32, 0x1FF, 0);

	cd = iconv_open("ASCII//TRANSLIT", "UTF-8");
	ndc_config.default_handler = core_get;
}

void ndx_open(void) {}
