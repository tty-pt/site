#ifndef GIG_DICT_H
#define GIG_DICT_H

#include "../i18n/i18n_dict.h"

static const i18n_entry_t gig_dict[] = {
	{ "Add gig", "Adicionar atuação" },
	{ "Date", "Data" },
	{ "Event", "Evento" },
	{ "Gig", "Atuação" },
	{ "Gig:", "Atuação:" },
	{ "Gigs", "Atuações" },
	{ "Set", "Alinhamento" },
	{ "Sets", "Alinhamentos" },
	{ "Songbooks", "Cancionários" }
};

#define GIG_DICT_COUNT (sizeof(gig_dict) / sizeof(gig_dict[0]))

#endif /* GIG_DICT_H */
