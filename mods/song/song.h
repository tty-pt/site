#ifndef SONG_MOD_H
#define SONG_MOD_H

#include <ttypt/xy-mod.h>

typedef struct {
	int transpose;
	int flags;
	int show_media;
	int zoom;
} song_viewer_prefs_t;

#ifndef SONG_IMPL

/* Read and transpose a song's data.txt from the given doc root.
 * If output is non-NULL, receives an allocated result the caller must free.
 * key receives the detected original key when non-NULL. */
XY_DECL(int, song_transpose_root,
	const char *, doc_root,
	const char *, song_id,
	int, semitones,
	int, flags,
	char **, output,
	int *, key);

/* Read a viewer preference for a user (e.g. "chords-bemol", "chords-latin").
 * Returns a malloc'd string the caller must free, or NULL on error/missing. */
XY_DECL(char *, song_get_pref,
	const char *, username,
	const char *, name);

/* Get the original key of a song by reading and parsing its data.txt from
 * the given doc root. Returns chromatic index 0-11 (0=C), or 0 if undetectable.
 */
XY_DECL(int, song_get_original_key_root,
	const char *, doc_root,
	const char *, song_id);

/* Get the original key of a song by reading and parsing its data.txt.
 * Returns chromatic index 0-11 (0=C), or 0 if undetectable. */
XY_DECL(int, song_get_original_key, const char *, song_id);

/* Get or set the shared chord-viewer zoom preference for a user.
 * Values are clamped to the 70-170 range; missing/invalid values default to
 * 100. */
XY_DECL(int, song_get_viewer_zoom, const char *, username);
XY_DECL(int, song_set_viewer_zoom, const char *, username, int, zoom);

/* Parse viewer preferences from request query parameters and user preferences
 */
XY_DECL(int, song_parse_viewer_prefs,
	int, fd,
	const char *, username,
	song_viewer_prefs_t *, out);

#endif /* SONG_IMPL */

#endif /* SONG_MOD_H */
