/*
 * parse.h - Song model: lines and space-tokens with per-token lead
 *
 * parse.c splits the input into lines and tokens, splits leading special runs
 * off each space-token, classifies each line, and detects the key. Pure — no
 * flags, no output.
 */

#ifndef TRANSP_PARSE_H
#define TRANSP_PARSE_H

#include "token.h"
#include <stddef.h>

typedef struct {
	const char *text; /* borrowed, points at first non-space byte */
	size_t len;
	size_t lead;              /* bytes of leading whitespace before it */
	transp_token_info_t info; /* CHORD only (kind TRANSP_TOK_CHORD) */
} transp_ptoken_t;

typedef struct {
	int is_chord_line; /* all tokens CHORD/SPECIAL/SEP */
	int is_comment;    /* first byte '%' */
	int is_empty;
	int has_verse;    /* lyric/chord line begins "N." */
	size_t verse_len; /* bytes of "N." prefix */
	const char *text; /* borrowed; line with \r\n trimmed, verse NOT
	                     stripped, leading whitespace intact */
	size_t len;
	transp_ptoken_t *toks; /* malloc'd; chord lines only, else NULL */
	size_t ntok;
} transp_pline_t;

typedef struct {
	transp_pline_t *lines;
	size_t nlines;
} transp_song_t;

int transp_song_parse(const char *input, transp_song_t *song, int *key);
/* 0 on success, -1 on OOM. *key = chromatic of first chord token, or -1. */
void transp_song_free(transp_song_t *song);
#endif /* TRANSP_PARSE_H */
