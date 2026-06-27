#ifndef MUSIC_C
#define MUSIC_C
#include "music.h"
#include <stdio.h>
#include <string.h>

/* ── Shared key name tables ─────────────────────────── */
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

const char *key_name(int semitones, int orig_key, int bemol, int latin)
{
	static char buf[64];
	const char **table = KEY_NAMES;
	if (bemol && latin)
		table = KEY_NAMES_BL;
	else if (bemol)
		table = KEY_NAMES_B;
	else if (latin)
		table = KEY_NAMES_LATIN;
	int idx = ((orig_key + semitones) % 12 + 12) % 12;
	if (semitones == 0)
		snprintf(buf, sizeof(buf), "%s (Original)", table[idx]);
	else
		snprintf(buf, sizeof(buf), "%s (%+d)", table[idx], semitones);
	return buf;
}

const char *target_key_name(int orig_key, int transpose, int flags)
{
	int bemol = (flags & 0x08) ? 1 : 0;
	int latin = (flags & 0x80) ? 1 : 0;
	const char **kt = KEY_NAMES;
	if (bemol && latin)
		kt = KEY_NAMES_BL;
	else if (bemol)
		kt = KEY_NAMES_B;
	else if (latin)
		kt = KEY_NAMES_LATIN;
	int idx = ((orig_key + transpose) % 12 + 12) % 12;
	return kt[idx];
}

#endif
