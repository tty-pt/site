#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

#include <ttypt/axil.h>
#include <ttypt/xy-mod.h>
#include <ttypt/corm.h>
#include <ttypt/sepal.h>
#include <ttypt/lemma.h>

#include <json-c/json.h>

#include "ttypt/axil-resume-search.h"

#define RS_CURLOPT_URL            10002
#define RS_CURLOPT_POSTFIELDS     10015
#define RS_CURLOPT_HTTPHEADER     10023
#define RS_CURLOPT_WRITEFUNCTION  20011
#define RS_CURLOPT_WRITEDATA      10001
#define RS_CURLOPT_POST           47
#define RS_CURLOPT_HTTPGET        80
#define RS_CURLOPT_TIMEOUT        13

struct rs_curl_syms {
	int   loaded;
	void *(*easy_init)(void);
	int   (*easy_setopt)(void *, int, ...);
	int   (*easy_perform)(void *);
	void  (*easy_cleanup)(void *);
	char *(*easy_escape)(void *, const char *, int);
	void  (*free)(void *);
	void *(*slist_append)(void *, const char *);
	void  (*slist_free_all)(void *);
};

static struct rs_curl_syms rs_curl;
static void                *rs_curl_dl;

static int
rs_curl_ensure(void)
{
	const char *names[] = { "libcurl.so.4", "libcurl.so", NULL };
	size_t i;
	void *sym;

	if (rs_curl.loaded)
		return 0;
	for (i = 0; names[i]; i++) {
		rs_curl_dl = qsys_dlopen(names[i], 0);
		if (rs_curl_dl)
			break;
	}
	if (!rs_curl_dl)
		return -1;
	sym = qsys_dlsym(rs_curl_dl, "curl_easy_init");
	memcpy(&rs_curl.easy_init, &sym, sizeof(rs_curl.easy_init));
	sym = qsys_dlsym(rs_curl_dl, "curl_easy_setopt");
	memcpy(&rs_curl.easy_setopt, &sym, sizeof(rs_curl.easy_setopt));
	sym = qsys_dlsym(rs_curl_dl, "curl_easy_perform");
	memcpy(&rs_curl.easy_perform, &sym, sizeof(rs_curl.easy_perform));
	sym = qsys_dlsym(rs_curl_dl, "curl_easy_cleanup");
	memcpy(&rs_curl.easy_cleanup, &sym, sizeof(rs_curl.easy_cleanup));
	sym = qsys_dlsym(rs_curl_dl, "curl_easy_escape");
	memcpy(&rs_curl.easy_escape, &sym, sizeof(rs_curl.easy_escape));
	sym = qsys_dlsym(rs_curl_dl, "curl_free");
	memcpy(&rs_curl.free, &sym, sizeof(rs_curl.free));
	sym = qsys_dlsym(rs_curl_dl, "curl_slist_append");
	memcpy(&rs_curl.slist_append, &sym, sizeof(rs_curl.slist_append));
	sym = qsys_dlsym(rs_curl_dl, "curl_slist_free_all");
	memcpy(&rs_curl.slist_free_all, &sym, sizeof(rs_curl.slist_free_all));

	if (!rs_curl.easy_init || !rs_curl.easy_setopt || !rs_curl.easy_perform ||
	    !rs_curl.easy_cleanup || !rs_curl.easy_escape || !rs_curl.free ||
	    !rs_curl.slist_append || !rs_curl.slist_free_all)
		return -1;
	rs_curl.loaded = 1;
	return 0;
}

static resume_search_state_t g_state;

struct resume_curl_resp {
	char *data;
	size_t len;
	size_t cap;
};

static size_t
resume_curl_write(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	struct resume_curl_resp *r = (struct resume_curl_resp *)userdata;
	size_t bytes = size * nmemb;
	if (r->len + bytes + 1 > r->cap) {
		size_t ncap = r->cap ? r->cap * 2 : 4096;
		while (ncap < r->len + bytes + 1) ncap *= 2;
		char *grown = realloc(r->data, ncap);
		if (!grown) return 0;
		r->data = grown;
		r->cap = ncap;
	}
	memcpy(r->data + r->len, ptr, bytes);
	r->len += bytes;
	r->data[r->len] = '\0';
	return bytes;
}

static int
collections_post_document(const char *base_url, const char *collection, uint32_t ref, const char *text)
{
	if (rs_curl_ensure() != 0) return -1;

	char clean_base[512];
	snprintf(clean_base, sizeof(clean_base), "%s", base_url);
	char *slash = strstr(clean_base, "/v1");
	if (slash) *slash = '\0';
	slash = strstr(clean_base, "/api");
	if (slash) *slash = '\0';
	size_t cblen = strlen(clean_base);
	while (cblen > 0 && clean_base[cblen - 1] == '/') clean_base[--cblen] = '\0';

	char url[768];
	snprintf(url, sizeof(url), "%s/v1/collections/%s/documents", clean_base, collection);

	struct json_object *obj = json_object_new_object();
	if (!obj) return -1;
	char id_str[64];
	snprintf(id_str, sizeof(id_str), "%u", ref);
	json_object_object_add(obj, "id", json_object_new_string(id_str));
	json_object_object_add(obj, "text", json_object_new_string(text));

	const char *body_json = json_object_to_json_string(obj);
	void *curl = rs_curl.easy_init();
	if (!curl) {
		json_object_put(obj);
		return -1;
	}

	void *hdrs = rs_curl.slist_append(NULL, "Content-Type: application/json");
	rs_curl.easy_setopt(curl, RS_CURLOPT_URL, url);
	rs_curl.easy_setopt(curl, RS_CURLOPT_POST, 1L);
	rs_curl.easy_setopt(curl, RS_CURLOPT_POSTFIELDS, body_json);
	rs_curl.easy_setopt(curl, RS_CURLOPT_HTTPHEADER, hdrs);
	rs_curl.easy_setopt(curl, RS_CURLOPT_TIMEOUT, 10L);

	int res = rs_curl.easy_perform(curl);
	rs_curl.slist_free_all(hdrs);
	rs_curl.easy_cleanup(curl);
	json_object_put(obj);
	return (res == 0) ? 0 : -1;
}

static struct json_object *
collections_query_search(const char *base_url, const char *collection, const char *query, size_t limit)
{
	if (rs_curl_ensure() != 0) return NULL;

	char clean_base[512];
	snprintf(clean_base, sizeof(clean_base), "%s", base_url);
	char *slash = strstr(clean_base, "/v1");
	if (slash) *slash = '\0';
	slash = strstr(clean_base, "/api");
	if (slash) *slash = '\0';
	size_t cblen = strlen(clean_base);
	while (cblen > 0 && clean_base[cblen - 1] == '/') clean_base[--cblen] = '\0';

	void *curl = rs_curl.easy_init();
	if (!curl) return NULL;

	char *escaped = rs_curl.easy_escape(curl, query, (int)strlen(query));
	if (!escaped) {
		rs_curl.easy_cleanup(curl);
		return NULL;
	}

	char url[1024];
	snprintf(url, sizeof(url), "%s/v1/collections/%s/search?q=%s&limit=%zu",
	         clean_base, collection, escaped, limit);
	rs_curl.free(escaped);

	struct resume_curl_resp resp = { NULL, 0, 0 };
	rs_curl.easy_setopt(curl, RS_CURLOPT_URL, url);
	rs_curl.easy_setopt(curl, RS_CURLOPT_HTTPGET, 1L);
	rs_curl.easy_setopt(curl, RS_CURLOPT_WRITEFUNCTION, resume_curl_write);
	rs_curl.easy_setopt(curl, RS_CURLOPT_WRITEDATA, &resp);
	rs_curl.easy_setopt(curl, RS_CURLOPT_TIMEOUT, 10L);

	int res = rs_curl.easy_perform(curl);
	rs_curl.easy_cleanup(curl);

	if (res != 0 || !resp.data) {
		if (resp.data) free(resp.data);
		return NULL;
	}

	struct json_object *parsed = json_tokener_parse(resp.data);
	free(resp.data);
	return parsed;
}

