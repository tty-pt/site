#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <crypt.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx.h>
#include <ttypt/qmap.h>

#include "papi.h"
#include "auth.h"

ndx_t ndx;

uint32_t auth_sessions_hd;

#define MAX_COOKIE_LEN 128

static void
rand_token(char *buf, size_t len)
{
	snprintf(buf, len, "%llx.%llx.%llx", 
		(unsigned long long)time(NULL),
		(unsigned long long)rand(),
		(unsigned long long)getpid());
}

static void
get_query_param(char *qs, const char *key, char *out, size_t outlen)
{
	out[0] = '\0';
	if (!qs || !*qs || !key)
		return;
	size_t keylen = strlen(key);
	char *p = qs;
	while (*p) {
		while (*p == '&') p++;
		if (!strncmp(p, key, keylen) && p[keylen] == '=') {
			char *amp = strchr(p, '&');
			size_t len;
			if (amp)
				len = amp - p - keylen - 1;
			else
				len = strlen(p) - keylen - 1;
			if (len >= outlen) len = outlen - 1;
			strncpy(out, p + keylen + 1, len);
			out[len] = '\0';
			return;
		}
		while (*p && *p != '&') p++;
	}
}

static void
url_decode(const char *src, char *dst, size_t dstlen)
{
	size_t j = 0;
	while (*src && j < dstlen - 1) {
		if (*src == '%' && src[1] && src[2]) {
			int c;
			sscanf(src + 1, "%2x", &c);
			dst[j++] = c;
			src += 3;
		} else if (*src == '+') {
			dst[j++] = ' ';
			src++;
		} else {
			dst[j++] = *src++;
		}
	}
	dst[j] = '\0';
}

static int
verify_password(const char *password, const char *hash)
{
	if (!password || !hash || !*password || !*hash)
		return 0;
	char *result = crypt(password, hash);
	if (!result)
		return 0;
	return strncmp(result, hash, 60) == 0;
}

static int
find_hash_in_file(const char *filename, const char *username, char *hash_out, size_t hash_size)
{
	int fd = open(filename, O_RDONLY);
	if (fd < 0)
		return 0;

	struct stat sb;
	if (fstat(fd, &sb) == -1 || sb.st_size == 0) {
		close(fd);
		return 0;
	}

	char *mapped = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (mapped == MAP_FAILED) {
		close(fd);
		return 0;
	}

	char copy[sb.st_size + 1];
	memcpy(copy, mapped, sb.st_size);
	copy[sb.st_size] = '\0';
	munmap(mapped, sb.st_size);
	close(fd);

	char *p = copy;
	size_t userlen = strlen(username);
	int found = 0;

	while (*p) {
		char *colon = strchr(p, ':');
		if (!colon)
			break;
		if (colon - p == (int)userlen && strncmp(p, username, userlen) == 0) {
			char *eol = strchr(colon + 1, '\n');
			size_t hashlen = eol ? (size_t)(eol - colon - 1) : strlen(colon + 1);
			if (hashlen >= hash_size)
				hashlen = hash_size - 1;
			strncpy(hash_out, colon + 1, hashlen);
			hash_out[hashlen] = '\0';
			found = 1;
			break;
		}
		p = strchr(p, '\n');
		if (!p)
			break;
		p++;
	}

	return found;
}

static void
auth_init_internal(void)
{
	static int initialized;
	if (initialized)
		return;
	initialized = 1;

	auth_sessions_hd = qmap_open(NULL, NULL, QM_STR, QM_STR, 0xFF, 0);
}

MODULE_API void
auth_init(void)
{
	auth_init_internal();
}

MODULE_API const char *
auth_session_get(const char *token)
{
	auth_init_internal();
	if (!token || !*token)
		return NULL;

	static char user[64];
	char path[256];
	snprintf(path, sizeof(path), "./sessions/%s", token);
	FILE *fp = fopen(path, "r");
	if (!fp)
		return NULL;
	if (fgets(user, sizeof(user), fp)) {
		size_t len = strlen(user);
		if (len > 0 && user[len-1] == '\n')
			user[len-1] = '\0';
		fclose(fp);
		return user;
	}
	fclose(fp);
	return NULL;
}

MODULE_API int
auth_session_set(const char *token, const char *username)
{
	auth_init_internal();
	if (!token || !*token || !username || !*username)
		return -1;
	return qmap_put(auth_sessions_hd, token, username);
}

MODULE_API void
auth_session_delete(const char *token)
{
	auth_init_internal();
	if (!token || !*token)
		return;
	qmap_del(auth_sessions_hd, token);
}

