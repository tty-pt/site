/*
 * auren_fe.c — Pure C isomorphic WebAssembly frontend for in-browser Auren demo.
 * Renders VDOM and reacts to local WebGPU inference / token events.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <json-c/json.h>

#include "bud/bud.h"
#include "bud/bud_app.h"
#include <ttypt/stele_wasm.h>
#include "../../common/site_wasm_app.h"

#include "auren_ui.h"
#include "../auren-types.h"

static auren_demo_state_t g_state = {0};

static void auren_stl_action(int action_id, const char *data)
{
	if (action_id == 1 && data) {
		/* User prompt */
		if (g_state.msg_count < AUREN_DEMO_MAX_MSGS) {
			auren_demo_msg_t *m = &g_state.msgs[g_state.msg_count++];
			snprintf(m->role, sizeof(m->role), "user");
			snprintf(m->content, sizeof(m->content), "%s", data);
		}
		g_state.streaming = 1;
		g_state.current_response[0] = '\0';
		stl_slot_invalidate(0);
	} else if (action_id == 2 && data) {
		/* Token chunk */
		size_t cur = strlen(g_state.current_response);
		size_t dlen = strlen(data);
		if (cur + dlen + 1 < sizeof(g_state.current_response)) {
			memcpy(g_state.current_response + cur, data, dlen);
			g_state.current_response[cur + dlen] = '\0';
			stl_slot_invalidate(0);
		}
	} else if (action_id == 3) {
		/* Done */
		if (g_state.streaming) {
			if (g_state.msg_count < AUREN_DEMO_MAX_MSGS) {
				auren_demo_msg_t *m = &g_state.msgs[g_state.msg_count++];
				snprintf(m->role, sizeof(m->role), "assistant");
				snprintf(m->content, sizeof(m->content), "%s", g_state.current_response);
			}
			g_state.streaming = 0;
			g_state.current_response[0] = '\0';
			stl_slot_invalidate(0);
		}
	} else if (action_id == 4 && data) {
		/* Progress */
		g_state.percent = atoi(data);
		if (g_state.percent >= 100) g_state.cached = 1;
		stl_slot_invalidate(0);
	} else if (action_id == 5 && data) {
		/* WebGPU availability flag */
		g_state.webgpu_available = atoi(data);
		stl_slot_invalidate(0);
	}
}

static int auren_stl_init(const char *json, int len)
{
	memset(&g_state, 0, sizeof(g_state));
	snprintf(g_state.model, sizeof(g_state.model), "smollm2-135m-instruct");
	snprintf(g_state.status, sizeof(g_state.status), "ready");
	g_state.webgpu_available = 1;
	g_state.cached = 0;
	g_state.percent = 100;
	g_state.streaming = 0;
	g_state.msg_count = 0;

	if (json && len > 0) {
		struct json_object *root = json_tokener_parse_ex(json_tokener_new(), json, len);
		if (root) {
			struct json_object *cached_obj = NULL;
			if (json_object_object_get_ex(root, "cached", &cached_obj)) {
				g_state.cached = json_object_get_boolean(cached_obj);
			}
			struct json_object *gpu_obj = NULL;
			if (json_object_object_get_ex(root, "webgpu", &gpu_obj)) {
				g_state.webgpu_available = json_object_get_boolean(gpu_obj);
			}
			json_object_put(root);
		}
	}
	return 0;
}

static bud_node *auren_stl_render(void)
{
	return auren_ui_render_view(&g_state);
}

STL_WASM_MODULE(auren, auren_stl_init, auren_stl_render, auren_stl_action);

ISO_EXPORT(void, wasm_action, int action_id, const char *data)
{
	auren_stl_action(action_id, data);
}
