/*
 * slugify_test.c — correctness suite for the real axil_slugify.
 *
 * Compiles the REAL external/axil/src/axil-encode.c (single self-contained
 * file) so the tests exercise the shipped implementation, not a copy.
 *
 * Intended semantics:
 *   - SAFE cases here must PASS under ASAN and valgrind both today and after
 *     the fix (they never pass a length larger than the provided buffer).
 *   - The over-read proof lives in caller_contract_test.c which replicates
 *     the buggy mpfd_get/axil_slugify caller pattern.
 *
 * Build (from repo root):
 *   clang -g -O0 -fsanitize=address \
 *       -I external/axil/include \
 *       -o /tmp/slugify_test tests/unit/slugify_test.c
 *
 * Exit status: 0 = all cases pass, 1 = a case failed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../external/axil/src/axil-encode.c"

static int failures;

#define CHECK(label, cond)                                                     \
	do {                                                                   \
		if (cond) {                                                    \
			printf("PASS %-40s\n", label);                         \
		} else {                                                       \
			printf("FAIL %-40s\n", label);                         \
			failures++;                                            \
		}                                                              \
	} while (0)

/* Live slugs — the exact strings from the crashing forms. */
static void test_live_slugs(void)
{
	char out[256];
	const char *t;

	t = "Apareceu no c\xc3\xa9u um sinal";
	axil_slugify(t, strlen(t), out, sizeof(out));
	CHECK("live title -> apareceu_no_ceu_um_sinal",
	      strcmp(out, "apareceu_no_ceu_um_sinal") == 0);

	t = "Jos\xc3\xa9 Geada";
	axil_slugify(t, strlen(t), out, sizeof(out));
	CHECK("author -> jose_geada", strcmp(out, "jose_geada") == 0);

	t = "Ofer\xc3\xb3rio";
	axil_slugify(t, strlen(t), out, sizeof(out));
	CHECK("type -> oferorio", strcmp(out, "oferorio") == 0);

	t = "Ave Maria";
	axil_slugify(t, strlen(t), out, sizeof(out));
	CHECK("ascii -> ave_maria", strcmp(out, "ave_maria") == 0);
}

/* Boundary lengths that are SAFE (never exceed the buffer). */
static void test_safe_boundaries(void)
{
	char out[256];
	char buf[256];
	size_t i;
	int rc;

	/* len == buf-1: reads exactly buf-1 bytes, within bounds. */
	memset(buf, 'x', sizeof(buf));
	for (i = 0; i < sizeof(buf) - 1; i++)
		buf[i] = (char)('a' + (i % 26));
	rc = axil_slugify(buf, sizeof(buf) - 1, out, sizeof(out));
	CHECK("len==buf-1 returns 0", rc == 0);
	CHECK("len==buf-1 has trailing NUL", out[strlen(out)] == '\0');

	/* len == buf: reads exactly buf bytes, within bounds. */
	rc = axil_slugify(buf, sizeof(buf), out, sizeof(out));
	CHECK("len==buf returns 0", rc == 0);

	/* Empty input -> "item" fallback. */
	rc = axil_slugify("", 0, out, sizeof(out));
	CHECK("empty -> item", rc == 0 && strcmp(out, "item") == 0);

	/* All-punctuation + one space -> single '_' (space folds, punct
	 * dropped). */
	rc = axil_slugify("!!! ???", 7, out, sizeof(out));
	CHECK("punct+space -> _", rc == 0 && strcmp(out, "_") == 0);

	/* Punct-only, no spaces -> "item" fallback. */
	rc = axil_slugify("!?*", 3, out, sizeof(out));
	CHECK("punct-only -> item", rc == 0 && strcmp(out, "item") == 0);

	/* NULL input -> -1, no crash. */
	rc = axil_slugify(NULL, 5, out, sizeof(out));
	CHECK("NULL title -> -1", rc == -1);

	/* Zero-length result buffer -> -1, no crash. */
	rc = axil_slugify("x", 1, NULL, 0);
	CHECK("zero result_len -> -1", rc == -1);
}

/* Invalid UTF-8 must not crash and must degrade to '?' / skip. */
static void test_invalid_utf8(void)
{
	char out[256];
	char bad[8];
	int rc;

	/* Lone continuation byte 0x80. */
	bad[0] = (char)0x80;
	rc = axil_slugify(bad, 1, out, sizeof(out));
	CHECK("lone continuation no crash", rc == 0);

	/* Truncated 3-byte sequence (E2 82 then EOF). */
	bad[0] = (char)0xe2;
	bad[1] = (char)0x82;
	rc = axil_slugify(bad, 2, out, sizeof(out));
	CHECK("truncated seq no crash", rc == 0);

	/* 0xFF lead byte. */
	bad[0] = (char)0xff;
	rc = axil_slugify(bad, 1, out, sizeof(out));
	CHECK("0xFF no crash", rc == 0);

	/* Invalid byte embedded in ASCII. */
	{
		char mix[16];
		mix[0] = 'a';
		mix[1] = (char)0xff;
		mix[2] = 'b';
		rc = axil_slugify(mix, 3, out, sizeof(out));
		CHECK("mixed invalid no crash", rc == 0);
		CHECK("mixed invalid keeps ascii a", strchr(out, 'a') != NULL);
		CHECK("mixed invalid keeps ascii b", strchr(out, 'b') != NULL);
	}
}

/* Unmappable / non-ASCII script chars degrade to '?' (TRANSLIT). */
static void test_unmappable(void)
{
	char out[256];
	int rc;

	/* CJK ideograph U+6771 'dong' (E6 9D B1). */
	rc = axil_slugify("\xe6\x9d\xb1", 3, out, sizeof(out));
	CHECK("CJK no crash", rc == 0);

	/* Emoji U+1F600 (F0 9F 98 80). */
	rc = axil_slugify("\xf0\x9f\x98\x80", 4, out, sizeof(out));
	CHECK("emoji no crash", rc == 0);

	/* Greek letter 'α' U+03B1 (CE B1). */
	rc = axil_slugify("\xce\xb1", 2, out, sizeof(out));
	CHECK("greek no crash", rc == 0);
}

/* Case folding + underscore substitution. */
static void test_folding(void)
{
	char out[256];
	int rc;

	rc = axil_slugify("Hello World 123", 15, out, sizeof(out));
	CHECK("fold -> hello_world_123",
	      rc == 0 && strcmp(out, "hello_world_123") == 0);

	rc = axil_slugify("a--b__c", 7, out, sizeof(out));
	CHECK("dashes dropped keep __ -> ab__c",
	      rc == 0 && strcmp(out, "ab__c") == 0);
}

#ifndef ICONV_LABEL
#define ICONV_LABEL "glibc"
#endif

int main(void)
{
	printf("slugify_test: iconv implementation = %s\n", ICONV_LABEL);
	test_live_slugs();
	test_safe_boundaries();
	test_invalid_utf8();
	test_unmappable();
	test_folding();

	if (failures) {
		printf("slugify_test: %d FAILURES\n", failures);
		return 1;
	}
	printf("slugify_test: all cases passed\n");
	return 0;
}
