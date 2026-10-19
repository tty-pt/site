#include <string.h>

#include "bud/bud.h"
#include "bud/bud_app.h"
#include "site_chrome.h"

static site_ui_chrome_state chrome_state;

void wasm_init(const char *json, int len)
{
	size_t jlen = len >= 0 ? (size_t)len : 0;
	memset(&chrome_state, 0, sizeof(chrome_state));
	bud_json_str_len(
	        json, jlen, "title", chrome_state.title,
	        sizeof(chrome_state.title));
	bud_json_str_len(
	        json, jlen, "path", chrome_state.path,
	        sizeof(chrome_state.path));
	bud_json_str_len(
	        json, jlen, "icon", chrome_state.icon,
	        sizeof(chrome_state.icon));
	bud_json_str_len(
	        json, jlen, "user", chrome_state.user,
	        sizeof(chrome_state.user));
	bud_json_str_len(
	        json, jlen, "lang", chrome_state.lang,
	        sizeof(chrome_state.lang));
}

bud_node *bud_app_render(void)
{
	return site_ui_chrome(&chrome_state);
}
