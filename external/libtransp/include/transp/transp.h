#ifndef TRANSP_H
#define TRANSP_H

/**
 * @file transp.h
 * @brief Chord transposition library.
 *
 * Transposes chord symbols in text while preserving lyrics and spacing
 * alignment.
 */

#include "transp_flags.h"
#include <stddef.h>

/** @brief Opaque transposer context handle. */
typedef struct transp_ctx transp_ctx_t;

/* Flags (bitwise OR together) — see transp_flags.h */

/**
 * @brief Initialize a transposer context.
 *
 * Creates a new context with chord lookup tables.
 * Should be called once and reused across multiple transpose operations.
 *
 * @return Context pointer on success, NULL on error.
 */
transp_ctx_t *transp_init(void);

/**
 * @brief Transpose an entire buffer (multi-line text).
 *
 * Parses, transposes and renders the input text, preserving lyrics and
 * spacing alignment.
 *
 * @param[in] ctx       Context from transp_init().
 * @param[in] input     Input text (UTF-8 encoded, can contain newlines).
 * @param[in] semitones Number of semitones to transpose (-11 to +11).
 * @param[in] flags     Bitwise OR of TRANSP_* flags.
 *
 * @return Newly allocated string (caller must free()), or NULL on error.
 *
 * Example:
 *   char *result = transp_buffer(ctx, "C G Am F\nLyrics", 2, TRANSP_HTML);
 *   // result: "<div><b>D A Bm G</b></div><div>Lyrics</div>"
 *   free(result);
 */
char *
transp_buffer(transp_ctx_t *ctx, const char *input, int semitones, int flags);

/**
 * @brief Get detected key from last transposition.
 *
 * Returns the chromatic index (0-11) of the detected key, or -1 if no key
 * detected yet. Key is detected from the first chord in the first transposition
 * operation.
 *
 * @param[in] ctx Transposer context.
 * @return 0=C, 1=C#, 2=D, ..., 11=B, or -1 if no key.
 */
int transp_get_key(transp_ctx_t *ctx);

/**
 * @brief Reset detected key.
 *
 * Clears the detected key so the next transposition will detect a new key.
 *
 * @param[in] ctx Transposer context.
 */
void transp_reset_key(transp_ctx_t *ctx);

/**
 * @brief Generate transposition shift table.
 *
 * Generates a reference table showing all 12 keys with their semitone offsets
 * from the detected key.
 *
 * @param[in] ctx   Context (must have detected key from prior transpose).
 * @param[in] latin Use Latin notation (1) or English (0).
 *
 * @return Newly allocated string (caller must free()), or NULL on error.
 *
 * Example output:
 *   C 0
 *   C# 1
 *   D 2
 *   ...
 */
char *transp_shift_table(transp_ctx_t *ctx, int latin);

/**
 * @brief Clean up resources.
 *
 * Frees the memory owned by the context.
 *
 * @param[in] ctx Context to free (can be NULL).
 */
void transp_free(transp_ctx_t *ctx);

#endif /* TRANSP_H */