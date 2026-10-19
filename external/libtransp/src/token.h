/*
 * token.h - Chord grammar classifier
 *
 * A token is a chord iff a single left-to-right scan consumes the entire
 * token as a root followed by zero or more suffix atoms. Anything else is a
 * lyric word. See CHORDS.md §2 for the full grammar.
 */

#ifndef TRANSP_TOKEN_H
#define TRANSP_TOKEN_H

#include <stddef.h>

typedef enum {
	TRANSP_TOK_NOT_CHORD = 0, /* lyric word / unparseable */
	TRANSP_TOK_CHORD = 1,     /* root + suffix consumes the whole token */
	TRANSP_TOK_SPECIAL = 2, /* repeat marker: | : - and |digits variants */
	TRANSP_TOK_SEP = 3,     /* lone '/' */
} transp_tok_kind_t;

typedef enum {
	TRANSP_QUAL_MAJOR = 0,  /* default: no minor/dim/sus atom */
	TRANSP_QUAL_MINOR,      /* m, min, - */
	TRANSP_QUAL_DIMINISHED, /* dim, º */
	TRANSP_QUAL_HALF_DIM,   /* h */
	TRANSP_QUAL_AUGMENTED,  /* aug, + */
	TRANSP_QUAL_SUSPENDED,  /* sus, sus2, sus4 */
	TRANSP_QUAL_POWER,      /* bare "5" (no third) */
	TRANSP_QUAL_UNDEFINED,  /* explicit third omission (no3/omit3) */
} transp_quality_t;

typedef struct {
	transp_tok_kind_t kind;
	int root;        /* chromatic 0-11 (C=0 … B=11), or -1 */
	size_t root_off; /* byte offset of the root within the token */
	size_t root_len; /* bytes of the root (1-3: "C", "A#", "Sol") */
	size_t mod_off;  /* == root_off + root_len */
	size_t mod_len;  /* bytes of the suffix (0 for bare roots) */
	int bass;        /* slash-bass chromatic 0-11, or -1 when none */
	size_t bass_off; /* byte offset of the bass root within the token */
	size_t bass_len; /* bytes of the bass root (0 when none) */
	transp_quality_t quality;
} transp_token_info_t;

int transp_token_analyze(const char *tok, size_t len, transp_token_info_t *out);
/* returns TRANSP_TOK_CHORD/SPECIAL/SEP/NOT_CHORD; fills *out only for CHORD */
#endif /* TRANSP_TOKEN_H */
