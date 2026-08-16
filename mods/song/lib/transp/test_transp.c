/*
 * test_transp.c - Unit tests for transp library
 */

#include "transp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define TEST(name) void test_##name(void)
#define RUN_TEST(name)                                                         \
	do {                                                                   \
		printf("Running " #name "... ");                               \
		test_##name();                                                 \
		printf("PASS\n");                                              \
	} while (0)

/* Helper: check if string contains substring */
static int str_contains(const char *haystack, const char *needle)
{
	return strstr(haystack, needle) != NULL;
}

TEST(basic_transpose)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "C G", 2, 0);
	assert(result != NULL);
	assert(str_contains(result, "D A"));
	free(result);

	transp_free(ctx);
}

TEST(transpose_with_modifiers)
{
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

TEST(transpose_minor_chords)
{
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

TEST(transpose_negative)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "D A Bm G", -2, 0);
	assert(result != NULL);
	assert(str_contains(result, "C G Am F"));
	free(result);

	transp_free(ctx);
}

TEST(html_output)
{
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

TEST(flat_notation)
{
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

TEST(latin_notation)
{
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

TEST(multiline_input)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "C G Am F\nAmazing Grace", 2, 0);
	assert(result != NULL);
	assert(str_contains(result, "D A Bm G"));
	assert(str_contains(result, "Amazing Grace"));
	free(result);

	transp_free(ctx);
}

TEST(hide_chords)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(
	        ctx, "C G Am F\nTest lyrics here", 0, TRANSP_HIDE_CHORDS);
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

TEST(hide_lyrics)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(
	        ctx, "C G Am F\nTest lyrics here", 0, TRANSP_HIDE_LYRICS);
	assert(result != NULL);
	assert(str_contains(result, "C"));
	assert(str_contains(result, "G"));
	assert(!str_contains(result, "Test"));
	assert(!str_contains(result, "lyrics"));
	free(result);

	transp_free(ctx);
}

TEST(key_detection)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	/* Key should be detected from first chord */
	char *result = transp_buffer(ctx, "G D Em C", 0, 0);
	assert(result != NULL);
	assert(transp_get_key(ctx) == 7); /* G = index 7 */
	free(result);

	transp_free(ctx);
}

TEST(shift_table)
{
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

TEST(repeat_markers_html)
{
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

TEST(repeat_markers_transpose)
{
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

TEST(repeat_markers_second_song)
{
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

TEST(repeat_brackets_html)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result =
	        transp_buffer(ctx, "|1 Bm7 C#7 |2 E/G# - |", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "<b>"));
	assert(str_contains(result, "</b>"));
	assert(str_contains(result, "|1"));
	assert(str_contains(result, "|2"));
	assert(str_contains(result, "Bm7"));
	assert(str_contains(result, "C#7"));
	assert(str_contains(result, "E/G#"));
	free(result);

	transp_free(ctx);
}

TEST(paren_chord_suffixes)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "A(no3)7 Dm", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "<b>"));
	assert(str_contains(result, "A(no3)7"));
	assert(str_contains(result, "Dm"));
	free(result);

	result = transp_buffer(ctx, "C Dm G(omit3) Dm", 0, TRANSP_HTML);
	assert(str_contains(result, "G(omit3)"));
	free(result);

	/* Unknown paren content must not become a chord */
	result = transp_buffer(ctx, "C(not) Dm", 0, TRANSP_HTML);
	assert(!str_contains(result, "<b>"));
	free(result);

	transp_free(ctx);
}

TEST(complex_song)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	const char *song = "1. Verse\n"
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

