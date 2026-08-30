#ifndef GRP_DICT_H
#define GRP_DICT_H

#include "../i18n/i18n_dict.h"

static const i18n_entry_t grp_dict[] = {
	{ "Add group", "Adicionar grupo" },
	{ "Add member", "Adicionar membro" },
	{ "Group", "Grupo" },
	{ "Group:", "Grupo:" },
	{ "Groups", "Grupos" },
	{ "Leader", "Líder" },
	{ "Member", "Membro" },
	{ "Members", "Membros" },
	{ "Repertoire", "Repertório" },
	{ "Role", "Função" }
};

#define GRP_DICT_COUNT (sizeof(grp_dict) / sizeof(grp_dict[0]))

#endif /* GRP_DICT_H */
