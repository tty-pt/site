/*
 * slug_fuzz.c — fuzzes the REAL axil_slugify from external/axil/src/
 * axil-encode.c. Two fuzz dimensions:
 *
 *   MODE 0 (correct usage): title buffer is exactly `size` bytes, so
 *   iconv reads within bounds. Detects crashes in the transliteration
 *   loop itself (EILSEQ/EINVAL handling, infinite loops) on arbitrary
 *   byte soup — a robustness test that must PASS even pre-fix.
 *
 *   MODE 1 (fixed caller contract): title buffer is a FIXED 128-byte
 *   stack array and the caller passes a CAPPED length (min(size,
 *   SMALL_BUF)), exactly as the fixed index.c:126/130 pattern does with
 *   the mpfd_get contract (returns the copied count). Pre-fix this mode
 *   reproduced the iconv over-read (300-byte length vs 256-byte buffer);
 *   post-fix it exercises the small-stack-buffer transliteration with a
 *   bounded length and must stay clean.
 *
 * Build (from repo root):
 *   clang -g -O0 -fsanitize=address \
 *       -I external/axil/include \
 *       -o /tmp/slug_fuzz tests/unit/slug_fuzz.c
 *   ASAN_OPTIONS=symbolize=0 /tmp/slug_fuzz [mode] [iterations] [seed]
 *
 * libFuzzer build (needs clang -fsanitize=fuzzer):
 *   clang -g -O0 -fsanitize=fuzzer,address -DSLUG_FUZZ_NO_MAIN \
 *       -I external/axil/include \
 *       -o /tmp/slug_fuzz_lf tests/unit/slug_fuzz.c
 *   (/tmp/slug_fuzz_lf -runs=2000)
 *
 * SLUG_FUZZ_NO_MAIN suppresses the deterministic main() so libFuzzer's
 * own main wins at link time. __has_feature(fuzzing) is NOT defined by
 * clang 18, so the macro must be passed explicitly.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../external/axil/src/axil-encode.c"

#define DEFAULT_MODE 1
#define DEFAULT_ITERS 20000
#define MAX_INPUT 4096
#define SMALL_BUF 128

static unsigned int rng_state;

static unsigned int next_rand(void)
{
	rng_state = rng_state * 1103515245u + 12345u;
	return (rng_state >> 16) & 0x7fff;
}

static void fill_soup(unsigned char *buf, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		unsigned int r = next_rand() % 100;
		if (r < 45)
			buf[i] = (unsigned char)('a' + (next_rand() % 26));
		else if (r < 60)
			buf[i] = (unsigned char)(next_rand() & 0x7f);
		else if (r < 75)
			buf[i] = (unsigned char)(0x80 + (next_rand() & 0x3f));
		else if (r < 85)
			buf[i] = (unsigned char)(0xc0 + (next_rand() & 0x3f));
		else if (r < 92)
			buf[i] = (unsigned char)(0xe0 + (next_rand() & 0x1f));
		else if (r < 97)
			buf[i] = (unsigned char)(0xf0 + (next_rand() & 0x0f));
		else
			buf[i] = 0;
	}
}

static void run_one_mode0(const unsigned char *in, size_t size)
{
	char out[256];

	axil_slugify((const char *)in, size, out, sizeof(out));
}

static void run_one_mode1(const unsigned char *in, size_t size)
{
	char buf[SMALL_BUF];
	char out[256];
	size_t len = size < SMALL_BUF ? size : SMALL_BUF;

	memcpy(buf, in, len);
	axil_slugify(buf, len, out, sizeof(out));
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size == 0 || size > MAX_INPUT)
		return 0;
	run_one_mode0(data, size);
	run_one_mode1(data, size);
	return 0;
}

static int run_deterministic(int mode, int iters, unsigned int seed)
{
	unsigned char in[MAX_INPUT];
	size_t size;
	int i;

	rng_state = seed ? seed : 1;
	for (i = 0; i < iters; i++) {
		size = 1 + (next_rand() % (MAX_INPUT - 1));
		fill_soup(in, size);
		if (mode == 0)
			run_one_mode0(in, size);
		else
			run_one_mode1(in, size);
	}
	printf("slug_fuzz: mode %d, %d iterations (seed %u): "
	       "no crash\n",
	       mode, iters, seed);
	return 0;
}

#ifndef SLUG_FUZZ_NO_MAIN
int main(int argc, char **argv)
{
	int mode = DEFAULT_MODE;
	int iters = DEFAULT_ITERS;
	unsigned int seed = 0;

	if (argc > 1)
		mode = atoi(argv[1]);
	if (argc > 2)
		iters = atoi(argv[2]);
	if (argc > 3)
		seed = (unsigned int)strtoul(argv[3], NULL, 10);

	if (mode == 1) {
		printf("slug_fuzz: fixed-caller mode "
		       "(SMALL_BUF=%d, len capped to buffer)\n",
		       SMALL_BUF);
	} else {
		printf("slug_fuzz: correct-usage robustness mode\n");
	}
	return run_deterministic(mode, iters, seed);
}
#endif
