/*
 * render.h - Transform the song model into transposed output
 *
 * Walks the parsed model: transposes roots, emits one <b> per chord line with
 * verbatim spacing (diff absorb/add), manages the spacing queue for lyric
 * lines (space/'-' fillers), verse numbers, HTML escaping, comments,
 * TRANSP_BREAK_SLASH, hide flags, empty-line and skip_empty handling. Per-song
 * state is local.
 */

#ifndef TRANSP_RENDER_H
#define TRANSP_RENDER_H

#include "parse.h"
#include "transp_flags.h"

char *transp_render(
        const transp_song_t *song, int semitones, int flags, char **en_table,
        char **latin_table, int *key);
/* malloc'd string; NULL on OOM. Both tables (English + Latin) + key live in
 * transp_ctx; the root table is chosen from TRANSP_LATIN, while a slash bass
 * is respelled through the table matching its own input language. */
#endif /* TRANSP_RENDER_H */
