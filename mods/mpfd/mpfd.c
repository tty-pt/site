#include <ttypt/xy-mod.h>

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <unistd.h>
#include <limits.h>
#include <ttypt/axil.h>
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

static int parse_content_length(const char *value, size_t *length)
{
	size_t result = 0;

	if (!value || !*value)
		return -1;

	for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
		if (*p < '0' || *p > '9')
			return -1;
		size_t digit = (size_t)(*p - '0');
		if (result > (SIZE_MAX - digit) / 10)
			return -1;
		result = result * 10 + digit;
	}

	if (result == 0)
		return -1;
	*length = result;
	return 0;
}

/* Safe substring search that works even with binary data (embedded '\0') */
static char *find_substr(
        const char *haystack, size_t hay_len, const char *needle,
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
	        boundary, sizeof(boundary), "--%.*s", (int)(bend - bstart),
	        bstart);

	size_t blen = strlen(boundary);
	snprintf(boundary_crlf, sizeof(boundary_crlf), "\r\n%s", boundary);

	size_t total_size = 0;
	size_t field_count = 0;
	int found_closing_boundary = 0;

	char *pos = body;
	while (1) {
		char *bpos = find_substr(
		        pos, body_len - (pos - body), boundary, blen);
		if (!bpos)
			break;

		pos = bpos + blen;

		/* End of multipart? */
		size_t remaining = body_len - (size_t)(pos - body);
		if (remaining >= 2 && memcmp(pos, "--", 2) == 0) {
			remaining -= 2;
			if (remaining != 0 &&
			    (remaining != 2 || memcmp(pos + 2, "\r\n", 2) != 0))
			{
				set_error("Data follows closing multipart "
				          "boundary");
				return -1;
			}
			found_closing_boundary = 1;
			break;
		}

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
		        data_start, data_remaining, boundary_crlf,
		        strlen(boundary_crlf));
		if (!next_sep) {
			set_error("Missing closing multipart boundary");
			return -1;
		}
		size_t data_len = next_sep - data_start;

		/* Enforce limits */
		if (field_count >= mpfd_max_field_count) {
			set_error(
			        "Too many fields (max %zu)",
			        mpfd_max_field_count);
			return -2;
		}

		if (data_len > mpfd_max_field_size) {
			set_error(
			        "Field '%s' too large (max %zu bytes)", key,
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

		/* qmap copies custom values using mpfd_val_measure(). */
		qmap_put(mpfd_db, key, val);
		free(val);
		field_count++;

	next_part:
		pos = next_sep ? next_sep : (body + body_len);
	}

	if (!found_closing_boundary) {
		set_error("Missing closing multipart boundary");
		return -1;
	}

	clear_error();
	return 0;
}

/* XY exports */

static void mpfd_clear(void)
{
	qmap_drop(mpfd_db);
	clear_error();
}

/* Parse & Lifecycle */
XY_IMPL(int, mpfd_parse, socket_t, fd, char *, body)
{
	char content_type[512] = { 0 };
	char clen_str[BUFSIZ] = { 0 };
	size_t body_len;

	axil_header_get(fd, "Content-Type", content_type, sizeof(content_type));

	/* Not multipart - not an error, just skip */
	if (!strstr(content_type, "multipart/form-data")) {
		return -1;
	}

	/* Clear previous data */
	mpfd_clear();

	if (axil_header_get(fd, "Content-Length", clen_str, sizeof(clen_str)) !=
	            0 ||
	    parse_content_length(clen_str, &body_len) != 0 || !body)
	{
		set_error("Invalid Content-Length");
		return -1;
	}

	/* Axil buffers this many bytes before dispatch but exposes no body
	 * length. Keep binary data intact and validate the terminal boundary
	 * in-window. */
	int result = parse_multipart(body, content_type, body_len);
	if (result != 0)
		qmap_drop(mpfd_db);
	return result;
}

/* Field Inspection - All O(1) */
static int mpfd_exists(const char *name)
{
	return qmap_get(mpfd_db, name) != NULL ? 1 : 0;
}

XY_IMPL(int, mpfd_len, const char *, name)
{
	struct mpfd_val *val = (struct mpfd_val *)qmap_get(mpfd_db, name);
	return val ? (int)val->len : -1;
}

static int mpfd_filename(const char *name, char *buf, size_t buf_len)
{
	struct mpfd_val *val = (struct mpfd_val *)qmap_get(mpfd_db, name);
	if (!val || val->filename_len == 0)
		return -1;
	size_t to_copy = val->filename_len < buf_len
	                         ? val->filename_len
	                         : (buf_len > 0 ? buf_len - 1 : 0);
	memcpy(buf, val->data, to_copy);
	if (to_copy > 0)
		buf[to_copy] = '\0';
	return (int)to_copy;
}

/* Data Retrieval */
XY_IMPL(int, mpfd_get, const char *, name, char *, buf, size_t, buf_len)
{
	struct mpfd_val *val = (struct mpfd_val *)qmap_get(mpfd_db, name);
	if (!val)
		return -1;
	size_t to_copy =
	        val->len < buf_len ? val->len : (buf_len > 0 ? buf_len - 1 : 0);
	memcpy(buf, val->data + val->filename_len, to_copy);
	if (to_copy > 0)
		buf[to_copy] = '\0';
	return (int)to_copy;
}

/* All parts for `name` joined '\n'-separated. Repeated parts arrive
 * when a form posts N checked boxes sharing one name; ref_normalize()
 * downstream splits on newlines, slugifies and dedups. Returns -1 when
 * the name is absent. With buf == NULL / buf_len == 0 returns the
 * untruncated joined length. Duplicates iterate in sorted order, not
 * document order — harmless: multi-ref storage is order-insensitive.
 */
XY_IMPL(int, mpfd_get_all, const char *, name, char *, buf, size_t,
        buf_len)
{
	uint32_t cur = qmap_get_multi(mpfd_db, name);
	const void *k;
	const void *v;
	size_t total = 0;
	size_t pos = 0;

	if (cur == QM_MISS)
		return -1;

	if (!buf || buf_len == 0) {
		while (qmap_next(&k, &v, cur)) {
			const struct mpfd_val *val = v;
			total += val->len + 1; /* part + '\n' */
		}
		qmap_fin(cur);
		return total ? (int)(total - 1) : 0;
	}

	while (qmap_next(&k, &v, cur)) {
		const struct mpfd_val *val = v;
		size_t len = val->len;

		if (pos && pos < buf_len - 1)
			buf[pos++] = '\n';
		if (pos >= buf_len - 1)
			break;
		if (len > buf_len - 1 - pos)
			len = buf_len - 1 - pos;
		memcpy(buf + pos, val->data + val->filename_len, len);
		pos += len;
	}
	qmap_fin(cur);
	buf[pos] = '\0';
	return (int)pos;
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

XY_MODULE_API void xy_install(void)
{
	mpfd_val_type = qmap_mreg(mpfd_val_measure);
	/* QM_SORTED | QM_MULTIVALUE: browsers submit N checked boxes as
	 * N repeated parts named {key}; without MULTIVALUE each put
	 * REPLACES and the last part silently wins. With it, parts
	 * accumulate while qmap_get keeps returning the first match, so
	 * every single-part reader behaves exactly as before. */
	mpfd_db = qmap_open(NULL, NULL, QM_STR, mpfd_val_type, 0xFF,
	        QM_SORTED | QM_MULTIVALUE);
}
