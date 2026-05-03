#include <ttypt/ndx-mod.h>

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <limits.h>
#include <ttypt/ndc.h>
#include <ttypt/qmap.h>
#include <ttypt/qsys.h>

#define CD "Content-Disposition: form-data; name=\""

static uint32_t mpfd_db;
static uint32_t mpfd_val_type;

/* Configuration limits */
static size_t mpfd_max_field_size = 10 * 1024 * 1024; /* 10 MB */
static size_t mpfd_max_total_size = 50 * 1024 * 1024; /* 50 MB */
static size_t mpfd_max_field_count = 100;

/* Error tracking */
static char mpfd_error_buf[256] = { 0 };

struct mpfd_val {
	uint32_t len;
	uint32_t filename_len;
	char data[];
};

static size_t mpfd_val_measure(const void *data)
{
	const struct mpfd_val *val = data;
	return sizeof(struct mpfd_val) + val->len + val->filename_len;
}

static void set_error(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vsnprintf(mpfd_error_buf, sizeof(mpfd_error_buf), fmt, args);
	va_end(args);
}

static void clear_error(void)
{
	mpfd_error_buf[0] = '\0';
}

/* Safe substring search that works even with binary data (embedded '\0') */
static char *find_substr(
        const char *haystack,
        size_t hay_len,
        const char *needle,
        size_t needle_len)
{
	if (needle_len == 0 || hay_len < needle_len)
		return NULL;
	for (size_t i = 0; i <= hay_len - needle_len; i++) {
		if (memcmp(haystack + i, needle, needle_len) == 0)
			return (char *)(haystack + i);
	}
	return NULL;
}

static int
parse_multipart(char *body, const char *content_type, size_t body_len)
{
	if (!body || body_len == 0)
		return -1;

	char boundary[256] = { 0 };
	char boundary_crlf[512] = { 0 };

	/* Extract boundary */
	const char *bstart = strstr(content_type, "boundary=");
	if (!bstart) {
		set_error("No boundary found in Content-Type");
		return -1;
	}
	bstart += 9;
	while (*bstart == ' ' || *bstart == '"')
		bstart++;
	const char *bend = bstart;
	while (*bend && *bend != '"' && *bend != ';' && *bend != '\r' &&
	       *bend != '\n')
		bend++;
	snprintf(
	        boundary,
	        sizeof(boundary),
	        "--%.*s",
	        (int)(bend - bstart),
	        bstart);

	size_t blen = strlen(boundary);
	snprintf(boundary_crlf, sizeof(boundary_crlf), "\r\n%s", boundary);

	size_t total_size = 0;
	size_t field_count = 0;

	char *pos = body;
	while (1) {
		char *bpos = find_substr(
		        pos, body_len - (pos - body), boundary, blen);
		if (!bpos)
			break;

		pos = bpos + blen;

		/* End of multipart? */
		if (body_len - (pos - body) >= 2 && strncmp(pos, "--", 2) == 0)
			break;

		/* Expect \r\n after boundary */
		if (body_len - (pos - body) < 2 || strncmp(pos, "\r\n", 2) != 0)
		{
			pos = bpos + 1;
			continue;
		}
		pos += 2;

		/* Headers until \r\n\r\n */
		char *headers_end = find_substr(
		        pos, body_len - (pos - body), "\r\n\r\n", 4);
		if (!headers_end)
			break;

		/* Find Content-Disposition (case-insensitive) */
		char *cd = NULL;
		for (char *p = pos; p + 19 <= headers_end; p++) {
			if (strncasecmp(p, "Content-Disposition:", 20) == 0) {
				cd = p;
				break;
			}
		}

		if (!cd) {
			pos = headers_end + 4;
			continue;
		}

		/* Find name="..." within Content-Disposition (case-insensitive)
		 */
		char *name_start = NULL;
		for (char *p = cd; p + 6 <= headers_end; p++) {
			if (strncasecmp(p, "name=\"", 6) == 0) {
				name_start = p + 6;
				break;
			}
		}

		if (!name_start) {
			pos = headers_end + 4;
			continue;
		}

		/* Extract name="..." */
		char key[256] = { 0 };
		char *q_end = name_start;
		while (q_end < headers_end && *q_end != '"')
			q_end++;
		if (q_end >= headers_end)
			goto next_part;
		size_t klen = q_end - name_start;
		if (klen >= sizeof(key))
			klen = sizeof(key) - 1;
		strncpy(key, name_start, klen);
		key[klen] = '\0';

		/* Extract filename if present (case-insensitive) */
		char filename[256] = { 0 };
		size_t fname_len = 0;
		char *fstart = NULL;
		for (char *p = cd; p + 10 <= headers_end; p++) {
			if (strncasecmp(p, "filename=\"", 10) == 0) {
				fstart = p + 10;
				break;
			}
		}

		if (fstart && fstart < headers_end) {
			char *fq_end = fstart;
			while (fq_end < headers_end && *fq_end != '"')
				fq_end++;
			if (fq_end < headers_end) {
				fname_len = fq_end - fstart;
				if (fname_len >= sizeof(filename))
					fname_len = sizeof(filename) - 1;
				strncpy(filename, fstart, fname_len);
				filename[fname_len] = '\0';
			}
		}

		char *data_start = headers_end + 4;
		size_t data_remaining = body_len - (data_start - body);

		/* Find next boundary line "\r\n--xxxx" */
		char *next_sep = find_substr(
		        data_start,
		        data_remaining,
		        boundary_crlf,
		        strlen(boundary_crlf));
		size_t data_len;
		if (next_sep) {
			data_len = next_sep - data_start;
		} else {
			data_len = data_remaining; /* last part */
		}

		/* Enforce limits */
		if (field_count >= mpfd_max_field_count) {
			set_error(
			        "Too many fields (max %zu)",
			        mpfd_max_field_count);
			return -2;
		}

		if (data_len > mpfd_max_field_size) {
			set_error(
			        "Field '%s' too large (max %zu bytes)",
			        key,
			        mpfd_max_field_size);
			return -2;
		}

		total_size += data_len;
		if (total_size > mpfd_max_total_size) {
			set_error(
			        "Total size too large (max %zu bytes)",
			        mpfd_max_total_size);
			return -2;
		}

		/* Allocate value */
		size_t total = fname_len + data_len;
		struct mpfd_val *val = malloc(sizeof(struct mpfd_val) + total);
		if (!val) {
			set_error("Memory allocation failed");
			return -2;
		}

		val->len = (uint32_t)data_len;
		val->filename_len = (uint32_t)fname_len;

		if (fname_len)
			memcpy(val->data, filename, fname_len);
		if (data_len)
			memcpy(val->data + fname_len, data_start, data_len);

		/* Store value in qmap - qmap takes ownership of the pointer */
		qmap_put(mpfd_db, key, val);
		field_count++;

	next_part:
		pos = next_sep ? next_sep : (body + body_len);
	}

	clear_error();
	return 0;
}

