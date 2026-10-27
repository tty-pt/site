/*
 * chat_fe.c — WebAssembly frontend for real-time chat streaming
 * Handles WebSocket connection, message streaming, and isomorphic UI rendering.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "bud/bud.h"
#include "bud/bud_app.h"
#include "hyle-bud/hyle-bud.h"

#include "chat_ui.h"
#include "../chat-types.h"

/* Persistent chat state for WASM session */
static chat_session_t g_session = {0};
static chat_session_summary_t g_summaries[32] = {0};
static size_t g_summary_count = 0;
static char g_user[256] = {0};
static char g_error_msg[512] = {0};

/* Track if we're currently receiving a streamed message */
static int g_streaming = 0;

/*
 * wasm_init(json, len)
 *   Initializes the chat state from SSR-provided JSON state blob.
 *   Format: { "session": {...}, "summaries": [...], "user": "...", "error": "..." }
 */
ISO_EXPORT(void, wasm_init, const char *json, int len)
{
	if (!json || len <= 0)
		return;

	memset(&g_session, 0, sizeof(g_session));
	memset(g_summaries, 0, sizeof(g_summaries));
	memset(g_user, 0, sizeof(g_user));
	memset(g_error_msg, 0, sizeof(g_error_msg));
	g_summary_count = 0;
	g_streaming = 0;

	/* TODO: Parse JSON state
	 *   For now, this is a stub. Real implementation would deserialize
	 *   the SSR state using json-c or similar.
	 *   The JSON comes from site_ui_respond_page via bud-state attribute.
	 */
}

/*
 * bud_app_render()
 *   Primary WASM render function called by bud hydration
 */
ISO_EXPORT(bud_node *, bud_app_render, void)
{
	return chat_ui_render_view(&g_session, g_summaries, g_summary_count, g_user, g_error_msg);
}

/*
 * wasm_ws_on_open()
 *   Called when WebSocket connects. Establish stream for current session.
 */
ISO_EXPORT(void, wasm_ws_on_open, void)
{
	if (!g_session.id[0])
		return;

	g_streaming = 1;
	/* TODO: Send subscription request to server for this session
	 *   The server is listening on /chat/:id for frame updates
	 */
}

/*
 * wasm_ws_on_message(bytes, len)
 *   Called when a WebSocket message arrives (text frame with streaming token).
 *   Expected format: JSON {"token":"...", "done":true/false} or raw delta content
 */
ISO_EXPORT(void, wasm_ws_on_message, const char *bytes, int len)
{
	if (!bytes || len <= 0 || !g_streaming)
		return;

	/* TODO: Parse incoming token from stream
	 *   Update the last assistant message with the new token
	 *   Trigger UI re-render via bud_patch or bud_hydrate
	 *
	 *   Expected token format (JSON from auren streaming):
	 *   {"choices":[{"delta":{"content":"..."}}]}
	 *   OR simplified: {"token":"...", "done":false}
	 */
}

/*
 * wasm_ws_on_close()
 *   Called when WebSocket disconnects
 */
ISO_EXPORT(void, wasm_ws_on_close, void)
{
	g_streaming = 0;
}

/*
 * chat_wasm_submit_message(prompt)
 *   (Internal: not exported to WASM)
 *   Called from chat UI when user submits a message.
 *   Sends JSON {"prompt":"..."} to server via HTTP POST /chat/:id/send
 *   Then opens WS connection for response streaming.
 */
static void chat_wasm_submit_message(const char *prompt)
{
	if (!prompt || !g_session.id[0])
		return;

	/* TODO: POST to /chat/{session_id}/send with prompt
	 *   Then connect WS to /chat/{session_id} for streaming
	 */
}
