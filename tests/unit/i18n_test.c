#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../mods/i18n/i18n_dict.h"
#include "../../mods/common/dict.h"
#include "../../mods/auth/dict.h"
#include "../../mods/song/dict.h"
#include "../../mods/gig/dict.h"
#include "../../mods/grp/dict.h"
#include "../../mods/poem/dict.h"

int main(void)
{
	printf("=== Testing i18n_dict (pure C isomorphic lookup) ===\n");

	/* English / Default fallback tests */
	assert(strcmp(i18n_t(NULL, "Submit"), "Submit") == 0);
	assert(strcmp(i18n_t("", "Submit"), "Submit") == 0);
	assert(strcmp(i18n_t("en", "Submit"), "Submit") == 0);
	assert(strcmp(i18n_t("en-US", "Cancel"), "Cancel") == 0);
	assert(strcmp(i18n_t("fr", "Save"), "Save") == 0);
	assert(strcmp(i18n_t("de", "Delete"), "Delete") == 0);
	assert(strcmp(i18n_t("pt", "Unknown Nonexistent String"), "Unknown Nonexistent String") == 0);
	assert(strcmp(i18n_t(NULL, NULL), "") == 0);
	assert(strcmp(i18n_t("pt", ""), "") == 0);

	/* Portuguese (pt, pt-PT, pt_PT) translation tests */
	assert(strcmp(i18n_t("pt", "Submit"), "Submeter") == 0);
	assert(strcmp(i18n_t("pt-PT", "Cancel"), "Cancelar") == 0);
	assert(strcmp(i18n_t("pt_PT", "Delete"), "Eliminar") == 0);
	assert(strcmp(i18n_t("PT", "Save"), "Guardar") == 0);
	assert(strcmp(i18n_t("pt-pt", "Add"), "Adicionar") == 0);
	assert(strcmp(i18n_t("pt", "Edit"), "Editar") == 0);
	assert(strcmp(i18n_t("pt", "Remove"), "Remover") == 0);
	assert(strcmp(i18n_t("pt", "Search"), "Pesquisar") == 0);
	assert(strcmp(i18n_t("pt", "Filters"), "Filtros") == 0);
	assert(strcmp(i18n_t("pt", "Clear all"), "Limpar tudo") == 0);
	assert(strcmp(i18n_t("pt", "No items found"), "Nenhum item encontrado") == 0);
	assert(strcmp(i18n_t("pt", "Are you sure you want to delete"), "Tem a certeza de que pretende eliminar") == 0);

	/* European Portuguese (pt-PT) distinct terminology checks */
	assert(strcmp(i18n_t("pt", "Username"), "Nome de utilizador") == 0);
	assert(strcmp(i18n_t("pt", "Password"), "Palavra-passe") == 0);
	assert(strcmp(i18n_t("pt", "Sign in"), "Iniciar sessão") == 0);
	assert(strcmp(i18n_t("pt", "Register"), "Registar") == 0);
	assert(strcmp(i18n_t("pt", "Confirm password"), "Confirmar palavra-passe") == 0);
	assert(strcmp(i18n_t("pt", "Don't have an account?"), "Não tem uma conta?") == 0);
	assert(strcmp(i18n_t("pt", "Already have an account?"), "Já tem uma conta?") == 0);

	/* Music and Site navigation checks */
	assert(strcmp(i18n_t("pt", "Songs"), "Músicas") == 0);
	assert(strcmp(i18n_t("pt", "Poems"), "Poemas") == 0);
	assert(strcmp(i18n_t("pt", "Gigs"), "Atuações") == 0);
	assert(strcmp(i18n_t("pt", "Groups"), "Grupos") == 0);
	assert(strcmp(i18n_t("pt", "Transpose"), "Transpor") == 0);
	assert(strcmp(i18n_t("pt", "Chords"), "Acordes") == 0);
	assert(strcmp(i18n_t("pt", "Lyrics"), "Letra") == 0);
	assert(strcmp(i18n_t("pt", "Watch on YouTube"), "Ver no YouTube") == 0);
	assert(strcmp(i18n_t("pt", "View PDF"), "Ver PDF") == 0);

	/* Per-module dictionary counts and entry verifications */
	assert(COMMON_DICT_COUNT > 0);
	assert(AUTH_DICT_COUNT > 0);
	assert(SONG_DICT_COUNT > 0);
	assert(GIG_DICT_COUNT > 0);
	assert(GRP_DICT_COUNT > 0);
	assert(POEM_DICT_COUNT > 0);

	/* Validate module-specific terms in modular dicts */
	assert(strcmp(auth_dict[0].pt, "Já tem uma conta?") == 0 || auth_dict[0].en != NULL);
	assert(song_dict[0].en != NULL && song_dict[0].pt != NULL);
	assert(gig_dict[0].en != NULL && gig_dict[0].pt != NULL);
	assert(grp_dict[0].en != NULL && grp_dict[0].pt != NULL);
	assert(poem_dict[0].en != NULL && poem_dict[0].pt != NULL);

	printf("ALL PASS: i18n_dict assertions passed\n");
	return 0;
}
