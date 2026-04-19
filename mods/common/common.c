#include <ttypt/ndx-mod.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <openssl/evp.h>

#include <ttypt/ndc.h>

#define COMMON_IMPL
#include "common.h"
#undef COMMON_IMPL

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
	return (int)j;
}

NDX_DEF(int, b64_encode, const char *, in, char *, out, size_t, outlen) {
	EVP_EncodeBlock((unsigned char *)out, (const unsigned char *)in, (int)strlen(in));
	return 0;
}

NDX_DEF(int, respond_plain, int, fd, int, status, const char *, msg)
{
	ndc_header_set(fd, "Content-Type", "text/plain");
	ndc_respond(fd, status, msg);
	return 1;
}

NDX_DEF(int, respond_json, int, fd, int, status, const char *, msg)
{
	ndc_header_set(fd, "Content-Type", "application/json");
	ndc_respond(fd, status, msg);
	return 1;
}

/* core_post is defined in index.c; declare here to call it from respond_error */
NDX_DECL(int, core_post, int, fd, char *, body, size_t, len);

/*
 * respond_error — send a pretty HTML error page when the client accepts
 * text/html (browser navigation), or a plain-text response otherwise
 * (API / curl / test clients).
 */
NDX_DEF(int, respond_error, int, fd, int, status, const char *, msg)
{
	char accept[256] = {0};
	ndc_header_get(fd, "Accept", accept, sizeof(accept));
	if (strstr(accept, "text/html")) {
		char enc[512] = {0};
		char body[640];
		int len;
		call_url_encode(msg, enc, sizeof(enc));
		len = snprintf(body, sizeof(body), "status=%d&error=%s", status, enc);
		return call_core_post(fd, body, (size_t)len);
	}
	return call_respond_plain(fd, status, msg);
}

NDX_DEF(int, bad_request, int, fd, const char *, msg)
{
	return call_respond_error(fd, 400, msg ? msg : "Bad request");
}

NDX_DEF(int, server_error, int, fd, const char *, msg)
{
	return call_respond_error(fd, 500, msg ? msg : "Internal server error");
}

NDX_DEF(int, not_found, int, fd, const char *, msg)
{
	return call_respond_error(fd, 404, msg ? msg : "Not found");
}

NDX_DEF(int, redirect, int, fd, const char *, location)
{
	ndc_header_set(fd, "Location", (char *)location);
	ndc_respond(fd, 303, "");
	return 0;
}

NDX_DEF(int, read_meta_file,
	const char *, item_path, const char *, name,
	char *, buf, size_t, sz)
{
	char p[PATH_MAX];
	snprintf(p, sizeof(p), "%s/%s", item_path, name);
	FILE *mfp = fopen(p, "r");
	if (!mfp)
		return -1;
	if (fgets(buf, (int)sz - 1, mfp)) {
		size_t l = strlen(buf);
		if (l > 0 && buf[l - 1] == '\n') buf[l - 1] = '\0';
	}
	fclose(mfp);
	return 0;
}

NDX_DEF(int, write_meta_file,
	const char *, item_path, const char *, name,
	const char *, buf, size_t, sz)
{
	char p[PATH_MAX];
	snprintf(p, sizeof(p), "%s/%s", item_path, name);
	FILE *fp = fopen(p, "w");
	if (!fp)
		return -1;
	fwrite(buf, 1, sz, fp);
	fclose(fp);
	return 0;
}

NDX_DEF(char *, slurp_file, const char *, path)
{
	FILE *fp = fopen(path, "r");
	if (!fp)
		return NULL;
	fseek(fp, 0, SEEK_END);
	long fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (fsize <= 0) {
		fclose(fp);
		return strdup("");
	}
	char *buf = malloc((size_t)fsize + 1);
	if (!buf) {
		fclose(fp);
		return NULL;
	}
	size_t got = fread(buf, 1, (size_t)fsize, fp);
	fclose(fp);
	buf[got] = '\0';
	return buf;
}

NDX_DEF(int, get_doc_root, int, fd, char *, buf, size_t, len)
{
	buf[0] = '\0';
	ndc_env_get(fd, buf, "DOCUMENT_ROOT");
	if (!buf[0])
		snprintf(buf, len, ".");
	return 0;
}