TEST(html_escape_lyrics)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	/* Lyric line (non-chord) with & and " — escaped in HTML mode */
	char *result =
	        transp_buffer(ctx, "Hello & world \"quoted\"", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "&amp;"));
	assert(str_contains(result, "&quot;"));
	assert(!str_contains(result, " & "));
	free(result);

	/* Comment line with HTML special chars — must be escaped */
	result = transp_buffer(ctx, "%<b>injected</b> & more", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(!str_contains(result, "<b>injected</b>"));
	assert(str_contains(result, "&lt;b&gt;injected&lt;/b&gt;"));
	assert(str_contains(result, "&amp;"));
	free(result);

	/* In plain (non-HTML) mode, chars passed through unescaped */
	result = transp_buffer(ctx, "Hello & world", 0, 0);
	assert(result != NULL);
	assert(str_contains(result, "Hello & world"));
	assert(!str_contains(result, "&amp;"));
	free(result);

	transp_free(ctx);
}

TEST(no_stray_close_bold_on_lyric_line)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	/* Word starting with a chord letter but not a chord */
	char *result = transp_buffer(ctx, "Amazing Grace", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(!str_contains(result, "<b>"));
	assert(!str_contains(result, "</b>"));
	assert(str_contains(result, "Amazing Grace"));
	free(result);

	transp_free(ctx);
}

TEST(valid_chords_still_bolded)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	/* All chords on a single line are wrapped in one <b> block */
	char *result =
	        transp_buffer(ctx, "Am Dm7 G#maj7 Bbm7b5", 0, TRANSP_HTML);
	assert(result != NULL);
	/* Bbm7b5 defaults to A#m7b5 (sharp notation) */
	assert(str_contains(result, "<b>Am Dm7 G#maj7 A#m7b5</b>"));
	free(result);

	transp_free(ctx);
}

TEST(lyric_word_not_treated_as_chord)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "Brilhará novo Sol", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(!str_contains(result, "<b>"));
	assert(str_contains(result, "Brilhará novo Sol"));
	free(result);

	transp_free(ctx);
}

TEST(lyric_word_with_chord_root_prefix)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "Erguendo ao alto", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(!str_contains(result, "<b>"));
	assert(str_contains(result, "Erguendo ao alto"));
	free(result);

	transp_free(ctx);
}

TEST(numbered_verse_no_duplicate_text)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "1. Verse text", 0, TRANSP_HTML);
	assert(result != NULL);
	/* The "1." must be bolded */
	assert(str_contains(result, "<b>1.</b>"));
	/* The rest must appear as lyrics */
	assert(str_contains(result, "Verse text"));
	/* "Verse text" must appear exactly once */
	char *first = strstr(result, "Verse text");
	assert(first != NULL);
	assert(strstr(first + 1, "Verse text") == NULL);
	free(result);

	transp_free(ctx);
}

TEST(slash_chord_identified)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "G/B D/F# A/E", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "G/B"));
	assert(str_contains(result, "D/F#"));
	assert(str_contains(result, "A/E"));
	assert(str_contains(result, "<b>"));
	free(result);

	transp_free(ctx);
}

TEST(slash_chord_transposed)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "G/B D/F#", 2, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "A/B"));
	assert(str_contains(result, "E/F#"));
	free(result);

	transp_free(ctx);
}

TEST(slash_chord_latin_bass)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "G/Do C/Sol D/Re", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "G/Do"));
	assert(str_contains(result, "C/Sol"));
	assert(str_contains(result, "D/Re"));
	assert(str_contains(result, "<b>"));
	free(result);

	transp_free(ctx);
}

TEST(slash_chord_with_accidental_bass)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "E/G# C/A# G/Bb", 0, 0);
	assert(result != NULL);
	assert(str_contains(result, "E/G#"));
	assert(str_contains(result, "C/A#"));
	assert(str_contains(result, "G/Bb"));
	free(result);

	transp_free(ctx);
}

TEST(major7_notation)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "C7M FM7", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "C7M"));
	assert(str_contains(result, "FM7"));
	assert(str_contains(result, "<b>"));
	free(result);

	transp_free(ctx);
}

TEST(major7_notation_transposed)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "C7M FM7", 2, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "D7M"));
	assert(str_contains(result, "GM7"));
	free(result);

	transp_free(ctx);
}

