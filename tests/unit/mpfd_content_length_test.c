#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *test_content_type;
static const char *test_content_length;

#include "../../mods/mpfd/mpfd.c"

static char *stored_key;
static void *stored_value;
static qmap_measure_t *stored_measure;

static void discard_log(int type, const char *fmt, ...)
{
	(void)type;
	(void)fmt;
}

qsyslog_t qsyslog = discard_log;

int axil_header_get(socket_t fd, const char *key, char *buf, size_t buf_len)
{
	const char *value = NULL;
	(void)fd;

	if (strcmp(key, "Content-Type") == 0)
		value = test_content_type;
	else if (strcmp(key, "Content-Length") == 0)
		value = test_content_length;
	if (!value)
		return -1;

	size_t len = strlen(value);
	if (len >= buf_len)
		len = buf_len - 1;
	memcpy(buf, value, len);
	buf[len] = '\0';
	return 0;
}

unsigned xy_areg(char *name, xy_adapter_t *adapter)
{
	(void)name;
	(void)adapter;
	return 0;
}

uint32_t qmap_mreg(qmap_measure_t *measure)
{
	stored_measure = measure;
	return 10;
}

uint32_t qmap_open(
        const char *filename, const char *database, uint32_t ktype,
        uint32_t vtype, uint32_t mask, uint32_t flags)
{
	(void)filename;
	(void)database;
	(void)ktype;
	(void)vtype;
	(void)mask;
	(void)flags;
	return 1;
}

void qmap_drop(uint32_t hd)
{
	(void)hd;
	free(stored_key);
	free(stored_value);
	stored_key = NULL;
	stored_value = NULL;
}

uint32_t qmap_put(uint32_t hd, const void *const key, const void *const value)
{
	(void)hd;
	qmap_drop(hd);
	stored_key = strdup(key);
	stored_value = malloc(stored_measure(value));
	if (!stored_key || !stored_value)
		exit(2);
	memcpy(stored_value, value, stored_measure(value));
	return 1;
}

const void *qmap_get(uint32_t hd, const void *const key)
{
	(void)hd;
	return stored_key && strcmp(stored_key, key) == 0 ? stored_value : NULL;
}

static int failures;

#define CHECK(label, condition)                                                \
	do {                                                                   \
		if (condition)                                                 \
			printf("PASS %s\n", label);                            \
		else {                                                         \
			printf("FAIL %s\n", label);                            \
			failures++;                                            \
		}                                                              \
	} while (0)

static size_t make_body(unsigned char *body)
{
	static const char prefix[] =
	        "--AaB03x\r\n"
	        "Content-Disposition: form-data; name=\"file\"; "
	        "filename=\"x.bin\"\r\n"
	        "Content-Type: application/octet-stream\r\n\r\n";
	static const unsigned char data[] = { 'A', 0, 'B' };
	static const char suffix[] = "\r\n--AaB03x--\r\n";
	size_t len = 0;

	memcpy(body + len, prefix, sizeof(prefix) - 1);
	len += sizeof(prefix) - 1;
	memcpy(body + len, data, sizeof(data));
	len += sizeof(data);
	memcpy(body + len, suffix, sizeof(suffix) - 1);
	return len + sizeof(suffix) - 1;
}

static int parse_with_length(unsigned char *body, const char *length)
{
	test_content_type = "multipart/form-data; boundary=AaB03x";
	test_content_length = length;
	return mpfd_parse(7, (char *)body);
}

int main(void)
{
	unsigned char body[512];
	char length[64];
	char overflow[64];
	char out[4];
	size_t body_len = make_body(body);

	xy_install();
	snprintf(length, sizeof(length), "%zu", body_len);
	CHECK("valid multipart accepted", parse_with_length(body, length) == 0);
	CHECK("binary field length preserved", mpfd_len("file") == 3);
	CHECK("embedded NUL preserved",
	      mpfd_get("file", out, sizeof(out)) == 3 && out[0] == 'A' &&
	              out[1] == 0 && out[2] == 'B');

	test_content_length = NULL;
	CHECK("missing Content-Length rejected",
	      mpfd_parse(7, (char *)body) < 0);
	CHECK("zero Content-Length rejected", parse_with_length(body, "0") < 0);
	CHECK("signed Content-Length rejected",
	      parse_with_length(body, "+1") < 0);
	CHECK("invalid Content-Length rejected",
	      parse_with_length(body, "12x") < 0);
	snprintf(overflow, sizeof(overflow), "%zu0", SIZE_MAX);
	CHECK("overflowing Content-Length rejected",
	      parse_with_length(body, overflow) < 0);

	snprintf(length, sizeof(length), "%zu", body_len - 1);
	CHECK("short declared length rejected",
	      parse_with_length(body, length) < 0);
	body[body_len] = 'X';
	snprintf(length, sizeof(length), "%zu", body_len + 1);
	CHECK("long declared length rejected",
	      parse_with_length(body, length) < 0);
	CHECK("failed parse clears fields", mpfd_len("file") == -1);

	qmap_drop(mpfd_db);
	if (failures)
		printf("mpfd_content_length_test: %d failures\n", failures);
	else
		printf("mpfd_content_length_test: all assertions passed\n");
	return failures != 0;
}
