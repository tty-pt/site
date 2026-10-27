#ifndef SOFTWARE_DICT_H
#define SOFTWARE_DICT_H

#include "../i18n/i18n_dict.h"

static const i18n_entry_t software_dict[] = {
	{ "Add software", "Adicionar software" },
	{ "Software", "Software" },
	{ "Software:", "Software:" },
	{ "Repository", "Repositório" },
	{ "Version", "Versão" },
	{ "License", "Licença" },
	{ "Category", "Categoria" },
	{ "Maintainer", "Responsável" },
	{ "Documentation", "Documentação" }
};

#define SOFTWARE_DICT_COUNT (sizeof(software_dict) / sizeof(software_dict[0]))

#endif /* SOFTWARE_DICT_H */
