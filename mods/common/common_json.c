#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/ndx-mod.h>

#include "common_internal.h"

static int ja_reserve(json_array_t *ja, size_t extra)
{
	size_t need = ja->len + extra + 1;
	size_t nc;
	char *nb;

	if (need <= ja->cap)
		return 0;
	nc = ja->cap ? ja->cap : 1024;
	while (nc < need)
		nc *= 2;
	nb = realloc(ja->buf, nc);
	if (!nb)
		return -1;
	ja->buf = nb;
	ja->cap = nc;
	return 0;
}

static int ja_append_str(json_array_t *ja, const char *s, size_t n)
{
	if (ja_reserve(ja, n) != 0)
		return -1;
	memcpy(ja->buf + ja->len, s, n);
	ja->len += n;
	ja->buf[ja->len] = '\0';
	return 0;
}

static int jo_reserve(json_object_t *jo, size_t extra)
{
	size_t need = jo->len + extra + 1;
	size_t nc;
	char *nb;

	if (need <= jo->cap)
		return 0;
	nc = jo->cap ? jo->cap : 1024;
	while (nc < need)
		nc *= 2;
	nb = realloc(jo->buf, nc);
	if (!nb)
		return -1;
	jo->buf = nb;
	jo->cap = nc;
	return 0;
}

static int jo_append_str(json_object_t *jo, const char *s, size_t n)
{
	if (jo_reserve(jo, n) != 0)
		return -1;
	memcpy(jo->buf + jo->len, s, n);
	jo->len += n;
	jo->buf[jo->len] = '\0';
	return 0;
}

static int ja_field_sep(json_array_t *ja)
{
	if (ja->len > 0 && ja->buf[ja->len - 1] != '{')
		return ja_append_str(ja, ",", 1);
	return 0;
}

static int jo_field_sep(json_object_t *jo)
{
	if (jo->len > 0 && jo->buf[jo->len - 1] != '{')
		return jo_append_str(jo, ",", 1);
	return 0;
}

NDX_LISTENER(json_array_t *, json_array_new, int, dummy)
{
	json_array_t *ja;

	(void)dummy;
	ja = calloc(1, sizeof(*ja));
	if (!ja)
		return NULL;
	ja->cap = 1024;
	ja->buf = malloc(ja->cap);
	if (!ja->buf) {
		free(ja);
		return NULL;
	}
	ja->buf[0] = '[';
	ja->buf[1] = '\0';
	ja->len = 1;
	ja->first = 1;
	return ja;
}

NDX_LISTENER(int, json_array_append_raw,
	json_array_t *, ja, const char *, s)
{
	if (!ja)
		return -1;
	if (!ja->first && ja_append_str(ja, ",", 1) != 0)
		return -1;
	ja->first = 0;
	return ja_append_str(ja, s, strlen(s));
}

NDX_LISTENER(int, json_array_begin_object, json_array_t *, ja)
{
	if (!ja)
		return -1;
	if (!ja->first && ja_append_str(ja, ",", 1) != 0)
		return -1;
	ja->first = 0;
	return ja_append_str(ja, "{", 1);
}

NDX_LISTENER(int, json_array_end_object, json_array_t *, ja)
{
	if (!ja)
		return -1;
	return ja_append_str(ja, "}", 1);
}

NDX_LISTENER(int, json_array_kv_str,
	json_array_t *, ja, const char *, key, const char *, value)
{
	const char *v = value ? value : "";
	size_t esc_cap;
	char *esc;
	int rc;

	if (!ja || !key)
		return -1;

	esc_cap = strlen(v) * 6 + 2;
	esc = malloc(esc_cap);
	if (!esc)
		return -1;
	json_escape(v, esc, esc_cap);

	rc = ja_field_sep(ja);
	if (rc == 0 && ja_reserve(ja, strlen(key) + strlen(esc) + 6) == 0) {
		ja->len += snprintf(ja->buf + ja->len, ja->cap - ja->len,
			"\"%s\":\"%s\"", key, esc);
	} else {
		rc = -1;
	}
	free(esc);
	return rc;
}

NDX_LISTENER(int, json_array_kv_int,
	json_array_t *, ja, const char *, key, int, value)
{
	if (!ja || !key)
		return -1;
	if (ja_field_sep(ja) != 0 || ja_reserve(ja, strlen(key) + 32) != 0)
		return -1;
	ja->len += snprintf(ja->buf + ja->len, ja->cap - ja->len,
		"\"%s\":%d", key, value);
	return 0;
}

