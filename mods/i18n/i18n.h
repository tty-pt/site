#ifndef I18N_H
#define I18N_H

#include <stddef.h>
#include <ttypt/xy.h>

#include "i18n_dict.h"

#define I18N_LOCALE_EN "en"
#define I18N_LOCALE_PT "pt"

#ifndef I18N_IMPL

XY_DECL(const char *, i18n_resolve_locale, int, fd);
XY_DECL(int, i18n_set_user_locale, const char *, username, const char *, lang);
XY_DECL(const char *, i18n_translate, const char *, lang, const char *, msgid);
XY_DECL(int, i18n_register_dict, const i18n_entry_t *, entries, size_t, count);

#endif /* !I18N_IMPL */

#endif /* I18N_H */
