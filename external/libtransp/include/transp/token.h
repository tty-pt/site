#ifndef TRANSP_TOKEN_H
#define TRANSP_TOKEN_H

/**
 * @file token.h
 * @brief Chord grammar classifier.
 *
 * A token is a chord iff a single left-to-right scan consumes the entire
 * token as a root followed by zero or more suffix atoms. Anything else is a
 * lyric word. See CHORDS.md §2 for the full grammar.
 */

#include <stddef.h>

/**
 * @brief Classification of a token by the chord grammar.
 */
typedef enum {
	/** Lyric word / unparseable. */
	TRANSP_TOK_NOT_CHORD = 0,
	/** Root + suffix consumes the whole token. */
	TRANSP_TOK_CHORD = 1,
	/** Repeat marker: | : - and |digits variants. */
	TRANSP_TOK_SPECIAL = 2,
	/** Lone '/'. */
	TRANSP_TOK_SEP = 3,
} transp_tok_kind_t;

/**
 * @brief Chord quality (suffix atom) category.
 */
typedef enum {
	/** Default: no minor/dim/sus atom. */
	TRANSP_QUAL_MAJOR = 0,
	/** m, min, -. */
	TRANSP_QUAL_MINOR,
	/** dim, º. */
	TRANSP_QUAL_DIMINISHED,
	/** h. */
	TRANSP_QUAL_HALF_DIM,
	/** aug, +. */
	TRANSP_QUAL_AUGMENTED,
	/** sus, sus2, sus4. */
	TRANSP_QUAL_SUSPENDED,
	/** Bare "5" (no third). */
	TRANSP_QUAL_POWER,
	/** Explicit third omission (no3/omit3). */
	TRANSP_QUAL_UNDEFINED,
} transp_quality_t;

/**
 * @brief Structured analysis of a chord token.
 */
typedef struct {
	/** Token classification. */
	transp_tok_kind_t kind;
	/** Chromatic 0-11 (C=0 … B=11), or -1. */
	int root;
	/** Byte offset of the root within the token. */
	size_t root_off;
	/** Bytes of the root (1-3: "C", "A#", "Sol"). */
	size_t root_len;
	/** == root_off + root_len. */
	size_t mod_off;
	/** Bytes of the suffix (0 for bare roots). */
	size_t mod_len;
	/** Slash-bass chromatic 0-11, or -1 when none. */
	int bass;
	/** Byte offset of the bass root within the token. */
	size_t bass_off;
	/** Bytes of the bass root (0 when none). */
	size_t bass_len;
	/** Chord quality. */
	transp_quality_t quality;
} transp_token_info_t;

/**
 * @brief Classify one token under the chord grammar.
 *
 * @param[in]  tok Token bytes (not NUL-terminated).
 * @param[in]  len Token byte length.
 * @param[out] out Receives analysis; filled only for kind TRANSP_TOK_CHORD.
 * @return TRANSP_TOK_CHORD/SPECIAL/SEP/NOT_CHORD.
 */
int transp_token_analyze(const char *tok, size_t len, transp_token_info_t *out);
#endif /* TRANSP_TOKEN_H */
