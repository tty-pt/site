/*
 * chat_fe.c — WebAssembly frontend for real-time chat streaming
 * Handles WebSocket message accumulation and DOM rendering updates.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <json-c/json.h>

#include "bud/bud.h"
#include "bud/bud_app.h"

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

/*
 * wasm_init(json, len)
 *   Initializes chat state from SSR-provided JSON blob.
 *   Format: JSON object with "session", "summaries", "user", "error" fields.
 */
ISO_EXPORT(void, wasm_init, const char *json, int len)
{
	if (!json || len <= 0)
		return;

	memset(&g_session, 0, sizeof(g_session));
	memset(g_summaries, 0, sizeof(g_summaries));
	memset(g_user, 0, sizeof(g_user));
	memset(g_error_msg, 0, sizeof(g_error_msg));
	memset(g_pending_tokens, 0, sizeof(g_pending_tokens));
	g_summary_count = 0;
	g_streaming = 0;

	/* Parse JSON state blob using json-c */
	struct json_object *root = json_tokener_parse_ex(
		json_tokener_new(),
		json,
		len);

	if (!root)
		return;

	/* Extract session object */
	struct json_object *session_obj = NULL;
	if (json_object_object_get_ex(root, "session", &session_obj) && session_obj) {
		/* Deserialize session (simplified—assumes DSV or manual parsing) */
		struct json_object *id_obj = NULL;
		if (json_object_object_get_ex(session_obj, "id", &id_obj)) {
			const char *id_str = json_object_get_string(id_obj);
			if (id_str)
				snprintf(g_session.id, sizeof(g_session.id), "%s", id_str);
		}
	}

	/* Extract user */
	struct json_object *user_obj = NULL;
	if (json_object_object_get_ex(root, "user", &user_obj)) {
		const char *user_str = json_object_get_string(user_obj);
		if (user_str)
			snprintf(g_user, sizeof(g_user), "%s", user_str);
	}

	/* Extract error message */
	struct json_object *error_obj = NULL;
	if (json_object_object_get_ex(root, "error", &error_obj)) {
		const char *error_str = json_object_get_string(error_obj);
		if (error_str)
			snprintf(g_error_msg, sizeof(g_error_msg), "%s", error_str);
	}

	json_object_put(root);
}

/*
 * bud_app_render()
 *   Primary WASM render function called by bud hydration.
 *   Returns the chat UI tree with current state.
 */
ISO_EXPORT(bud_node *, bud_app_render, void)
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

	/* Parse incoming JSON message */
	struct json_object *msg = json_tokener_parse_ex(
		json_tokener_new(),
		bytes,
		len);

	if (!msg)
		return;

	/* Check for token field */
	struct json_object *token_obj = NULL;
	if (json_object_object_get_ex(msg, "token", &token_obj)) {
		const char *token = json_object_get_string(token_obj);
		if (token && token[0]) {
			/* Accumulate token into pending buffer */
			size_t curr_len = strlen(g_pending_tokens);
			size_t token_len = strlen(token);
			if (curr_len + token_len < sizeof(g_pending_tokens) - 1) {
				memcpy(g_pending_tokens + curr_len, token, token_len);
				g_pending_tokens[curr_len + token_len] = '\0';
				g_streaming = 1;
			}
		}
	}

	/* Check for done flag */
	struct json_object *done_obj = NULL;
	if (json_object_object_get_ex(msg, "done", &done_obj)) {
		int is_done = json_object_get_boolean(done_obj);
		if (is_done && g_streaming) {
			/* Stream complete: update session with final accumulated response */
			if (g_session.msg_count > 0) {
				chat_msg_t *last_msg = &g_session.messages[g_session.msg_count - 1];
				if (last_msg->role == CHAT_ROLE_ASSISTANT) {
					snprintf(last_msg->content, sizeof(last_msg->content), "%s", g_pending_tokens);
				}
			}
			g_streaming = 0;
			memset(g_pending_tokens, 0, sizeof(g_pending_tokens));
		}
	}

	json_object_put(msg);
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
