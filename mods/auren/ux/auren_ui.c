#ifndef AUREN_UI_C
#define AUREN_UI_C

#include "auren_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bud_node *render_msg(const auren_demo_msg_t *msg)
{
	const char *role_class = (strcmp(msg->role, "user") == 0) ? "msg-user" : "msg-assistant";
	const char *role_label = (strcmp(msg->role, "user") == 0) ? "User" : "SmolLM2 (Local GPU)";

	return bud_tpl(
		"<div class='auren-msg %s'>"
		"  <div class='auren-msg-header'>%s</div>"
		"  <div class='auren-msg-body'>%s</div>"
		"</div>",
		role_class, role_label, msg->content);
}

bud_node *auren_ui_render_view(const auren_demo_state_t *st)
{
	/* Build message list */
	bud_node *msg_list = NULL;
	for (size_t i = 0; i < st->msg_count; i++) {
		bud_node *m = render_msg(&st->msgs[i]);
		if (!msg_list) {
			msg_list = m;
		} else {
			msg_list = bud_tpl("%node%node", msg_list, m);
		}
	}

	/* If streaming, append live response block */
	if (st->streaming) {
		bud_node *streaming_node = bud_tpl(
			"<div class='auren-msg msg-assistant streaming'>"
			"  <div class='auren-msg-header'>SmolLM2 (Local GPU - Streaming)</div>"
			"  <div class='auren-msg-body'>%s<span class='auren-cursor'></span></div>"
			"</div>",
			st->current_response);

		if (!msg_list) {
			msg_list = streaming_node;
		} else {
			msg_list = bud_tpl("%node%node", msg_list, streaming_node);
		}
	}

	if (!msg_list) {
		msg_list = bud_tpl(
			"<div class='auren-empty-state'>"
			"  <p>No messages yet. Ask SmolLM2 a question to run client-side inference!</p>"
			"</div>");
	}

	/* Build hardware badge */
	const char *badge_class = st->webgpu_available ? "badge-gpu-on" : "badge-gpu-wait";
	const char *badge_text = st->webgpu_available ? "WebGPU Active (Local Hardware)" : "Checking Local Hardware...";

	/* Progress bar if downloading */
	bud_node *progress_node = NULL;
	if (st->percent < 100 && !st->cached) {
		char pct_buf[32];
		snprintf(pct_buf, sizeof(pct_buf), "%d%%", st->percent);
		progress_node = bud_tpl(
			"<div class='auren-progress-box'>"
			"  <div class='auren-progress-label'>Downloading SmolLM2 weights to browser OPFS cache: %s</div>"
			"  <div class='auren-progress-bar'><div class='auren-progress-fill' style='width: %s;'></div></div>"
			"</div>",
			pct_buf, pct_buf);
	} else if (st->cached) {
		progress_node = bud_tpl(
			"<div class='auren-cache-pill'>"
			"  <span class='cache-icon'>⚡</span> Weights loaded from persistent browser cache (0 bytes transferred)"
			"</div>");
	}

	return bud_tpl(
		"<div class='auren-container'>"
		"  <div class='auren-header'>"
		"    <div class='auren-title-row'>"
		"      <h1>Auren In-Browser LLM Demo</h1>"
		"      <span class='auren-badge %s'>%s</span>"
		"    </div>"
		"    <p class='auren-desc'>"
		"      Powered by <strong>SmolLM2-135M-Instruct</strong>. Model weights are cached locally via OPFS and evaluated entirely inside your browser via WebGPU & WebAssembly. Zero cloud tokens, zero data egress."
		"    </p>"
		"    %node"
		"  </div>"
		"  <div class='auren-chat-viewport'>"
		"    %node"
		"  </div>"
		"  <form class='auren-input-bar' id='auren-form' method='POST' action='/auren/prompt'>"
		"    <input type='text' id='auren-input' name='prompt' placeholder='Ask SmolLM2 anything...' autocomplete='off' />"
		"    <button type='submit' id='auren-submit-btn' class='btn-primary'>Generate</button>"
		"  </form>"
		"  <div class='auren-quick-prompts'>"
		"    <span class='quick-label'>Quick tests:</span>"
		"    <button type='button' class='btn-chip' data-prompt='Explain pointers in C in 10 words.'>Pointers in C</button>"
		"    <button type='button' class='btn-chip' data-prompt='What is the benefit of isomorphic WebAssembly?'>Isomorphic WASM</button>"
		"    <button type='button' class='btn-chip' data-prompt='Write a short poem about Linux.'>Linux Poem</button>"
		"  </div>"
		"</div>",
		badge_class, badge_text,
		progress_node,
		msg_list);
}

#endif /* AUREN_UI_C */
