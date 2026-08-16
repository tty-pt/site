/* WASM entry for the shared list-page enhancement bundle.
 * Dual-compiled from the same sources as the native SSR render (list.c) so
 * bud node ids align on both sides. See docs/C-ISOMORPHIC-BUD.md. */

#include <string.h>
#include <stdlib.h>

#include "bud/bud.h"
#include "bud/bud_jsx.h"
#include "bud/bud_app.h"

#include <hyle-bud/hyle-bud.h>

#include "../../common/viewer_zoom.h" /* VIEWER_ZOOM_* + STR for site_ui.c */
#include "../../common/ux/site_ui.c" /* site_ui_layout, site_ui_empty_state, ... */
#include "list.c"                    /* list_state_t + list_render + ... */

static list_state_t g_state = { 0 };

void wasm_init(const char *json, int len)
{
	(void)len;
	list_state_from_json(&g_state, json);
}

bud_node *bud_app_render(void)
{
	return list_render(&g_state);
}
