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

	/* 5. Clean 2D UI Mode suppresses terminal/contents/actions/keypad */
	nd_client_state_t st;
	memset(&st, 0, sizeof(st));
	strncpy(st.user, "hero", sizeof(st.user) - 1);
	strncpy(st.preferred_driver, "lamina-seven", sizeof(st.preferred_driver) - 1);
	strncpy(st.preferred_map_driver, "lamina", sizeof(st.preferred_map_driver) - 1);
	st.layout_mode = ND_LAYOUT_GAME_2D;

	bud_node *root = nd_ui_render(&st);
	assert(root != NULL);
	char *html = bud_render_html(root);
	assert(html != NULL);

	/* Must contain custom stage canvas and room map */
	assert(strstr(html, "id=\"test-canvas\"") != NULL);
	assert(strstr(html, "id=\"room-map\"") != NULL);
	assert(strstr(html, "game-2d-mode") != NULL);

	/* Must NOT contain right-panel, directions keypad, action bar, or contents */
	assert(strstr(html, "id=\"right-panel\"") == NULL);
	assert(strstr(html, "id=\"directions\"") == NULL);
	assert(strstr(html, "id=\"g-actions\"") == NULL);
	assert(strstr(html, "id=\"contents\"") == NULL);
	assert(strstr(html, "id=\"term\"") == NULL);

	bud_free_string(html);
	printf("PASS: ND_LAYOUT_GAME_2D cleanly renders ONLY game canvas and minimap\n");

	/* 6. Standard layout mode still renders terminal and right-panel */
	st.layout_mode = ND_LAYOUT_STANDARD_HYBRID;
	strncpy(st.preferred_driver, "tty", sizeof(st.preferred_driver) - 1);
	root = nd_ui_render(&st);
	html = bud_render_html(root);
	assert(html != NULL);
	assert(strstr(html, "id=\"term\"") != NULL);
	assert(strstr(html, "id=\"right-panel\"") != NULL);
	assert(strstr(html, "id=\"directions\"") != NULL);
	bud_free_string(html);
	printf("PASS: ND_LAYOUT_STANDARD_HYBRID renders standard terminal and controls\n");

	/* 7. Driver transition handshake and input reset (R2, R3) */
	extern nd_client_state_t client_state;
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
