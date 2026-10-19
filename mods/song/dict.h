#ifndef SONG_DICT_H
#define SONG_DICT_H

#include "../i18n/i18n_dict.h"

static const i18n_entry_t song_dict[] = {
	{ "Add song", "Adicionar música" },
	{ "Chords", "Acordes" },
	{ "Key", "Tom" },
	{ "Key:", "Tom:" },
	{ "Latin", "Latim" },
	{ "Latin notation", "Notação latina" },
	{ "Lines", "Versos" },
	{ "Lyrics", "Letra" },
	{ "Media", "Multimédia" },
	{ "Song", "Música" },
	{ "Song:", "Música:" },
	{ "Songs", "Músicas" },
	{ "Transpose", "Transpor" },
	{ "View PDF", "Ver PDF" },
	{ "Watch on YouTube", "Ver no YouTube" },
	{ "e.g. \"a quiet place\"", "ex.: \"um lugar calmo\"" }
};

#define SONG_DICT_COUNT (sizeof(song_dict) / sizeof(song_dict[0]))

#endif /* SONG_DICT_H */
