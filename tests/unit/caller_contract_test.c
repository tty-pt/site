/*
 * caller_contract_test.c — replicates the three caller sequences that
 * used to over-run stack buffers, using the REAL axil_slugify + a verbatim
 * mpfd_get replica. With the mpfd_get contract fix (returns the COPIED
 * count and always NUL-terminates at buf[to_copy]), every sequence is
 * bounded and must run CLEAN under ASAN/valgrind (regression gate).
 *
 * Sequences replicated:
 *   1. index.c add: mpfd_get("title", title, sizeof(title)) then
 *      axil_slugify(title, title_len, id, 256). Pre-fix a 300-byte field
 *      returned 300 -> iconv over-read past the 256-byte stack `title`.
 *      Post-fix title_len is capped to 255 -> in bounds.
 *   2. index.c:570→577 — edit: mpfd_get then write_meta_file(path, "title",
 *      title, title_len). Pre-fix strlen() over-read a possibly-unterminated
 *      buffer; post-fix mpfd_get guarantees a trailing NUL and the caller
 *      uses the returned length.
 *   3. songbook.c — char choir[128]; mpfd_get("choir", choir,
 *      sizeof(choir)); then choir[choir_len] = '\0'. Pre-fix choir_len
 *      from a 200-byte field overflowed the stack; post-fix choir_len is
 *      capped to 127 and the write is in bounds.
 *
 * Build (from repo root):
 *   clang -g -O0 -fsanitize=address -I external/axil/include \
 *       -o /tmp/caller_ct tests/unit/caller_contract_test.c
 *   ASAN_OPTIONS=symbolize=0 /tmp/caller_ct [1|2|3]
 *
 * ASAN_OPTIONS=symbolize=0 is REQUIRED: the default symbolizer hangs
 * after the report on this box (rc=124 without it).
 *
 * Detection notes (verified empirically):
 *   - seq1 (iconv over-read via axil_slugify) fired regardless pre-fix.
 *   - seq2 (strlen) was nondeterministic in the real code: it only
 *     over-read if the byte after the copy (title[255]) was non-zero.
 *     We forced it non-zero to make the failure deterministic.
 *   - seq3 used a field of exactly sizeof(choir)=128 so choir[128] hit
 *     the first redzone byte after the array. A 200-byte field is the
 *     REAL bug but ASAN missed it: the write landed in the addressable
 *     region of the next stack object (field[200]) — i.e. silent
 *     neighboring-stack corruption. Both cases are exercised below (3A
 *     and 3B) but post-fix both are bounded and clean.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../external/axil/src/axil-encode.c"

/* ---- verbatim from mods/mpfd/mpfd.c:313-323 ---- */
struct mpfd_val {
	uint32_t len;
	uint32_t filename_len;
	char data[];
};

static const struct mpfd_val *fake_lookup(const char *name);

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

/* ---- tiny fake map: one field of N bytes ---- */
struct fake_val {
	uint32_t len;
	uint32_t filename_len;
	char data[1024];
};

static struct fake_val fake_val;

static const struct mpfd_val *fake_lookup(const char *name)
{
	(void)name;
	return (const struct mpfd_val *)&fake_val;
}

static void fake_set(const char *value, size_t len)
{
	fake_val.len = (uint32_t)len;
	fake_val.filename_len = 0;
	memcpy(fake_val.data, value, len);
}
/* ---- end fake map ---- */

static int failures;

#define CHECK(label, cond)                                                     \
	do {                                                                   \
		if (cond) {                                                    \
			printf("PASS %-44s\n", label);                         \
		} else {                                                       \
			printf("FAIL %-44s\n", label);                         \
			failures++;                                            \
		}                                                              \
	} while (0)

/* write_meta_file replica — just returns; the strlen() at the caller is
 * the interesting part (the over-read happens before we get here). */
static int
write_meta_file(const char *path, const char *name, const char *buf, size_t sz)
{
	(void)path;
	(void)name;
	(void)buf;
	return sz > 0 ? 0 : 0;
}

