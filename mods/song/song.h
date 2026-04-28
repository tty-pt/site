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
NDX_HOOK_DECL(int, song_transpose, const char *, input, int, semitones, int, flags, char **, output, int *, key);

/*
 * Reset transpose key detection
 * 
 * Call this before transposing different songs to ensure clean key detection.
 */
NDX_HOOK_DECL(int, song_reset_key, int, dummy);

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
NDX_HOOK_DECL(int, song_get_random_by_type, const char *, type, char **, out_id);

/* Parse a colon-separated item line: id:int_field:format
 * Used by choir and songbook to parse their data.txt lines. */
NDX_HOOK_DECL(int, parse_item_line, const char *, line, char *, id_out, int *, int_out, char *, format_out);

/* Build a JSON array of all known song types from the persistent type index.
 * Returns a malloc'd string the caller must free, or NULL on error. */
NDX_HOOK_DECL(char *, song_get_types_json, int, dummy);

/* Build a JSON array of all songs from items/song/items/.
 * If include_type is non-zero, each object includes a "type" field.
 * Returns a malloc'd string the caller must free, or NULL on error. */
NDX_HOOK_DECL(char *, build_all_songs_json, const char *, doc_root, int, include_type);

/* Read a song title from doc root + song id into out. */
NDX_HOOK_DECL(int, song_read_title,
	const char *, doc_root, const char *, song_id, char *, out, size_t, out_sz);

/* Read and transpose a song's data.txt from the given doc root.
 * If output is non-NULL, receives an allocated result the caller must free.
 * key receives the detected original key when non-NULL. */
NDX_HOOK_DECL(int, song_transpose_root,
	const char *, doc_root, const char *, song_id,
	int, semitones, int, flags, char **, output, int *, key);

/* Get the original key of a song by reading and parsing its data.txt from
 * the given doc root. Returns chromatic index 0-11 (0=C), or 0 if undetectable. */
NDX_HOOK_DECL(int, song_get_original_key_root,
	const char *, doc_root, const char *, song_id);

/* Get the original key of a song by reading and parsing its data.txt.
 * Returns chromatic index 0-11 (0=C), or 0 if undetectable. */
NDX_HOOK_DECL(int, song_get_original_key, const char *, song_id);

/* Get or set the shared chord-viewer zoom preference for a user.
 * Values are clamped to the 70-170 range; missing/invalid values default to 100. */
NDX_HOOK_DECL(int, song_get_viewer_zoom, const char *, username);
NDX_HOOK_DECL(int, song_set_viewer_zoom, const char *, username, int, zoom);

#endif /* CHORDS_API_H */
