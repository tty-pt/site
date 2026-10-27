#ifndef CHAT_H
#define CHAT_H

#include <ttypt/axil.h>
#include <ttypt/axil-xy.h>
#include "chat-types.h"

XY_DECL(int, chat_get_session, const char *, id, chat_session_t *, out);
XY_DECL(int, chat_save_session, const chat_session_t *, session);

#endif
