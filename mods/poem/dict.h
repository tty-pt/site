#ifndef POEM_DICT_H
#define POEM_DICT_H

#include "../i18n/i18n_dict.h"

static const i18n_entry_t poem_dict[] = {
	{ "Add poem", "Adicionar poema" },
	{ "Lines", "Versos" },
	{ "Poem", "Poema" },
	{ "Poem:", "Poema:" },
	{ "Poems", "Poemas" },
	{ "Verses", "Versos" }
};

#define POEM_DICT_COUNT (sizeof(poem_dict) / sizeof(poem_dict[0]))

#endif /* POEM_DICT_H */
