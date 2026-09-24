#ifndef TRANSP_PARSE_H
#define TRANSP_PARSE_H

/**
 * @file parse.h
 * @brief Song model: lines and space-tokens with per-token lead.
 *
 * parse.c splits the input into lines and tokens, splits leading special runs
 * off each space-token, classifies each line, and detects the key. Pure — no
 * flags, no output.
 */

#include "token.h"
#include <stddef.h>

/**
 * @brief One space-delimited token of a line.
 */
typedef struct {
	/** Borrowed; points at first non-space byte. */
	const char *text;
	/** Token byte length. */
	size_t len;
	/** Bytes of leading whitespace before it. */
	size_t lead;
	/** CHORD only (kind TRANSP_TOK_CHORD). */
	transp_token_info_t info;
} transp_ptoken_t;

/**
 * @brief One classified line of the song.
 */
typedef struct {
	/** All tokens CHORD/SPECIAL/SEP. */
	int is_chord_line;
	/** First byte is '%'. */
	int is_comment;
	/** Zero length (no content). */
	int is_empty;
	/** Lyric/chord line begins "N.". */
	int has_verse;
	/** Bytes of "N." prefix. */
	size_t verse_len;
	/** Borrowed; line with \r\n trimmed, verse NOT
	 *   stripped, leading whitespace intact. */
	const char *text;
	/** Line byte length. */
	size_t len;
	/** Malloc'd; chord lines only, else NULL. */
	transp_ptoken_t *toks;
	/** Token count. */
	size_t ntok;
} transp_pline_t;

/**
 * @brief Parsed song: an array of lines.
 */
typedef struct {
	/** Malloc'd line array. */
	transp_pline_t *lines;
	/** Line count. */
	size_t nlines;
} transp_song_t;

/**
 * @brief Parse text into a song model.
 *
 * Records borrowed pointers into @p input; the caller must keep the
 * input alive until transp_song_free().
 *
 * @param[in]  input Input text (UTF-8; may contain newlines).
 * @param[out] song  Receives the parsed model.
 * @param[out] key   Receives chromatic of first chord token, or -1.
 * @return 0 on success, -1 on OOM.
 */
int transp_song_parse(const char *input, transp_song_t *song, int *key);
/**
 * @brief Free a parsed song model.
 *
 * @param[in] song Song model to free.
 */
void transp_song_free(transp_song_t *song);
#endif /* TRANSP_PARSE_H */