static void
respond_error_json(socket_t fd, int code, const char *msg)
{
	struct json_object *obj = json_object_new_object();
	if (obj) {
		json_object_object_add(obj, "ok", json_object_new_boolean(0));
		json_object_object_add(obj, "error", json_object_new_string(msg ? msg : "unknown error"));
		const char *s = json_object_to_json_string(obj);
		axil_respond_json(fd, code, s);
		json_object_put(obj);
	} else {
		axil_respond_plain(fd, code, msg ? msg : "internal server error");
	}
}

static void
sanitize_filename(const char *src, char *dst, size_t dst_len)
{
	size_t j = 0;
	if (!src || !src[0]) {
		snprintf(dst, dst_len, "resume.pdf");
		return;
	}
	const char *base = strrchr(src, '/');
	if (base)
		base++;
	else
		base = src;

	for (size_t i = 0; base[i] && j + 1 < dst_len; i++) {
		unsigned char c = (unsigned char)base[i];
		if (isalnum(c) || c == '.' || c == '_' || c == '-') {
			dst[j++] = (char)c;
		} else {
			dst[j++] = '_';
		}
	}
	dst[j] = '\0';
	if (j == 0) {
		snprintf(dst, dst_len, "resume.pdf");
	}
}

static int
ensure_dir_exists(const char *dir)
{
	struct stat st;
	if (stat(dir, &st) == 0) {
		if (S_ISDIR(st.st_mode))
			return 0;
		return -1;
	}
	if (mkdir(dir, 0755) == 0)
		return 0;
	if (errno == EEXIST)
		return 0;
	return -1;
}

static int
base64_decode_char(char c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

static unsigned char *
base64_decode(const char *src, size_t *out_len)
{
	size_t len = strlen(src);
	if (len == 0) {
		*out_len = 0;
		return NULL;
	}
	size_t max_out = (len / 4 + 1) * 3;
	unsigned char *out = malloc(max_out);
	if (!out) {
		*out_len = 0;
		return NULL;
	}

	size_t i = 0, j = 0;
	while (i < len) {
		while (i < len && (src[i] == ' ' || src[i] == '\r' || src[i] == '\n' || src[i] == '\t'))
			i++;
		if (i >= len) break;

		int b0 = base64_decode_char(src[i++]);
		int b1 = (i < len) ? base64_decode_char(src[i++]) : -1;
		int b2 = (i < len) ? (src[i] == '=' ? -2 : base64_decode_char(src[i])) : -1;
		if (i < len && src[i] != '\0') i++;
		int b3 = (i < len) ? (src[i] == '=' ? -2 : base64_decode_char(src[i])) : -1;
		if (i < len && src[i] != '\0') i++;

		if (b0 < 0 || b1 < 0) break;
		out[j++] = (unsigned char)((b0 << 2) | (b1 >> 4));
		if (b2 >= 0) {
			out[j++] = (unsigned char)(((b1 & 0x0F) << 4) | (b2 >> 2));
			if (b3 >= 0) {
				out[j++] = (unsigned char)(((b2 & 0x03) << 6) | b3);
			}
		}
	}

	*out_len = j;
	return out;
}

/**
 * Execute pdftotext under the hood to extract full text from a PDF file.
 */
static int
extract_pdf_text(const char *pdf_path, char **text_out, size_t *len_out)
{
	*text_out = NULL;
	*len_out = 0;

	if (!pdf_path || !pdf_path[0])
		return -1;

	char cmd[1024];
	/* Pass -layout to preserve layout formatting, or direct stdout '-' */
	snprintf(cmd, sizeof(cmd), "pdftotext -layout '%s' - 2>/dev/null", pdf_path);

	FILE *fp = popen(cmd, "r");
	if (!fp)
		return -1;

	size_t cap = 16384;
	size_t total = 0;
	char *buf = malloc(cap);
	if (!buf) {
		pclose(fp);
		return -1;
	}

	char chunk[4096];
	size_t n;
	while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
		if (total + n + 1 > cap) {
			size_t new_cap = cap * 2;
			if (new_cap < total + n + 1)
				new_cap = total + n + 65536;
			char *new_buf = realloc(buf, new_cap);
			if (!new_buf) {
				free(buf);
				pclose(fp);
				return -1;
			}
			buf = new_buf;
			cap = new_cap;
		}
		memcpy(buf + total, chunk, n);
		total += n;
	}

	int status = pclose(fp);
	buf[total] = '\0';

	if (status != 0 || total == 0) {
		free(buf);
		return -1;
	}

	*text_out = buf;
	*len_out = total;
	return 0;
}

/**
 * Extract multipart form parts: filename, candidate name, and binary file content.
 */
static int
parse_multipart_upload(const char *body, size_t body_len, const char *content_type,
                       char *candidate_buf, size_t candidate_sz,
                       char *filename_buf, size_t filename_sz,
                       const unsigned char **file_data, size_t *file_data_len)
{
	*file_data = NULL;
	*file_data_len = 0;
	candidate_buf[0] = '\0';
	filename_buf[0] = '\0';

	const char *bpos = strcasestr(content_type, "boundary=");
	if (!bpos)
		return -1;
	bpos += 9;
	char boundary[256];
	size_t blen = 0;
	if (*bpos == '"') {
		bpos++;
		while (*bpos && *bpos != '"' && blen + 1 < sizeof(boundary) - 4)
			boundary[blen++] = *bpos++;
	} else {
		while (*bpos && *bpos != ';' && *bpos != ' ' && *bpos != '\r' && *bpos != '\n' && blen + 1 < sizeof(boundary) - 4)
			boundary[blen++] = *bpos++;
	}
	boundary[blen] = '\0';
	if (blen == 0)
		return -1;

	char delim[260];
	int delim_len = snprintf(delim, sizeof(delim), "--%s", boundary);

	const char *cur = (const char *)memmem(body, body_len, delim, delim_len);
	if (!cur)
		return -1;

	while (cur && (size_t)(cur - body) < body_len) {
		cur += delim_len;
		if ((size_t)(cur - body) + 2 <= body_len && cur[0] == '-' && cur[1] == '-')
			break; /* Final boundary */

		if ((size_t)(cur - body) + 2 <= body_len && cur[0] == '\r' && cur[1] == '\n')
			cur += 2;
		else if ((size_t)(cur - body) + 1 <= body_len && cur[0] == '\n')
			cur += 1;

		const char *hdr_start = cur;
		const char *hdr_end = (const char *)memmem(hdr_start, body_len - (hdr_start - body), "\r\n\r\n", 4);
		size_t skip_nl = 4;
		if (!hdr_end) {
			hdr_end = (const char *)memmem(hdr_start, body_len - (hdr_start - body), "\n\n", 2);
			skip_nl = 2;
		}
		if (!hdr_end)
			break;

		size_t hdr_len = hdr_end - hdr_start;
		const char *part_data = hdr_end + skip_nl;
		size_t remaining = body_len - (part_data - body);

		const char *next_delim = (const char *)memmem(part_data, remaining, delim, delim_len);
		if (!next_delim)
			break;

		size_t part_len = next_delim - part_data;
		if (part_len >= 2 && part_data[part_len - 2] == '\r' && part_data[part_len - 1] == '\n')
			part_len -= 2;
		else if (part_len >= 1 && part_data[part_len - 1] == '\n')
			part_len -= 1;

		/* Check part headers */
		char *hdr_copy = malloc(hdr_len + 1);
		if (hdr_copy) {
			memcpy(hdr_copy, hdr_start, hdr_len);
			hdr_copy[hdr_len] = '\0';

			/* Check name field */
			char *name_ptr = strcasestr(hdr_copy, "name=\"");
			char field_name[128] = {0};
			if (name_ptr) {
				name_ptr += 6;
				char *quote = strchr(name_ptr, '"');
				if (quote) {
					size_t flen = quote - name_ptr;
					if (flen >= sizeof(field_name)) flen = sizeof(field_name) - 1;
					memcpy(field_name, name_ptr, flen);
					field_name[flen] = '\0';
				}
			}

			/* Check filename field */
			char *fn_ptr = strcasestr(hdr_copy, "filename=\"");
			if (fn_ptr) {
				fn_ptr += 10;
				char *quote = strchr(fn_ptr, '"');
				if (quote) {
					size_t flen = quote - fn_ptr;
					if (flen >= filename_sz) flen = filename_sz - 1;
					memcpy(filename_buf, fn_ptr, flen);
					filename_buf[flen] = '\0';
				}
				*file_data = (const unsigned char *)part_data;
				*file_data_len = part_len;
			} else if (strcasecmp(field_name, "file") == 0 || strcasecmp(field_name, "resume") == 0 || strcasecmp(field_name, "pdf") == 0) {
				*file_data = (const unsigned char *)part_data;
				*file_data_len = part_len;
				if (!filename_buf[0])
					snprintf(filename_buf, filename_sz, "resume.pdf");
			} else if (strcasecmp(field_name, "candidate") == 0 || strcasecmp(field_name, "name") == 0) {
				size_t csz = part_len;
				if (csz >= candidate_sz) csz = candidate_sz - 1;
				memcpy(candidate_buf, part_data, csz);
				candidate_buf[csz] = '\0';
			}

			free(hdr_copy);
		}

		cur = next_delim;
	}

	return (*file_data && *file_data_len > 0) ? 0 : -1;
}

