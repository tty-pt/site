#ifndef SITE_CHROME_H
#define SITE_CHROME_H

#include "bud/bud.h"
#include "bud/bud_state.h"

#define SITE_CHROME_TITLE_MAX 256
#define SITE_CHROME_PATH_MAX 1024
#define SITE_CHROME_ICON_MAX 32
#define SITE_CHROME_USER_MAX 128

#define SITE_UI_CHROME_SCHEMA(F, st) \
	F(STR, st, title, SITE_CHROME_TITLE_MAX, "") \
	F(STR, st, path,  SITE_CHROME_PATH_MAX,  "") \
	F(STR, st, icon,  SITE_CHROME_ICON_MAX,  "") \
	F(STR, st, user,  SITE_CHROME_USER_MAX,  "") \
	F(STR, st, lang,  32,                    "en")

BUD_DEFINE_STATE(site_ui_chrome_state, SITE_UI_CHROME_SCHEMA)

bud_node *site_ui_chrome(const site_ui_chrome_state *state);

#endif
