/*
 * token.c - Chord grammar classifier
 *
 * A token is a chord iff a single left-to-right scan consumes the entire
 * token as a root followed by zero or more suffix atoms. Whole-token
 * consumption is the guard that keeps lyric words out. See CHORDS.md §2.
 */

#include "token.h"

#include <ctype.h>
#include <string.h>

/* Root tables. Base index: C/Do=0 D/Re=2 E/Mi=4 F/Fa=5 G/Sol=7 A/La=9
 * B/Si=11. Latin is tried first so "Fa" wins over English 'F'. */
static const struct {
	const char *name;
	int idx;
} latin_roots[] = {
	{ "Do", 0 },  { "Re", 2 }, { "Mi", 4 },  { "Fa", 5 },
	{ "Sol", 7 }, { "La", 9 }, { "Si", 11 },
};

static int english_idx(char c)
{
	switch (c) {
	case 'A':
		return 9;
	case 'B':
		return 11;
	case 'C':
		return 0;
	case 'D':
		return 2;
	case 'E':
		return 4;
	case 'F':
		return 5;
	case 'G':
		return 7;
	default:
		return -1;
	}
}

/* Parse a root at s[0..len). Returns 1 and fills *root (0-11) and *nbytes. */
static int parse_root(const char *s, size_t len, int *root, size_t *nbytes)
{
	size_t i;
	int r = -1;

	for (i = 0; i < sizeof(latin_roots) / sizeof(latin_roots[0]); i++) {
		size_t l = strlen(latin_roots[i].name);
		if (len >= l && memcmp(s, latin_roots[i].name, l) == 0) {
			r = latin_roots[i].idx;
			i = l; /* matched root's byte length, not the array
			          index */
			break;
		}
	}
	if (r < 0) {
		if (len < 1 || english_idx(s[0]) < 0)
			return 0;
		r = english_idx(s[0]);
		i = 1;
	}
	if (i < len && (s[i] == '#' || s[i] == 'b')) {
		r = (s[i] == '#') ? (r + 1) % 12 : (r + 11) % 12;
		i++;
	}
	*root = r;
	*nbytes = i;
	return 1;
}

/* Paren-group word list (matches CHORDS.md §2.2). */
static int paren_word(const char *s, size_t len)
{
	static const char *words[] = {
		"no", "add", "sus", "maj", "dim", "aug", "omit",
	};
	for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
		size_t wlen = strlen(words[i]);
		if (len == wlen && memcmp(s, words[i], wlen) == 0)
			return 1;
	}
	return 0;
}

/* Scan a parenthesized group starting at s[(*pos)]. Advances *pos past the
 * closing ')'. Returns 1 on success, 0 on invalid content / unterminated. */
static int paren_group(const char *s, size_t len, size_t *pos)
{
	size_t i = *pos + 1;

	while (i < len && s[i] != ')') {
		unsigned char c = (unsigned char)s[i];
		if (c >= '0' && c <= '9') {
			i++;
		} else if (c == '#' || c == 'b') {
			i++;
		} else if (
		        c == 0xC2 && i + 1 < len &&
		        (unsigned char)s[i + 1] == 0xBA)
		{
			i += 2;
		} else if (isalpha(c)) {
			size_t start = i;
			while (i < len && isalpha((unsigned char)s[i]))
				i++;
			if (!paren_word(s + start, i - start))
				return 0;
		} else {
			return 0;
		}
	}
	if (i >= len || s[i] != ')')
		return 0;
	*pos = i + 1;
	return 1;
}

/* Suffix word atoms, longest-first. Returns bytes consumed (0 = no match). */
static size_t word_atom(const char *s, size_t len, size_t pos)
{
	static const char *words[] = {
		"maj", "min", "dim", "aug", "sus", "add", "omit", "no",
	};
	size_t remain = len - pos;

	for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
		size_t wlen = strlen(words[i]);
		if (remain >= wlen && memcmp(s + pos, words[i], wlen) == 0)
			return wlen;
	}
	if (remain >= 1) {
		char c = s[pos];
		if (c == 'm' || c == 'M' || c == 'h')
			return 1;
	}
	return 0;
}

/* Chord quality from the suffix bytes (the part after the root, before any
 * '/' bass). Mirrors word_atom's longest-first matching; the FIRST quality-
 * bearing atom wins, so Gm7(5º) is MINOR (the (5º) is a fifth alteration, not
 * a quality change). Parentheses are skipped entirely: interval parsing — a
 * later layer — owns paren content. Default is MAJOR; bare "5" is POWER;
 * no3/omit3 make the third undefined. */
