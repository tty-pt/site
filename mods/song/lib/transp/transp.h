/*
 * transp.h - Chord transposition library
 *
 * Transposes chord symbols in text while preserving lyrics and spacing
 * alignment.
 */

#ifndef TRANSP_H
#define TRANSP_H

#include <stddef.h>

/* Opaque context handle */
typedef struct transp_ctx transp_ctx_t;

/* Flags (bitwise OR together) */
#define TRANSP_HTML 0x04            /* HTML output with <div><b> tags */
#define TRANSP_BEMOL 0x08           /* Flat notation (Db instead of C#) */
#define TRANSP_BREAK_SLASH 0x20     /* Insert newline before " / " */
#define TRANSP_REMOVE_COMMENTS 0x10 /* Remove lines starting with % */
#define TRANSP_HIDE_CHORDS 0x01     /* Show only lyrics */
#define TRANSP_HIDE_LYRICS 0x02     /* Show only chords */
#define TRANSP_LATIN 0x80           /* Latin notation (Do/Re/Mi) */

/*
 * Initialize transposer context
 *
 * Creates a new context with chord lookup tables.
 * Should be called once and reused across multiple transpose operations.
 *
 * Returns: context pointer on success, NULL on error
 */
transp_ctx_t *transp_init(void);

/*
 * Transpose entire buffer (multi-line text)
 *
 * Params:
 *   ctx       - Context from transp_init()
 *   input     - Input text (UTF-8 encoded, can contain newlines)
 *   semitones - Number of semitones to transpose (-11 to +11)
 *   flags     - Bitwise OR of TRANSP_* flags
 *
 * Returns: newly allocated string (caller must free()), or NULL on error
 *
 * Example:
 *   char *result = transp_buffer(ctx, "C G Am F\nLyrics", 2, TRANSP_HTML);
 *   // result: "<div><b>D A Bm G</b></div><div>Lyrics</div>"
 *   free(result);
 */
char *
transp_buffer(transp_ctx_t *ctx, const char *input, int semitones, int flags);

/*
 * Get detected key from last transposition
 *
 * Returns the chromatic index (0-11) of the detected key, or -1 if no key
 * detected yet. Key is detected from the first chord in the first transposition
 * operation.
 *
 * Returns: 0=C, 1=C#, 2=D, ..., 11=B, or -1 if no key
 */
int transp_get_key(transp_ctx_t *ctx);

/*
 * Reset detected key
 *
 * Clears the detected key so the next transposition will detect a new key.
 *
 * Params:
 *   ctx - Context to reset
 */
void transp_reset_key(transp_ctx_t *ctx);

/*
 * Generate transposition shift table
 *
 * Generates a reference table showing all 12 keys with their semitone offsets
 * from the detected key.
 *
 * Params:
 *   ctx   - Context (must have detected key from prior transpose)
 *   latin - Use Latin notation (1) or English (0)
 *
 * Returns: newly allocated string (caller must free()), or NULL on error
 *
 * Example output:
 *   C 0
 *   C# 1
 *   D 2
 *   ...
 */
char *transp_shift_table(transp_ctx_t *ctx, int latin);

/*
 * Clean up resources
 *
 * Frees all memory associated with context including qmap databases
 * and spacing queue.
 *
 * Params:
 *   ctx - Context to free (can be NULL)
 */
void transp_free(transp_ctx_t *ctx);

#endif /* TRANSP_H */
