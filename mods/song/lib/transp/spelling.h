#ifndef TRANSP_SPELLING_H
#define TRANSP_SPELLING_H

/* Spelling-family decisions shared by the transp renderer and the song key
 * dropdown (mods/song/ux/music.c). Single source of truth for the circle-of-
 * fifths family rule; see CHORDS.md §10.2. */

enum {
	SPELL_FAMILY_SHARP = 0,
	SPELL_FAMILY_FLAT = 1,
};

/* chord_str() spell parameter. SPELL_FAMILY is an upstream state only — the
 * caller resolves it (via spelling_family) into SPELL_SHARP/SPELL_FLAT before
 * calling (see CHORDS.md §10.3 step 2). */
enum {
	SPELL_SHARP = 0,
	SPELL_FLAT = 1,
};

/* Family of a tonic (chromatic 0-11): FLAT keys spell accidentals as flats,
 * SHARP keys as sharps. Boundary keys resolve by Brazilian convention to
 * Db (1) and F# (6). Out-of-range input (e.g. key == -1) falls back to SHARP. */
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