TEST(invalid_slash_not_a_chord)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "G/thing C/foo", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(!str_contains(result, "<b>"));
	assert(str_contains(result, "G/thing C/foo"));
	free(result);

	transp_free(ctx);
}

TEST(augmented_chord_bolded)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	/* Cº is a valid chord — bolded with the º suffix preserved */
	char *result = transp_buffer(ctx, "Cº Dº7 Eº", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "Cº"));
	assert(str_contains(result, "Dº7"));
	assert(str_contains(result, "Eº"));
	assert(str_contains(result, "<b>"));
	free(result);

	transp_free(ctx);
}

TEST(augmented_chord_transposed)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "Cº Fº", 2, 0);
	assert(result != NULL);
	assert(str_contains(result, "Dº"));
	assert(str_contains(result, "Gº"));
	free(result);

	transp_free(ctx);
}

/* =========================================================================
 * Section A — roots (English)
 * ===================================================================== */

TEST(roots_plain)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "C D E F G A B", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "<b>C D E F G A B</b>"));
	free(result);

	transp_free(ctx);
}

TEST(roots_sharp)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "C# D# F# G# A#", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "<b>C# D# F# G# A#</b>"));
	free(result);

	transp_free(ctx);
}

TEST(roots_flat_display_as_sharp)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	/* Flat input renders with sharp defaults (Bbm7b5 -> A#m7b5 precedent)
	 */
	char *result = transp_buffer(ctx, "Db Eb Gb Ab Bb", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "<b>C# D# F# G# A#</b>"));
	free(result);

	/* A# and Bb map to the same chromatic index */
	result = transp_buffer(ctx, "A# Bb", 0, TRANSP_HTML);
	assert(str_contains(result, "<b>A# A#</b>"));
	free(result);

	transp_free(ctx);
}

/* =========================================================================
 * Section B — qualities, extensions, slash bass
 * ===================================================================== */

TEST(quality_minor)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "Cm Fm Am", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "<b>Cm Fm Am</b>"));
	free(result);

	transp_free(ctx);
}

TEST(quality_diminished_words)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "Gdim Cdim7", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "<b>Gdim Cdim7</b>"));
	free(result);

	transp_free(ctx);
}

TEST(quality_sus_add_extension_slash)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result =
	        transp_buffer(ctx, "Gadd9 Gsus2 Gsus4 G6/9", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "<b>Gadd9 Gsus2 Gsus4 G6/9</b>"));
	free(result);

	result = transp_buffer(ctx, "D9/F# C6 Cmaj9", 0, TRANSP_HTML);
	assert(str_contains(result, "<b>D9/F# C6 Cmaj9</b>"));
	free(result);

	transp_free(ctx);
}

TEST(power_chord)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "C5", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "<b>C5</b>"));
	free(result);

	transp_free(ctx);
}

TEST(slash_bass_with_accidentals)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(
	        ctx, "G/B D/F# A/E E/G# C/A# G/Bb", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "G/B"));
	assert(str_contains(result, "D/F#"));
	assert(str_contains(result, "A/E"));
	assert(str_contains(result, "E/G#"));
	assert(str_contains(result, "C/A#"));
	assert(str_contains(result, "G/Bb"));
	assert(str_contains(result, "<b>"));
	free(result);

	transp_free(ctx);
}

/* =========================================================================
 * Section C — special symbols and repeat markers
 * ===================================================================== */

TEST(special_symbols_alone)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "| : -", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "<b>| : -</b>"));
	free(result);

	transp_free(ctx);
}

/* =========================================================================
 * Section D — line structure and lyric rejection
 * ===================================================================== */

TEST(lyric_syllables_not_chords)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	/* Hyphenated syllables, lowercase starts, accents: all lyric */
	char *result = transp_buffer(
	        ctx,
	        "No Senhor es-tá  a miseri-\ncórdia e a abundânte   "
	        "re-den--ção.",
	        0, TRANSP_HTML);
	assert(result != NULL);
	assert(!str_contains(result, "<b>"));
	assert(str_contains(result, "No Senhor es-tá  a miseri-"));
	assert(str_contains(result, "córdia e a abundânte   re-den--ção."));
	free(result);

	transp_free(ctx);
}

