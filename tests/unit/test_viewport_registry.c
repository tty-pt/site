#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <bud/bud.h>
#include "nd/bcp-types.h"
#include "nd/region-types.h"
#include "nd_viewport.h"
#include "nd_seven_motion.h"
#include "nd_seven.h"
#include "nd_ui.h"

static bud_node *mock_stage_mount(void)
{
	bud_node *cv = bud_element("canvas");
	bud_set_attr(cv, "id", "test-canvas");
	return cv;
}

int main(void)
{
	printf("=== Testing Viewport Driver Registry and Clean 2D UI Mode ===\n");

	/* 1. Initial state has default tty driver */
	nd_ui_init_defaults();
	assert(nd_viewport_driver_count() >= 1);
	const nd_viewport_driver_t *active = nd_get_active_viewport_driver();
	assert(active != NULL);
	assert(strcmp(active->id, "tty") == 0);
	printf("PASS: default tty driver registered and active\n");

	/* 2. Register custom viewport driver */
	nd_viewport_driver_t custom_drv = {
		.id = "lamina-seven",
		.label = "🎮 Seven Ways",
		.required_capability = "lamina",
		.mount = mock_stage_mount,
	};
	int idx = nd_register_viewport_driver(&custom_drv);
	assert(idx >= 1);
	assert(nd_viewport_driver_count() >= 2);
	assert(nd_set_active_viewport_driver("lamina-seven") == idx);
	active = nd_get_active_viewport_driver();
	assert(strcmp(active->id, "lamina-seven") == 0);
	printf("PASS: custom driver registered and set active\n");

	/* 3. Fallback on invalid driver ID */
	assert(nd_set_active_viewport_driver("non-existent-xyz") == -1);
	printf("PASS: invalid driver ID cleanly rejected\n");

	/* 4. Layout constant verification */
	assert(ND_LAYOUT_GAME_2D == 3);
	printf("PASS: ND_LAYOUT_GAME_2D constant is 3\n");

	/* 5. Seven RPG Mode renders game canvas, RPG chat terminal, and room contents/map */
	nd_client_state_t st;
	memset(&st, 0, sizeof(st));
	strncpy(st.user, "hero", sizeof(st.user) - 1);
	strncpy(st.preferred_driver, "lamina-seven", sizeof(st.preferred_driver) - 1);
	strncpy(st.preferred_map_driver, "lamina", sizeof(st.preferred_map_driver) - 1);
	st.room.ref = 1;
	strncpy(st.room.name, "Town Square", sizeof(st.room.name) - 1);
	st.layout_mode = ND_LAYOUT_GAME_2D;

	bud_node *root = nd_ui_render(&st);
	assert(root != NULL);
	char *html = bud_render_html(root);
	assert(html != NULL);

	/* Must contain custom stage canvas and room map */
	assert(strstr(html, "id=\"test-canvas\"") != NULL);
	assert(strstr(html, "id=\"room-map\"") != NULL);
	assert(strstr(html, "game-2d-mode") != NULL);

	/* Must contain RPG chat console with micro-icon controls, 8-way resize handles, hover toolbar, and terminal */
	assert(strstr(html, "id=\"rpg-chat\"") != NULL);
	assert(strstr(html, "data-bud-persist-key=\"nd-win-chat\"") != NULL);
	assert(strstr(html, "id=\"chat-toolbar\"") != NULL);
	assert(strstr(html, "id=\"chat-min-btn\"") != NULL);
	assert(strstr(html, "id=\"chat-toggle-btn\"") != NULL);
	assert(strstr(html, "id=\"chat-reset-btn\"") != NULL);
	assert(strstr(html, "id=\"chat-resize-n\"") != NULL);
	assert(strstr(html, "id=\"chat-resize-s\"") != NULL);
	assert(strstr(html, "id=\"chat-resize-e\"") != NULL);
	assert(strstr(html, "id=\"chat-resize-w\"") != NULL);
	assert(strstr(html, "id=\"chat-resize-ne\"") != NULL);
	assert(strstr(html, "id=\"chat-resize-nw\"") != NULL);
	assert(strstr(html, "id=\"chat-resize-se\"") != NULL);
	assert(strstr(html, "id=\"chat-resize-sw\"") != NULL);
	assert(strstr(html, "id=\"term\"") != NULL);
	assert(strstr(html, "chat-compact") != NULL);
	assert(strstr(html, "hud-win-toolbar") != NULL);
	assert(strstr(html, "✕") != NULL);
	assert(strstr(html, "⤢") != NULL);
	assert(strstr(html, "↺") != NULL);

	/* 4 HUD windows (chat, title, map, contents) must be movable/resizable HUD windows with persist keys, while #bottom-right-actions is a permanent toolbar-free dock without viewport switcher */
	assert(strstr(html, "data-bud-persist-key=\"nd-win-title\"") != NULL);
	assert(strstr(html, "id=\"title-toolbar\"") != NULL);
	assert(strstr(html, "id=\"title-resize-se\"") != NULL);
	assert(strstr(html, "data-bud-persist-key=\"nd-win-map\"") != NULL);
	assert(strstr(html, "id=\"map-toolbar\"") != NULL);
	assert(strstr(html, "id=\"map-resize-se\"") != NULL);
	assert(strstr(html, "id=\"hud-win-target\"") == NULL);
	assert(strstr(html, "data-bud-persist-key=\"nd-win-contents\"") != NULL);
	assert(strstr(html, "id=\"contents-toolbar\"") != NULL);
	assert(strstr(html, "id=\"contents-resize-se\"") != NULL);
	assert(strstr(html, "id=\"bottom-right-actions\"") != NULL);
	assert(strstr(html, "hud-actions-dock") != NULL);
	assert(strstr(html, "id=\"actions-toolbar\"") == NULL);
	assert(strstr(html, "id=\"actions-resize-se\"") == NULL);
	assert(strstr(html, "id=\"toggle-3d-btn\"") == NULL);
	assert(strstr(html, "data-bud-action=\"cycle-viewport\"") == NULL);
	assert(strstr(html, "Switch Viewport") == NULL);
	assert(strstr(html, "👁️") != NULL);
	assert(strstr(html, "id=\"bottom-left-windows\"") != NULL);

	/* Test Windows-style drag resize dimensions on #rpg-chat */
	nd_ui_set_chat_size(640, 300);
	assert(nd_ui_get_chat_width() == 640);
	assert(nd_ui_get_chat_height() == 300);
	bud_free_string(html);
	root = nd_ui_render(&st);
	html = bud_render_html(root);
	assert(html != NULL);
	assert(strstr(html, "style=\"width:640px;height:300px;\"") != NULL);
	nd_ui_set_chat_size(0, 0);

	/* Test moving and resizing a HUD window + localStorage restore via nd_ui_restore_persist */
	nd_ui_restore_persist("nd-win-contents", "left:40px;top:50px;right:auto;bottom:auto;transform:none;width:300px;height:180px;|hud-win hud-win-contents");
	int cx = 0, cy = 0, cw = 0, ch = 0;
	nd_ui_get_win_rect(ND_WIN_CONTENTS, &cx, &cy, &cw, &ch);
	assert(cx == 40 && cy == 50 && cw == 300 && ch == 180);
	bud_free_string(html);
	root = nd_ui_render(&st);
	html = bud_render_html(root);
	assert(html != NULL);
	assert(strstr(html, "style=\"left:40px;top:50px;right:auto;bottom:auto;transform:none;width:300px;height:180px;\"") != NULL);
	nd_ui_reset_win(ND_WIN_CONTENTS);
	nd_ui_get_win_rect(ND_WIN_CONTENTS, &cx, &cy, &cw, &ch);
	assert(cx == -1 && cy == -1 && cw == 0 && ch == 0);

	/* Test chat toggle mutation into expanded state */
	nd_ui_set_chat_expanded(1);
	assert(nd_ui_get_chat_expanded() == 1);
	bud_free_string(html);
	root = nd_ui_render(&st);
	html = bud_render_html(root);
	assert(html != NULL);
	assert(strstr(html, "chat-expanded") != NULL);
	assert(strstr(html, "⤡") != NULL);
	nd_ui_set_chat_expanded(0);

	/* Test closing chat window: renders bottom-left restore button while preserving #term */
	nd_ui_set_chat_minimized(1);
	assert(nd_ui_get_chat_minimized() == 1);
	bud_free_string(html);
	root = nd_ui_render(&st);
	html = bud_render_html(root);
	assert(html != NULL);
	assert(strstr(html, "chat-minimized") != NULL);
	assert(strstr(html, "id=\"restore-win-chat\"") != NULL);
	assert(strstr(html, "id=\"term\"") != NULL);
	nd_ui_set_chat_minimized(0);

	/* Test Room Contents round avatar, Popper-style per-item dropdown menu, and unified #hud-win-map Look-At swap */
	st.entity_count = 1;
	st.entities[0].ref = 77;
	st.entities[0].location = st.room.ref;
	st.entities[0].ch = '!';
	st.entities[0].fg = 10;
	strncpy(st.entities[0].name, "Emerald Potion", sizeof(st.entities[0].name) - 1);
	strncpy(st.entities[0].art, "items/potion.png", sizeof(st.entities[0].art) - 1);
	st.selected_item_ref = 77;
	st.target_ref = 77;
	bud_free_string(html);
	root = nd_ui_render(&st);
	html = bud_render_html(root);
	assert(html != NULL);
	assert(strstr(html, "<nd-avatar><span class=\"nd-glyph cf10\">!</span><img src=\"/nd/art/items/potion.png\"") != NULL);
	assert(strstr(html, "nd-item-popup nd-item-popper") != NULL);
	assert(strstr(html, "nd-popper-item") != NULL);
	assert(strstr(html, "👁️ Look") != NULL);
	assert(strstr(html, "🖐 Get") != NULL);
	assert(strstr(html, "hud-win-map-target") != NULL);
	assert(strstr(html, "👁️ Look-At") != NULL);
	assert(strstr(html, "id=\"target-card\"") != NULL);
	st.selected_item_ref = 0;
	st.target_ref = 0;
	st.entity_count = 0;

	/* Must contain room contents, top-center room header, and bottom-right actions dock */
	assert(strstr(html, "id=\"contents\"") != NULL);
	assert(strstr(html, "id=\"room-header\"") != NULL);
	assert(strstr(html, "seven-room-header") != NULL);
	assert(strstr(html, "id=\"bottom-right-actions\"") != NULL);

	bud_free_string(html);
	printf("PASS: Seven mode cleanly renders game canvas, RPG chat terminal, and HUD elements\n");

	/* 6. Standard layout mode still renders terminal and right-panel */
	st.layout_mode = ND_LAYOUT_STANDARD_HYBRID;
	strncpy(st.preferred_driver, "tty", sizeof(st.preferred_driver) - 1);
	root = nd_ui_render(&st);
	html = bud_render_html(root);
	assert(html != NULL);
	assert(strstr(html, "id=\"term\"") != NULL);
	assert(strstr(html, "id=\"right-panel\"") != NULL);
	assert(strstr(html, "id=\"directions\"") != NULL);
	assert(strstr(html, "data-bud-action=\"cycle-viewport\"") == NULL);
	assert(strstr(html, "Switch Viewport") == NULL);
	bud_free_string(html);
	printf("PASS: ND_LAYOUT_STANDARD_HYBRID renders standard terminal and controls\n");

	/* Test viewport cycling handler */
	int on_cycle_viewport(bud_event *e);
	extern nd_client_state_t client_state;
	extern int g_active_driver_idx;
	strncpy(client_state.preferred_driver, "tty", sizeof(client_state.preferred_driver) - 1);
	g_active_driver_idx = 0;
	on_cycle_viewport(NULL);
	assert(strcmp(client_state.preferred_driver, "lamina-seven") == 0);
	assert(client_state.layout_mode == ND_LAYOUT_GAME_2D);
	on_cycle_viewport(NULL);
	assert(strcmp(client_state.preferred_driver, "tty") == 0);
	assert(client_state.layout_mode == ND_LAYOUT_STANDARD_HYBRID);
	printf("PASS: on_cycle_viewport cycles smoothly between tty and lamina-seven drivers\n");

	/* 7. Driver transition handshake and input reset (R2, R3) */
	void wasm_tick(float dt, int width, int height);

	/* Set client to non-seven mode */
	strncpy(client_state.preferred_driver, "tty", sizeof(client_state.preferred_driver) - 1);
	wasm_tick(0.016f, 640, 480);

	/* Simulate key press held */
	nd_seven_key("d", 1);

	/* Transition into Seven stage */
	strncpy(client_state.preferred_driver, "lamina-seven", sizeof(client_state.preferred_driver) - 1);
	wasm_tick(0.016f, 640, 480);

	/* The driver transition handshake must have reset the held keys so lamb does not auto-walk */
	assert(nd_seven_get_walking() == 0);

	/* Transition back to tty and verify reset */
	nd_seven_key("d", 1);
	strncpy(client_state.preferred_driver, "tty", sizeof(client_state.preferred_driver) - 1);
	wasm_tick(0.016f, 640, 480);
	strncpy(client_state.preferred_driver, "lamina-seven", sizeof(client_state.preferred_driver) - 1);
	wasm_tick(0.016f, 640, 480);
	assert(nd_seven_get_walking() == 0);
	printf("PASS: driver transition handshake into seven stage resets button and key input state\n");

	printf("ALL VIEWPORT REGISTRY & CLEAN UI TESTS PASSED\n");
	return 0;
}
