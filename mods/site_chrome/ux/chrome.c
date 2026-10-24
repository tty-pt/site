#include <string.h>

#include "bud/bud.h"
#include "bud/bud_app.h"
#include "site_chrome.h"

static site_ui_chrome_state chrome_state;

ISO_EXPORT(void, wasm_init, const char *json, int len)
{
	size_t jlen = len >= 0 ? (size_t)len : 0;
	memset(&chrome_state, 0, sizeof(chrome_state));
	site_ui_chrome_state_deserialize(&chrome_state, json, jlen);
}

ISO_EXPORT(bud_node *, bud_app_render, void)
{
	return site_ui_chrome(&chrome_state);
}