int
resume_search_init(const char *url, const char *model, const char *db_path, const char *upload_dir)
{
	int err = 0;
	const char *env_url = getenv("AUREN_EMBED_URL");
	if (!env_url || !env_url[0])
		env_url = getenv("CORM_SEPAL_EMBED_URL");
	if (!env_url || !env_url[0])
		env_url = url;
	if (!env_url || !env_url[0])
		env_url = AXIL_RESUME_SEARCH_DEFAULT_URL;

	const char *env_model = getenv("AUREN_EMBED_MODEL");
	if (!env_model || !env_model[0])
		env_model = getenv("CORM_SEPAL_EMBED_MODEL");
	if (!env_model || !env_model[0])
		env_model = model;
	if (!env_model || !env_model[0])
		env_model = AXIL_RESUME_SEARCH_DEFAULT_MODEL;

	const char *env_db = getenv("RESUME_SEARCH_DB");
	if (!env_db || !env_db[0])
		env_db = getenv("SEPAL_DB_PATH");
	if (!env_db || !env_db[0])
		env_db = db_path;

	const char *env_dir = getenv("RESUME_UPLOAD_DIR");
	if (!env_dir || !env_dir[0])
		env_dir = upload_dir;
	if (!env_dir || !env_dir[0])
		env_dir = AXIL_RESUME_SEARCH_DEFAULT_UPLOAD_DIR;

	snprintf(g_state.url, sizeof(g_state.url), "%s", env_url);
	snprintf(g_state.model, sizeof(g_state.model), "%s", env_model);
	if (env_db && env_db[0])
		snprintf(g_state.db_path, sizeof(g_state.db_path), "%s", env_db);
	else
		g_state.db_path[0] = '\0';
	snprintf(g_state.upload_dir, sizeof(g_state.upload_dir), "%s", env_dir);

	if (ensure_dir_exists(g_state.upload_dir) != 0) {
		fprintf(stderr, "axil-resume-search: failed to create upload dir '%s'\n", g_state.upload_dir);
		return -1;
	}

	if (sepal_configure_embeddings(g_state.url, g_state.model, NULL) != 0) {
		fprintf(stderr, "axil-resume-search: failed to configure embeddings (%s, %s)\n",
		        g_state.url, g_state.model);
		return -1;
	}

	g_state.vs = sepal_open(g_state.db_path[0] ? g_state.db_path : NULL, &err);
	if (!g_state.vs) {
		fprintf(stderr, "axil-resume-search: failed to open sepal vector store (err=%d)\n", err);
		return -1;
	}

	g_state.ldb = lemma_open(0);
	if (!g_state.ldb) {
		fprintf(stderr, "axil-resume-search: failed to open lemma database\n");
		sepal_close(g_state.vs);
		g_state.vs = NULL;
		return -1;
	}

	g_state.meta_hd = corm_open(NULL, NULL, CM_STR, CM_STR, 1023, 0);
	if (g_state.meta_hd == CM_MISS) {
		fprintf(stderr, "axil-resume-search: failed to open corm metadata storage map\n");
		lemma_close(g_state.ldb);
		g_state.ldb = NULL;
		sepal_close(g_state.vs);
		g_state.vs = NULL;
		return -1;
	}

	g_state.text_hd = corm_open(NULL, NULL, CM_STR, CM_STR, 1023, 0);
	if (g_state.text_hd == CM_MISS) {
		fprintf(stderr, "axil-resume-search: failed to open corm text storage map\n");
		corm_close(g_state.meta_hd);
		g_state.meta_hd = 0;
		lemma_close(g_state.ldb);
		g_state.ldb = NULL;
		sepal_close(g_state.vs);
		g_state.vs = NULL;
		return -1;
	}

	g_state.next_ref = 1;
	g_state.initialized = 1;
	return 0;
}

void
resume_search_cleanup(void)
{
	if (!g_state.initialized)
		return;

	if (g_state.vs) {
		sepal_close(g_state.vs);
		g_state.vs = NULL;
	}
	if (g_state.ldb) {
		lemma_close(g_state.ldb);
		g_state.ldb = NULL;
	}
	if (g_state.meta_hd != CM_MISS && g_state.meta_hd != 0) {
		corm_close(g_state.meta_hd);
		g_state.meta_hd = 0;
	}
	if (g_state.text_hd != CM_MISS && g_state.text_hd != 0) {
		corm_close(g_state.text_hd);
		g_state.text_hd = 0;
	}
	g_state.initialized = 0;
}