static transp_quality_t chord_quality(const char *s, size_t n)
{
	size_t i = 0;

	while (i < n) {
		if (s[i] == '/')
			break;
		if (n - i >= 3 && memcmp(s + i, "maj", 3) == 0) {
			return TRANSP_QUAL_MAJOR;
		} else if (n - i >= 3 && memcmp(s + i, "min", 3) == 0) {
			return TRANSP_QUAL_MINOR;
		} else if (n - i >= 3 && memcmp(s + i, "dim", 3) == 0) {
			return TRANSP_QUAL_DIMINISHED;
		} else if (n - i >= 3 && memcmp(s + i, "aug", 3) == 0) {
			return TRANSP_QUAL_AUGMENTED;
		} else if (n - i >= 3 && memcmp(s + i, "sus", 3) == 0) {
			return TRANSP_QUAL_SUSPENDED;
		} else if (n - i >= 2 && memcmp(s + i, "no", 2) == 0) {
			return TRANSP_QUAL_UNDEFINED;
		} else if (n - i >= 4 && memcmp(s + i, "omit", 4) == 0) {
			return TRANSP_QUAL_UNDEFINED;
		}
		if (s[i] == 'm')
			return TRANSP_QUAL_MINOR;
		if (s[i] == 'M')
			return TRANSP_QUAL_MAJOR;
		if (s[i] == 'h')
			return TRANSP_QUAL_HALF_DIM;
		if (s[i] == '-')
			return TRANSP_QUAL_MINOR;
		if ((unsigned char)s[i] == 0xC2 && i + 1 < n &&
		    (unsigned char)s[i + 1] == 0xBA)
			return TRANSP_QUAL_DIMINISHED;
		if (s[i] == '+')
			return TRANSP_QUAL_AUGMENTED;
		if (s[i] >= '0' && s[i] <= '9') {
			size_t d = i;
			while (d < n && s[d] >= '0' && s[d] <= '9')
				d++;
			/* bare "5" power chord, no third */
			if (d == i + 1 && s[i] == '5' && d == n)
				return TRANSP_QUAL_POWER;
			i = d;
			continue;
		}
		if (s[i] == '(') {
			while (i < n && s[i] != ')')
				i++;
			if (i < n)
				i++;
			continue;
		}
		i++;
	}
	return TRANSP_QUAL_MAJOR;
}

int transp_token_analyze(const char *tok, size_t len, transp_token_info_t *out)
{
	int root;
	size_t root_len, pos;

	/* Repeat marker: all chars from { '|', ':', '-', digit } with at least
	 * one of '|' ':' '-'. (parse.c usually pre-splits these, but the rule
	 * is part of the classifier contract.) */
	{
		int has_special = 0, all_special = 1;
		for (size_t i = 0; i < len; i++) {
			char c = tok[i];
			if (c == '|' || c == ':' || c == '-')
				has_special = 1;
			else if (!(c >= '0' && c <= '9'))
				all_special = 0;
		}
		if (all_special && has_special)
			return TRANSP_TOK_SPECIAL;
	}

	if (!parse_root(tok, len, &root, &root_len))
		return TRANSP_TOK_NOT_CHORD;

	/* Defaults; the slash-bass branch below overrides them. */
	out->bass = -1;
	out->bass_off = 0;
	out->bass_len = 0;

	pos = root_len;
	while (pos < len) {
		unsigned char c = (unsigned char)tok[pos];

		if (c == '/') {
			int b;
			size_t blen;
			if (parse_root(
			            tok + pos + 1, len - pos - 1, &b, &blen) &&
			    pos + 1 + blen == len)
			{
				/* slash bass — must end the token */
				out->bass = b;
				out->bass_off = pos + 1;
				out->bass_len = blen;
				pos = len;
				break;
			}
			if (pos + 1 < len && tok[pos + 1] >= '0' &&
			    tok[pos + 1] <= '9')
			{
				/* extension slash, e.g. G6/9 */
				pos++;
				while (pos < len && tok[pos] >= '0' &&
				       tok[pos] <= '9')
					pos++;
				continue;
			}
			return TRANSP_TOK_NOT_CHORD;
		}
		if (c == '(') {
			if (!paren_group(tok, len, &pos))
				return TRANSP_TOK_NOT_CHORD;
			continue;
		}
		if (c == '#' || c == 'b') {
			/* altered extension: #5 b9 … require digits */
			if (pos + 1 >= len ||
			    !(tok[pos + 1] >= '0' && tok[pos + 1] <= '9'))
				return TRANSP_TOK_NOT_CHORD;
			pos++;
			while (pos < len && tok[pos] >= '0' && tok[pos] <= '9')
				pos++;
			continue;
		}
		if (c >= '0' && c <= '9') {
			while (pos < len && tok[pos] >= '0' && tok[pos] <= '9')
				pos++;
			continue;
		}
		if (c == 0xC2 && pos + 1 < len &&
		    (unsigned char)tok[pos + 1] == 0xBA)
		{
			/* º (U+00BA) */
			pos += 2;
			continue;
		}
		if (c == '+' || c == '-') {
			pos++;
			continue;
		}
		{
			size_t got = word_atom(tok, len, pos);
			if (got == 0)
				return TRANSP_TOK_NOT_CHORD;
			pos += got;
		}
	}

	out->kind = TRANSP_TOK_CHORD;
	out->root = root;
	out->root_off = 0;
	out->root_len = root_len;
	out->mod_off = root_len;
	out->mod_len = len - root_len;
	out->quality = chord_quality(tok + root_len, len - root_len);
	return TRANSP_TOK_CHORD;
}