/* NDX exports */

static void mpfd_clear(void)
{
	qmap_drop(mpfd_db);
	clear_error();
}

/* Parse & Lifecycle */
NDX_LISTENER(int, mpfd_parse, socket_t, fd, char *, body)
{
	char content_type[512] = { 0 };
	char clen_str[32] = { 0 };

	ndc_env_get(fd, content_type, "HTTP_CONTENT_TYPE");
	ndc_env_get(fd, clen_str, "HTTP_CONTENT_LENGTH");

	/* Not multipart - not an error, just skip */
	if (!strstr(content_type, "multipart/form-data")) {
		return -1;
	}

	size_t body_len = clen_str[0] ? strtoul(clen_str, NULL, 10) : 0;

	/* Clear previous data */
	mpfd_clear();

	/* Parse and return result */
	return parse_multipart(body, content_type, body_len);
}

/* Field Inspection - All O(1) */
static int mpfd_exists(const char *name)
{
	return qmap_get(mpfd_db, name) != NULL ? 1 : 0;
}

NDX_LISTENER(int, mpfd_len, const char *, name)
{
	struct mpfd_val *val = (struct mpfd_val *)qmap_get(mpfd_db, name);
	return val ? (int)val->len : -1;
}

static int mpfd_filename(const char *name, char *buf, size_t buf_len)
{
	struct mpfd_val *val = (struct mpfd_val *)qmap_get(mpfd_db, name);
	if (!val || val->filename_len == 0)
		return -1;
	size_t to_copy =
	        val->filename_len < buf_len ? val->filename_len : buf_len;
	memcpy(buf, val->data, to_copy);
	if (to_copy > 0 && buf_len > to_copy)
		buf[to_copy] = '\0';
	return (int)val->filename_len;
}

/* Data Retrieval */
NDX_LISTENER(int, mpfd_get, const char *, name, char *, buf, size_t, buf_len)
{
	struct mpfd_val *val = (struct mpfd_val *)qmap_get(mpfd_db, name);
	if (!val)
		return -1;
	size_t to_copy = val->len < buf_len ? val->len : buf_len;
	memcpy(buf, val->data + val->filename_len, to_copy);
	if (to_copy > 0 && buf_len > to_copy)
		buf[to_copy] = '\0';
	return (int)val->len;
}

static int mpfd_save(const char *name, const char *path)
{
	struct mpfd_val *val = (struct mpfd_val *)qmap_get(mpfd_db, name);
	if (!val)
		return -1;
	FILE *fp = fopen(path, "wb");
	if (!fp)
		return -2;
	fwrite(val->data + val->filename_len, 1, val->len, fp);
	fclose(fp);
	return 0;
}

/* Configuration */
static int mpfd_set_limits(size_t max_field_size, size_t max_total_size)
{
	mpfd_max_field_size = max_field_size;
	mpfd_max_total_size = max_total_size;
	return 0;
}