int
handle_post_resume(socket_t cfd, char *body)
{
	if (!g_state.initialized) {
		respond_error_json(cfd, 500, "resume-search module not initialized");
		return 0;
	}

	char clen_buf[64] = {0};
	axil_env_get(cfd, clen_buf, sizeof(clen_buf), "HTTP_CONTENT_LENGTH");
	size_t body_len = 0;
	if (clen_buf[0])
		body_len = (size_t)strtoul(clen_buf, NULL, 10);
	else if (body)
		body_len = strlen(body);

	if (body_len == 0 || !body) {
		respond_error_json(cfd, 400, "empty request body");
		return 0;
	}

	if (body_len > AXIL_RESUME_SEARCH_MAX_FILE_SIZE) {
		respond_error_json(cfd, 413, "payload exceeds maximum supported size (16MB)");
		return 0;
	}

	char ct[256] = {0};
	if (axil_env_get(cfd, ct, sizeof(ct), "CONTENT_TYPE") != 0 || !ct[0])
		axil_env_get(cfd, ct, sizeof(ct), "HTTP_CONTENT_TYPE");

	char candidate[256] = {0};
	char raw_filename[256] = {0};
	char sanitized_fn[256] = {0};
	const unsigned char *file_bytes = NULL;
	size_t file_len = 0;
	unsigned char *allocated_bytes = NULL;

	/* Query parameters or custom headers can provide metadata overrides */
	axil_req_param(cfd, body, "candidate", candidate, sizeof(candidate));
	if (!candidate[0])
		axil_req_param(cfd, body, "name", candidate, sizeof(candidate));
	axil_req_param(cfd, body, "filename", raw_filename, sizeof(raw_filename));

	if (strstr(ct, "multipart/form-data")) {
		char mp_cand[256] = {0};
		char mp_fn[256] = {0};
		if (parse_multipart_upload(body, body_len, ct, mp_cand, sizeof(mp_cand),
		                           mp_fn, sizeof(mp_fn), &file_bytes, &file_len) != 0) {
			respond_error_json(cfd, 400, "failed to parse multipart/form-data upload");
			return 0;
		}
		if (mp_cand[0] && !candidate[0])
			snprintf(candidate, sizeof(candidate), "%s", mp_cand);
		if (mp_fn[0] && !raw_filename[0])
			snprintf(raw_filename, sizeof(raw_filename), "%s", mp_fn);
	} else if (strstr(ct, "application/json") || body[0] == '{') {
		struct json_object *req = json_tokener_parse(body);
		if (!req || !json_object_is_type(req, json_type_object)) {
			if (req) json_object_put(req);
			respond_error_json(cfd, 400, "invalid JSON payload");
			return 0;
		}

		struct json_object *cand_obj = json_object_object_get(req, "candidate");
		if (!cand_obj) cand_obj = json_object_object_get(req, "name");
		if (cand_obj && json_object_is_type(cand_obj, json_type_string))
			snprintf(candidate, sizeof(candidate), "%s", json_object_get_string(cand_obj));

		struct json_object *fn_obj = json_object_object_get(req, "filename");
		if (fn_obj && json_object_is_type(fn_obj, json_type_string))
			snprintf(raw_filename, sizeof(raw_filename), "%s", json_object_get_string(fn_obj));

		struct json_object *b64_obj = json_object_object_get(req, "pdf_base64");
		if (!b64_obj) b64_obj = json_object_object_get(req, "data");

		if (b64_obj && json_object_is_type(b64_obj, json_type_string)) {
			const char *b64_str = json_object_get_string(b64_obj);
			allocated_bytes = base64_decode(b64_str, &file_len);
			file_bytes = allocated_bytes;
		}

		/* Alternatively, accept pre-stored file_path */
		struct json_object *path_obj = json_object_object_get(req, "file_path");
		if (path_obj && json_object_is_type(path_obj, json_type_string) && (!file_bytes || file_len == 0)) {
			const char *ext_path = json_object_get_string(path_obj);
			FILE *efp = fopen(ext_path, "rb");
			if (efp) {
				fseek(efp, 0, SEEK_END);
				long fsz = ftell(efp);
				fseek(efp, 0, SEEK_SET);
				if (fsz > 0 && fsz <= AXIL_RESUME_SEARCH_MAX_FILE_SIZE) {
					allocated_bytes = malloc(fsz);
					if (allocated_bytes && fread(allocated_bytes, 1, fsz, efp) == (size_t)fsz) {
						file_bytes = allocated_bytes;
						file_len = (size_t)fsz;
					}
				}
				fclose(efp);
			}
		}

		json_object_put(req);

		if (!file_bytes || file_len == 0) {
			respond_error_json(cfd, 400, "missing 'pdf_base64' or valid 'file_path' in JSON request");
			return 0;
		}
	} else {
		/* Raw binary PDF upload */
		file_bytes = (const unsigned char *)body;
		file_len = body_len;
	}

	if (!file_bytes || file_len < 10) {
		if (allocated_bytes) free(allocated_bytes);
		respond_error_json(cfd, 400, "empty or truncated file data");
		return 0;
	}

	/* Validate PDF magic header '%PDF-' */
	size_t check_len = (file_len < 1024) ? file_len : 1024;
	if (memmem(file_bytes, check_len, "%PDF-", 5) == NULL) {
		if (allocated_bytes) free(allocated_bytes);
		respond_error_json(cfd, 400, "invalid file format: missing %PDF- header");
		return 0;
	}

	uint32_t ref = g_state.next_ref++;
	if (!raw_filename[0])
		snprintf(raw_filename, sizeof(raw_filename), "candidate_%u.pdf", ref);

	sanitize_filename(raw_filename, sanitized_fn, sizeof(sanitized_fn));

	if (!candidate[0]) {
		/* Default candidate name from sanitized filename */
		char base_cand[256];
		snprintf(base_cand, sizeof(base_cand), "%s", sanitized_fn);
		char *dot = strrchr(base_cand, '.');
		if (dot) *dot = '\0';
		snprintf(candidate, sizeof(candidate), "%s", base_cand);
	}

	char file_path[1024];
	snprintf(file_path, sizeof(file_path), "%s/resume_%u_%s",
	         g_state.upload_dir, ref, sanitized_fn);

	/* Write physical PDF file to storage */
	FILE *wfp = fopen(file_path, "wb");
	if (!wfp) {
		if (allocated_bytes) free(allocated_bytes);
		respond_error_json(cfd, 500, "failed to save PDF file to disk");
		return 0;
	}
	size_t written = fwrite(file_bytes, 1, file_len, wfp);
	fclose(wfp);

	if (allocated_bytes) {
		free(allocated_bytes);
		allocated_bytes = NULL;
	}

	if (written != file_len) {
		unlink(file_path);
		respond_error_json(cfd, 500, "incomplete file write to storage");
		return 0;
	}

	/* Run pdftotext under the hood to extract text */
	char *extracted_text = NULL;
	size_t text_len = 0;
	if (extract_pdf_text(file_path, &extracted_text, &text_len) != 0 || !extracted_text || text_len == 0) {
		if (extracted_text) free(extracted_text);
		unlink(file_path);
		respond_error_json(cfd, 422, "failed to extract readable text from PDF via pdftotext");
		return 0;
	}

	/* If candidate was defaulted to filename, try extracting Name from PDF text */
	char base_cand[256];
	snprintf(base_cand, sizeof(base_cand), "%s", sanitized_fn);
	char *dot_pos = strrchr(base_cand, '.');
	if (dot_pos) *dot_pos = '\0';
	if (strcmp(candidate, base_cand) == 0 || !candidate[0]) {
		const char *ntag = "NAME / TITLE: ";
		char *npos = strstr(extracted_text, ntag);
		if (!npos) npos = strstr(extracted_text, "Name: ");
		if (npos) {
			size_t taglen = (npos[1] == 'a') ? 6 : strlen(ntag);
			npos += taglen;
			char *nl = strchr(npos, '\n');
			if (nl) {
				size_t nlen = nl - npos;
				if (nlen > 0 && nlen < sizeof(candidate)) {
					memcpy(candidate, npos, nlen);
					candidate[nlen] = '\0';
					if (nlen > 0 && candidate[nlen - 1] == '\r')
						candidate[nlen - 1] = '\0';
				}
			}
		}
	}

	/* Compute snippet for summary display (first 300 chars) */
	char snippet[320] = {0};
	size_t snip_len = 0;
	for (size_t i = 0; i < text_len && snip_len + 4 < sizeof(snippet); i++) {
		char c = extracted_text[i];
		if (c == '\r' || c == '\n' || c == '\t')
			c = ' ';
		if (c == ' ' && snip_len > 0 && snippet[snip_len - 1] == ' ')
			continue;
		snippet[snip_len++] = c;
		if (snip_len >= 280) break;
	}
	snippet[snip_len] = '\0';
	if (snip_len >= 280) {
		strcat(snippet, "...");
	}

	/* Generate embedding vector for the resume via axil-auren daemon */
	/* Bound embedding input text to 8000 chars for model context window */
	char *embed_input = extracted_text;
	char *embed_bounded = NULL;
	if (text_len > 8000) {
		embed_bounded = malloc(8001);
		if (embed_bounded) {
			memcpy(embed_bounded, extracted_text, 8000);
			embed_bounded[8000] = '\0';
			embed_input = embed_bounded;
		}
	}

	float *vec = NULL;
	size_t dim = 0;
	int fetch_rc = sepal_embed_fetch(embed_input, &vec, &dim);
	if (embed_bounded) free(embed_bounded);

	if (fetch_rc != 0 || !vec || dim == 0) {
		free(extracted_text);
		unlink(file_path);
		char errbuf[768];
		snprintf(errbuf, sizeof(errbuf), "failed to fetch embedding vector from %s", g_state.url);
		respond_error_json(cfd, 502, errbuf);
		return 0;
	}

	/* Store vector into Sepal */
	int put_rc = sepal_put(g_state.vs, ref, vec, dim);
	free(vec);
	if (put_rc != 0) {
		free(extracted_text);
		unlink(file_path);
		respond_error_json(cfd, 500, "failed to persist vector in sepal vector store");
		return 0;
	}

	/* Also index in axil-auren Collections API under 'resumes' collection */
	(void)collections_post_document(g_state.url, "resumes", ref, embed_input);

	/* Lexical indexing in Lemma */
	lemma_unindex_ref(g_state.ldb, "text", ref);
	lemma_index_ref(g_state.ldb, "text", ref, extracted_text);
	lemma_unindex_ref(g_state.ldb, "candidate", ref);
	lemma_index_ref(g_state.ldb, "candidate", ref, candidate);

	/* Store full extracted text in Corm text store */
	char kbuf[32];
	snprintf(kbuf, sizeof(kbuf), "%u", ref);
	corm_put(g_state.text_hd, kbuf, extracted_text);

	/* Construct and store structured metadata JSON in Corm */
	struct json_object *meta_obj = json_object_new_object();
	json_object_object_add(meta_obj, "ref", json_object_new_int64(ref));
	json_object_object_add(meta_obj, "candidate", json_object_new_string(candidate));
	json_object_object_add(meta_obj, "file_name", json_object_new_string(sanitized_fn));
	json_object_object_add(meta_obj, "file_path", json_object_new_string(file_path));
	json_object_object_add(meta_obj, "file_size", json_object_new_int64(file_len));
	json_object_object_add(meta_obj, "text_len", json_object_new_int64(text_len));
	json_object_object_add(meta_obj, "dim", json_object_new_int64(dim));
	json_object_object_add(meta_obj, "snippet", json_object_new_string(snippet));
	json_object_object_add(meta_obj, "created_at", json_object_new_int64((int64_t)time(NULL)));

	const char *meta_json_str = json_object_to_json_string(meta_obj);
	corm_put(g_state.meta_hd, kbuf, meta_json_str);

	/* Prepare response JSON */
	struct json_object *resp = json_object_new_object();
	json_object_object_add(resp, "ok", json_object_new_boolean(1));
	json_object_object_add(resp, "ref", json_object_new_int64(ref));
	json_object_object_add(resp, "candidate", json_object_new_string(candidate));
	json_object_object_add(resp, "file_name", json_object_new_string(sanitized_fn));
	json_object_object_add(resp, "file_path", json_object_new_string(file_path));
	json_object_object_add(resp, "file_size", json_object_new_int64(file_len));
	json_object_object_add(resp, "text_len", json_object_new_int64(text_len));
	json_object_object_add(resp, "dim", json_object_new_int64(dim));
	json_object_object_add(resp, "snippet", json_object_new_string(snippet));

	char file_url[128];
	snprintf(file_url, sizeof(file_url), "/resume/file?ref=%u", ref);
	json_object_object_add(resp, "file_url", json_object_new_string(file_url));

	const char *resp_str = json_object_to_json_string(resp);
	axil_respond_json(cfd, 200, resp_str);

	json_object_put(resp);
	json_object_put(meta_obj);
	free(extracted_text);
	return 0;
}

