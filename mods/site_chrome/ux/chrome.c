#include <string.h>

#include "bud/bud.h"
#include "bud/bud_app.h"
#include "../../common/site_wasm_app.h"
#include "site_chrome.h"
#include <ttypt/stele_wasm.h>

static site_ui_chrome_state chrome_state;

static int chrome_stl_init(const char *json, int len)
{
	size_t jlen = len >= 0 ? (size_t)len : 0;
	memset(&chrome_state, 0, sizeof(chrome_state));
	site_ui_chrome_state_deserialize(&chrome_state, json, jlen);
	return 0;
}

static bud_node *chrome_stl_render(void)
{
	return site_ui_chrome(&chrome_state);
}

BUD_APP_ROUTE(chrome_stl_render);

ISO_EXPORT(int, wasm_init, const char *json, int len)
{
	return chrome_stl_init(json, len);
}

BUD_WASM_MODULE(site_chrome, chrome_stl_init, chrome_stl_render, NULL);

