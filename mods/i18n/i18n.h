#ifndef I18N_H
#define I18N_H

/*
 * mods/i18n — Caller-facing XY hook declarations.
 * Types and translation dictionary live in i18n_dict.h.
 * Implementers must include i18n_dict.h, not this header.
 */

#include <stddef.h>
#include <ttypt/xy.h>

#include "i18n_dict.h"

XY_DECL(const char *, i18n_resolve_locale, int, fd);
XY_DECL(int, i18n_set_user_locale, const char *, username, const char *, lang);
XY_DECL(const char *, i18n_translate, const char *, lang, const char *, msgid);
XY_DECL(int, i18n_register_dict, const i18n_entry_t *, entries, size_t, count);

#endif /* I18N_H */
