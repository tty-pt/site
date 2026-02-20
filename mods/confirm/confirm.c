#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx.h>
#include <ttypt/qmap.h>

#include "papi.h"

static uint32_t sessions_hd;

static void
rand_str(char *buf, size_t len)
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

static void
session_set(const char *token, const char *username)
{
	if (!sessions_hd) {
		sessions_hd = qmap_open(NULL, NULL, QM_STR, QM_STR, 0xFF, 0);
	}
	qmap_put(sessions_hd, token, username);
}

static void
send_error(int fd, const char *msg)
{
	ndc_writef(fd,
		"HTTP/1.1 400 Bad Request\r\n"
		"Content-Type: text/plain; charset=UTF-8\r\n"
		"\r\n"
		"%s\n",
		msg);
	ndc_close(fd);
}

static void
send_redirect(int fd, const char *location)
{
	ndc_writef(fd, "HTTP/1.1 303 See Other\r\n");
	ndc_writef(fd, "Location: %s\r\n\r\n", location);
	ndc_close(fd);
}

static void
confirm_handler(int fd, char *qs)
{
	char username[64] = { 0 };
	char rcode[64] = { 0 };
	char doc_root[256] = { 0 };

	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	get_query_param(qs, "username", username, sizeof(username));
	get_query_param(qs, "rcode", rcode, sizeof(rcode));
	url_decode(username, username, sizeof(username));
	url_decode(rcode, rcode, sizeof(rcode));

	if (!*username || !*rcode) {
		send_error(fd, "Missing username or confirmation code");
		return;
	}

	char rcode_path[512];
	snprintf(rcode_path, sizeof(rcode_path),
		"%s/users/%s/rcode", doc_root[0] ? doc_root : ".", username);

	FILE *fp = fopen(rcode_path, "r");
	if (!fp) {
		send_error(fd, "Invalid confirmation code or account already activated");
		return;
	}

	char stored_rcode[64] = { 0 };
	fgets(stored_rcode, sizeof(stored_rcode), fp);
	fclose(fp);

	size_t len = strlen(stored_rcode);
	if (len > 0 && stored_rcode[len-1] == '\n')
		stored_rcode[len-1] = '\0';

	if (strcmp(rcode, stored_rcode) != 0) {
		send_error(fd, "Invalid confirmation code");
		return;
	}

	unlink(rcode_path);

	char token[64];
	rand_str(token, sizeof(token));
	session_set(token, username);

	ndc_writef(fd, "HTTP/1.1 303 See Other\r\n");
	ndc_writef(fd, "Set-Cookie: QSESSION=%s; SameSite=Lax\r\n", token);
	ndc_writef(fd, "Location: /\r\n\r\n");
	ndc_close(fd);
}

static void
confirm_get(int fd, char *body)
{
	(void)body;
	char qs[512] = { 0 };
	ndc_env_get(fd, qs, "QUERY_STRING");
	confirm_handler(fd, qs);
}

MODULE_API void
ndx_install(void)
{
	sessions_hd = qmap_open(NULL, NULL, QM_STR, QM_STR, 0xFF, 0);
	ndc_register_handler("/confirm", confirm_get);
}

MODULE_API void
ndx_open(void)
{
	ndc_register_handler("/confirm", confirm_get);
}

MODULE_API ndx_t *
get_ndx_ptr(void)
{
	static ndx_t ndx;
	return &ndx;
}
