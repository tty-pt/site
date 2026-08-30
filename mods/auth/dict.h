#ifndef AUTH_DICT_H
#define AUTH_DICT_H

#include "../i18n/i18n_dict.h"

static const i18n_entry_t auth_dict[] = {
	{ "Already have an account?", "Já tem uma conta?" },
	{ "Confirm password", "Confirmar palavra-passe" },
	{ "Confirm password:", "Confirmar palavra-passe:" },
	{ "Don't have an account?", "Não tem uma conta?" },
	{ "Email:", "Email:" },
	{ "Invalid credentials", "Credenciais inválidas" },
	{ "Log in", "Entrar" },
	{ "Log in here", "Inicie sessão aqui" },
	{ "Password", "Palavra-passe" },
	{ "Password:", "Palavra-passe:" },
	{ "Passwords do not match", "As palavras-passe não coincidem" },
	{ "Register", "Registar" },
	{ "Register here", "Registe-se aqui" },
	{ "Sign in", "Iniciar sessão" },
	{ "Username", "Nome de utilizador" },
	{ "Username already exists", "Nome de utilizador já existe" },
	{ "Username:", "Nome de utilizador:" }
};

#define AUTH_DICT_COUNT (sizeof(auth_dict) / sizeof(auth_dict[0]))

#endif /* AUTH_DICT_H */