static int
compare_candidates_desc(const void *a, const void *b)
{
	const struct json_object * const *ja = (const struct json_object * const *)a;
	const struct json_object * const *jb = (const struct json_object * const *)b;
	struct json_object *sa = json_object_object_get(*ja, "score");
	struct json_object *sb = json_object_object_get(*jb, "score");
	double da = sa ? json_object_get_double(sa) : 0.0;
	double db = sb ? json_object_get_double(sb) : 0.0;
	if (da < db) return 1;
	if (da > db) return -1;
	return 0;
}

int
handle_get_candidates(socket_t cfd, char *body)
{
	if (!g_state.initialized) {
		respond_error_json(cfd, 500, "resume-search module not initialized");
		return 0;
	}

	char job_query[AXIL_RESUME_SEARCH_MAX_QUERY_LEN] = {0};
	if (axil_req_param(cfd, body, "job", job_query, sizeof(job_query)) < 0) {
		if (axil_req_param(cfd, body, "q", job_query, sizeof(job_query)) < 0) {
			if (axil_req_param(cfd, body, "opportunity", job_query, sizeof(job_query)) < 0)
				job_query[0] = '\0';
		}
	}

	if (!job_query[0]) {
		respond_error_json(cfd, 400, "missing job opportunity query parameter ('job' or 'q')");
		return 0;
	}

	char kbuf[32] = {0};
	size_t limit = AXIL_RESUME_SEARCH_DEFAULT_LIMIT;
	if (axil_req_param(cfd, body, "k", kbuf, sizeof(kbuf)) >= 0 ||
	    axil_req_param(cfd, body, "limit", kbuf, sizeof(kbuf)) >= 0)
	{
		long v = strtol(kbuf, NULL, 10);
		if (v > 0) {
			limit = (size_t)v;
			if (limit > AXIL_RESUME_SEARCH_MAX_HITS)
				limit = AXIL_RESUME_SEARCH_MAX_HITS;
		}
	}

	char mbuf[32] = {0};
	search_mode_t mode = SEARCH_MODE_SEPAL;
	if (axil_req_param(cfd, body, "mode", mbuf, sizeof(mbuf)) >= 0) {
		if (strcasecmp(mbuf, "lemma") == 0)
			mode = SEARCH_MODE_LEMMA;
		else if (strcasecmp(mbuf, "hybrid") == 0)
			mode = SEARCH_MODE_HYBRID;
		else if (strcasecmp(mbuf, "collections") == 0)
			mode = SEARCH_MODE_COLLECTIONS;
		else
			mode = SEARCH_MODE_SEPAL;
	}

	char simbuf[32] = {0};
	float min_sim = 0.0f;
	if (axil_req_param(cfd, body, "min_sim", simbuf, sizeof(simbuf)) >= 0) {
		float f = strtof(simbuf, NULL);
		if (f > 0.0f)
			min_sim = f;
	}

	struct json_object *resp = json_object_new_object();
	struct json_object *candidates_arr = json_object_new_array();
	if (!resp || !candidates_arr) {
		if (resp) json_object_put(resp);
		if (candidates_arr) json_object_put(candidates_arr);
		respond_error_json(cfd, 500, "memory allocation error");
		return 0;
	}

	if (mode == SEARCH_MODE_COLLECTIONS) {
		struct json_object *coll_res = collections_query_search(g_state.url, "resumes", job_query, limit);
		if (coll_res) {
			struct json_object *res_arr = json_object_object_get(coll_res, "results");
			if (res_arr && json_object_is_type(res_arr, json_type_array)) {
				size_t rlen = json_object_array_length(res_arr);
				for (size_t i = 0; i < rlen; i++) {
					struct json_object *item = json_object_array_get_idx(res_arr, i);
					if (!item) continue;
					struct json_object *id_obj = json_object_object_get(item, "id");
					struct json_object *sc_obj = json_object_object_get(item, "score");
					const char *id_str = id_obj ? json_object_get_string(id_obj) : NULL;
					double score = sc_obj ? json_object_get_double(sc_obj) : 0.0;
					if (score < (double)min_sim) continue;

					uint32_t r = id_str ? (uint32_t)strtoul(id_str, NULL, 10) : 0;
					char r_key[32];
					snprintf(r_key, sizeof(r_key), "%u", r);
					const char *meta_str = (const char *)corm_get(g_state.meta_hd, r_key);
					if (!meta_str) continue;
					struct json_object *stored_meta = json_tokener_parse(meta_str);
					if (!stored_meta) continue;

					struct json_object *hit_obj = json_object_new_object();
					json_object_object_add(hit_obj, "ref", json_object_new_int64(r));
					json_object_object_add(hit_obj, "score", json_object_new_double(score));
					json_object_object_add(hit_obj, "candidate",
					                       json_object_get(json_object_object_get(stored_meta, "candidate")));
					json_object_object_add(hit_obj, "file_name",
					                       json_object_get(json_object_object_get(stored_meta, "file_name")));
					json_object_object_add(hit_obj, "file_path",
					                       json_object_get(json_object_object_get(stored_meta, "file_path")));
					json_object_object_add(hit_obj, "file_size",
					                       json_object_get(json_object_object_get(stored_meta, "file_size")));
					json_object_object_add(hit_obj, "snippet",
					                       json_object_get(json_object_object_get(stored_meta, "snippet")));

					char furl[128];
					snprintf(furl, sizeof(furl), "/resume/file?ref=%u", r);
					json_object_object_add(hit_obj, "file_url", json_object_new_string(furl));

					json_object_array_add(candidates_arr, hit_obj);
					json_object_put(stored_meta);
				}
			}
			json_object_put(coll_res);
		}
	} else if (mode == SEARCH_MODE_SEPAL || mode == SEARCH_MODE_HYBRID) {
		float *qvec = NULL;
		size_t qdim = 0;
		int fetch_rc = sepal_embed_fetch(job_query, &qvec, &qdim);
		if (fetch_rc != 0 || !qvec || qdim == 0) {
			if (qvec) free(qvec);
			json_object_put(resp);
			json_object_put(candidates_arr);
			char errbuf[768];
			snprintf(errbuf, sizeof(errbuf), "failed to embed job opportunity via %s", g_state.url);
			respond_error_json(cfd, 502, errbuf);
			return 0;
		}

		sepal_hit_t hits[AXIL_RESUME_SEARCH_MAX_HITS];
		size_t nhits = sepal_search(g_state.vs, qvec, qdim, limit, min_sim, 0, hits);
		free(qvec);

		uint32_t lemma_matches[AXIL_RESUME_SEARCH_MAX_HITS];
		size_t n_lemma = 0;
		if (mode == SEARCH_MODE_HYBRID) {
			uint32_t out_hd = corm_open(NULL, NULL, CM_STR, CM_STR, 255, 0);
			if (out_hd != CM_MISS) {
				int handled = 0;
				lemma_query(g_state.ldb, "text", job_query, out_hd, &handled);
				if (handled) {
					uint32_t cur = corm_iter(out_hd, NULL, 0);
					const void *k_ptr, *v_ptr;
					while (corm_next(&k_ptr, &v_ptr, cur) && n_lemma < AXIL_RESUME_SEARCH_MAX_HITS) {
						lemma_matches[n_lemma++] = (uint32_t)strtoul((const char *)k_ptr, NULL, 10);
					}
					corm_fin(cur);
				}
				corm_close(out_hd);
			}
		}

		for (size_t i = 0; i < nhits; i++) {
			uint32_t r = hits[i].ref;
			char r_key[32];
			snprintf(r_key, sizeof(r_key), "%u", r);
			const char *meta_str = (const char *)corm_get(g_state.meta_hd, r_key);
			if (!meta_str) continue;

			struct json_object *stored_meta = json_tokener_parse(meta_str);
			if (!stored_meta) continue;

			float score = hits[i].score;
			int lexical_match = 0;
			if (mode == SEARCH_MODE_HYBRID) {
				for (size_t j = 0; j < n_lemma; j++) {
					if (lemma_matches[j] == r) {
						lexical_match = 1;
						break;
					}
				}
				if (lexical_match)
					score += 0.15f;
			}

			struct json_object *hit_obj = json_object_new_object();
			json_object_object_add(hit_obj, "ref", json_object_new_int64(r));
			json_object_object_add(hit_obj, "score", json_object_new_double((double)score));
			json_object_object_add(hit_obj, "candidate",
			                       json_object_get(json_object_object_get(stored_meta, "candidate")));
			json_object_object_add(hit_obj, "file_name",
			                       json_object_get(json_object_object_get(stored_meta, "file_name")));
			json_object_object_add(hit_obj, "file_path",
			                       json_object_get(json_object_object_get(stored_meta, "file_path")));
			json_object_object_add(hit_obj, "file_size",
			                       json_object_get(json_object_object_get(stored_meta, "file_size")));
			json_object_object_add(hit_obj, "snippet",
			                       json_object_get(json_object_object_get(stored_meta, "snippet")));

			char furl[128];
			snprintf(furl, sizeof(furl), "/resume/file?ref=%u", r);
			json_object_object_add(hit_obj, "file_url", json_object_new_string(furl));

			if (mode == SEARCH_MODE_HYBRID)
				json_object_object_add(hit_obj, "lexical_match", json_object_new_boolean(lexical_match));

			json_object_array_add(candidates_arr, hit_obj);
			json_object_put(stored_meta);
		}

		/* If hybrid mode and we need to include pure lexical candidates */
		if (mode == SEARCH_MODE_HYBRID && json_object_array_length(candidates_arr) < limit) {
			for (size_t j = 0; j < n_lemma && json_object_array_length(candidates_arr) < limit; j++) {
				uint32_t lref = lemma_matches[j];
				int already_added = 0;
				for (size_t i = 0; i < nhits; i++) {
					if (hits[i].ref == lref) {
						already_added = 1;
						break;
					}
				}
				if (!already_added) {
					char r_key[32];
					snprintf(r_key, sizeof(r_key), "%u", lref);
					const char *meta_str = (const char *)corm_get(g_state.meta_hd, r_key);
					if (meta_str) {
						struct json_object *stored_meta = json_tokener_parse(meta_str);
						if (stored_meta) {
							struct json_object *hit_obj = json_object_new_object();
							json_object_object_add(hit_obj, "ref", json_object_new_int64(lref));
							json_object_object_add(hit_obj, "score", json_object_new_double(0.50));
							json_object_object_add(hit_obj, "candidate",
							                       json_object_get(json_object_object_get(stored_meta, "candidate")));
							json_object_object_add(hit_obj, "file_name",
							                       json_object_get(json_object_object_get(stored_meta, "file_name")));
							json_object_object_add(hit_obj, "file_path",
							                       json_object_get(json_object_object_get(stored_meta, "file_path")));
							json_object_object_add(hit_obj, "file_size",
							                       json_object_get(json_object_object_get(stored_meta, "file_size")));
							json_object_object_add(hit_obj, "snippet",
							                       json_object_get(json_object_object_get(stored_meta, "snippet")));
							char furl[128];
							snprintf(furl, sizeof(furl), "/resume/file?ref=%u", lref);
							json_object_object_add(hit_obj, "file_url", json_object_new_string(furl));
							json_object_object_add(hit_obj, "lexical_match", json_object_new_boolean(1));

							json_object_array_add(candidates_arr, hit_obj);
							json_object_put(stored_meta);
						}
					}
				}
			}
		}

		if (mode == SEARCH_MODE_HYBRID)
			json_object_array_sort(candidates_arr, compare_candidates_desc);

	} else if (mode == SEARCH_MODE_LEMMA) {
		uint32_t out_hd = corm_open(NULL, NULL, CM_STR, CM_STR, 255, 0);
		if (out_hd != CM_MISS) {
			int handled = 0;
			lemma_query(g_state.ldb, "text", job_query, out_hd, &handled);
			if (handled) {
				uint32_t cur = corm_iter(out_hd, NULL, 0);
				const void *k_ptr, *v_ptr;
				size_t count = 0;
				while (corm_next(&k_ptr, &v_ptr, cur) && count < limit) {
					uint32_t r = (uint32_t)strtoul((const char *)k_ptr, NULL, 10);
					char r_key[32];
					snprintf(r_key, sizeof(r_key), "%u", r);
					const char *meta_str = (const char *)corm_get(g_state.meta_hd, r_key);
					if (meta_str) {
						struct json_object *stored_meta = json_tokener_parse(meta_str);
						if (stored_meta) {
							struct json_object *hit_obj = json_object_new_object();
							json_object_object_add(hit_obj, "ref", json_object_new_int64(r));
							json_object_object_add(hit_obj, "score", json_object_new_double(1.0));
							json_object_object_add(hit_obj, "candidate",
							                       json_object_get(json_object_object_get(stored_meta, "candidate")));
							json_object_object_add(hit_obj, "file_name",
							                       json_object_get(json_object_object_get(stored_meta, "file_name")));
							json_object_object_add(hit_obj, "file_path",
							                       json_object_get(json_object_object_get(stored_meta, "file_path")));
							json_object_object_add(hit_obj, "file_size",
							                       json_object_get(json_object_object_get(stored_meta, "file_size")));
							json_object_object_add(hit_obj, "snippet",
							                       json_object_get(json_object_object_get(stored_meta, "snippet")));
							char furl[128];
							snprintf(furl, sizeof(furl), "/resume/file?ref=%u", r);
							json_object_object_add(hit_obj, "file_url", json_object_new_string(furl));

							json_object_array_add(candidates_arr, hit_obj);
							json_object_put(stored_meta);
							count++;
						}
					}
				}
				corm_fin(cur);
			}
			corm_close(out_hd);
		}
	}

	const char *mode_str = (mode == SEARCH_MODE_LEMMA) ? "lemma" :
	                       (mode == SEARCH_MODE_HYBRID) ? "hybrid" : "sepal";

	json_object_object_add(resp, "ok", json_object_new_boolean(1));
	json_object_object_add(resp, "job", json_object_new_string(job_query));
	json_object_object_add(resp, "mode", json_object_new_string(mode_str));
	json_object_object_add(resp, "count", json_object_new_int64(json_object_array_length(candidates_arr)));
	json_object_object_add(resp, "candidates", candidates_arr);

	const char *resp_str = json_object_to_json_string(resp);
	axil_respond_json(cfd, 200, resp_str);
	json_object_put(resp);
	return 0;
}

