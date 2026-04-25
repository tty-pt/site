#include <ttypt/ndx-mod.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <openssl/evp.h>

#include <ttypt/ndc.h>

#include "../proxy/proxy.h"

#define COMMON_IMPL
#include "common.h"
#undef COMMON_IMPL

static char *form_body_finish(form_body_t *fb, size_t *out_len);
static int write_file_path(const char *path, const char *buf, size_t sz);
static int remove_path_recursive(const char *path);
int item_child_path(const char *item_path, const char *name,
	char *out, size_t outlen);

NDX_LISTENER(int, json_escape, const char *, in, char *, out, size_t, outlen) {
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

NDX_LISTENER(int, url_encode, const char *, in, char *, out, size_t, outlen) {
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

NDX_LISTENER(int, b64_encode, const char *, in, char *, out, size_t, outlen) {
	EVP_EncodeBlock((unsigned char *)out, (const unsigned char *)in, (int)strlen(in));
	return 0;
}

NDX_LISTENER(int, respond_plain, int, fd, int, status, const char *, msg)
{
	ndc_header_set(fd, "Content-Type", "text/plain");
	ndc_respond(fd, status, msg);
	return 1;
}

NDX_LISTENER(int, respond_json, int, fd, int, status, const char *, msg)
{
	ndc_header_set(fd, "Content-Type", "application/json");
	ndc_respond(fd, status, msg);
	return 1;
}

/* core_post is defined in index.c; declare here to call it from respond_error */
NDX_HOOK_DECL(int, core_post, int, fd, char *, body, size_t, len);

/*
 * respond_error — send a pretty HTML error page when the client accepts
 * text/html (browser navigation), or a plain-text response otherwise
 * (API / curl / test clients).
 */
NDX_LISTENER(int, respond_error, int, fd, int, status, const char *, msg)
{
	char accept[256] = {0};
	ndc_header_get(fd, "Accept", accept, sizeof(accept));
	if (strstr(accept, "text/html")) {
		char enc[512] = {0};
		char body[640];
		int len;
		url_encode(msg, enc, sizeof(enc));
		len = snprintf(body, sizeof(body), "status=%d&error=%s", status, enc);
		return core_post(fd, body, (size_t)len);
	}
	return respond_plain(fd, status, msg);
}

NDX_LISTENER(int, bad_request, int, fd, const char *, msg)
{
	return respond_error(fd, 400, msg ? msg : "Bad request");
}

NDX_LISTENER(int, server_error, int, fd, const char *, msg)
{
	return respond_error(fd, 500, msg ? msg : "Internal server error");
}

NDX_LISTENER(int, not_found, int, fd, const char *, msg)
{
	return respond_error(fd, 404, msg ? msg : "Not found");
}

NDX_LISTENER(int, redirect, int, fd, const char *, location)
{
	ndc_header_set(fd, "Location", (char *)location);
	ndc_respond(fd, 303, "");
	return 0;
}

NDX_LISTENER(int, read_meta_file,
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

NDX_LISTENER(int, write_meta_file,
	const char *, item_path, const char *, name,
	const char *, buf, size_t, sz)
{
	char p[PATH_MAX];
	snprintf(p, sizeof(p), "%s/%s", item_path, name);
	return write_file_path(p, buf, sz);
}

NDX_LISTENER(int, meta_fields_read,
	const char *, item_path, meta_field_t *, fields, size_t, count)
{
	if (!item_path || !fields)
		return -1;

	for (size_t i = 0; i < count; i++) {
		if (!fields[i].name || !fields[i].buf || fields[i].sz == 0)
			continue;
		fields[i].buf[0] = '\0';
		read_meta_file(item_path, fields[i].name, fields[i].buf, fields[i].sz);
	}

	return 0;
}

NDX_LISTENER(int, meta_fields_write,
	const char *, item_path, const meta_field_t *, fields, size_t, count)
{
	if (!item_path || !fields)
		return -1;

	for (size_t i = 0; i < count; i++) {
		const char *value;

		if (!fields[i].name || !fields[i].buf)
			continue;

		value = fields[i].buf;
		if (write_meta_file(item_path, fields[i].name, value,
				strlen(value)) != 0)
			return -1;
	}

	return 0;
}

static int
write_file_path(const char *path, const char *buf, size_t sz)
{
	FILE *fp = fopen(path, "w");
	if (!fp)
		return -1;
	if (sz > 0 && fwrite(buf, 1, sz, fp) != sz) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	return 0;
}

NDX_LISTENER(int, write_item_child_file,
	const char *, item_path, const char *, name, const char *, buf, size_t, sz)
{
	char p[PATH_MAX];
	if (item_child_path(item_path, name, p, sizeof(p)) != 0)
		return -1;
	return write_file_path(p, buf, sz);
}

NDX_LISTENER(char *, slurp_file, const char *, path)
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

NDX_LISTENER(char *, slurp_item_child_file,
	const char *, item_path, const char *, name)
{
	char path[PATH_MAX];
	if (item_child_path(item_path, name, path, sizeof(path)) != 0)
		return NULL;
	return slurp_file(path);
}

NDX_LISTENER(int, get_doc_root, int, fd, char *, buf, size_t, len)
{
	buf[0] = '\0';
	ndc_env_get(fd, buf, "DOCUMENT_ROOT");
	if (!buf[0])
		snprintf(buf, len, ".");
	return 0;
}

NDX_LISTENER(int, item_dir_exists, const char *, item_path)
{
	struct stat st;
	return stat(item_path, &st) == 0 && S_ISDIR(st.st_mode);
}

NDX_LISTENER(int, item_child_path,
	const char *, item_path, const char *, name,
	char *, out, size_t, outlen)
{
	int n = snprintf(out, outlen, "%s/%s", item_path, name);
	if (n < 0 || (size_t)n >= outlen)
		return -1;
	return 0;
}

static int
remove_path_recursive(const char *path)
{
	struct stat st;
	if (lstat(path, &st) != 0)
		return -1;

	if (!S_ISDIR(st.st_mode))
		return unlink(path);

	DIR *dir = opendir(path);
	if (!dir)
		return -1;

	struct dirent *entry;
	int rc = 0;
	while ((entry = readdir(dir)) != NULL) {
		char child[PATH_MAX];

		if (strcmp(entry->d_name, ".") == 0 ||
				strcmp(entry->d_name, "..") == 0)
			continue;

		if (snprintf(child, sizeof(child), "%s/%s",
				path, entry->d_name) >= (int)sizeof(child)) {
			rc = -1;
			break;
		}

		if (remove_path_recursive(child) != 0) {
			rc = -1;
			break;
		}
	}
	closedir(dir);

	if (rc != 0)
		return -1;
	return rmdir(path);
}

NDX_LISTENER(int, item_remove_path_recursive, const char *, item_path)
{
	if (!item_path || !item_path[0])
		return -1;
	return remove_path_recursive(item_path);
}

NDX_LISTENER(int, core_post_json, int, fd, const char *, json)
{
	proxy_header("Content-Type", "application/json");
	return core_post(fd, (char *)json, strlen(json));
}

NDX_LISTENER(int, core_post_form, int, fd, form_body_t *, fb)
{
	size_t pb_len = 0;
	char *post_body = form_body_finish(fb, &pb_len);
	if (!post_body)
		return respond_error(fd, 500, "OOM");

	int rc = core_post(fd, post_body, pb_len);
	free(post_body);
	return rc;
}


/* ---------------------------------------------------------------------------
 * Phase A helpers
 * ------------------------------------------------------------------------- */

NDX_LISTENER(int, module_path_build,
	const char *, doc_root, const char *, module,
	char *, out, size_t, outlen)
{
	const char *root = (doc_root && doc_root[0]) ? doc_root : ".";
	int n = snprintf(out, outlen, "%s/items/%s", root, module);
	if (n < 0 || (size_t)n >= outlen)
		return -1;
	return 0;
}

NDX_LISTENER(int, module_items_path_build,
	const char *, doc_root, const char *, module,
	char *, out, size_t, outlen)
{
	const char *root = (doc_root && doc_root[0]) ? doc_root : ".";
	int n = snprintf(out, outlen, "%s/items/%s/items", root, module);
	if (n < 0 || (size_t)n >= outlen)
		return -1;
	return 0;
}

NDX_LISTENER(int, item_path_build_root,
	const char *, doc_root, const char *, module, const char *, id,
	char *, out, size_t, outlen)
{
	const char *root = (doc_root && doc_root[0]) ? doc_root : ".";
	int n = snprintf(out, outlen, "%s/items/%s/items/%s", root, module, id);
	if (n < 0 || (size_t)n >= outlen)
		return -1;
	return 0;
}

NDX_LISTENER(int, item_path_build,
	int, fd, const char *, module, const char *, id,
	char *, out, size_t, outlen)
{
	char doc_root[256] = {0};
	ndc_env_get(fd, doc_root, "DOCUMENT_ROOT");
	return item_path_build_root(doc_root, module, id, out, outlen);
}

NDX_LISTENER(int, datalist_extract_id,
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

static int jo_reserve(json_object_t *jo, size_t extra)
{
	size_t need = jo->len + extra + 1;
	if (need <= jo->cap) return 0;
	size_t nc = jo->cap ? jo->cap : 1024;
	while (nc < need) nc *= 2;
	char *nb = realloc(jo->buf, nc);
	if (!nb) return -1;
	jo->buf = nb;
	jo->cap = nc;
	return 0;
}

static int jo_append_str(json_object_t *jo, const char *s, size_t n)
{
	if (jo_reserve(jo, n) != 0) return -1;
	memcpy(jo->buf + jo->len, s, n);
	jo->len += n;
	jo->buf[jo->len] = '\0';
	return 0;
}

NDX_LISTENER(json_array_t *, json_array_new, int, dummy)
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

NDX_LISTENER(int, json_array_append_raw,
	json_array_t *, ja, const char *, s)
{
	if (!ja) return -1;
	if (!ja->first) {
		if (ja_append_str(ja, ",", 1) != 0) return -1;
	}
	ja->first = 0;
	return ja_append_str(ja, s, strlen(s));
}

NDX_LISTENER(int, json_array_begin_object, json_array_t *, ja)
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

NDX_LISTENER(int, json_array_end_object, json_array_t *, ja)
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

NDX_LISTENER(int, json_array_kv_str,
	json_array_t *, ja, const char *, key, const char *, value)
{
	if (!ja || !key) return -1;
	const char *v = value ? value : "";
	/* Escape into a scratch buffer sized generously. */
	size_t vlen = strlen(v);
	size_t esc_cap = vlen * 6 + 2;
	char *esc = malloc(esc_cap);
	if (!esc) return -1;
	json_escape(v, esc, esc_cap);

	if (ja_field_sep(ja) != 0) { free(esc); return -1; }
	if (ja_reserve(ja, strlen(key) + strlen(esc) + 6) != 0) {
		free(esc); return -1;
	}
	ja->len += snprintf(ja->buf + ja->len, ja->cap - ja->len,
		"\"%s\":\"%s\"", key, esc);
	free(esc);
	return 0;
}

NDX_LISTENER(int, json_array_kv_int,
	json_array_t *, ja, const char *, key, int, value)
{
	if (!ja || !key) return -1;
	if (ja_field_sep(ja) != 0) return -1;
	if (ja_reserve(ja, strlen(key) + 32) != 0) return -1;
	ja->len += snprintf(ja->buf + ja->len, ja->cap - ja->len,
		"\"%s\":%d", key, value);
	return 0;
}

NDX_LISTENER(int, json_array_kv_bool,
	json_array_t *, ja, const char *, key, int, value)
{
	if (!ja || !key) return -1;
	if (ja_field_sep(ja) != 0) return -1;
	if (ja_reserve(ja, strlen(key) + 10) != 0) return -1;
	ja->len += snprintf(ja->buf + ja->len, ja->cap - ja->len,
		"\"%s\":%s", key, value ? "true" : "false");
	return 0;
}

NDX_LISTENER(char *, json_array_finish, json_array_t *, ja)
{
	if (!ja) return NULL;
	if (ja_append_str(ja, "]", 1) != 0) {
		free(ja->buf); free(ja); return NULL;
	}
	char *out = ja->buf;
	free(ja);
	return out;
}

/* --- JSON object builder --- */

static int jo_field_sep(json_object_t *jo)
{
	if (jo->len > 0 && jo->buf[jo->len - 1] != '{')
		return jo_append_str(jo, ",", 1);
	return 0;
}

NDX_LISTENER(json_object_t *, json_object_new, int, dummy)
{
	(void)dummy;
	json_object_t *jo = calloc(1, sizeof(*jo));
	if (!jo) return NULL;
	jo->cap = 1024;
	jo->buf = malloc(jo->cap);
	if (!jo->buf) { free(jo); return NULL; }
	jo->buf[0] = '{';
	jo->buf[1] = '\0';
	jo->len = 1;
	jo->first = 1;
	return jo;
}

NDX_LISTENER(int, json_object_kv_str,
	json_object_t *, jo, const char *, key, const char *, value)
{
	if (!jo || !key) return -1;
	const char *v = value ? value : "";
	size_t vlen = strlen(v);
	size_t esc_cap = vlen * 6 + 2;
	char *esc = malloc(esc_cap);
	if (!esc) return -1;
	json_escape(v, esc, esc_cap);

	if (jo_field_sep(jo) != 0) { free(esc); return -1; }
	if (jo_reserve(jo, strlen(key) + strlen(esc) + 6) != 0) {
		free(esc); return -1;
	}
	jo->len += snprintf(jo->buf + jo->len, jo->cap - jo->len,
		"\"%s\":\"%s\"", key, esc);
	free(esc);
	return 0;
}

NDX_LISTENER(int, json_object_kv_int,
	json_object_t *, jo, const char *, key, int, value)
{
	if (!jo || !key) return -1;
	if (jo_field_sep(jo) != 0) return -1;
	if (jo_reserve(jo, strlen(key) + 32) != 0) return -1;
	jo->len += snprintf(jo->buf + jo->len, jo->cap - jo->len,
		"\"%s\":%d", key, value);
	return 0;
}

NDX_LISTENER(int, json_object_kv_bool,
	json_object_t *, jo, const char *, key, int, value)
{
	if (!jo || !key) return -1;
	if (jo_field_sep(jo) != 0) return -1;
	if (jo_reserve(jo, strlen(key) + 10) != 0) return -1;
	jo->len += snprintf(jo->buf + jo->len, jo->cap - jo->len,
		"\"%s\":%s", key, value ? "true" : "false");
	return 0;
}

NDX_LISTENER(int, json_object_kv_raw,
	json_object_t *, jo, const char *, key, const char *, value)
{
	if (!jo || !key || !value) return -1;
	if (jo_field_sep(jo) != 0) return -1;
	if (jo_reserve(jo, strlen(key) + strlen(value) + 4) != 0) return -1;
	jo->len += snprintf(jo->buf + jo->len, jo->cap - jo->len,
		"\"%s\":%s", key, value);
	return 0;
}

NDX_LISTENER(char *, json_object_finish, json_object_t *, jo)
{
	if (!jo) return NULL;
	if (jo_append_str(jo, "}", 1) != 0) {
		free(jo->buf); free(jo); return NULL;
	}
	char *out = jo->buf;
	free(jo);
	return out;
}

NDX_LISTENER(int, json_object_free, json_object_t *, jo)
{
	if (!jo) return 0;
	free(jo->buf);
	free(jo);
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

NDX_LISTENER(form_body_t *, form_body_new, int, dummy)
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

NDX_LISTENER(int, form_body_add,
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
	url_encode(name, en, n_cap);
	url_encode(v, ev, v_cap);

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

static char *
form_body_finish(form_body_t *fb, size_t *out_len)
{
	if (!fb) { if (out_len) *out_len = 0; return NULL; }
	if (out_len) *out_len = fb->len;
	char *out = fb->buf;
	free(fb);
	return out;
}

MODULE_API void
ndx_install(void)
{
}