TEST(comment_line_html)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result =
	        transp_buffer(ctx, "%Intro comment\nC G\n", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "<b class='comment'>Intro comment</b>"));
	free(result);

	transp_free(ctx);
}

TEST(comment_removal_skips_blank)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(
	        ctx, "%Intro\n\nC G\nLyrics", 0,
	        TRANSP_HTML | TRANSP_REMOVE_COMMENTS);
	assert(result != NULL);
	assert(!str_contains(result, "Intro"));
	assert(str_contains(result, "<b>C G</b>"));
	assert(str_contains(result, "Lyrics"));
	free(result);

	transp_free(ctx);
}

TEST(break_slash_lyrics)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(
	        ctx, "Line one / line two", 0,
	        TRANSP_HTML | TRANSP_BREAK_SLASH);
	assert(result != NULL);
	assert(str_contains(result, "Line one \n"));
	assert(str_contains(result, "line two"));
	free(result);

	transp_free(ctx);
}

/* =========================================================================
 * Section F — DESIRED: fail against the old whitelist, pass after the
 * grammar refactor. These pin the rework's contract.
 * ===================================================================== */

TEST(paren_diminished_fifth)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(
	        ctx, "Fm Gm7(5º) Fm  Gm7(5º) Fm Bbm7 C5", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(
	        result, "<b>Fm Gm7(5º) Fm  Gm7(5º) Fm A#m7 C5</b>"));
	free(result);

	transp_free(ctx);
}

TEST(paren_diminished_transposed)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(ctx, "Gm7(5º)", 2, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "<b>Am7(5º)</b>"));
	free(result);

	transp_free(ctx);
}

TEST(consistent_quality_suffixes)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(
	        ctx, "Gaug Gmaj Gsus4 Gomit3 Gno3 Gh G-", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(
	        result, "<b>Gaug Gmaj Gsus4 Gomit3 Gno3 Gh G-</b>"));
	free(result);

	transp_free(ctx);
}

TEST(latin_input_roots)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	/* Latin solfege accepted as input; renders in the chosen notation */
	char *result =
	        transp_buffer(ctx, "Sol Do La-", 0, TRANSP_LATIN | TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "<b>Sol Do La-</b>"));
	free(result);

	/* Same Latin input rendered in English defaults */
	result = transp_buffer(ctx, "Sol", 0, TRANSP_HTML);
	assert(str_contains(result, "<b>G</b>"));
	free(result);

	transp_free(ctx);
}

TEST(latin_input_roots_transposed)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result =
	        transp_buffer(ctx, "Sol Do La-", 2, TRANSP_LATIN | TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(result, "<b>La Re Si-</b>"));
	free(result);

	transp_free(ctx);
}

TEST(combo_grammar)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	char *result = transp_buffer(
	        ctx, "Gmaj7(9) F#m7b5(5º) Csus2(omit3) Bb/D", 2, TRANSP_HTML);
	assert(result != NULL);
	assert(str_contains(
	        result, "<b>Amaj7(9) G#m7b5(5º) Dsus2(omit3) C/D</b>"));
	free(result);

	transp_free(ctx);
}

/* =========================================================================
 * Section G — regression guards (whole-song + false-positive rejection)
 * ===================================================================== */

