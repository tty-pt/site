#ifndef CHAT_UI_C
#define CHAT_UI_C

#include "chat_ui.h"
#include "../markdown.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GENERATING_SENTINEL "[generating...]"

static bud_node *render_msg_node(const chat_msg_t *msg)
{
	const char *role_class = "msg-user";
	const char *role_label = "You";

	if (msg->role == CHAT_ROLE_ASSISTANT) {
		role_class = "msg-assistant";
		role_label = "Auren Assistant";
	} else if (msg->role == CHAT_ROLE_SYSTEM) {
		role_class = "msg-system";
		role_label = "System";
	}

	if (strcmp(msg->content, GENERATING_SENTINEL) == 0) {
		return bud_tpl(
		        "<div class='chat-message msg-assistant msg-generating'>"
		        "  <div class='msg-header'>Auren Assistant</div>"
		        "  <div class='msg-body'><span class='thinking-spinner'></span> Thinking...</div>"
		        "</div>");
	}

	char *html_content = chat_markdown_to_html(msg->content);
	const char *rendered = html_content ? html_content : msg->content;

	bud_node *node = bud_tpl(
	        "<div class='chat-message %s'>"
	        "  <div class='msg-header'>%s</div>"
	        "  <div class='msg-body'>%node</div>"
	        "</div>",
	        role_class, role_label, bud_raw(rendered));

	if (html_content)
		free(html_content);

	return node;
}

bud_node *chat_ui_render_view(
        const chat_session_t *session,
        const chat_session_summary_t *summaries,
        size_t summary_count,
        const char *user,
        const char *error_msg)
{
	(void)user;

	/* 1. Build session list sidebar */
	bud_node *session_items = NULL;
	for (size_t i = 0; i < summary_count; i++) {
		const chat_session_summary_t *s = &summaries[i];
		int is_active = (session && strcmp(session->id, s->id) == 0);
		const char *active_cls = is_active ? "session-item active" : "session-item";

		bud_node *item = bud_tpl(
		        "<div class='%s'>"
		        "  <a href='/chat/%s' class='session-link'>"
		        "    <span class='session-title'>%s</span>"
		        "    <span class='session-count'>%zu</span>"
		        "  </a>"
		        "  <form method='POST' action='/chat/%s/delete' class='session-del-form' onsubmit=\"return confirm('Delete this conversation?');\">"
		        "    <button type='submit' class='btn-del' title='Delete conversation'>&times;</button>"
		        "  </form>"
		        "</div>",
		        active_cls, s->id, s->title[0] ? s->title : "New Conversation", s->msg_count, s->id);

		if (!session_items)
			session_items = item;
		else
			session_items = bud_tpl("%node%node", session_items, item);
	}

	if (!session_items) {
		session_items = bud_tpl("<p class='text-muted p-2'>No active conversations.</p>");
	}

	bud_node *sidebar = bud_tpl(
	        "<aside class='chat-sidebar'>"
	        "  <div class=\"sidebar-header\">"
	        "    <h2>Conversations</h2>"
	        "    <form method=\"POST\" action=\"/chat/new\" class=\"m-0\">"
	        "      <button type=\"submit\" class=\"btn btn-sm\">+ New</button>"
	        "    </form>"
	        "  </div>"
	        "  <div class=\"session-list\">"
	        "    %node"
	        "  </div>"
	        "</aside>",
	        session_items);

	/* 2. Build message log */
	bud_node *messages_node = NULL;
	if (session && session->msg_count > 0) {
		for (size_t i = 0; i < session->msg_count; i++) {
			bud_node *mnode = render_msg_node(&session->messages[i]);
			if (!messages_node)
				messages_node = mnode;
			else
				messages_node = bud_tpl("%node%node", messages_node, mnode);
		}
	} else {
		messages_node = bud_tpl(
		        "<div class='chat-welcome'>"
		        "  <h3>Auren Neural Chat</h3>"
		        "  <p>Start a conversation powered by in-process local inference on the NeverDark stack.</p>"
		        "</div>");
	}

	bud_node *err_node = NULL;
	if (error_msg && error_msg[0]) {
		err_node = bud_tpl("<div class='chat-error-banner'>%s</div>", error_msg);
	}

	/* 3. Build input form */
	char post_action[CHAT_ID_MAX + 32] = "/chat/send";
	if (session && session->id[0]) {
		snprintf(post_action, sizeof(post_action), "/chat/%s/send", session->id);
	}

	bud_node *input_form = bud_tpl(
	        "<form method='POST' action='%s' class='chat-input-row'>"
	        "  <input type='text' name='prompt' placeholder='Ask anything...' required autofocus class='chat-input-field' autocomplete='off'/>"
	        "  <button type='submit' class='btn'>Send</button>"
	        "</form>",
	        post_action);

	/* 4. Combine into full chat layout */
	const char *current_title = (session && session->title[0]) ? session->title : "Conversation";
	const char *current_model = (session && session->model[0]) ? session->model : "olmoe";

	bud_node *main_chat = bud_tpl(
	        "<div class='chat-main'>"
	        "  <div class='chat-header'>"
	        "    <div class='chat-title-group'>"
	        "      <span class='chat-title'>%s</span>"
	        "      <span class='model-badge'>%s</span>"
	        "    </div>"
	        "  </div>"
	        "  %node"
	        "  <div class='chat-messages'>"
	        "    %node"
	        "  </div>"
	        "  <div class='chat-footer'>"
	        "    %node"
	        "  </div>"
	        "</div>",
	        current_title, current_model,
	        err_node,
	        messages_node,
	        input_form);

	return bud_tpl(
	        "<div class='chat-wrapper'>"
	        "  <div class='chat-panel'>"
	        "    %node"
	        "    %node"
	        "  </div>"
	        "</div>",
	        sidebar,
	        main_chat);
}

#endif
