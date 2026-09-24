#ifndef TRANSP_FLAGS_H
#define TRANSP_FLAGS_H

/**
 * @file transp_flags.h
 * @brief Bitwise transpose output flags.
 */

/* Flags (bitwise OR together) */

/** @brief Emit HTML markup (div per line, b per chord line, escaped text). */
#define TRANSP_HTML 0x04
/** @brief Spell accidentals as flats (Db instead of C#). */
#define TRANSP_BEMOL 0x08
/** @brief Break lyric lines at a '/' followed by a space. */
#define TRANSP_BREAK_SLASH 0x20
/** @brief Drop '%' comment lines and skip a following empty line. */
#define TRANSP_REMOVE_COMMENTS 0x10
/** @brief Omit chord symbols from the output. */
#define TRANSP_HIDE_CHORDS 0x01
/** @brief Omit lyric lines from the output. */
#define TRANSP_HIDE_LYRICS 0x02
/** @brief Use Latin solfege root names; minor suffix 'm' renders as '-'. */
#define TRANSP_LATIN 0x80

#endif