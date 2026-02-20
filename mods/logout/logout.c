#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx.h>
#include <ttypt/qmap.h>

#include "papi.h"

static uint32_t sessions_hd;

static void
get_cookie_token(const char *cookie, char *token, size_t len)
{
	token[0] = '\0';
	if (!cookie || !*cookie)
		return;
	const char *p = cookie;
	while (*p) {
		while (*p == ' ' || *p == '\t') p++;
		if (!strncmp(p, "QSESSION=", 9)) {
			p += 9;
			const char *amp = strchr(p, '&');
			size_t tlen = amp ? (amp - p) : strlen(p);
			if (tlen >= len) tlen = len - 1;
			strncpy(token, p, tlen);
			token[tlen] = '\0';
			return;
		}
		while (*p && *p != '&') p++;
		if (*p == '&') p++;
	}
}

static void
session_delete(const char *token)
{
	if (!sessions_hd) {
		sessions_hd = qmap_open(NULL, NULL, QM_STR, QM_STR, 0xFF, 0);
	}
	qmap_del(sessions_hd, token);

	char path[256];
	snprintf(path, sizeof(path), "./sessions/%s", token);
	unlink(path);
}

static void
logout_handler(int fd, char *body)
{
	(void) body;
	char cookie[256] = { 0 };
	char qs[256] = { 0 };
	char ret[256] = { 0 };

	ndc_env_get(fd, cookie, "HTTP_COOKIE");
	ndc_env_get(fd, qs, "QUERY_STRING");

	const char *p = qs;
	while (*p) {
		while (*p == '&') p++;
		if (!strncmp(p, "ret=", 4)) {
			p += 4;
			size_t len = strlen(p);
			if (len >= sizeof(ret)) len = sizeof(ret) - 1;
			strncpy(ret, p, len);
			ret[len] = '\0';
			break;
		}
		while (*p && *p != '&') p++;
	}

	char token[128] = { 0 };
	get_cookie_token(cookie, token, sizeof(token));

	if (*token) {
		session_delete(token);
	}

	const char *redirect = *ret ? ret : "/";

	ndc_writef(fd, "HTTP/1.1 303 See Other\r\n");
	ndc_writef(fd, "Set-Cookie: QSESSION=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT\r\n");
	ndc_writef(fd, "Location: %s\r\n\r\n", redirect);
	ndc_close(fd);
}

MODULE_API void
ndx_install(void)
{
	sessions_hd = qmap_open(NULL, NULL, QM_STR, QM_STR, 0xFF, 0);
	ndc_register_handler("/logout", logout_handler);
}

MODULE_API void
ndx_open(void)
{
	ndc_register_handler("/logout", logout_handler);
}

MODULE_API ndx_t *
get_ndx_ptr(void)
{
	static ndx_t ndx;
	return &ndx;
}
