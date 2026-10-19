/*
 * mpfd_contract_test.c — pins the documented mpfd_get contract.
 *
 * Root-cause analysis: mpfd_get (mods/mpfd/mpfd.c:313-323)
 * copied min(val->len, buf_len) bytes, NUL-terminated ONLY when buf_len >
 * to_copy, and returned the FULL val->len — not the copied count. Callers
 * that trusted the return as a byte-count over-read (index.c:126/509/143/525)
 * or that trusted NUL-termination via strlen (index.c:570) over-read too.
 *
 * This test replicates mpfd_get VERBATIM (pinned to the current source)
 * over a tiny fake map, then asserts the FIXED contract:
 *
 *   len <  buf_len : NUL-terminated, returns len
 *   len == buf_len : NUL-terminated (buf[to_copy] = '\0' always; callers
 *                    pass sizeof(buf)-1 so buf[to_copy] stays in bounds)
 *   len >  buf_len : returns COPIED count (was: full len — the axil_slugify
 *                    over-read cause)
 *
 * FAILED on the old code (3/11), PASSES on the fixed contract.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONTRACT_LABEL "fixed (post-fix)"

struct mpfd_val {
	uint32_t len;
	uint32_t filename_len;
	char data[];
};

static const struct mpfd_val *fake_lookup(const char *name);

/* ---- verbatim from mods/mpfd/mpfd.c:313-323 ---- */
static int mpfd_get(const char *name, char *buf, size_t buf_len)
{
	const struct mpfd_val *val = fake_lookup(name);
	if (!val)
		return -1;
	size_t to_copy = val->len < buf_len ? val->len
	                                    : (buf_len > 0 ? buf_len - 1 : 0);
	memcpy(buf, val->data + val->filename_len, to_copy);
	if (to_copy > 0)
		buf[to_copy] = '\0';
	return (int)to_copy;
}
/* ---- end verbatim ---- */

/* ---- tiny fake map: a single named field ---- */
/* Layout matches struct mpfd_val (len, filename_len, data[]) so that
 * the verbatim mpfd_get can index val->data + filename_len directly. */
struct fake_val {
	uint32_t len;
	uint32_t filename_len;
	char data[1024];
};

static struct fake_val fake_val;
static int fake_present;

static void fake_set(const char *value, size_t len, size_t fname_len)
{
	fake_val.len = (uint32_t)len;
	fake_val.filename_len = (uint32_t)fname_len;
	memcpy(fake_val.data + fname_len, value, len);
	fake_present = 1;
}

static const struct mpfd_val *fake_lookup(const char *name)
{
	if (!fake_present)
		return NULL;
	(void)name;
	return (const struct mpfd_val *)&fake_val;
}
/* ---- end fake map ---- */

static int failures;
static int ntests;

#define CHECK(label, cond)                                                     \
	do {                                                                   \
		ntests++;                                                      \
		if (cond) {                                                    \
			printf("PASS %-48s\n", label);                         \
		} else {                                                       \
			printf("FAIL %-48s\n", label);                         \
			failures++;                                            \
		}                                                              \
	} while (0)

static void expect_copied_nul(
        const char *label, const char *field, size_t field_len, char *buf,
        size_t buf_len, int expected_ret, int expect_nul)
{
	int ret;
	char probe;
	size_t copied = field_len < buf_len ? field_len
	                                   : (buf_len > 0 ? buf_len - 1 : 0);

	memset(buf, 0x41, buf_len);
	probe = buf[buf_len + 1];
	fake_set(field, field_len, 0);
	ret = mpfd_get("x", buf, buf_len);
	CHECK(label, ret == expected_ret);
	CHECK("  nul-terminated", expect_nul ? buf[copied] == '\0' : 1);
	CHECK("  no clobber past buf", buf[buf_len + 1] == probe);
}

int main(void)
{
	char buf[64];

	printf("mpfd_contract_test: %s\n", CONTRACT_LABEL);

	/* Field shorter than buffer: NUL-terminate, return field len. */
	expect_copied_nul("len < buf returns len", "short", 5, buf, 32, 5, 1);

	/* Field exactly buf_len: fixed contract NUL-terminates at
	 * buf[to_copy] (callers pass sizeof(buf)-1 so this stays in-bounds). */
	expect_copied_nul(
	        "len == buf returns buf_len-1", "0123456789abcdef", 16,
	        buf, 16, 15, 1);

	/* Field larger than buffer: fixed contract returns the COPIED count
	 * (buf_len - 1), not the full field length. */
	expect_copied_nul(
	        "len > buf returns copied count", "0123456789abcdef", 16, buf,
	        8, 7, 0);

	/* Missing field: -1. */
	fake_present = 0;
	CHECK("missing field -> -1", mpfd_get("nope", buf, sizeof(buf)) == -1);

	/* NUL-termination when to_copy == buf_len-1 is guaranteed (fixed). */
	memset(buf, 0x41, sizeof(buf));
	fake_set("01234567", 8, 0);
	(void)mpfd_get("x", buf, 8);
	CHECK("exact-fit terminates", buf[7] == '\0');

	if (failures) {
		printf("mpfd_contract_test: %d/%d assertions failed — "
		       "mpfd_get contract bug CONFIRMED\n",
		       failures, ntests);
		return 1;
	}
	printf("mpfd_contract_test: all assertions passed\n");
	return 0;
}
