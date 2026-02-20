#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <crypt.h>
#include <errno.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx.h>
#include <ttypt/qmap.h>

#include "papi.h"

#define MAX_COOKIE_LEN 64

static void
rand_token(char *buf, size_t len)
{
	FILE *fp = fopen("/dev/urandom", "r");
	if (!fp) {
		snprintf(buf, len, "%lld%lld", 
			(long long)time(NULL), (long long)rand());
		return;
	}
	size_t r = fread(buf, 1, len - 1, fp);
	buf[r] = '\0';
	fclose(fp);
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
session_set(const char *token, const char *username, char *doc_root)
{
	(void)doc_root;
	char path[256];
	snprintf(path, sizeof(path), "./sessions/%s", token);
	FILE *fp = fopen(path, "w");
	if (!fp)
		return -1;
	fprintf(fp, "%s", username);
	fclose(fp);
	return 0;
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

	FILE *fp = fopen(htpasswd_path, "r");
	char line[256];
	char found_hash[128] = { 0 };
	int found = 0;

	if (fp) {
		while (fgets(line, sizeof(line), fp)) {
			char *colon = strchr(line, ':');
			if (colon && !strncmp(line, username, colon - line)) {
				strncpy(found_hash, colon + 1, sizeof(found_hash) - 1);
				found = 1;
				break;
			}
		}
		fclose(fp);
	}

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

	int valid = verify_password(password, found_hash);

	if (!valid) {
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

	session_set(token, username, doc_root);

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
	char doc_root[256] = { 0 };

	ndc_env_get(fd, method, "REQUEST_METHOD");
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");

	if (!strcmp(method, "POST")) {
		authenticate(fd, body, doc_root);
		return;
	}

	ndc_writef(fd,
		"HTTP/1.1 405 Method Not Allowed\r\n"
		"Content-Type: text/plain\r\n\r\n"
		"Method not allowed");
	ndc_close(fd);
}

MODULE_API void
ndx_install(void)
{
	ndc_register_handler("POST:/login", login_handler);
}

MODULE_API void
ndx_open(void)
{
}

MODULE_API ndx_t *
get_ndx_ptr(void)
{
	static ndx_t ndx;
	return &ndx;
}
