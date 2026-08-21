#ifndef SITE_CHROME_H
#define SITE_CHROME_H

#include "bud/bud.h"

#define SITE_CHROME_TITLE_MAX 256
#define SITE_CHROME_PATH_MAX 1024
#define SITE_CHROME_ICON_MAX 32
#define SITE_CHROME_USER_MAX 128

typedef struct {
	char title[SITE_CHROME_TITLE_MAX];
	char path[SITE_CHROME_PATH_MAX];
	char icon[SITE_CHROME_ICON_MAX];
	char user[SITE_CHROME_USER_MAX];
} site_ui_chrome_state;

bud_node *site_ui_chrome(const site_ui_chrome_state *state);

#endif
