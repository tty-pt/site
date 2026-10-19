#ifndef SITE_UI_C
#define SITE_UI_C

#include "site_ui.h"

static __thread const char *g_site_ui_locale = NULL;

const char *site_ui_get_locale(void)
{
	return g_site_ui_locale ? g_site_ui_locale : I18N_LOCALE_EN;
}

void site_ui_set_locale(const char *lang)
{
	g_site_ui_locale = (lang && lang[0]) ? lang : I18N_LOCALE_EN;
	hyle_bud_set_translator(ui_t);
}

/* WASM detail init helpers: collapse repeated json-length normalization into intent-level call. */
void wasm_state_init(const char *json, int len, const bud_field_desc_t *fields, void *state)
{
	size_t jlen = len >= 0 ? (size_t)len : 0;
	bud_state_apply_len(state, fields, json, jlen);
}

#include "site_paths.c"
#include "site_layout.c"
#include "site_forms.c"
#include "site_media.c"

#ifndef __wasm__
#include "site_chrome.c"
#include "site_page.c"
#endif

#endif
