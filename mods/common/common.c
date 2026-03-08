/* #include "common.h" */
#include <ttypt/ndx-mod.h>

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <openssl/evp.h>

#include <ttypt/ndc.h>
#include <ttypt/qmap.h>

static uint32_t query_db;

NDX_DEF(int, get_cookie, const char *, cookie, char *, token, size_t, len) {
	const char *p;

	token[0] = '\0';

	if (!cookie || !*cookie)
		return -1;

	p = cookie;

	while (*p) {
		while (*p == ' ' || *p == '\t')
			p++;

		if (!strncmp(p, "QSESSION=", 9)) {
			p += 9;
			const char *amp = strchr(p, '&');
			size_t tlen = amp ? (amp - p) : strlen(p);
			if (tlen >= len) tlen = len - 1;
			strncpy(token, p, tlen);
			token[tlen] = '\0';
			return 0;
		}

		while (*p && *p != '&')
			p++;

		if (*p == '&')
			p++;
	}
	
	return 0;
}

NDX_DEF(int, json_escape, const char *, in, char *, out, size_t, outlen) {
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
	return 0;
}

NDX_DEF(int, url_encode, const char *, in, char *, out, size_t, outlen) {
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
	return 0;
}

NDX_DEF(int, b64_encode, const char *, in, char *, out, size_t, outlen) {
	EVP_EncodeBlock(out, in, strlen(in));
	return 0;
}

NDX_DEF(int, query_parse, char *, query) {
	if (!query_db) return -1;
	
	qmap_drop(query_db);
	query_db = qmap_open(NULL, NULL, QM_STR, QM_STR, 0xFF, 0);
	if (!query_db) return -1;
	
	if (!query || !*query) return 0;
	
	char *query_copy = strdup(query);
	if (!query_copy) return -1;
	
	char *saveptr;
	char *param = strtok_r(query_copy, "&", &saveptr);
	
	while (param) {
		char *eq = strchr(param, '=');
		if (eq) {
			*eq = '\0';
			char *key = param;
			char *value = eq + 1;
			
			size_t val_len = strlen(value);
			char *decoded = malloc(val_len + 1);
			if (decoded) {
				size_t j = 0;
				for (size_t i = 0; value[i] && j < val_len; i++) {
					if (value[i] == '+') {
						decoded[j++] = ' ';
					} else if (value[i] == '%' && value[i+1] && value[i+2]) {
						int c;
						sscanf(value + i + 1, "%2x", &c);
						decoded[j++] = c;
						i += 2;
					} else {
						decoded[j++] = value[i];
					}
				}
				decoded[j] = '\0';
				qmap_put(query_db, key, decoded);
				free(decoded);
			}
		}
		param = strtok_r(NULL, "&", &saveptr);
	}
	
	free(query_copy);
	return 0;
}

NDX_DEF(int, query_exists, const char *, name) {
	if (!query_db) return 0;
	return qmap_get(query_db, name) != NULL ? 1 : 0;
}

NDX_DEF(int, query_param, const char *, name, char *, buf, size_t, buf_len) {
	const char *val;
	size_t len;

	if (!query_db || !buf || !buf_len)
		return -1;

	buf[0] = '\0';
	
	val = (const char *) qmap_get(query_db, name);

	if (!val)
		return -1;
	
	len = strlen(val);

	if (len >= buf_len)
		len = buf_len - 1;

	memcpy(buf, val, len);
	buf[len] = '\0';
	return len;
}

MODULE_API void
ndx_install(void)
{
	query_db = qmap_open(NULL, NULL, QM_STR, QM_STR, 0xFF, 0);
}

MODULE_API void
ndx_open(void)
{
}
