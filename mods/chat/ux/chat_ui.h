#ifndef CHAT_UI_H
#define CHAT_UI_H

#include "bud/bud.h"
#include "../chat-types.h"

bud_node *chat_ui_render_view(
        const chat_session_t *session,
        const chat_session_summary_t *summaries,
        size_t summary_count,
        const char *user,
        const char *error_msg);

#endif
