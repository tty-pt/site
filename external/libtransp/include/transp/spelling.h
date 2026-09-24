#ifndef TRANSP_SPELLING_H
#define TRANSP_SPELLING_H

/**
 * @file spelling.h
 * @brief Spelling-family decisions shared by the renderer and key dropdown.
 *
 * Single source of truth for the circle-of-fifths family rule.
 */

/**
 * @brief Circle-of-fifths spelling family of a key.
 *
 * Flat-family keys spell accidentals as flats, sharp-family keys as
 * sharps. Boundary keys resolve by Brazilian convention to Db (1) and
 * F# (6).
 */
enum {
	/** Key spelled with sharps. */
	SPELL_FAMILY_SHARP = 0,
	/** Key spelled with flats. */
	SPELL_FAMILY_FLAT = 1,
};

/**
 * @brief Chord-string spell decision.
 *
 * SPELL_FAMILY is an upstream state only — the caller resolves it (via
 * spelling_family) into SPELL_SHARP/SPELL_FLAT before calling.
 */
enum {
	/** Spell chord names with sharps. */
	SPELL_SHARP = 0,
	/** Spell chord names with flats. */
	SPELL_FLAT = 1,
};

/**
 * @brief Spelling family of a tonic (chromatic 0-11).
 *
 * FLAT keys spell accidentals as flats, SHARP keys as sharps. Boundary
 * keys resolve by Brazilian convention to Db (1) and F# (6). Out-of-range
 * input (e.g. key == -1) falls back to SHARP.
 *
 * @param[in] chrom Chromatic tonic 0-11.
 * @return SPELL_FAMILY_SHARP or SPELL_FAMILY_FLAT.
 */
static inline int spelling_family(int chrom)
{
	static const int table[12] = {
		SPELL_FAMILY_SHARP, /* 0  C  */
		SPELL_FAMILY_FLAT,  /* 1  Db */
		SPELL_FAMILY_SHARP, /* 2  D  */
		SPELL_FAMILY_FLAT,  /* 3  Eb */
		SPELL_FAMILY_SHARP, /* 4  E  */
		SPELL_FAMILY_FLAT,  /* 5  F  */
		SPELL_FAMILY_SHARP, /* 6  F# */
		SPELL_FAMILY_SHARP, /* 7  G  */
		SPELL_FAMILY_FLAT,  /* 8  Ab */
		SPELL_FAMILY_SHARP, /* 9  A  */
		SPELL_FAMILY_FLAT,  /* 10 Bb */
		SPELL_FAMILY_SHARP, /* 11 B  */
	};
	if (chrom >= 0 && chrom < 12)
		return table[chrom];
	return SPELL_FAMILY_SHARP;
}

#endif