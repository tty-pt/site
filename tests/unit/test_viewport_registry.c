#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <bud/bud.h>
#include "nd/bcp-types.h"
#include "nd/region-types.h"
#include "nd_viewport.h"
#include "nd_seven_motion.h"
#include "nd_seven.h"
#include "nd_seven_gl.h"
#include "nd_ui.h"

static bud_node *mock_stage_mount(void)
{
	bud_node *cv = bud_element("canvas");
	bud_set_attr(cv, "id", "test-canvas");
	return cv;
}

static bud_node *find_by_attr(bud_node *n, const char *attr, const char *val)
{
	if (!n) return NULL;
	const char *v = bud_get_attr(n, attr);
	if (v && strcmp(v, val) == 0)
		return n;
	size_t cnt = bud_node_child_count(n);
	for (size_t i = 0; i < cnt; i++) {
		bud_node *found = find_by_attr((bud_node *)bud_node_child(n, i), attr, val);
		if (found) return found;
	}
	return NULL;
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
	/* Verify corrupted undersized persist values (e.g. 6px width or 24px toolbar height) are clamped to window limits */
	nd_ui_restore_persist("nd-win-contents", "left:40px;top:4px;width:6px;height:24px;|hud-win hud-win-contents");
	nd_ui_get_win_rect(ND_WIN_CONTENTS, &cx, &cy, &cw, &ch);
	assert(cx == 40 && cy == 24 && cw == 200 && ch == 120);
	nd_ui_restore_persist("nd-win-contents", "left:40px;top:50px;right:auto;bottom:auto;transform:none;width:300px;height:180px;|hud-win hud-win-contents");
	bud_free_string(html);
	root = nd_ui_render(&st);
	html = bud_render_html(root);
	assert(html != NULL);
	assert(strstr(html, "style=\"left:40px;top:50px;right:auto;bottom:auto;transform:none;width:300px;height:180px;\"") != NULL);

	/* Test live event dispatch for right-border resize (#contents-resize-e), bottom-right corner resize (#contents-resize-se), and titlebar drag (#contents-toolbar) */
	{
		bud_runtime *rt = bud_runtime_new(root);
		assert(rt != NULL);
		bud_node *resize_e = find_by_attr(root, "id", "contents-resize-e");
		bud_node *resize_se = find_by_attr(root, "id", "contents-resize-se");
		bud_node *toolbar = find_by_attr(root, "id", "contents-toolbar");
		assert(resize_e != NULL && resize_se != NULL && toolbar != NULL);
		/* Child resize handles and toolbars must NOT carry data-win-id so closest('[data-win-id]') never matches a handle/toolbar */
		assert(bud_get_attr(resize_e, "data-win-id") == NULL);
		assert(bud_get_attr(resize_se, "data-win-id") == NULL);
		assert(bud_get_attr(toolbar, "data-win-id") == NULL);

		/* 1. Mousedown on right border (#contents-resize-e) with window rect (768, 256, 300, 240): window must NOT teleport to mouse X (1068) */
		bud_runtime_dispatch(rt, resize_e, "mousedown", "1068,350,1080,720,768,256,300,240");
		nd_ui_get_win_rect(ND_WIN_CONTENTS, &cx, &cy, &cw, &ch);
		assert(cx == 768 && cy == 256 && cw == 300 && ch == 240);
		bud_runtime_dispatch(rt, root, "mousemove@window", "1118,350,1080,720,-1,-1,0,0");
		nd_ui_get_win_rect(ND_WIN_CONTENTS, &cx, &cy, &cw, &ch);
		assert(cx == 768 && cy == 256 && cw == 350 && ch == 240);
		bud_runtime_dispatch(rt, root, "mouseup@window", "1118,350,1080,720,-1,-1,0,0");

		/* 2. Mousedown on bottom-right corner (#contents-resize-se): window must NOT teleport to corner */
		bud_runtime_dispatch(rt, resize_se, "mousedown", "1118,496,1080,720,768,256,350,240");
		nd_ui_get_win_rect(ND_WIN_CONTENTS, &cx, &cy, &cw, &ch);
		assert(cx == 768 && cy == 256 && cw == 350 && ch == 240);
		bud_runtime_dispatch(rt, root, "mousemove@window", "1148,536,1080,720,-1,-1,0,0");
		nd_ui_get_win_rect(ND_WIN_CONTENTS, &cx, &cy, &cw, &ch);
		assert(cx == 768 && cy == 256 && cw == 380 && ch == 280);
		bud_runtime_dispatch(rt, root, "mouseup@window", "1148,536,1080,720,-1,-1,0,0");

		/* 3. Mousedown on titlebar (#contents-toolbar): window must lock exact window rect (768, 256, 380, 280) with zero jump */
		bud_runtime_dispatch(rt, toolbar, "mousedown", "900,244,1080,720,768,256,380,280");
		nd_ui_get_win_rect(ND_WIN_CONTENTS, &cx, &cy, &cw, &ch);
		assert(cx == 768 && cy == 256 && cw == 380 && ch == 280);
		bud_runtime_dispatch(rt, root, "mousemove@window", "800,264,1080,720,-1,-1,0,0");
		nd_ui_get_win_rect(ND_WIN_CONTENTS, &cx, &cy, &cw, &ch);
		assert(cx == 668 && cy == 276 && cw == 380 && ch == 280);
		bud_runtime_dispatch(rt, root, "mouseup@window", "800,264,1080,720,-1,-1,0,0");

		free(rt);
	}

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
	assert(strstr(html, "id=\"contents-item-popper\"") != NULL);
	assert(strstr(html, "nd-item-popup nd-item-popper") != NULL);
	assert(strstr(html, "</nd-contents></div><div id=\"contents-item-popper\"") != NULL);
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

	/* 8. Comprehensive Settings Modal End-to-End Application & Persistence Tests */
	{
		strncpy(client_state.preferred_driver, "lamina-seven", sizeof(client_state.preferred_driver) - 1);
		client_state.layout_mode = ND_LAYOUT_GAME_2D;
		nd_seven_set_zoom(1.0f);
		nd_ui_set_active_modal("");

		/* 8a. Open Settings Modal via clicking #dock-settings-btn in the bottom-right dock */
		root = nd_ui_render(&client_state);
		bud_runtime *rt = bud_runtime_new(root);
		assert(rt != NULL);
		bud_node *modal_overlay = find_by_attr(root, "id", "settings-modal");
		assert(modal_overlay != NULL);
		assert(strcmp(bud_get_attr(modal_overlay, "class"), "hidden") == 0);

		bud_node *dock_settings_btn = find_by_attr(root, "id", "dock-settings-btn");
		assert(dock_settings_btn != NULL);
		bud_runtime_dispatch(rt, dock_settings_btn, "click", "");
		assert(strcmp(nd_ui_get_active_modal(), "settings") == 0);
		free(rt);

		root = nd_ui_render(&client_state);
		modal_overlay = find_by_attr(root, "id", "settings-modal");
		assert(modal_overlay != NULL);
		assert(strstr(bud_get_attr(modal_overlay, "class"), "modal") != NULL);

		/* Verify initial Medium (Default) state and persisted style attribute */
		assert(nd_seven_get_quality_preset() == ND_QUALITY_MEDIUM);
		assert((int)(nd_seven_get_res_scale() * 100.0f + 0.5f) == 75);
		assert((int)(nd_seven_get_detail_scale() * 100.0f + 0.5f) == 75);
		assert(nd_seven_get_cloud_quality() == ND_CLOUD_QUALITY_MEDIUM);
		assert(nd_seven_get_msaa() == 0);
		assert(nd_seven_get_fps_cap() == 60);
		bud_node *settings_card = find_by_attr(root, "id", "settings");
		assert(settings_card != NULL);
		const char *init_style = bud_get_attr(settings_card, "style");
		assert(init_style != NULL);
		assert(strcmp(init_style, "--nd-res:75;--nd-det:75;--nd-cloud:2;--nd-msaa:0;--nd-fps:60;--nd-zoom:100;") == 0);
		html = bud_render_html(root);
		assert(strstr(html, "⚖️ Medium (Default)") != NULL);
		bud_free_string(html);

		/* 8b. Graphics Quality Preset cycling: Medium -> High -> Low -> Medium -> High */
		rt = bud_runtime_new(root);
		bud_node *preset_btn = find_by_attr(root, "id", "settings-preset-toggle");
		assert(preset_btn != NULL);
		bud_runtime_dispatch(rt, preset_btn, "click", "");
		assert(nd_seven_get_quality_preset() == ND_QUALITY_HIGH);
		assert((int)(nd_seven_get_res_scale() * 100.0f + 0.5f) == 100);
		assert((int)(nd_seven_get_detail_scale() * 100.0f + 0.5f) == 100);
		assert(nd_seven_get_cloud_quality() == ND_CLOUD_QUALITY_HIGH);
		assert(nd_seven_get_msaa() == 1);
		assert(nd_seven_get_fps_cap() == 0);
		free(rt);

		root = nd_ui_render(&client_state);
		html = bud_render_html(root);
		assert(strstr(html, "✨ High") != NULL);
		assert(strstr(html, "--nd-res:100;--nd-det:100;--nd-cloud:3;--nd-msaa:1;--nd-fps:0;--nd-zoom:100;") != NULL);
		bud_free_string(html);

		rt = bud_runtime_new(root);
		preset_btn = find_by_attr(root, "id", "settings-preset-toggle");
		bud_runtime_dispatch(rt, preset_btn, "click", "");
		assert(nd_seven_get_quality_preset() == ND_QUALITY_LOW);
		assert((int)(nd_seven_get_res_scale() * 100.0f + 0.5f) == 67);
		assert((int)(nd_seven_get_detail_scale() * 100.0f + 0.5f) == 72);
		assert(nd_seven_get_cloud_quality() == ND_CLOUD_QUALITY_LOW);
		assert(nd_seven_get_msaa() == 0);
		assert(nd_seven_get_fps_cap() == 30);
		free(rt);

		root = nd_ui_render(&client_state);
		html = bud_render_html(root);
		assert(strstr(html, "🔋 Low (Fastest)") != NULL);
		assert(strstr(html, "--nd-res:67;--nd-det:72;--nd-cloud:1;--nd-msaa:0;--nd-fps:30;--nd-zoom:100;") != NULL);
		bud_free_string(html);

		rt = bud_runtime_new(root);
		preset_btn = find_by_attr(root, "id", "settings-preset-toggle");
		bud_runtime_dispatch(rt, preset_btn, "click", "");
		assert(nd_seven_get_quality_preset() == ND_QUALITY_MEDIUM);
		assert((int)(nd_seven_get_res_scale() * 100.0f + 0.5f) == 75);
		assert((int)(nd_seven_get_detail_scale() * 100.0f + 0.5f) == 75);
		assert(nd_seven_get_cloud_quality() == ND_CLOUD_QUALITY_MEDIUM);
		assert(nd_seven_get_msaa() == 0);
		assert(nd_seven_get_fps_cap() == 60);
		free(rt);

		root = nd_ui_render(&client_state);
		html = bud_render_html(root);
		assert(strstr(html, "⚖️ Medium (Default)") != NULL);
		assert(strstr(html, "--nd-res:75;--nd-det:75;--nd-cloud:2;--nd-msaa:0;--nd-fps:60;--nd-zoom:100;") != NULL);
		bud_free_string(html);

		rt = bud_runtime_new(root);
		preset_btn = find_by_attr(root, "id", "settings-preset-toggle");
		bud_runtime_dispatch(rt, preset_btn, "click", "");
		assert(nd_seven_get_quality_preset() == ND_QUALITY_HIGH);
		assert((int)(nd_seven_get_res_scale() * 100.0f + 0.5f) == 100);
		assert((int)(nd_seven_get_detail_scale() * 100.0f + 0.5f) == 100);
		assert(nd_seven_get_cloud_quality() == ND_CLOUD_QUALITY_HIGH);
		assert(nd_seven_get_msaa() == 1);
		assert(nd_seven_get_fps_cap() == 0);
		free(rt);

		root = nd_ui_render(&client_state);
		html = bud_render_html(root);
		assert(strstr(html, "✨ High") != NULL);
		assert(strstr(html, "--nd-res:100;--nd-det:100;--nd-cloud:3;--nd-msaa:1;--nd-fps:0;--nd-zoom:100;") != NULL);
		bud_free_string(html);

		/* 8c. Render Resolution Scale: slider input/change, - (#settings-res-out), + (#settings-res-in) */
		rt = bud_runtime_new(root);
		bud_node *res_slider = find_by_attr(root, "id", "settings-res-slider");
		bud_node *res_out = find_by_attr(root, "id", "settings-res-out");
		bud_node *res_in = find_by_attr(root, "id", "settings-res-in");
		assert(res_slider != NULL && res_out != NULL && res_in != NULL);

		bud_runtime_dispatch(rt, res_slider, "input", "60");
		assert((int)(nd_seven_get_res_scale() * 100.0f + 0.5f) == 60);
		assert(nd_seven_get_quality_preset() == ND_QUALITY_CUSTOM);

		bud_runtime_dispatch(rt, res_slider, "change", "30");
		assert((int)(nd_seven_get_res_scale() * 100.0f + 0.5f) == 30);

		bud_runtime_dispatch(rt, res_out, "click", "");
		assert((int)(nd_seven_get_res_scale() * 100.0f + 0.5f) == 25);

		/* Verify lower bound clamp at 25% */
		bud_runtime_dispatch(rt, res_out, "click", "");
		assert((int)(nd_seven_get_res_scale() * 100.0f + 0.5f) == 25);

		bud_runtime_dispatch(rt, res_in, "click", "");
		assert((int)(nd_seven_get_res_scale() * 100.0f + 0.5f) == 30);
		bud_runtime_dispatch(rt, res_in, "click", "");
		assert((int)(nd_seven_get_res_scale() * 100.0f + 0.5f) == 35);
		free(rt);

		root = nd_ui_render(&client_state);
		html = bud_render_html(root);
		assert(strstr(html, "id=\"settings-res-value\" class=\"settings-badge\">35%</span>") != NULL);
		assert(strstr(html, "🎛️ Custom") != NULL);
		bud_free_string(html);

		/* 8d. Scenery Detail: slider input/change, - (#settings-detail-out), + (#settings-detail-in), and keyboard ',' / '.' */
		rt = bud_runtime_new(root);
		bud_node *det_slider = find_by_attr(root, "id", "settings-detail-slider");
		bud_node *det_out = find_by_attr(root, "id", "settings-detail-out");
		bud_node *det_in = find_by_attr(root, "id", "settings-detail-in");
		assert(det_slider != NULL && det_out != NULL && det_in != NULL);

		bud_runtime_dispatch(rt, det_slider, "input", "80");
		assert((int)(nd_seven_get_detail_scale() * 100.0f + 0.5f) == 80);

		bud_runtime_dispatch(rt, det_slider, "change", "100");
		assert((int)(nd_seven_get_detail_scale() * 100.0f + 0.5f) == 100);

		bud_runtime_dispatch(rt, det_out, "click", "");
		int det_after_out = (int)(nd_seven_get_detail_scale() * 100.0f + 0.5f);
		assert(det_after_out < 100);

		bud_runtime_dispatch(rt, det_in, "click", "");
		assert((int)(nd_seven_get_detail_scale() * 100.0f + 0.5f) == 100);

		/* Keyboard shortcuts ',' and '.' */
		bud_runtime_dispatch(rt, root, "keydown@window", ",");
		assert((int)(nd_seven_get_detail_scale() * 100.0f + 0.5f) == det_after_out);
		bud_runtime_dispatch(rt, root, "keydown@window", ".");
		assert((int)(nd_seven_get_detail_scale() * 100.0f + 0.5f) == 100);
		free(rt);

		/* 8e. Clouds & Shadows Quality cycling: High (3) -> Off (0) -> Low (1) -> Medium (2) -> High (3) */
		root = nd_ui_render(&client_state);
		rt = bud_runtime_new(root);
		bud_node *cloud_btn = find_by_attr(root, "id", "settings-cloud-toggle");
		assert(cloud_btn != NULL);
		assert(nd_seven_get_cloud_quality() == ND_CLOUD_QUALITY_HIGH);

		bud_runtime_dispatch(rt, cloud_btn, "click", "");
		assert(nd_seven_get_cloud_quality() == ND_CLOUD_QUALITY_OFF);
		free(rt);
		root = nd_ui_render(&client_state);
		html = bud_render_html(root);
		assert(strstr(html, "🚫 Off") != NULL);
		bud_free_string(html);

		rt = bud_runtime_new(root);
		cloud_btn = find_by_attr(root, "id", "settings-cloud-toggle");
		bud_runtime_dispatch(rt, cloud_btn, "click", "");
		assert(nd_seven_get_cloud_quality() == ND_CLOUD_QUALITY_LOW);
		free(rt);
		root = nd_ui_render(&client_state);
		html = bud_render_html(root);
		assert(strstr(html, "💨 Low (1/4 Res, No CSM)") != NULL);
		bud_free_string(html);

		rt = bud_runtime_new(root);
		cloud_btn = find_by_attr(root, "id", "settings-cloud-toggle");
		bud_runtime_dispatch(rt, cloud_btn, "click", "");
		assert(nd_seven_get_cloud_quality() == ND_CLOUD_QUALITY_MEDIUM);
		free(rt);
		root = nd_ui_render(&client_state);
		html = bud_render_html(root);
		assert(strstr(html, "⛅ Medium (1/3 Res + CSM)") != NULL);
		bud_free_string(html);

		rt = bud_runtime_new(root);
		cloud_btn = find_by_attr(root, "id", "settings-cloud-toggle");
		bud_runtime_dispatch(rt, cloud_btn, "click", "");
		assert(nd_seven_get_cloud_quality() == ND_CLOUD_QUALITY_HIGH);
		free(rt);
		root = nd_ui_render(&client_state);
		html = bud_render_html(root);
		assert(strstr(html, "☁️ High (1/2 Res + CSM)") != NULL);
		bud_free_string(html);

		/* 8f. Anti-Aliasing (4x MSAA) toggle: On (1) -> Off (0) -> On (1) */
		rt = bud_runtime_new(root);
		bud_node *msaa_btn = find_by_attr(root, "id", "settings-msaa-toggle");
		assert(msaa_btn != NULL);
		assert(nd_seven_get_msaa() == 1);

		bud_runtime_dispatch(rt, msaa_btn, "click", "");
		assert(nd_seven_get_msaa() == 0);
		free(rt);
		root = nd_ui_render(&client_state);
		html = bud_render_html(root);
		assert(strstr(html, "Off (1x)") != NULL);
		bud_free_string(html);

		rt = bud_runtime_new(root);
		msaa_btn = find_by_attr(root, "id", "settings-msaa-toggle");
		bud_runtime_dispatch(rt, msaa_btn, "click", "");
		assert(nd_seven_get_msaa() == 1);
		free(rt);
		root = nd_ui_render(&client_state);
		html = bud_render_html(root);
		assert(strstr(html, "4x MSAA (On)") != NULL);
		bud_free_string(html);

		/* 8g. Frame Rate Cap cycling: Uncapped (0) -> 30 FPS -> 60 FPS -> Uncapped (0) + wasm_tick pacing verification */
		rt = bud_runtime_new(root);
		bud_node *fps_btn = find_by_attr(root, "id", "settings-fps-toggle");
		assert(fps_btn != NULL);
		assert(nd_seven_get_fps_cap() == 0);

		/* Under 30 FPS cap (min_dt = 0.031s), a 12ms tick after a redraw is skipped until accumulated dt >= 31ms */
		bud_runtime_dispatch(rt, fps_btn, "click", "");
		assert(nd_seven_get_fps_cap() == 30);
		{
			extern int g_map_dirty;
			g_map_dirty = 0;
			wasm_tick(0.035f, 640, 480); /* flushes accum */
			uint32_t fc0 = nd_seven_gl_get_frame_count();
			wasm_tick(0.012f, 640, 480); /* 12ms < 31ms: skipped */
			assert(nd_seven_gl_get_frame_count() == fc0);
			wasm_tick(0.020f, 640, 480); /* 12+20 = 32ms >= 31ms: renders */
			assert(nd_seven_gl_get_frame_count() == fc0 + 1);
		}
		free(rt);
		root = nd_ui_render(&client_state);
		html = bud_render_html(root);
		assert(strstr(html, "30 FPS (Battery)") != NULL);
		bud_free_string(html);

		/* Under 60 FPS cap (min_dt = 0.0155s), a 8ms tick is skipped until accumulated dt >= 15.5ms */
		rt = bud_runtime_new(root);
		fps_btn = find_by_attr(root, "id", "settings-fps-toggle");
		bud_runtime_dispatch(rt, fps_btn, "click", "");
		assert(nd_seven_get_fps_cap() == 60);
		{
			extern int g_map_dirty;
			g_map_dirty = 0;
			wasm_tick(0.020f, 640, 480); /* flushes accum */
			uint32_t fc0 = nd_seven_gl_get_frame_count();
			wasm_tick(0.008f, 640, 480); /* 8ms < 15.5ms: skipped */
			assert(nd_seven_gl_get_frame_count() == fc0);
			wasm_tick(0.008f, 640, 480); /* 8+8 = 16ms >= 15.5ms: renders */
			assert(nd_seven_gl_get_frame_count() == fc0 + 1);
		}
		free(rt);
		root = nd_ui_render(&client_state);
		html = bud_render_html(root);
		assert(strstr(html, "60 FPS") != NULL);
		bud_free_string(html);

		/* Under Uncapped (0), every tick renders immediately even at 4ms (240 Hz) */
		rt = bud_runtime_new(root);
		fps_btn = find_by_attr(root, "id", "settings-fps-toggle");
		bud_runtime_dispatch(rt, fps_btn, "click", "");
		assert(nd_seven_get_fps_cap() == 0);
		{
			uint32_t fc0 = nd_seven_gl_get_frame_count();
			wasm_tick(0.004f, 640, 480);
			assert(nd_seven_gl_get_frame_count() == fc0 + 1);
			wasm_tick(0.004f, 640, 480);
			assert(nd_seven_gl_get_frame_count() == fc0 + 2);
		}
		free(rt);
		root = nd_ui_render(&client_state);
		html = bud_render_html(root);
		assert(strstr(html, "Uncapped (VSync)") != NULL);
		bud_free_string(html);

		/* 8h. Camera Zoom: slider input/change, - (#settings-zoom-out), + (#settings-zoom-in), 100% (#settings-zoom-reset), and keyboard '-', '+', '0' */
		rt = bud_runtime_new(root);
		bud_node *zoom_slider = find_by_attr(root, "id", "settings-zoom-slider");
		bud_node *zoom_out = find_by_attr(root, "id", "settings-zoom-out");
		bud_node *zoom_in = find_by_attr(root, "id", "settings-zoom-in");
		bud_node *zoom_reset = find_by_attr(root, "id", "settings-zoom-reset");
		assert(zoom_slider != NULL && zoom_out != NULL && zoom_in != NULL && zoom_reset != NULL);

		bud_runtime_dispatch(rt, zoom_slider, "input", "150");
		assert((int)(nd_seven_get_zoom() * 100.0f + 0.5f) == 150);

		bud_runtime_dispatch(rt, zoom_slider, "change", "200");
		assert((int)(nd_seven_get_zoom() * 100.0f + 0.5f) == 200);

		bud_runtime_dispatch(rt, zoom_out, "click", "");
		int zoom_after_out = (int)(nd_seven_get_zoom() * 100.0f + 0.5f);
		assert(zoom_after_out < 200);

		bud_runtime_dispatch(rt, zoom_in, "click", "");
		assert((int)(nd_seven_get_zoom() * 100.0f + 0.5f) == 200);

		bud_runtime_dispatch(rt, zoom_reset, "click", "");
		assert((int)(nd_seven_get_zoom() * 100.0f + 0.5f) == 100);

		/* Keyboard shortcuts '-', '+', '0' */
		bud_runtime_dispatch(rt, root, "keydown@window", "+");
		assert((int)(nd_seven_get_zoom() * 100.0f + 0.5f) == 115);
		bud_runtime_dispatch(rt, root, "keydown@window", "-");
		assert((int)(nd_seven_get_zoom() * 100.0f + 0.5f) == 100);
		bud_runtime_dispatch(rt, root, "keydown@window", "+");
		assert((int)(nd_seven_get_zoom() * 100.0f + 0.5f) == 115);
		bud_runtime_dispatch(rt, root, "keydown@window", "0");
		assert((int)(nd_seven_get_zoom() * 100.0f + 0.5f) == 100);
		free(rt);

		/* 8i. 3D Viewport Toggle (#settings-viewport-toggle) */
		root = nd_ui_render(&client_state);
		rt = bud_runtime_new(root);
		bud_node *vp_tog = find_by_attr(root, "id", "settings-viewport-toggle");
		assert(vp_tog != NULL);
		bud_runtime_dispatch(rt, vp_tog, "click", "");
		assert(strcmp(client_state.preferred_driver, "tty") == 0);
		assert(client_state.layout_mode == ND_LAYOUT_STANDARD_HYBRID);
		free(rt);

		root = nd_ui_render(&client_state);
		html = bud_render_html(root);
		assert(strstr(html, "📺 Terminal (Off)") != NULL);
		bud_free_string(html);

		/* 8j. Persistence restore-persist in Terminal Mode AND Seven 3D Mode */
		rt = bud_runtime_new(root);
		bud_runtime_dispatch(rt, root, "restore-persist",
		                     "nd-settings-quality|--nd-res:65;--nd-det:120;--nd-cloud:2;--nd-msaa:0;--nd-fps:60;--nd-zoom:140;|rpg-modal-card");
		assert((int)(nd_seven_get_res_scale() * 100.0f + 0.5f) == 65);
		assert((int)(nd_seven_get_detail_scale() * 100.0f + 0.5f) == 120);
		assert(nd_seven_get_cloud_quality() == ND_CLOUD_QUALITY_MEDIUM);
		assert(nd_seven_get_msaa() == 0);
		assert(nd_seven_get_fps_cap() == 60);
		assert((int)(nd_seven_get_zoom() * 100.0f + 0.5f) == 140);
		assert(nd_seven_get_quality_preset() == ND_QUALITY_CUSTOM);

		/* Switch back to Seven 3D via #settings-viewport-toggle */
		vp_tog = find_by_attr(root, "id", "settings-viewport-toggle");
		assert(vp_tog != NULL);
		bud_runtime_dispatch(rt, vp_tog, "click", "");
		assert(strcmp(client_state.preferred_driver, "lamina-seven") == 0);
		assert(client_state.layout_mode == ND_LAYOUT_GAME_2D);
		free(rt);

		/* Verify restore-persist in Seven 3D mode back to High (Default) */
		root = nd_ui_render(&client_state);
		rt = bud_runtime_new(root);
		bud_runtime_dispatch(rt, root, "restore-persist",
		                     "nd-settings-quality|--nd-res:100;--nd-det:100;--nd-cloud:3;--nd-msaa:1;--nd-fps:0;--nd-zoom:100;|rpg-modal-card");
		assert(nd_seven_get_quality_preset() == ND_QUALITY_HIGH);
		assert((int)(nd_seven_get_zoom() * 100.0f + 0.5f) == 100);

		/* 8k. Close Settings Modal via #settings-close-btn */
		bud_node *close_btn = find_by_attr(root, "id", "settings-close-btn");
		assert(close_btn != NULL);
		bud_runtime_dispatch(rt, close_btn, "click", "");
		assert(strcmp(nd_ui_get_active_modal(), "") == 0);
		free(rt);

		root = nd_ui_render(&client_state);
		modal_overlay = find_by_attr(root, "id", "settings-modal");
		assert(strcmp(bud_get_attr(modal_overlay, "class"), "hidden") == 0);

		/* 8l. Regression test: Open Settings, drop down settings (res slider input+change, detail slider input+change, cloud quality), and click Anti-Aliasing button (#settings-msaa-toggle) during active 3D rendering */
		rt = bud_runtime_new(root);
		dock_settings_btn = find_by_attr(root, "id", "dock-settings-btn");
		assert(dock_settings_btn != NULL);
		bud_runtime_dispatch(rt, dock_settings_btn, "click", "");
		assert(strcmp(nd_ui_get_active_modal(), "settings") == 0);
		free(rt);

		wasm_tick(0.016f, 640, 480);
		uint32_t st_w = 0, st_h = 0, sc_w = 0, sc_h = 0, cl_w = 0, cl_h = 0;
		int sc_samples = -1;
		bool csm_act = false;
		nd_seven_gl_get_target_info(&st_w, &st_h, &sc_w, &sc_h, &sc_samples, &cl_w, &cl_h, &csm_act);
		assert(sc_samples == 4);

		root = nd_ui_render(&client_state);
		rt = bud_runtime_new(root);
		res_slider = find_by_attr(root, "id", "settings-res-slider");
		det_slider = find_by_attr(root, "id", "settings-detail-slider");
		msaa_btn = find_by_attr(root, "id", "settings-msaa-toggle");
		assert(res_slider != NULL && det_slider != NULL && msaa_btn != NULL);

		/* Drop down resolution to 65%: input followed by change with identical value must not trigger a duplicate 3D stage redraw */
		bud_runtime_dispatch(rt, res_slider, "input", "65");
		uint32_t fc_after_res_input = nd_seven_gl_get_frame_count();
		bud_runtime_dispatch(rt, res_slider, "change", "65");
		assert(nd_seven_gl_get_frame_count() == fc_after_res_input);

		/* Drop down detail to 70%: input followed by change with identical value must not trigger a duplicate 3D stage redraw */
		bud_runtime_dispatch(rt, det_slider, "input", "70");
		uint32_t fc_after_det_input = nd_seven_gl_get_frame_count();
		bud_runtime_dispatch(rt, det_slider, "change", "70");
		assert(nd_seven_gl_get_frame_count() == fc_after_det_input);

		/* Click Anti-Aliasing button (#settings-msaa-toggle): must cleanly toggle 4x MSAA -> 1x Off in a single pass and reallocate FBO targets */
		bud_runtime_dispatch(rt, msaa_btn, "click", "");
		assert(nd_seven_get_msaa() == 0);
		nd_seven_gl_get_target_info(&st_w, &st_h, &sc_w, &sc_h, &sc_samples, &cl_w, &cl_h, &csm_act);
		assert(sc_samples == 0);

		/* Click Anti-Aliasing button again: must cleanly toggle 1x Off -> 4x MSAA */
		bud_runtime_dispatch(rt, msaa_btn, "click", "");
		assert(nd_seven_get_msaa() == 1);
		nd_seven_gl_get_target_info(&st_w, &st_h, &sc_w, &sc_h, &sc_samples, &cl_w, &cl_h, &csm_act);
		assert(sc_samples == 4);
		free(rt);

		nd_seven_apply_quality_preset(ND_QUALITY_HIGH);
		nd_ui_set_active_modal("");
		printf("PASS: all Settings modal controls, presets, sliders, buttons, keyboard shortcuts, and persistence verified\n");
	}

	printf("ALL VIEWPORT REGISTRY & CLEAN UI TESTS PASSED\n");
	return 0;
}
