#ifndef TRANSP_MUSIC_H
#define TRANSP_MUSIC_H

/**
 * @file music.h
 * @brief Key names for the transp renderer and key dropdown.
 */

/**
 * @brief Format a transposed key's display name.
 *
 * Renders the family-spelled key with its semitone offset appended
 * ("D (+2)"), or " (Original)" when the shift is zero.
 *
 * @param[in] semitones Semitone shift.
 * @param[in] orig_key  Chromatic source key 0-11.
 * @param[in] latin     Use Latin solfege names when nonzero.
 * @return Display name in a static buffer (do not free).
 */
const char *key_name(int semitones, int orig_key, int latin);

/**
 * @brief Name of the transposed target key.
 *
 * Family-spelled (flat keys spelled as flats), without the offset
 * suffix; Latin solfege when latin is nonzero.
 *
 * @param[in] orig_key  Chromatic source key 0-11.
 * @param[in] transpose Semitone shift.
 * @param[in] latin     Use Latin solfege names when nonzero.
 * @return Static key name string.
 */
const char *target_key_name(int orig_key, int transpose, int latin);

#endif