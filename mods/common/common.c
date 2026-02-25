#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include <ttypt/ndc.h>
#include <ttypt/ndx.h>
#include <ttypt/qmap.h>

#include "common.h"

struct ndx_ctx { int x; };
static struct ndx_ctx ndx;

void
get_cookie(const char *cookie, char *token, size_t len)
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

void
query_param(char *query, const char *key, char *out, size_t out_len)
{
	if (!out || !out_len)
		return;
	out[0] = 0;
	if (!query)
		return;

	size_t key_len = strlen(key);
	for (char *p = query; *p; ) {
		while (*p == '&')
			p++;
		if (!strncmp(p, key, key_len) && p[key_len] == '=') {
			char *val = p + key_len + 1;
			size_t n = 0;
			while (val[n] && val[n] != '&')
				n++;
			if (n >= out_len)
				n = out_len - 1;
			memcpy(out, val, n);
			out[n] = 0;

			size_t j = 0;
			for (size_t i = 0; out[i]; i++) {
				if (out[i] == '+') {
					out[j++] = ' ';
				} else if (out[i] == '%' && out[i+1] && out[i+2]) {
					int c;
					sscanf(out + i + 1, "%2x", &c);
					out[j++] = c;
					i += 2;
				} else {
					out[j++] = out[i];
				}
			}
			out[j] = 0;
			return;
		}
		while (*p && *p != '&')
			p++;
	}
}

void
json_escape(const char *in, char *out, size_t outlen)
{
	size_t j = 0;
	for (size_t i = 0; in[i] && j + 2 < outlen; i++) {
		unsigned char c = (unsigned char)in[i];
		if (c == '"' || c == '\\') {
			if (j + 2 >= outlen) break;
			out[j++] = '\\';
			out[j++] = c;
		} else if (c == '\n') {
			if (j + 2 >= outlen) break;
			out[j++] = '\\';
			out[j++] = 'n';
		} else if (c == '\r') {
			if (j + 2 >= outlen) break;
			out[j++] = '\\';
			out[j++] = 'r';
		} else if (c == '\t') {
			if (j + 2 >= outlen) break;
			out[j++] = '\\';
			out[j++] = 't';
		} else if (c < 0x20) {
			if (j + 6 >= outlen) break;
			j += snprintf(out + j, outlen - j, "\\u%04x", c);
		} else {
			out[j++] = c;
		}
	}
	out[j] = '\0';
}

void
url_encode(const char *in, char *out, size_t outlen)
{
	size_t j = 0;
	for (size_t i = 0; in[i] && j + 4 < outlen; i++) {
		unsigned char c = (unsigned char)in[i];
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out[j++] = c;
		} else {
			j += snprintf(out + j, outlen - j, "%%%02X", c);
		}
	}
	out[j] = '\0';
}

MODULE_API void
ndx_install(void)
{
}

MODULE_API void
ndx_open(void)
{
}

struct ndx_ctx;

MODULE_API struct ndx_ctx *
get_ndx_ptr(void)
{
	return &ndx;
}
