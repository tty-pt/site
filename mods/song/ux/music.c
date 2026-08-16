#ifndef MUSIC_C
#define MUSIC_C
#include "music.h"
#include <stdio.h>
#include <string.h>

#include "spelling.h"

/* ── Shared key name tables ─────────────────── */
static const char *KEY_NAMES[] = { "C",  "C#", "D",  "D#", "E",  "F",
	                           "F#", "G",  "G#", "A",  "A#", "B" };
static const char *KEY_NAMES_B[] = { "C",  "Db", "D",  "Eb", "E",  "F",
	                             "Gb", "G",  "Ab", "A",  "Bb", "B" };
static const char *KEY_NAMES_LATIN[] = { "Do",   "Do#", "Re",  "Re#",
	                                 "Mi",   "Fa",  "Fa#", "Sol",
	                                 "Sol#", "La",  "La#", "Si" };
static const char *KEY_NAMES_BL[] = {
	"Do",   "Reb", "Re",  "Mib", "Mi",  "Fa",
	"Solb", "Sol", "Lab", "La",  "Sib", "Si"
};

const char *key_name(int semitones, int orig_key, int latin)
{
	static char buf[64];
	int idx = ((orig_key + semitones) % 12 + 12) % 12;
	const char **table;
	if (spelling_family(idx) == SPELL_FAMILY_FLAT)
		table = latin ? KEY_NAMES_BL : KEY_NAMES_B;
	else
		table = latin ? KEY_NAMES_LATIN : KEY_NAMES;
	if (semitones == 0)
		snprintf(buf, sizeof(buf), "%s (Original)", table[idx]);
	else
		snprintf(buf, sizeof(buf), "%s (%+d)", table[idx], semitones);
	return buf;
}

const char *target_key_name(int orig_key, int transpose, int latin)
{
	int idx = ((orig_key + transpose) % 12 + 12) % 12;
	const char **kt;
	if (spelling_family(idx) == SPELL_FAMILY_FLAT)
		kt = latin ? KEY_NAMES_BL : KEY_NAMES_B;
	else
		kt = latin ? KEY_NAMES_LATIN : KEY_NAMES;
	return kt[idx];
}

#endif
