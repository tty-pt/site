#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/ndx-mod.h>

#include "common_internal.h"

/* ---------------------------------------------------------------------------
 * Private generic buffer — identical layout to json_array_t, json_object_t,
 * and form_body_t.  All three public types are cast to this for the shared
 * helpers below; the cast is safe because the struct fields are identical.
 * ------------------------------------------------------------------------- */
typedef struct {
	char *buf;
	size_t len;
	size_t cap;
	int first;
} json_buf_t;

static int jb_reserve(json_buf_t *jb, size_t extra) {
	size_t need = jb->len + extra + 1;
	size_t nc;
	char *nb;

	if (need <= jb->cap)
		return 0;
	nc = jb->cap ? jb->cap : 1024;
	while (nc < need)
		nc *= 2;
	nb = realloc(jb->buf, nc);
	if (!nb)
		return -1;
	jb->buf = nb;
	jb->cap = nc;
	return 0;
}

static int jb_append_str(json_buf_t *jb, const char *s, size_t n) {
	if (jb_reserve(jb, n) != 0)
		return -1;
	memcpy(jb->buf + jb->len, s, n);
	jb->len += n;
	jb->buf[jb->len] = '\0';
	return 0;
}

static int jb_field_sep(json_buf_t *jb) {
	if (jb->len > 0 && jb->buf[jb->len - 1] != '{')
		return jb_append_str(jb, ",", 1);
	return 0;
}

/* Convenience casts */
#define JB(p) ((json_buf_t *)(p))

/* ---------------------------------------------------------------------------
 * Private kv helpers — shared by both json_array_t and json_object_t variants
 * ------------------------------------------------------------------------- */

static int
jb_kv_str(json_buf_t *jb, const char *key, const char *value) {
	const char *v = value ? value : "";
	size_t esc_cap;
	char *esc;
	int rc;

	if (!jb || !key)
		return -1;

	esc_cap = strlen(v) * 6 + 2;
	esc = malloc(esc_cap);
	if (!esc)
		return -1;
	json_escape(v, esc, esc_cap);

	rc = jb_field_sep(jb);
	if (rc == 0 && jb_reserve(jb, strlen(key) + strlen(esc) + 6) == 0)
		jb->len += snprintf(jb->buf + jb->len, jb->cap - jb->len,
		                    "\"%s\":\"%s\"", key, esc);
	else
		rc = -1;
	free(esc);
	return rc;
}

static int
jb_kv_int(json_buf_t *jb, const char *key, int value) {
	if (!jb || !key)
		return -1;
	if (jb_field_sep(jb) != 0 || jb_reserve(jb, strlen(key) + 32) != 0)
		return -1;
	jb->len += snprintf(jb->buf + jb->len, jb->cap - jb->len,
	                    "\"%s\":%d", key, value);
	return 0;
}

static int
jb_kv_bool(json_buf_t *jb, const char *key, int value) {
	if (!jb || !key)
		return -1;
	if (jb_field_sep(jb) != 0 || jb_reserve(jb, strlen(key) + 10) != 0)
		return -1;
	jb->len += snprintf(jb->buf + jb->len, jb->cap - jb->len,
	                    "\"%s\":%s", key, value ? "true" : "false");
	return 0;
}

/* ---------------------------------------------------------------------------
 * json_array_t
 * ------------------------------------------------------------------------- */

NDX_LISTENER(json_array_t *, json_array_new, int, dummy) {
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
             json_array_t *, ja, const char *, s) {
	if (!ja)
		return -1;
	if (!ja->first && jb_append_str(JB(ja), ",", 1) != 0)
		return -1;
	ja->first = 0;
	return jb_append_str(JB(ja), s, strlen(s));
}

NDX_LISTENER(int, json_array_begin_object, json_array_t *, ja) {
	if (!ja)
		return -1;
	if (!ja->first && jb_append_str(JB(ja), ",", 1) != 0)
		return -1;
	ja->first = 0;
	return jb_append_str(JB(ja), "{", 1);
}

NDX_LISTENER(int, json_array_end_object, json_array_t *, ja) {
	if (!ja)
		return -1;
	return jb_append_str(JB(ja), "}", 1);
}

NDX_LISTENER(int, json_array_kv_str,
             json_array_t *, ja, const char *, key, const char *, value) {
	return jb_kv_str(JB(ja), key, value);
}

NDX_LISTENER(int, json_array_kv_int,
             json_array_t *, ja, const char *, key, int, value) {
	return jb_kv_int(JB(ja), key, value);
}

NDX_LISTENER(int, json_array_kv_bool,
             json_array_t *, ja, const char *, key, int, value) {
	return jb_kv_bool(JB(ja), key, value);
}

NDX_LISTENER(char *, json_array_finish, json_array_t *, ja) {
	char *out;

	if (!ja)
		return NULL;
	if (jb_append_str(JB(ja), "]", 1) != 0) {
		free(ja->buf);
		free(ja);
		return NULL;
	}
	out = ja->buf;
	free(ja);
	return out;
}

/* ---------------------------------------------------------------------------
 * json_object_t
 * ------------------------------------------------------------------------- */

NDX_LISTENER(json_object_t *, json_object_new, int, dummy) {
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
             json_object_t *, jo, const char *, key, const char *, value) {
	return jb_kv_str(JB(jo), key, value);
}

NDX_LISTENER(int, json_object_kv_int,
             json_object_t *, jo, const char *, key, int, value) {
	return jb_kv_int(JB(jo), key, value);
}

NDX_LISTENER(int, json_object_kv_bool,
             json_object_t *, jo, const char *, key, int, value) {
	return jb_kv_bool(JB(jo), key, value);
}

NDX_LISTENER(int, json_object_kv_raw,
             json_object_t *, jo, const char *, key, const char *, value) {
	if (!jo || !key || !value)
		return -1;
	if (jb_field_sep(JB(jo)) != 0 ||
	    jb_reserve(JB(jo), strlen(key) + strlen(value) + 4) != 0)
		return -1;
	jo->len += snprintf(jo->buf + jo->len, jo->cap - jo->len,
	                    "\"%s\":%s", key, value);
	return 0;
}

NDX_LISTENER(char *, json_object_finish, json_object_t *, jo) {
	char *out;

	if (!jo)
		return NULL;
	if (jb_append_str(JB(jo), "}", 1) != 0) {
		free(jo->buf);
		free(jo);
		return NULL;
	}
	out = jo->buf;
	free(jo);
	return out;
}

NDX_LISTENER(int, json_object_free, json_object_t *, jo) {
	if (!jo)
		return 0;
	free(jo->buf);
	free(jo);
	return 0;
}

int json_object_append_fragment(json_object_t *jo, const char *fragment, size_t len) {
	if (!jo || !fragment)
		return -1;
	if (jb_field_sep(JB(jo)) != 0)
		return -1;
	return jb_append_str(JB(jo), fragment, len);
}