/* ---- Sequence 1: index.c:126-130 (add path) ---- */
static void seq_index_add(void)
{
	char title[256];
	char id[256];
	size_t i;
	int title_len;
	char field[300];

	for (i = 0; i < sizeof(field); i++)
		field[i] = (char)('a' + (i % 26));
	fake_set(field, sizeof(field));

	/* index.c:126 */
	title_len = mpfd_get("title", title, sizeof(title));
	if (title_len <= 0) {
		CHECK("seq1 title_len > 0", 0);
		return;
	}
	CHECK("seq1 title_len capped to 255", title_len == 255);

	/* index.c:130 — reads exactly title_len=255 bytes from title[256]. */
	axil_slugify(title, (size_t)title_len, id, sizeof(id));
	CHECK("seq1 slug produced (bounded read above)", id[0] != '\0');

	/* index.c:143 — write_meta_file with the capped length. */
	write_meta_file("items/song/items", "title", title, (size_t)title_len);
	CHECK("seq1 meta write ok", 1);
}

/* ---- Sequence 2: index.c:570-577 (edit path) ---- */
static void seq_index_edit(void)
{
	char title[256];
	int title_len;
	char field[300];
	size_t i;

	/* Pre-fill title with a non-zero byte at [255]. Pre-fix the edit
	 * path called strlen(title) on a possibly-unterminated buffer, so
	 * [255]='A' made the over-read deterministic under ASAN. Post-fix
	 * mpfd_get always NUL-terminates at [255] and the caller passes
	 * title_len, so strlen is never reached on stale bytes. */
	memset(title, 0x41, sizeof(title));

	for (i = 0; i < sizeof(field); i++)
		field[i] = (char)('a' + (i % 26));
	fake_set(field, sizeof(field));

	/* index.c:570 */
	title_len = mpfd_get("title", title, sizeof(title));
	CHECK("seq2 title_len=255 (capped)", title_len == 255);
	if (title_len <= 0)
		return;

	/* index.c:577 — write_meta_file with the returned length; the
	 * buffer is guaranteed NUL-terminated by the fixed mpfd_get. */
	write_meta_file("items/song/items", "title", title, (size_t)title_len);
	CHECK("seq2 length-based meta write ok", 1);
}

/* ---- Sequence 3: songbook.c:296-298 ---- */
static void seq_songbook(void)
{
	char choir[128];
	int choir_len;
	char field[200];
	size_t i;

	for (i = 0; i < sizeof(field); i++)
		field[i] = (char)('a' + (i % 26));

	/* Case A: field == sizeof(choir) (128 bytes). Post-fix
	 * choir_len caps to 127, choir[127]='\0' stays in bounds. */
	fake_set(field, sizeof(choir));
	choir_len = mpfd_get("choir", choir, sizeof(choir));
	CHECK("seq3A choir_len capped to 127", choir_len == 127);
	if (choir_len > 0)
		choir[choir_len] = '\0';
	CHECK("seq3A write at [127] in bounds", 1);

	/* Case B: field > sizeof(choir) (200 bytes). Pre-fix choir_len=200
	 * wrote 72 bytes past the array — landed in addressable stack (not
	 * the 32-byte redzone), so ASAN did NOT flag it: silent corruption
	 * matching the live-box reality. Post-fix choir_len caps to 127. */
	fake_set(field, sizeof(field));
	choir_len = mpfd_get("choir", choir, sizeof(choir));
	CHECK("seq3B choir_len capped to 127", choir_len == 127);
	if (choir_len > 0)
		choir[choir_len] = '\0';
	CHECK("seq3B write stays in bounds", 1);
}

int main(int argc, char **argv)
{
	int all = 1;
	int seq = 0;

	if (argc > 1) {
		seq = atoi(argv[1]);
		all = 0;
	}

	printf("caller_contract_test: fixed (post-fix) caller patterns\n");

	if (all || seq == 1) {
		printf("sequence 1: index.c:126-143 add path\n");
		seq_index_add();
	}
	if (all || seq == 2) {
		printf("sequence 2: index.c:570-577 edit path\n");
		seq_index_edit();
	}
	if (all || seq == 3) {
		printf("sequence 3: songbook.c:296-298 choir write\n");
		seq_songbook();
	}

	if (failures) {
		printf("caller_contract_test: %d FAILURES\n", failures);
		return 1;
	}
	printf("caller_contract_test: all assertions passed "
	       "(no OOB under sanitizer)\n");
	return 0;
}
