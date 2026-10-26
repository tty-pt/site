#ifndef ND_UI_H
#define ND_UI_H

#include <bud/bud.h>
#include <nd/bcp.h>
#include <nd/ui-slot.h>
#include "nd_viewport.h"
#include "nd_lamina.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Construct the isomorphic Bud UI tree for NeverDark.
 * Used on server for native SSR and in WASM for browser runtime tree.
 */
bud_node *nd_ui_render(const nd_client_state_t *st);

/*
 * Serialize client state to JSON for embedding in SSR <script id="bud-state">.
 */
void nd_state_serialize(const nd_client_state_t *st, char *buf, size_t cap);

/*
 * Initialize UI registries and default drivers/extensions.
 */
void nd_ui_init_defaults(void);

/*
 * Retrieve current client state pointer.
 */
const nd_client_state_t *nd_ui_get_client_state(void);

/*
 * Client WASM entrypoint rehydrating JSON state from SSR.
 */
void wasm_init(const char *json, int len);

#ifdef __cplusplus
}
#endif

#endif /* ND_UI_H */