int
handle_get_file(socket_t cfd, char *body)
{
	if (!g_state.initialized) {
		respond_error_json(cfd, 500, "resume-search module not initialized");
		return 0;
	}

	char ref_buf[32] = {0};
	char path_buf[512] = {0};
	axil_req_param(cfd, body, "ref", ref_buf, sizeof(ref_buf));
	axil_req_param(cfd, body, "path", path_buf, sizeof(path_buf));

	char target_path[768] = {0};

	if (ref_buf[0]) {
		const char *meta_str = (const char *)corm_get(g_state.meta_hd, ref_buf);
		if (!meta_str) {
			respond_error_json(cfd, 404, "resume not found for specified ref");
			return 0;
		}
		struct json_object *meta = json_tokener_parse(meta_str);
		if (!meta) {
			respond_error_json(cfd, 500, "corrupted metadata record");
			return 0;
		}
		struct json_object *p_obj = json_object_object_get(meta, "file_path");
		if (p_obj && json_object_is_type(p_obj, json_type_string)) {
			snprintf(target_path, sizeof(target_path), "%s", json_object_get_string(p_obj));
		}
		json_object_put(meta);
	} else if (path_buf[0]) {
		/* Verify safe path */
		if (strstr(path_buf, "..") != NULL) {
			respond_error_json(cfd, 403, "invalid file path traversal detected");
			return 0;
		}
		snprintf(target_path, sizeof(target_path), "%s", path_buf);
	}

	if (!target_path[0]) {
		respond_error_json(cfd, 400, "missing 'ref' or 'path' parameter");
		return 0;
	}

	struct stat st;
	if (stat(target_path, &st) != 0 || !S_ISREG(st.st_mode)) {
		respond_error_json(cfd, 404, "file not found on disk");
		return 0;
	}

	/* Deliver PDF file using axil static file responder */
	int rc = axil_respond_file(cfd, target_path, "pdf");
	if (rc != 0) {
		respond_error_json(cfd, 500, "failed to stream file response");
	}
	return 0;
}

