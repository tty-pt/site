#ifndef CHORDS_API_H
#define CHORDS_API_H

#include <ttypt/ndx-mod.h>

/*
 * Transpose chord chart text
 * 
 * Parameters:
 *   input: Input chord chart text
 *   semitones: Number of semitones to transpose (-11 to +11)
 *   flags: Bitwise OR of TRANSP_* flags (see transp.h)
 *   output: Pointer to receive allocated result string (caller must free)
 *   key: Pointer to receive detected original key (0-11, chromatic index)
 * 
 * Returns: 0 on success, -1 on error
 * 
 * Example:
 *   char *result;
 *   int key;
 *   if (call_song_transpose("C G Am F\n", 2, 0, &result, &key) == 0) {
 *       printf("Original key: %d, Result: %s", key, result);
 *       free(result);
 *   }
 */
NDX_DECL(int, song_transpose, const char *, input, int, semitones, int, flags, char **, output, int *, key);

/*
 * Reset transpose key detection
 * 
 * Call this before transposing different songs to ensure clean key detection.
 */
NDX_DECL(int, song_reset_key, int, dummy);

/*
 * Get a random song by type/format
 * 
 * Parameters:
 *   type: The song type/format to search for (e.g., "aleluia", "santo")
 *   out_id: Pointer to receive allocated song ID string (caller must free)
 * 
 * Returns: 0 on success, -1 on error
 * 
 * If no songs match the given type, falls back to "any" type.
 */
NDX_DECL(int, song_get_random_by_type, const char *, type, char **, out_id);

/* Parse a colon-separated item line: id:int_field:format
 * Used by choir and songbook to parse their data.txt lines. */
NDX_DECL(int, parse_item_line, const char *, line, char *, id_out, int *, int_out, char *, format_out);

#endif /* CHORDS_API_H */