/* ---------------------------------------------------------------------------
 * Phase A helpers
 * ------------------------------------------------------------------------- */

NDX_DEF(int, item_path_build,
	int, fd, const char *, module, const char *, id,
	char *, out, size_t, outlen)
{
	char doc_root[256] = {0};
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	const char *root = doc_root[0] ? doc_root : ".";
	int n = snprintf(out, outlen, "%s/items/%s/items/%s", root, module, id);
	if (n < 0 || (size_t)n >= outlen)
		return -1;
	return n;
}

NDX_DEF(int, datalist_extract_id,
	const char *, in, char *, id_out, size_t, outlen)
{
	if (!in || !id_out || outlen == 0) return -1;
	const char *lb = strrchr(in, '[');
	const char *rb = strrchr(in, ']');
	if (!lb || !rb || rb <= lb + 1) {
		/* No "[id]" suffix: leave id_out as a copy of in (if distinct). */
		if (in != id_out) snprintf(id_out, outlen, "%s", in);
		return -1;
	}
	size_t n = (size_t)(rb - lb - 1);
	if (n >= outlen) n = outlen - 1;
	memmove(id_out, lb + 1, n);
	id_out[n] = '\0';
	return 0;
}


/* --- JSON array builder --- */

static int ja_reserve(json_array_t *ja, size_t extra)
{
	size_t need = ja->len + extra + 1;
	if (need <= ja->cap) return 0;
	size_t nc = ja->cap ? ja->cap : 1024;
	while (nc < need) nc *= 2;
	char *nb = realloc(ja->buf, nc);
	if (!nb) return -1;
	ja->buf = nb;
	ja->cap = nc;
	return 0;
}

static int ja_append_str(json_array_t *ja, const char *s, size_t n)
{
	if (ja_reserve(ja, n) != 0) return -1;
	memcpy(ja->buf + ja->len, s, n);
	ja->len += n;
	ja->buf[ja->len] = '\0';
	return 0;
}

NDX_DEF(json_array_t *, json_array_new, int, dummy)
{
	(void)dummy;
	json_array_t *ja = calloc(1, sizeof(*ja));
	if (!ja) return NULL;
	ja->cap = 1024;
	ja->buf = malloc(ja->cap);
	if (!ja->buf) { free(ja); return NULL; }
	ja->buf[0] = '[';
	ja->buf[1] = '\0';
	ja->len = 1;
	ja->first = 1;
	return ja;
}

NDX_DEF(int, json_array_append_raw,
	json_array_t *, ja, const char *, s)
{
	if (!ja) return -1;
	if (!ja->first) {
		if (ja_append_str(ja, ",", 1) != 0) return -1;
	}
	ja->first = 0;
	return ja_append_str(ja, s, strlen(s));
}

NDX_DEF(int, json_array_begin_object, json_array_t *, ja)
{
	if (!ja) return -1;
	if (!ja->first) {
		if (ja_append_str(ja, ",", 1) != 0) return -1;
	}
	ja->first = 0;
	if (ja_append_str(ja, "{", 1) != 0) return -1;
	/* Mark that the object itself has no fields yet by piggy-backing:
	 * we treat ja->first == 0 inside the object; field-separator logic
	 * is handled by _kv_* below using an embedded convention — check
	 * whether the char before our current position is '{'. */
	return 0;
}

NDX_DEF(int, json_array_end_object, json_array_t *, ja)
{
	if (!ja) return -1;
	return ja_append_str(ja, "}", 1);
}

/* Field-separator helper: append ',' if last char is not '{'. */
static int ja_field_sep(json_array_t *ja)
{
	if (ja->len > 0 && ja->buf[ja->len - 1] != '{') {
		return ja_append_str(ja, ",", 1);
	}
	return 0;
}