int
handle_get_list(socket_t cfd, char *body)
{
	(void)body;
	if (!g_state.initialized) {
		respond_error_json(cfd, 500, "resume-search module not initialized");
		return 0;
	}

	struct json_object *resp = json_object_new_object();
	struct json_object *arr = json_object_new_array();

	for (uint32_t r = 1; r < g_state.next_ref; r++) {
		char r_key[32];
		snprintf(r_key, sizeof(r_key), "%u", r);
		const char *meta_str = (const char *)corm_get(g_state.meta_hd, r_key);
		if (meta_str) {
			struct json_object *stored_meta = json_tokener_parse(meta_str);
			if (stored_meta) {
				char furl[128];
				snprintf(furl, sizeof(furl), "/resume/file?ref=%u", r);
				json_object_object_add(stored_meta, "file_url", json_object_new_string(furl));
				json_object_array_add(arr, stored_meta);
			}
		}
	}

	json_object_object_add(resp, "ok", json_object_new_boolean(1));
	json_object_object_add(resp, "count", json_object_new_int64(json_object_array_length(arr)));
	json_object_object_add(resp, "candidates", arr);

	const char *resp_str = json_object_to_json_string(resp);
	axil_respond_json(cfd, 200, resp_str);
	json_object_put(resp);
	return 0;
}

