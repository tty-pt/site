/*
 * auren_fe.c — Pure C isomorphic WebAssembly frontend for in-browser Auren demo.
 * Renders VDOM and reacts to local WebGPU inference / token events.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "bud/bud.h"
#include "bud/bud_app.h"
#include <ttypt/stele_wasm.h>
#include "../../common/site_wasm_app.h"

#include "auren_ui.h"
#include "../auren-types.h"

static auren_demo_state_t g_state = {0};

/* JSON boolean field: true/false/null, or a number (non-zero = true). */
static int json_bool(const char *json, size_t len, const char *key, int def)
{
	char v[16];
	bud_json_str_len(json, len, key, v, sizeof(v));
	if (!v[0])
		return def;
	if (strcmp(v, "true") == 0)
		return 1;
	if (strcmp(v, "false") == 0 || strcmp(v, "null") == 0)
		return 0;
	return atoi(v) != 0;
}

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
		stl_invalidate();
	} else if (action_id == 2 && data) {
		/* Token chunk */
		size_t cur = strlen(g_state.current_response);
		size_t dlen = strlen(data);
		if (cur + dlen + 1 < sizeof(g_state.current_response)) {
			memcpy(g_state.current_response + cur, data, dlen);
			g_state.current_response[cur + dlen] = '\0';
			stl_invalidate();
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
			stl_invalidate();
		}
	} else if (action_id == 4 && data) {
		/* Progress */
		g_state.percent = atoi(data);
		if (g_state.percent >= 100) g_state.cached = 1;
		stl_invalidate();
	} else if (action_id == 5 && data) {
		/* WebGPU availability flag */
		g_state.webgpu_available = atoi(data);
		stl_invalidate();
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
		/* bud's jsmn-backed helpers: json-c is not linked into WASM. */
		g_state.cached = json_bool(json, (size_t)len, "cached", g_state.cached);
		g_state.webgpu_available = json_bool(json, (size_t)len, "webgpu", g_state.webgpu_available);
	}
	return 0;
}

static bud_node *auren_stl_render(void)
{
	return auren_ui_render_view(&g_state);
}

/* BUD_WASM_MODULE_EXT emits bud_app_render and the wasm_init / wasm_action
 * entry points the stele client calls; STL_WASM_MODULE alone left them
 * undefined (bud_app_render became a host import stubbed to NULL). */
BUD_WASM_MODULE_EXT(auren, auren_stl_init, auren_stl_render, auren_stl_action, NULL, NULL, NULL);
