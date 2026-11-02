#include <string.h>

#include "bud/bud.h"
#include "bud/bud_app.h"
#include "site_chrome.h"
#include <ttypt/qsys_wasm.h>

static site_ui_chrome_state chrome_state;

static int chrome_qsys_init(const char *json, int len)
{
	size_t jlen = len >= 0 ? (size_t)len : 0;
	memset(&chrome_state, 0, sizeof(chrome_state));
	site_ui_chrome_state_deserialize(&chrome_state, json, jlen);
	return 0;
}

static bud_node *chrome_qsys_render(void)
{
	return site_ui_chrome(&chrome_state);
}

QSYS_WASM_MODULE(site_chrome, chrome_qsys_init, chrome_qsys_render, NULL);