int
handle_get_ui(socket_t cfd, char *body)
{
	(void)body;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverlength-strings"
	static const char ui_html[] =
		"<!DOCTYPE html>\n"
		"<html lang=\"en\">\n"
		"<head>\n"
		"  <meta charset=\"utf-8\">\n"
		"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
		"  <title>Resume & Job Candidate Matcher | Pure-C Axil & Auren</title>\n"
		"  <style>\n"
		"    :root {\n"
		"      --bg: #0f172a;\n"
		"      --card: #1e293b;\n"
		"      --border: #334155;\n"
		"      --text: #f8fafc;\n"
		"      --text-muted: #94a3b8;\n"
		"      --primary: #38bdf8;\n"
		"      --primary-hover: #0ea5e9;\n"
		"      --accent: #10b981;\n"
		"      --danger: #ef4444;\n"
		"    }\n"
		"    * { box-sizing: border-box; margin: 0; padding: 0; }\n"
		"    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;\n"
		"           background: var(--bg); color: var(--text); line-height: 1.5; padding: 2rem 1rem; }\n"
		"    .container { max-width: 960px; margin: 0 auto; }\n"
		"    header { margin-bottom: 2rem; border-bottom: 1px solid var(--border); padding-bottom: 1.25rem; }\n"
		"    h1 { font-size: 1.85rem; color: var(--primary); display: flex; align-items: center; gap: 0.5rem; }\n"
		"    p.sub { color: var(--text-muted); font-size: 0.95rem; margin-top: 0.35rem; }\n"
		"    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 1.5rem; margin-bottom: 1.5rem; }\n"
		"    @media(max-width: 768px) { .grid { grid-template-columns: 1fr; } }\n"
		"    .card { background: var(--card); border: 1px solid var(--border); border-radius: 8px; padding: 1.25rem; }\n"
		"    h2 { font-size: 1.2rem; color: #fff; margin-bottom: 1rem; display: flex; align-items: center; gap: 0.5rem; }\n"
		"    label { display: block; font-size: 0.85rem; font-weight: 600; color: var(--text-muted); margin-bottom: 0.35rem; }\n"
		"    input[type='text'], input[type='number'], select, textarea {\n"
		"      width: 100%; padding: 0.6rem 0.8rem; background: #0f172a; border: 1px solid var(--border);\n"
		"      color: #fff; border-radius: 6px; font-size: 0.9rem; margin-bottom: 0.85rem;\n"
		"    }\n"
		"    input[type='file'] { width: 100%; margin-bottom: 0.85rem; color: var(--text-muted); font-size: 0.9rem; }\n"
		"    textarea { resize: vertical; min-height: 110px; font-family: inherit; }\n"
		"    button { background: var(--primary); color: #0f172a; border: none; border-radius: 6px;\n"
		"             padding: 0.65rem 1.25rem; font-weight: 700; font-size: 0.9rem; cursor: pointer; transition: background 0.15s; }\n"
		"    button:hover { background: var(--primary-hover); }\n"
		"    .hit { background: #0f172a; border: 1px solid var(--border); border-radius: 6px; padding: 1rem; margin-top: 0.75rem; }\n"
		"    .hit-head { display: flex; justify-content: space-between; align-items: center; margin-bottom: 0.4rem; }\n"
		"    .candidate-name { font-weight: 700; color: #fff; font-size: 1.05rem; }\n"
		"    .badge { background: #0284c7; color: #fff; padding: 2px 8px; border-radius: 12px; font-size: 0.75rem; font-weight: 700; }\n"
		"    .badge-hybrid { background: #059669; }\n"
		"    .snippet { font-size: 0.85rem; color: var(--text-muted); background: #162032; padding: 0.6rem; border-radius: 4px; margin-top: 0.5rem; }\n"
		"    .actions { margin-top: 0.6rem; display: flex; gap: 0.5rem; }\n"
		"    .btn-sm { padding: 0.35rem 0.75rem; font-size: 0.8rem; text-decoration: none; border-radius: 4px;\n"
		"              background: #334155; color: #fff; display: inline-flex; align-items: center; gap: 0.25rem; font-weight: 600; }\n"
		"    .btn-sm:hover { background: #475569; }\n"
		"    #upload-status { font-size: 0.85rem; margin-top: 0.6rem; word-break: break-all; }\n"
		"    .status-ok { color: var(--accent); }\n"
		"    .status-err { color: var(--danger); }\n"
		"  </style>\n"
		"</head>\n"
		"<body>\n"
		"  <div class=\"container\">\n"
		"    <header>\n"
		"      <h1>📄 Resume Candidate Matcher</h1>\n"
		"      <p class=\"sub\">Upload PDF resumes &rarr; pdftotext extraction &rarr; dense vector embeddings via Auren (port 4242) &rarr; semantic job opportunity search.</p>\n"
		"    </header>\n"
		"\n"
		"    <div class=\"grid\">\n"
		"      <!-- Upload Form -->\n"
		"      <div class=\"card\">\n"
		"        <h2>📤 Upload Resume (PDF)</h2>\n"
		"        <form id=\"upload-form\">\n"
		"          <label for=\"cand-name\">Candidate Name / Title</label>\n"
		"          <input type=\"text\" id=\"cand-name\" placeholder=\"e.g. Alice Smith - Systems Engineer\">\n"
		"          <label for=\"cand-pdf\">Select PDF File</label>\n"
		"          <input type=\"file\" id=\"cand-pdf\" accept=\"application/pdf\" required>\n"
		"          <button type=\"submit\" id=\"upload-btn\">Upload & Ingest Resume</button>\n"
		"          <div id=\"upload-status\"></div>\n"
		"        </form>\n"
		"      </div>\n"
		"\n"
		"      <!-- Match Job Opportunity Form -->\n"
		"      <div class=\"card\">\n"
		"        <h2>🎯 Match Job Opportunity</h2>\n"
		"        <form id=\"search-form\">\n"
		"          <label for=\"job-desc\">Job Opportunity / Role Description</label>\n"
		"          <textarea id=\"job-desc\" placeholder=\"Paste job requirements, skills, duties, technologies...\" required></textarea>\n"
		"          <div style=\"display:grid; grid-template-columns: 2fr 1fr; gap: 0.75rem;\">\n"
		"            <div>\n"
		"              <label for=\"search-mode\">Matching Mode</label>\n"
		"              <select id=\"search-mode\">\n"
		"                <option value=\"sepal\">Semantic Vectors (Sepal)</option>\n"
		"                <option value=\"hybrid\" selected>Hybrid (Vector + Keywords)</option>\n"
		"                <option value=\"lemma\">Lexical (Lemma)</option>\n"
		"              </select>\n"
		"            </div>\n"
		"            <div>\n"
		"              <label for=\"search-k\">Limit</label>\n"
		"              <input type=\"number\" id=\"search-k\" value=\"10\" min=\"1\" max=\"100\">\n"
		"            </div>\n"
		"          </div>\n"
		"          <button type=\"submit\" id=\"search-btn\">Find Matching Candidates</button>\n"
		"        </form>\n"
		"      </div>\n"
		"    </div>\n"
		"\n"
		"    <!-- Candidates Results Card -->\n"
		"    <div class=\"card\">\n"
		"      <h2>🏆 Matched Candidates</h2>\n"
		"      <div id=\"results-area\"><p style=\"color: var(--text-muted); font-size: 0.9rem;\">Upload resumes and enter a job opportunity above to rank candidates.</p></div>\n"
		"    </div>\n"
		"  </div>\n"
		"\n"
		"  <script>\n"
		"    const uploadForm = document.getElementById('upload-form');\n"
		"    const uploadStatus = document.getElementById('upload-status');\n"
		"    const searchForm = document.getElementById('search-form');\n"
		"    const resultsArea = document.getElementById('results-area');\n"
		"\n"
		"    uploadForm.onsubmit = async (e) => {\n"
		"      e.preventDefault();\n"
		"      const name = document.getElementById('cand-name').value;\n"
		"      const fileInput = document.getElementById('cand-pdf');\n"
		"      if (!fileInput.files.length) return;\n"
		"      const file = fileInput.files[0];\n"
		"\n"
		"      uploadStatus.innerHTML = '<span style=\"color:#38bdf8\">Uploading and extracting text via pdftotext...</span>';\n"
		"      const fd = new FormData();\n"
		"      fd.append('candidate', name);\n"
		"      fd.append('file', file, file.name);\n"
		"\n"
		"      try {\n"
		"        const res = await fetch('/resume', { method: 'POST', body: fd });\n"
		"        const data = await res.json();\n"
		"        if (data.ok) {\n"
		"          uploadStatus.innerHTML = `<span class=\"status-ok\">✓ Ingested Candidate #${data.ref}: <b>${data.candidate}</b> (${data.file_size} bytes, ${data.text_len} chars text). Stored at ${data.file_path}</span>`;\n"
		"          uploadForm.reset();\n"
		"        } else {\n"
		"          uploadStatus.innerHTML = `<span class=\"status-err\">✗ Upload failed: ${data.error || 'unknown error'}</span>`;\n"
		"        }\n"
		"      } catch (err) {\n"
		"        uploadStatus.innerHTML = `<span class=\"status-err\">✗ Network error: ${err.message}</span>`;\n"
		"      }\n"
		"    };\n"
		"\n"
		"    searchForm.onsubmit = async (e) => {\n"
		"      e.preventDefault();\n"
		"      const job = document.getElementById('job-desc').value;\n"
		"      const mode = document.getElementById('search-mode').value;\n"
		"      const k = document.getElementById('search-k').value || 10;\n"
		"\n"
		"      resultsArea.innerHTML = '<p style=\"color:#38bdf8\">Searching candidates and computing vector similarity...</p>';\n"
		"      try {\n"
		"        const res = await fetch(`/candidates?job=${encodeURIComponent(job)}&mode=${encodeURIComponent(mode)}&k=${k}`);\n"
		"        const data = await res.json();\n"
		"        if (!data.candidates || data.candidates.length === 0) {\n"
		"          resultsArea.innerHTML = '<p style=\"color:var(--text-muted)\">No matching candidates found.</p>';\n"
		"          return;\n"
		"        }\n"
		"        resultsArea.innerHTML = data.candidates.map((c, idx) => `\n"
		"          <div class=\"hit\">\n"
		"            <div class=\"hit-head\">\n"
		"              <div>\n"
		"                <span style=\"color:var(--text-muted);font-size:0.85rem;margin-right:0.4rem;\">#${idx+1}</span>\n"
		"                <span class=\"candidate-name\">${c.candidate || c.file_name}</span>\n"
		"              </div>\n"
		"              <div>\n"
		"                <span class=\"badge ${c.lexical_match ? 'badge-hybrid' : ''}\">Score: ${(c.score * 100).toFixed(1)}%</span>\n"
		"              </div>\n"
		"            </div>\n"
		"            <div style=\"font-size:0.8rem;color:var(--text-muted);\">\n"
		"              Stored Path: <code>${c.file_path}</code> &bull; Size: ${c.file_size} bytes\n"
		"            </div>\n"
		"            <div class=\"snippet\"><b>Resume Excerpt:</b> ${c.snippet || 'No excerpt available.'}</div>\n"
		"            <div class=\"actions\">\n"
		"              <a class=\"btn-sm\" href=\"${c.file_url}\" target=\"_blank\">👁 View PDF</a>\n"
		"              <a class=\"btn-sm\" href=\"${c.file_url}\" download=\"${c.file_name}\">⬇ Download PDF</a>\n"
		"            </div>\n"
		"          </div>\n"
		"        `).join('');\n"
		"      } catch (err) {\n"
		"        resultsArea.innerHTML = `<span class=\"status-err\">Search failed: ${err.message}</span>`;\n"
		"      }\n"
		"    };\n"
		"  </script>\n"
		"</body>\n"
		"</html>\n";
#pragma GCC diagnostic pop

	axil_header_set(cfd, "Content-Type", "text/html; charset=utf-8");
	axil_respond(cfd, 200, ui_html);
	return 0;
}

XY_MODULE_API void
xy_install(void)
{
	if (resume_search_init(NULL, NULL, NULL, NULL) != 0) {
		fprintf(stderr, "axil-resume-search: xy_install initialization failed\n");
		return;
	}

	axil_register_handler("POST:/resume", handle_post_resume);
	axil_register_handler("POST:/api/resume", handle_post_resume);
	axil_register_handler("POST:/upload", handle_post_resume);

	axil_register_handler("GET:/candidates", handle_get_candidates);
	axil_register_handler("GET:/search", handle_get_candidates);
	axil_register_handler("GET:/api/candidates", handle_get_candidates);
	axil_register_handler("GET:/api/search", handle_get_candidates);

	axil_register_handler("GET:/resume/file", handle_get_file);
	axil_register_handler("GET:/candidates/list", handle_get_list);
	axil_register_handler("GET:/resumes", handle_get_list);

	axil_register_handler("GET:/", handle_get_ui);
	axil_register_handler("GET:/ui", handle_get_ui);

	fprintf(stderr, "axil-resume-search: mounted /resume, /candidates, /resume/file, and / (UI form) endpoints\n");
}

XY_MODULE_API void
xy_shutdown(void)
{
	resume_search_cleanup();
}
