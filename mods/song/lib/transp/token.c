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
	return TRANSP_TOK_CHORD;
}