TEST(user_song_full)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	const char *song = "Fm   Cm A#m G#  Gº\n"
	                   "No Senhor es-tá  a miseri-\n"
	                   "Fm Gm7(5º) Fm  Gm7(5º) Fm Bbm7 C5\n"
	                   "córdia e a abundânte   re-den--ção.\n"
	                   "\n"
	                   "Fm                           Em     Cm\n"
	                   "1. Do profundo abismo clamo por Vós, Senhor.\n"
	                   "C#               C#7   Cm\n"
	                   "Senhor escutai a minha voz.\n";

	char *result = transp_buffer(ctx, song, 0, TRANSP_HTML);
	assert(result != NULL);

	/* Chord lines stay one bolded block each, spacing preserved */
	assert(str_contains(result, "<b>Fm   Cm A#m G#  Gº</b>"));
	assert(str_contains(
	        result, "<b>Fm Gm7(5º) Fm  Gm7(5º) Fm A#m7 C5</b>"));
	assert(str_contains(result, "<b>Fm"));
	assert(str_contains(result, "Em     Cm</b>"));
	assert(str_contains(result, "<b>C#"));
	assert(str_contains(result, "C#7   Cm</b>"));

	/* Lyrics rendered plainly, never bolded */
	assert(str_contains(result, "No Senhor es-tá  a miseri-"));
	assert(str_contains(result, "córdia e a abundânte   re-den--ção."));
	assert(str_contains(
	        result, "Do profundo abismo clamo por Vós, Senhor."));
	assert(str_contains(result, "Senhor escutai a minha voz."));
	assert(!str_contains(result, "<b>No Senhor"));
	assert(!str_contains(result, "<b>córdia"));
	assert(!str_contains(result, "<b>Do profundo"));
	assert(!str_contains(result, "<b>Senhor escutai"));

	/* Key detected from first chord: F */
	assert(transp_get_key(ctx) == 5);
	free(result);

	transp_free(ctx);
}

TEST(lyric_false_positives_guard)
{
	transp_ctx_t *ctx = transp_init();
	assert(ctx != NULL);

	/* Old whitelist read "Amada Amiga Amina" as chords (A +
	 * mada/miga/mina); the grammar must reject them — they are lyric words.
	 */
	char *result = transp_buffer(ctx, "Amada Amiga Amina", 0, TRANSP_HTML);
	assert(result != NULL);
	assert(!str_contains(result, "<b>"));
	assert(str_contains(result, "Amada Amiga Amina"));
	free(result);

	transp_free(ctx);
}

int main(void)
{
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
	RUN_TEST(repeat_brackets_html);
	RUN_TEST(paren_chord_suffixes);
	RUN_TEST(complex_song);
	RUN_TEST(html_escape_lyrics);
	RUN_TEST(no_stray_close_bold_on_lyric_line);
	RUN_TEST(valid_chords_still_bolded);
	RUN_TEST(lyric_word_not_treated_as_chord);
	RUN_TEST(lyric_word_with_chord_root_prefix);
	RUN_TEST(numbered_verse_no_duplicate_text);
	RUN_TEST(slash_chord_identified);
	RUN_TEST(slash_chord_transposed);
	RUN_TEST(slash_chord_latin_bass);
	RUN_TEST(slash_chord_with_accidental_bass);
	RUN_TEST(major7_notation);
	RUN_TEST(major7_notation_transposed);
	RUN_TEST(invalid_slash_not_a_chord);
	RUN_TEST(augmented_chord_bolded);
	RUN_TEST(augmented_chord_transposed);

	RUN_TEST(roots_plain);
	RUN_TEST(roots_sharp);
	RUN_TEST(roots_flat_display_as_sharp);
	RUN_TEST(quality_minor);
	RUN_TEST(quality_diminished_words);
	RUN_TEST(quality_sus_add_extension_slash);
	RUN_TEST(power_chord);
	RUN_TEST(slash_bass_with_accidentals);
	RUN_TEST(special_symbols_alone);
	RUN_TEST(lyric_syllables_not_chords);
	RUN_TEST(comment_line_html);
	RUN_TEST(comment_removal_skips_blank);
	RUN_TEST(break_slash_lyrics);

	RUN_TEST(paren_diminished_fifth);
	RUN_TEST(paren_diminished_transposed);
	RUN_TEST(consistent_quality_suffixes);
	RUN_TEST(latin_input_roots);
	RUN_TEST(latin_input_roots_transposed);
	RUN_TEST(combo_grammar);

	RUN_TEST(user_song_full);
	RUN_TEST(lyric_false_positives_guard);

	printf("\nAll tests passed!\n");
	return 0;
}