NDX_LISTENER(int, json_array_kv_bool,
	json_array_t *, ja, const char *, key, int, value)
{
	if (!ja || !key)
		return -1;
	if (ja_field_sep(ja) != 0 || ja_reserve(ja, strlen(key) + 10) != 0)
		return -1;
	ja->len += snprintf(ja->buf + ja->len, ja->cap - ja->len,
		"\"%s\":%s", key, value ? "true" : "false");
	return 0;
}

NDX_LISTENER(char *, json_array_finish, json_array_t *, ja)
{
	char *out;

	if (!ja)
		return NULL;
	if (ja_append_str(ja, "]", 1) != 0) {
		free(ja->buf);
		free(ja);
		return NULL;
	}
	out = ja->buf;
	free(ja);
	return out;
}

NDX_LISTENER(json_object_t *, json_object_new, int, dummy)
{
	json_object_t *jo;

	(void)dummy;
	jo = calloc(1, sizeof(*jo));
	if (!jo)
		return NULL;
	jo->cap = 1024;
	jo->buf = malloc(jo->cap);
	if (!jo->buf) {
		free(jo);
		return NULL;
	}
	jo->buf[0] = '{';
	jo->buf[1] = '\0';
	jo->len = 1;
	jo->first = 1;
	return jo;
}

NDX_LISTENER(int, json_object_kv_str,
	json_object_t *, jo, const char *, key, const char *, value)
{
	const char *v = value ? value : "";
	size_t esc_cap;
	char *esc;
	int rc;

	if (!jo || !key)
		return -1;

	esc_cap = strlen(v) * 6 + 2;
	esc = malloc(esc_cap);
	if (!esc)
		return -1;
	json_escape(v, esc, esc_cap);

	rc = jo_field_sep(jo);
	if (rc == 0 && jo_reserve(jo, strlen(key) + strlen(esc) + 6) == 0) {
		jo->len += snprintf(jo->buf + jo->len, jo->cap - jo->len,
			"\"%s\":\"%s\"", key, esc);
	} else {
		rc = -1;
	}
	free(esc);
	return rc;
}

NDX_LISTENER(int, json_object_kv_int,
	json_object_t *, jo, const char *, key, int, value)
{
	if (!jo || !key)
		return -1;
	if (jo_field_sep(jo) != 0 || jo_reserve(jo, strlen(key) + 32) != 0)
		return -1;
	jo->len += snprintf(jo->buf + jo->len, jo->cap - jo->len,
		"\"%s\":%d", key, value);
	return 0;
}

NDX_LISTENER(int, json_object_kv_bool,
	json_object_t *, jo, const char *, key, int, value)
{
	if (!jo || !key)
		return -1;
	if (jo_field_sep(jo) != 0 || jo_reserve(jo, strlen(key) + 10) != 0)
		return -1;
	jo->len += snprintf(jo->buf + jo->len, jo->cap - jo->len,
		"\"%s\":%s", key, value ? "true" : "false");
	return 0;
}

NDX_LISTENER(int, json_object_kv_raw,
	json_object_t *, jo, const char *, key, const char *, value)
{
	if (!jo || !key || !value)
		return -1;
	if (jo_field_sep(jo) != 0 ||
			jo_reserve(jo, strlen(key) + strlen(value) + 4) != 0)
		return -1;
	jo->len += snprintf(jo->buf + jo->len, jo->cap - jo->len,
		"\"%s\":%s", key, value);
	return 0;
}

NDX_LISTENER(char *, json_object_finish, json_object_t *, jo)
{
	char *out;

	if (!jo)
		return NULL;
	if (jo_append_str(jo, "}", 1) != 0) {
		free(jo->buf);
		free(jo);
		return NULL;
	}
	out = jo->buf;
	free(jo);
	return out;
}

NDX_LISTENER(int, json_object_free, json_object_t *, jo)
{
	if (!jo)
		return 0;
	free(jo->buf);
	free(jo);
	return 0;
}

int
json_object_append_fragment(json_object_t *jo, const char *fragment, size_t len)
{
	if (!jo || !fragment)
		return -1;
	if (jo_field_sep(jo) != 0)
		return -1;
	return jo_append_str(jo, fragment, len);
}
