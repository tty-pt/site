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
 * RPG Chat expand/collapse state query & mutation.
 */
int nd_ui_get_chat_expanded(void);
void nd_ui_set_chat_expanded(int expanded);
int nd_ui_get_chat_minimized(void);
void nd_ui_set_chat_minimized(int minimized);
int nd_ui_get_chat_width(void);
int nd_ui_get_chat_height(void);
void nd_ui_set_chat_size(int width, int height);

/*
 * Floating HUD Window Management (move, resize, minimize, reset, localStorage restore).
 */
typedef enum {
	ND_WIN_CHAT = 0,
	ND_WIN_TITLE = 1,
	ND_WIN_MAP = 2,
	ND_WIN_CONTENTS = 3,
	ND_WIN_ACTIONS = 4,
	ND_WIN_TARGET = 5,
	ND_WIN_COUNT = 6
} nd_hud_win_id_t;

void nd_ui_set_win_rect(int win_id, int x, int y, int w, int h);
void nd_ui_get_win_rect(int win_id, int *x, int *y, int *w, int *h);
void nd_ui_set_win_minimized(int win_id, int minimized);
int nd_ui_get_win_minimized(int win_id);
void nd_ui_reset_win(int win_id);
void nd_ui_restore_persist(const char *key, const char *val);

/*
 * Client WASM entrypoint rehydrating JSON state from SSR.
 */
void wasm_init(const char *json, int len);

#ifdef __cplusplus
}
#endif

#endif /* ND_UI_H */
