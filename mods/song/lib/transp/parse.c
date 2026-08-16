/*
 * parse.c - Split the input into the song model
 *
 * The caller (transp_buffer) strdups the input first; parse records borrowed
 * pointers into that copy. A trailing empty segment after a final '\n' is
 * dropped ("C G\n" is one line, "C G\n\n" is two), matching the historical
 * byte-walk splitter. See CHORDS.md §8.3.
 */

#include "parse.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static transp_pline_t *grow_lines(transp_song_t *song)
{
	size_t ncap = song->nlines ? song->nlines * 2 : 8;
	transp_pline_t *p = realloc(song->lines, ncap * sizeof(*p));
	if (!p)
		return NULL;
	song->lines = p;
	return &song->lines[song->nlines];
}

static transp_ptoken_t *grow_tokens(transp_pline_t *pl)
{
	size_t ncap = pl->ntok ? pl->ntok * 2 : 4;
	transp_ptoken_t *p = realloc(pl->toks, ncap * sizeof(*p));
	if (!p)
		return NULL;
	pl->toks = p;
	return &pl->toks[pl->ntok];
}

/* Split the leading special run off a space-token so "|:", "|1", "|C" and
 * "/G" behave like the historical byte walk. The tail is classified with
 * transp_token_analyze. lead is attached to the first piece only. */
static int tokenize_piece(
        transp_pline_t *pl, const char *start, const char *end, size_t lead)
{
	const char *p = start;
	int first = 1;

	while (p < end) {
		char c = *p;
		size_t consume = 0;
		transp_tok_kind_t kind;

		if (c == '|') {
			consume = 1;
			while (p + consume < end &&
			       isdigit((unsigned char)p[consume]))
				consume++;
			kind = TRANSP_TOK_SPECIAL;
		} else if (c == ':' || c == '-') {
			consume = 1;
			kind = TRANSP_TOK_SPECIAL;
		} else if (c == '/') {
			consume = 1;
			kind = TRANSP_TOK_SEP;
		} else {
			break;
		}

		transp_ptoken_t *t = grow_tokens(pl);
		if (!t)
			return 0;
		t->text = p;
		t->len = consume;
		t->lead = first ? lead : 0;
		t->info.kind = kind;
		t->info.root = -1;
		t->info.root_off = 0;
		t->info.root_len = 0;
		t->info.mod_off = 0;
		t->info.mod_len = 0;
		pl->ntok++;
		first = 0;
		p += consume;
	}

	if (p < end) {
		transp_ptoken_t *t = grow_tokens(pl);
		if (!t)
			return 0;
		t->text = p;
		t->len = (size_t)(end - p);
		t->lead = first ? lead : 0;
		t->info.kind = transp_token_analyze(p, t->len, &t->info);
		pl->ntok++;
	}
	return 1;
}

/* Tokenize the text after the verse prefix (if any). */
static int tokenize_line(transp_pline_t *pl, const char *from)
{
	const char *end = pl->text + pl->len;
	const char *p = from;

	while (p < end) {
		size_t lead = 0;
		while (p < end && *p == ' ')
			p++, lead++;
		if (p >= end)
			break;
		const char *start = p;
		while (p < end && *p != ' ')
			p++;
		if (!tokenize_piece(pl, start, p, lead))
			return 0;
	}
	return 1;
}

int transp_song_parse(const char *input, transp_song_t *song, int *key)
{
	const char *p = input;

	song->lines = NULL;
	song->nlines = 0;

	while (*p) {
		const char *nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p) : strlen(p);
		transp_pline_t *pl = grow_lines(song);
		if (!pl)
			goto oom;

		memset(pl, 0, sizeof(*pl));
		pl->text = p;
		pl->len = len;
		if (len > 0 && pl->text[len - 1] == '\r')
			pl->len--;
		pl->is_empty = (pl->len == 0);
		pl->is_comment = (pl->len > 0 && pl->text[0] == '%');
		pl->is_chord_line = !pl->is_comment && !pl->is_empty;

		/* Verse prefix: leading digit, then a '.' — "1.", "1,2." */
		if (pl->len > 0 && isdigit((unsigned char)pl->text[0])) {
			const char *dot = memchr(pl->text, '.', pl->len);
			if (dot) {
				pl->has_verse = 1;
				pl->verse_len = (size_t)(dot - pl->text) + 1;
			}
		}

		if (pl->is_comment || pl->is_empty) {
			song->nlines++;
			p = nl ? nl + 1 : p + len;
			continue;
		}

		if (!tokenize_line(pl, pl->text + pl->verse_len))
			goto oom;

		/* Classify: all tokens CHORD/SPECIAL/SEP -> chord line. A
		 * leading "N." demotes the rest to lyrics (§5.6); a line with
		 * no tokens (whitespace only) is a lyric line. */
		if (pl->ntok > 0 && !pl->has_verse) {
			for (size_t i = 0; i < pl->ntok; i++) {
				if (pl->toks[i].info.kind ==
				    TRANSP_TOK_NOT_CHORD)
				{
					pl->is_chord_line = 0;
					break;
				}
			}
		} else {
			pl->is_chord_line = 0;
		}

		if (pl->is_chord_line) {
			for (size_t i = 0; i < pl->ntok; i++) {
				transp_token_info_t *ti = &pl->toks[i].info;
				if (ti->kind == TRANSP_TOK_CHORD &&
				    ti->root >= 0 && *key == -1)
					*key = ti->root;
			}
		} else {
			free(pl->toks);
			pl->toks = NULL;
			pl->ntok = 0;
		}

		song->nlines++;
		p = nl ? nl + 1 : p + len;
	}

	return 0;
oom:
	transp_song_free(song);
	return -1;
}

void transp_song_free(transp_song_t *song)
{
	for (size_t i = 0; i < song->nlines; i++)
		free(song->lines[i].toks);
	free(song->lines);
	song->lines = NULL;
	song->nlines = 0;
}