NDX_DEF(int, json_array_kv_str,
	json_array_t *, ja, const char *, key, const char *, value)
{
	if (!ja || !key) return -1;
	const char *v = value ? value : "";
	/* Escape into a scratch buffer sized generously. */
	size_t vlen = strlen(v);
	size_t esc_cap = vlen * 6 + 2;
	char *esc = malloc(esc_cap);
	if (!esc) return -1;
	call_json_escape(v, esc, esc_cap);

	if (ja_field_sep(ja) != 0) { free(esc); return -1; }
	if (ja_reserve(ja, strlen(key) + strlen(esc) + 6) != 0) {
		free(esc); return -1;
	}
	ja->len += snprintf(ja->buf + ja->len, ja->cap - ja->len,
		"\"%s\":\"%s\"", key, esc);
	free(esc);
	return 0;
}

NDX_DEF(int, json_array_kv_int,
	json_array_t *, ja, const char *, key, int, value)
{
	if (!ja || !key) return -1;
	if (ja_field_sep(ja) != 0) return -1;
	if (ja_reserve(ja, strlen(key) + 32) != 0) return -1;
	ja->len += snprintf(ja->buf + ja->len, ja->cap - ja->len,
		"\"%s\":%d", key, value);
	return 0;
}

NDX_DEF(int, json_array_kv_bool,
	json_array_t *, ja, const char *, key, int, value)
{
	if (!ja || !key) return -1;
	if (ja_field_sep(ja) != 0) return -1;
	if (ja_reserve(ja, strlen(key) + 10) != 0) return -1;
	ja->len += snprintf(ja->buf + ja->len, ja->cap - ja->len,
		"\"%s\":%s", key, value ? "true" : "false");
	return 0;
}

NDX_DEF(char *, json_array_finish, json_array_t *, ja)
{
	if (!ja) return NULL;
	if (ja_append_str(ja, "]", 1) != 0) {
		free(ja->buf); free(ja); return NULL;
	}
	char *out = ja->buf;
	free(ja);
	return out;
}

NDX_DEF(int, json_array_free, json_array_t *, ja)
{
	if (!ja) return 0;
	free(ja->buf);
	free(ja);
	return 0;
}

/* --- Form body builder --- */

static int fb_reserve(form_body_t *fb, size_t extra)
{
	size_t need = fb->len + extra + 1;
	if (need <= fb->cap) return 0;
	size_t nc = fb->cap ? fb->cap : 2048;
	while (nc < need) nc *= 2;
	char *nb = realloc(fb->buf, nc);
	if (!nb) return -1;
	fb->buf = nb;
	fb->cap = nc;
	return 0;
}

NDX_DEF(form_body_t *, form_body_new, int, dummy)
{
	(void)dummy;
	form_body_t *fb = calloc(1, sizeof(*fb));
	if (!fb) return NULL;
	fb->cap = 2048;
	fb->buf = malloc(fb->cap);
	if (!fb->buf) { free(fb); return NULL; }
	fb->buf[0] = '\0';
	fb->first = 1;
	return fb;
}

NDX_DEF(int, form_body_add,
	form_body_t *, fb, const char *, name, const char *, value)
{
	if (!fb || !name) return -1;
	const char *v = value ? value : "";

	size_t nl = strlen(name);
	size_t vl = strlen(v);
	size_t n_cap = nl * 3 + 2;
	size_t v_cap = vl * 3 + 2;
	char *en = malloc(n_cap);
	char *ev = malloc(v_cap);
	if (!en || !ev) { free(en); free(ev); return -1; }
	call_url_encode(name, en, n_cap);
	call_url_encode(v, ev, v_cap);

	if (fb_reserve(fb, strlen(en) + strlen(ev) + 3) != 0) {
		free(en); free(ev); return -1;
	}
	if (!fb->first)
		fb->buf[fb->len++] = '&';
	fb->first = 0;
	fb->len += snprintf(fb->buf + fb->len, fb->cap - fb->len,
		"%s=%s", en, ev);
	free(en);
	free(ev);
	return 0;
}

NDX_DEF(char *, form_body_finish,
	form_body_t *, fb, size_t *, out_len)
{
	if (!fb) { if (out_len) *out_len = 0; return NULL; }
	if (out_len) *out_len = fb->len;
	char *out = fb->buf;
	free(fb);
	return out;
}

NDX_DEF(int, form_body_free, form_body_t *, fb)
{
	if (!fb) return 0;
	free(fb->buf);
	free(fb);
	return 0;
}

MODULE_API void
ndx_install(void)
{
}
