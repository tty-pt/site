/* mpfd_get_all: repeated multipart parts accumulate (QM_SORTED |
 * QM_MULTIVALUE) and join '\n'-separated; single-part readers are
 * unaffected (qmap_get keeps returning the first match). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *test_content_type;
static const char *test_content_length;

#include "../../mods/mpfd/mpfd.c"

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

/* Split on '\n' and report whether exactly the expected tokens (in any
 * order — duplicates iterate sorted, not in document order) were found. */
static int joined_has_tokens(
        const char *joined, const char *const *tokens, int ntokens)
{
	char buf[1024];
	int matched[8] = { 0 };
	int count = 0;
	char *tok;
	char *save;

	if (!joined || strlen(joined) >= sizeof(buf))
		return 0;
	memcpy(buf, joined, strlen(joined) + 1);

	for (tok = strtok_r(buf, "\n", &save); tok;
	        tok = strtok_r(NULL, "\n", &save)) {
		int found = 0;
		int i;
		count++;
		for (i = 0; i < ntokens; i++) {
			if (!matched[i] && strcmp(tok, tokens[i]) == 0) {
				matched[i] = 1;
				found = 1;
				break;
			}
		}
		if (!found)
			return 0;
	}
	if (count != ntokens)
		return 0;
	for (count = 0; count < ntokens; count++)
		if (!matched[count])
			return 0;
	return 1;
}

static size_t make_body(char *body)
{
	static const char body_tpl[] =
	        "--AaB03x\r\n"
	        "Content-Disposition: form-data; name=\"type\"\r\n\r\n"
	        "%s\r\n"
	        "--AaB03x\r\n"
	        "Content-Disposition: form-data; name=\"type\"\r\n\r\n"
	        "%s\r\n"
	        "--AaB03x\r\n"
	        "Content-Disposition: form-data; name=\"type\"\r\n\r\n"
	        "%s\r\n"
	        "--AaB03x\r\n"
	        "Content-Disposition: form-data; name=\"title\"\r\n\r\n"
	        "Solo\r\n"
	        "--AaB03x\r\n"
	        "Content-Disposition: form-data; name=\"pdf\"; "
	        "filename=\"sheet.pdf\"\r\n"
	        "Content-Type: application/pdf\r\n\r\n"
	        "%%PDFdata\r\n"
	        "--AaB03x--\r\n";

	return (size_t)snprintf(body, 4096, body_tpl, "folk", "pão",
	        "jazz");
}

static int parse_body(const char *body)
{
	test_content_type = "multipart/form-data; boundary=AaB03x";
	test_content_length = NULL;
	static char length[32];
	snprintf(length, sizeof(length), "%zu", strlen(body));
	test_content_length = length;
	return mpfd_parse(7, (char *)body);
}

int main(void)
{
	char body[4096];
	char out[256];
	char small[4];
	int rc;
	int measured;

	xy_install();

	size_t body_len = make_body(body);
	(void)body_len;
	rc = parse_body(body);
	CHECK("parse ok", rc == 0);

	/* Measure mode returns untruncated joined length
	 * ("folk\npão\njazz" — 11 ASCII + 3 UTF-8 = 14). */
	measured = mpfd_get_all("type", NULL, 0);
	CHECK("measure length", measured == 14);
	CHECK("measure matches fill",
	        measured == mpfd_get_all("type", out, sizeof(out)));

	CHECK("join has all three tokens",
	        joined_has_tokens(out,
	                (const char *const[]){ "folk", "pão", "jazz" }, 3));

	/* Truncation stays NUL-safe. */
	rc = mpfd_get_all("type", small, sizeof(small));
	CHECK("truncated copy length", rc == 3);
	CHECK("truncated NUL-safe", memchr(small, '\0', sizeof(small)) !=
	                                   NULL);

	/* Single-part reader untouched: first match wins. */
	rc = mpfd_get("title", out, sizeof(out));
	CHECK("mpfd_get single part", rc == 4 && strcmp(out, "Solo") == 0);

	/* File parts expose their filename separately, never their data,
	 * through the multi reader either. */
	measured = mpfd_get_all("pdf", NULL, 0);
	CHECK("file part not joined as text",
	        measured <= 0 || strstr(out, "PDF") == NULL);

	/* Absent name. */
	CHECK("absent name is -1", mpfd_get_all("nope", NULL, 0) == -1);
	CHECK("absent name fill is -1",
	        mpfd_get_all("nope", out, sizeof(out)) == -1);

	/* Re-parse clears prior request state (drop keeps handle valid). */
	rc = parse_body("--AaB03x\r\n"
	                "Content-Disposition: form-data; "
	                "name=\"type\"\r\n\r\n"
	                "blues\r\n"
	                "--AaB03x--\r\n");
	CHECK("re-parse ok", rc == 0);
	rc = mpfd_get_all("type", out, sizeof(out));
	CHECK("state cleared between parses",
	        rc == 5 && strcmp(out, "blues") == 0);

	printf("%s\n", failures ? "FAILED" : "ALL PASS");
	return failures ? 1 : 0;
}
