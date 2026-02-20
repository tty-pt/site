#ifndef AUTH_H
#define AUTH_H

#include <stdint.h>

extern uint32_t auth_sessions_hd;

void auth_init(void);
const char *auth_session_get(const char *token);
int auth_session_set(const char *token, const char *username);
void auth_session_delete(const char *token);

#endif
