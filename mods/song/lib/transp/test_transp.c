/*
 * test_transp.c - Unit tests for transp library
 */

#include "transp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define TEST(name) void test_##name(void)
#define RUN_TEST(name)                           \
	do {                                     \
		printf("Running " #name "... "); \
		test_##name();                   \
		printf("PASS\n");                \
	} while (0)

/* Helper: check if string contains substring */
static int str_contains(const char *haystack, const char *needle) {
	return strstr(haystack, needle) != NULL;
}

TEST(basic_transpose) {
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "C G", 2, 0);
	assert(result != NULL);
	assert(str_contains(result, "D A"));
	free(result);

	transp_free(ctx);
}

TEST(transpose_with_modifiers) {
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "Cmaj7 Dm7 G7sus4", 5, 0);
	assert(result != NULL);
	assert(str_contains(result, "Fmaj7"));
	assert(str_contains(result, "Gm7"));
	assert(str_contains(result, "C7sus4"));
	free(result);

	transp_free(ctx);
}

TEST(transpose_minor_chords) {
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "Am Dm Em", 2, 0);
	assert(result != NULL);
	assert(str_contains(result, "Bm"));
	assert(str_contains(result, "Em"));
	assert(str_contains(result, "F#m"));
	free(result);

	transp_free(ctx);
}

TEST(transpose_negative) {
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "D A Bm G", -2, 0);
	assert(result != NULL);
	assert(str_contains(result, "C G Am F"));
	free(result);

	transp_free(ctx);
}

TEST(html_output) {
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "C G", 2, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "<div>"));
	assert(str_contains(result, "<b>D A</b>"));
	assert(str_contains(result, "</div>"));
	free(result);

	transp_free(ctx);
}

TEST(flat_notation) {
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	/* C# with bemol flag should become Db */
	char *result = transp_buffer(ctx, "C", 1, TRANSP_BEMOL);
	assert(result != NULL);
	assert(str_contains(result, "Db"));
	assert(!str_contains(result, "C#"));
	free(result);

	transp_free(ctx);
}

TEST(latin_notation) {
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "C G Am", 0, TRANSP_LATIN);
	assert(result != NULL);
	assert(str_contains(result, "Do"));
	assert(str_contains(result, "Sol"));
	assert(str_contains(result, "La-")); /* Latin uses - for minor */
	free(result);

	transp_free(ctx);
}

TEST(multiline_input) {
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "C G Am F\nAmazing Grace", 2, 0);
	assert(result != NULL);
	assert(str_contains(result, "D A Bm G"));
	assert(str_contains(result, "Amazing Grace"));
	free(result);

	transp_free(ctx);
}

TEST(hide_chords) {
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "C G Am F\nTest lyrics here", 0, TRANSP_HIDE_CHORDS);
	assert(result != NULL);
	/* Should not contain chord names as standalone tokens */
	assert(!str_contains(result, "C "));
	assert(!str_contains(result, " G "));
	assert(!str_contains(result, "Am "));
	assert(!str_contains(result, " F"));
	assert(str_contains(result, "Test lyrics"));
	free(result);

	transp_free(ctx);
}

TEST(hide_lyrics) {
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "C G Am F\nTest lyrics here", 0, TRANSP_HIDE_LYRICS);
	assert(result != NULL);
	assert(str_contains(result, "C"));
	assert(str_contains(result, "G"));
	assert(!str_contains(result, "Test"));
	assert(!str_contains(result, "lyrics"));
	free(result);

	transp_free(ctx);
}

TEST(key_detection) {
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	/* Key should be detected from first chord */
	char *result = transp_buffer(ctx, "G D Em C", 0, 0);
	assert(result != NULL);
	assert(transp_get_key(ctx) == 7); /* G = index 7 */
	free(result);

	transp_free(ctx);
}

TEST(shift_table) {
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	/* First transpose to detect key */
	char *result = transp_buffer(ctx, "C G", 0, 0);
	free(result);

	/* Get shift table */
	char *table = transp_shift_table(ctx, 0);
	assert(table != NULL);
	assert(str_contains(table, "C 0"));
	assert(str_contains(table, "D 2"));
	assert(str_contains(table, "G 7"));
	free(table);

	transp_free(ctx);
}

TEST(repeat_markers_html) {
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "|: C G :|", 0, TRANSP_HTML);
	assert(result != NULL);
	/* Must be bolded */
	assert(str_contains(result, "<b>"));
	assert(str_contains(result, "</b>"));
	/* No stray closing tag before content */
	assert(!str_contains(result, "<div></b>"));
	/* Markers intact */
	assert(str_contains(result, "|:"));
	assert(str_contains(result, ":|"));
	/* Chords present */
	assert(str_contains(result, "C"));
	assert(str_contains(result, "G"));
	free(result);

	transp_free(ctx);
}

TEST(repeat_markers_transpose) {
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "|: C G :|", 2, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "<b>"));
	assert(!str_contains(result, "<div></b>"));
	assert(str_contains(result, "|:"));
	assert(str_contains(result, ":|"));
	assert(str_contains(result, "D"));
	assert(str_contains(result, "A"));
	free(result);

	transp_free(ctx);
}

TEST(repeat_markers_second_song) {
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	/* First song — leaves ctx in post-song state */
	char *first = transp_buffer(ctx, "|: C G :|", 0, TRANSP_HTML);
	assert(first != NULL);
	free(first);

	/* Reset key as songbook does between songs */
	transp_reset_key(ctx);

	/* Second song — repeat markers must still render correctly */
	char *result = transp_buffer(ctx, "|: C G :|", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "<b>"));
	assert(!str_contains(result, "<div></b>"));
	assert(str_contains(result, "|:"));
	assert(str_contains(result, ":|"));
	assert(str_contains(result, "C"));
	assert(str_contains(result, "G"));
	free(result);

	transp_free(ctx);
}

TEST(complex_song) {
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	const char *song =
	    "1. Verse\n"
	    "C       G       Am      F\n"
	    "Amazing Grace, how sweet the sound\n"
	    "F       C       G\n"
	    "That saved a wretch like me\n";

	char *result = transp_buffer(ctx, song, 2, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "D       A       Bm      G"));
	assert(str_contains(result, "G       D       A"));
	assert(str_contains(result, "Amazing Grace"));
	assert(str_contains(result, "<div>"));
	free(result);

	transp_free(ctx);
}

int main(void) {
	printf("=== Transp Library Unit Tests ===\n\n");

	RUN_TEST(basic_transpose);
	RUN_TEST(transpose_with_modifiers);
	RUN_TEST(transpose_minor_chords);
	RUN_TEST(transpose_negative);
	RUN_TEST(html_output);
	RUN_TEST(flat_notation);
	RUN_TEST(latin_notation);
	RUN_TEST(multiline_input);
	RUN_TEST(hide_chords);
	RUN_TEST(hide_lyrics);
	RUN_TEST(key_detection);
	RUN_TEST(shift_table);
	RUN_TEST(repeat_markers_html);
	RUN_TEST(repeat_markers_transpose);
	RUN_TEST(repeat_markers_second_song);
	RUN_TEST(complex_song);

	printf("\nAll tests passed!\n");
	return 0;
}
