#ifndef CHORDS_API_H
#define CHORDS_API_H

#include <ttypt/ndx.h>

/*
 * Transpose chord chart text
 * 
 * Parameters:
 *   input: Input chord chart text
 *   semitones: Number of semitones to transpose (-11 to +11)
 *   flags: Bitwise OR of TRANSP_* flags (see transp.h)
 *   output: Pointer to receive allocated result string (caller must free)
 * 
 * Returns: 0 on success, -1 on error
 * 
 * Example:
 *   char *result;
 *   if (call_chords_transpose("C G Am F\n", 2, 0, &result) == 0) {
 *       printf("%s", result);
 *       free(result);
 *   }
 */
NDX_DECL(int, chords_transpose, const char *, input, int, semitones, int, flags, char **, output);

#endif /* CHORDS_API_H */