static void
session_handler(int fd, char *body)
{
	(void) body;
	char cookie[256] = { 0 };
	ndc_env_get(fd, cookie, "HTTP_COOKIE");

	char *token = NULL;
	char *p = cookie;
	while (*p) {
		while (*p == ' ' || *p == '\t') p++;
		if (!strncmp(p, "QSESSION=", 9)) {
			token = p + 9;
			char *amp = strchr(token, '&');
			if (amp) *amp = '\0';
			break;
		}
		while (*p && *p != '&') p++;
		if (*p == '&') p++;
	}

	const char *user = NULL;
	if (token && *token) {
		user = auth_session_get(token);
	}

	if (user) {
		ndc_writef(fd, "HTTP/1.1 200 OK\r\n");
		ndc_writef(fd, "Content-Type: text/plain\r\n\r\n");
		ndc_writef(fd, "%s", user);
	} else {
		ndc_writef(fd, "HTTP/1.1 200 OK\r\n");
		ndc_writef(fd, "Content-Type: text/plain\r\n\r\n");
	}
	ndc_close(fd);
}

static void
send_redirect(int fd, const char *location)
{
	ndc_writef(fd, "HTTP/1.1 303 See Other\r\n");
	ndc_writef(fd, "Location: %s\r\n", location);
	ndc_writef(fd, "\r\n");
	ndc_close(fd);
}

static void
authenticate(int fd, char *body, char *doc_root)
{
	char username[64] = { 0 };
	char password[64] = { 0 };
	char ret[256] = { 0 };

	get_query_param(body, "username", username, sizeof(username));
	get_query_param(body, "password", password, sizeof(password));
	get_query_param(body, "ret", ret, sizeof(ret));

	url_decode(username, username, sizeof(username));
	url_decode(password, password, sizeof(password));
	url_decode(ret, ret, sizeof(ret));

	if (!*username || !*password) {
		ndc_writef(fd,
			"HTTP/1.1 400 Bad Request\r\n"
			"Content-Type: text/plain\r\n\r\n"
			"Missing username or password");
		ndc_close(fd);
		return;
	}

	char htpasswd_path[512];
	snprintf(htpasswd_path, sizeof(htpasswd_path),
		"%s/etc/shadow", doc_root[0] ? doc_root : ".");

	char found_hash[128] = { 0 };
	int found = find_hash_in_file(htpasswd_path, username, found_hash, sizeof(found_hash));

	if (!found) {
		ndc_writef(fd,
			"HTTP/1.1 400 Bad Request\r\n"
			"Content-Type: text/plain\r\n\r\n"
			"No such user");
		ndc_close(fd);
		return;
	}

	size_t hash_len = strlen(found_hash);
	if (hash_len > 0 && found_hash[hash_len - 1] == '\n')
		found_hash[hash_len - 1] = '\0';

	if (!verify_password(password, found_hash)) {
		ndc_writef(fd,
			"HTTP/1.1 401 Unauthorized\r\n"
			"Content-Type: text/plain\r\n\r\n"
			"Invalid password");
		ndc_close(fd);
		return;
	}

	char user_dir[512];
	snprintf(user_dir, sizeof(user_dir),
		"%s/users/%s", doc_root[0] ? doc_root : ".", username);
	struct stat st;
	if (stat(user_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
		ndc_writef(fd,
			"HTTP/1.1 400 Bad Request\r\n"
			"Content-Type: text/plain\r\n\r\n"
			"Account not activated");
		ndc_close(fd);
		return;
	}

	char token[MAX_COOKIE_LEN];
	rand_token(token, sizeof(token));

	auth_session_set(token, username);

	char session_path[512];
	snprintf(session_path, sizeof(session_path), "./sessions/%s", token);
	FILE *sfp = fopen(session_path, "w");
	if (sfp) {
		fprintf(sfp, "%s", username);
		fclose(sfp);
	}

	char redirect[300];
	if (*ret) {
		snprintf(redirect, sizeof(redirect), "%s", ret);
	} else {
		snprintf(redirect, sizeof(redirect), "/");
	}

	ndc_writef(fd, "HTTP/1.1 303 See Other\r\n");
	ndc_writef(fd, "Set-Cookie: QSESSION=%s; SameSite=Lax\r\n", token);
	ndc_writef(fd, "Location: %s\r\n\r\n", redirect);
	ndc_close(fd);
}

static void
login_handler(int fd, char *body)
{
	char method[16] = { 0 };
	char qs[512] = { 0 };
	char doc_root[256] = { 0 };

	ndc_env_get(fd, method, "REQUEST_METHOD");
	ndc_env_get(fd, qs, "QUERY_STRING");
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");

	if (!strcmp(method, "POST")) {
		authenticate(fd, body, doc_root);
		return;
	}
}

MODULE_API void
ndx_install(void)
{
	auth_init_internal();
	ndc_register_handler("/api/session", session_handler);
	ndc_register_handler("POST:/login", login_handler);
}

MODULE_API void
ndx_open(void)
{
	auth_init_internal();
	ndc_register_handler("/api/session", session_handler);
}

MODULE_API ndx_t *
get_ndx_ptr(void)
{
	return &ndx;
}
