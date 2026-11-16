/*
 * chat_fe.c — WebAssembly frontend for real-time chat streaming
 * Handles WebSocket message accumulation and DOM rendering updates.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "bud/bud.h"
#include "bud/bud_app.h"
#include <ttypt/stele_wasm.h>
#include "../../common/site_wasm_app.h"

#include "chat_ui.h"
#include "../chat-types.h"

/* Persistent chat state for WASM session */
static chat_session_t g_session = {0};
static chat_session_summary_t g_summaries[32] = {0};
static size_t g_summary_count = 0;
static char g_user[256] = {0};
static char g_error_msg[512] = {0};
static char g_pending_tokens[CHAT_MSG_CONTENT_MAX] = {0};
static int g_streaming = 0;

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

/*
 * wasm_init(json, len)
 *   Initializes chat state from SSR-provided JSON blob.
 *   Format: JSON object with "session", "summaries", "user", "error" fields.
 */
static int chat_stl_init(const char *json, int len)
{
	if (!json || len <= 0)
		return 0;

	memset(&g_session, 0, sizeof(g_session));
	memset(g_summaries, 0, sizeof(g_summaries));
	memset(g_user, 0, sizeof(g_user));
	memset(g_error_msg, 0, sizeof(g_error_msg));
	memset(g_pending_tokens, 0, sizeof(g_pending_tokens));
	g_summary_count = 0;
	g_streaming = 0;

	/* Parse the SSR state blob with bud's jsmn-backed helpers: json-c is
	 * not linked into the WASM build, so referencing it would leave
	 * unresolved host imports. */
	size_t jlen = (size_t)len;
	/* The nested object is at most as long as the whole blob. */
	char *session_json = malloc(jlen + 1);
	if (session_json) {
		bud_json_str_len(json, jlen, "session", session_json, jlen + 1);
		if (session_json[0])
			bud_json_str_len(session_json, 0, "id", g_session.id, sizeof(g_session.id));
		free(session_json);
	}
	bud_json_str_len(json, jlen, "user", g_user, sizeof(g_user));
	bud_json_str_len(json, jlen, "error", g_error_msg, sizeof(g_error_msg));
	return 0;
}

static bud_node *chat_stl_render(void)
{
	return chat_ui_render_view(&g_session, g_summaries, g_summary_count, g_user, g_error_msg);
}

/*
 * wasm_ws_on_open()
 *   Called when WebSocket connects. Setup is complete.
 */
ISO_EXPORT(void, wasm_ws_on_open, void)
{
	g_streaming = 0;  /* Ready to receive tokens */
}

/*
 * wasm_ws_on_message(bytes, len)
 *   Called when WebSocket receives a message.
 *   Expected format: JSON {"token":"...","done":false/true}
 *   Token messages are accumulated; "done":true signals end of stream.
 */
ISO_EXPORT(void, wasm_ws_on_message, const char *bytes, int len)
{
	if (!bytes || len <= 0)
		return;

	size_t mlen = (size_t)len;
	static char token[CHAT_MSG_CONTENT_MAX];

	/* Accumulate the token chunk into the pending buffer */
	bud_json_str_len(bytes, mlen, "token", token, sizeof(token));
	if (token[0]) {
		size_t curr_len = strlen(g_pending_tokens);
		size_t token_len = strlen(token);
		if (curr_len + token_len < sizeof(g_pending_tokens) - 1) {
			memcpy(g_pending_tokens + curr_len, token, token_len);
			g_pending_tokens[curr_len + token_len] = '\0';
			g_streaming = 1;
		}
	}

	/* "done":true ends the stream */
	if (json_bool(bytes, mlen, "done", 0) && g_streaming) {
		if (g_session.msg_count > 0) {
			chat_msg_t *last_msg = &g_session.messages[g_session.msg_count - 1];
			if (last_msg->role == CHAT_ROLE_ASSISTANT)
				snprintf(last_msg->content, sizeof(last_msg->content), "%s", g_pending_tokens);
		}
		g_streaming = 0;
		memset(g_pending_tokens, 0, sizeof(g_pending_tokens));
	}
}

/*
 * wasm_ws_on_close()
 *   Called when WebSocket disconnects.
 */
ISO_EXPORT(void, wasm_ws_on_close, void)
{
	g_streaming = 0;
	memset(g_pending_tokens, 0, sizeof(g_pending_tokens));
}

static void chat_stl_cleanup(void)
{
	memset(&g_session, 0, sizeof(g_session));
	memset(g_summaries, 0, sizeof(g_summaries));
	memset(g_user, 0, sizeof(g_user));
	memset(g_error_msg, 0, sizeof(g_error_msg));
	memset(g_pending_tokens, 0, sizeof(g_pending_tokens));
	g_summary_count = 0;
	g_streaming = 0;
}

BUD_WASM_MODULE_EXT(chat_fe, chat_stl_init, chat_stl_render, NULL, NULL, chat_stl_cleanup, NULL);

