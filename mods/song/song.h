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
 *   if (song_transpose("C G Am F\n", 2, 0, &result, &key) == 0) {
 *       printf("Original key: %d, Result: %s", key, result);
 *       free(result);
 *   }
 */
/* Read and transpose a song's data.txt from the given doc root.
 * If output is non-NULL, receives an allocated result the caller must free.
 * key receives the detected original key when non-NULL. */
NDX_HOOK_DECL(int, song_transpose_root,
	const char *, doc_root,
	const char *, song_id,
	int, semitones,
	int, flags,
	char **, output,
	int *, key);

/* Read a viewer preference for a user (e.g. "chords-bemol", "chords-latin").
 * Returns a malloc'd string the caller must free, or NULL on error/missing. */
NDX_HOOK_DECL(char *, song_get_pref,
	const char *, username,
	const char *, name);

/* Get the original key of a song by reading and parsing its data.txt from
 * the given doc root. Returns chromatic index 0-11 (0=C), or 0 if undetectable.
 */
NDX_HOOK_DECL(int, song_get_original_key_root,
	const char *, doc_root,
	const char *, song_id);

/* Get the original key of a song by reading and parsing its data.txt.
 * Returns chromatic index 0-11 (0=C), or 0 if undetectable. */
NDX_HOOK_DECL(int, song_get_original_key, const char *, song_id);

/* Get or set the shared chord-viewer zoom preference for a user.
 * Values are clamped to the 70-170 range; missing/invalid values default to
 * 100. */
NDX_HOOK_DECL(int, song_get_viewer_zoom, const char *, username);
NDX_HOOK_DECL(int, song_set_viewer_zoom, const char *, username, int, zoom);

#endif /* CHORDS_API_H */
