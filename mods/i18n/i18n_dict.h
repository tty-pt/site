#ifndef I18N_DICT_H
#define I18N_DICT_H

#include <stddef.h>
#include <string.h>

/*
 * Pure C isomorphic translation dictionary.
 * Zero external dependencies (only stddef.h and string.h).
 * Valid in native server .so (SSR) and browser wasm32-wasi (.wasm).
 *
 * Primary target: European Portuguese (pt-PT).
 * Default fallback: English (en).
 */

#define I18N_LOCALE_EN "en"
#define I18N_LOCALE_PT "pt"

typedef struct {
	const char *en;
	const char *pt;
} i18n_entry_t;

/*
 * Translation catalog sorted by English message for clarity.
 * Uses canonical European Portuguese (pt-PT) terminology:
 *   - "Nome de utilizador" (not usuário)
 *   - "Palavra-passe" (not senha)
 *   - "Registar" (not registrar)
 *   - "Guardar" (not salvar)
 *   - "Eliminar" / "Apagar"
 *   - "Atuações" / "Alinhamentos"
 */
static const i18n_entry_t i18n_pt_table[] = {
	{ "Actions", "Ações" },
	{ "Add", "Adicionar" },
	{ "Add member", "Adicionar membro" },
	{ "Add new", "Adicionar novo" },
	{ "Add song", "Adicionar música" },
	{ "Advanced filters", "Filtros avançados" },
	{ "All", "Todos" },
	{ "Already have an account?", "Já tem uma conta?" },
	{ "Any", "Qualquer" },
	{ "Apply", "Aplicar" },
	{ "Are you sure you want to delete", "Tem a certeza de que pretende eliminar" },
	{ "Ascending", "Ascendente" },
	{ "Author", "Autor" },
	{ "Author:", "Autor:" },
	{ "Back", "Voltar" },
	{ "Bad request", "Pedido inválido" },
	{ "Cancel", "Cancelar" },
	{ "Category", "Categoria" },
	{ "Chords", "Acordes" },
	{ "Clear", "Limpar" },
	{ "Clear all", "Limpar tudo" },
	{ "Close Menu", "Fechar menu" },
	{ "Confirm password", "Confirmar palavra-passe" },
	{ "Confirm password:", "Confirmar palavra-passe:" },
	{ "Custom", "Personalizado" },
	{ "Date", "Data" },
	{ "Delete", "Eliminar" },
	{ "Descending", "Descendente" },
	{ "Don't have an account?", "Não tem uma conta?" },
	{ "Edit", "Editar" },
	{ "Email:", "Email:" },
	{ "Event", "Evento" },
	{ "Filter", "Filtrar" },
	{ "Filters", "Filtros" },
	{ "Forbidden", "Acesso proibido" },
	{ "Gig", "Atuação" },
	{ "Gig:", "Atuação:" },
	{ "Gigs", "Atuações" },
	{ "Group", "Grupo" },
	{ "Group:", "Grupo:" },
	{ "Groups", "Grupos" },
	{ "Home", "Início" },
	{ "Internal server error", "Erro interno do servidor" },
	{ "Invalid credentials", "Credenciais inválidas" },
	{ "Key", "Tom" },
	{ "Key:", "Tom:" },
	{ "Latin", "Latim" },
	{ "Latin notation", "Notação latina" },
	{ "Leader", "Líder" },
	{ "Lines", "Versos" },
	{ "Log in", "Entrar" },
	{ "Log in here", "Inicie sessão aqui" },
	{ "Login", "Iniciar sessão" },
	{ "Logout", "Terminar sessão" },
	{ "Lyrics", "Letra" },
	{ "Me", "Perfil" },
	{ "Media", "Multimédia" },
	{ "Member", "Membro" },
	{ "Members", "Membros" },
	{ "Menu", "Menu" },
	{ "Next", "Seguinte" },
	{ "Next →", "Seguinte →" },
	{ "No items found", "Nenhum item encontrado" },
	{ "Not found", "Não encontrado" },
	{ "Open", "Abrir" },
	{ "Order by", "Ordenar por" },
	{ "Page", "Página" },
	{ "Password", "Palavra-passe" },
	{ "Password:", "Palavra-passe:" },
	{ "Passwords do not match", "As palavras-passe não coincidem" },
	{ "Poem", "Poema" },
	{ "Poem:", "Poema:" },
	{ "Poems", "Poemas" },
	{ "Previous", "Anterior" },
	{ "Print", "Imprimir" },
	{ "Private", "Privado" },
	{ "Public", "Público" },
	{ "Register", "Registar" },
	{ "Register here", "Registe-se aqui" },
	{ "Remove", "Remover" },
	{ "Reset", "Repor" },
	{ "Role", "Função" },
	{ "Save", "Guardar" },
	{ "Search", "Pesquisar" },
	{ "Search...", "Pesquisar..." },
	{ "Search…", "Pesquisar…" },
	{ "Search everything", "Pesquisar tudo" },
	{ "Search options", "Pesquisar opções" },
	{ "Sign in", "Iniciar sessão" },
	{ "Song", "Música" },
	{ "Song:", "Música:" },
	{ "Songs", "Músicas" },
	{ "Status", "Estado" },
	{ "Submit", "Submeter" },
	{ "Title", "Título" },
	{ "Title:", "Título:" },
	{ "Transpose", "Transpor" },
	{ "Type", "Tipo" },
	{ "Unauthorized", "Não autorizado" },
	{ "Username", "Nome de utilizador" },
	{ "Username:", "Nome de utilizador:" },
	{ "Username already exists", "Nome de utilizador já existe" },
	{ "Verses", "Versos" },
	{ "View PDF", "Ver PDF" },
	{ "Watch on YouTube", "Ver no YouTube" },
	{ "Wrap lines", "Quebrar linhas" },
	{ "Zoom", "Zoom" },
	{ "e.g. \"a quiet place\"", "ex.: \"um lugar calmo\"" },
	{ "← Prev", "← Anterior" },
	{ " / page", " / página" },
	{ "/ page", "/ página" },
	{ "de", "de" },
	{ "delete", "eliminar" },
	{ "edit", "editar" },
	{ "login", "iniciar sessão" },
	{ "logout", "terminar sessão" },
	{ "me", "perfil" },
	{ "of", "de" },
	{ "options", "opções" },
	{ "register", "registar" },
	{ "rows", "linhas" }
};

#define I18N_PT_TABLE_SIZE (sizeof(i18n_pt_table) / sizeof(i18n_pt_table[0]))

static inline int i18n_is_pt_locale(const char *lang)
{
	if (!lang || !lang[0])
		return 0;
	if ((lang[0] == 'p' || lang[0] == 'P') &&
	    (lang[1] == 't' || lang[1] == 'T')) {
		char next = lang[2];
		return (next == '\0' || next == '-' || next == '_' || next == ';');
	}
	return 0;
}

/*
 * Pure C translation lookup.
 * Returns translated string if lang is Portuguese, or fallback to msgid.
 */
static inline const char *i18n_t(const char *lang, const char *msgid)
{
	size_t i;

	if (!msgid || !msgid[0])
		return "";
	if (!i18n_is_pt_locale(lang))
		return msgid;

	for (i = 0; i < I18N_PT_TABLE_SIZE; i++) {
		if (strcmp(i18n_pt_table[i].en, msgid) == 0)
			return i18n_pt_table[i].pt;
	}

	return msgid;
}

#endif /* I18N_DICT_H */
