#ifndef CHAT_TYPES_H
#define CHAT_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define CHAT_ID_MAX 64
#define CHAT_TITLE_MAX 128
#define CHAT_USER_MAX 64
#define CHAT_MODEL_MAX 64
#define CHAT_MAX_MESSAGES 128
#define CHAT_MSG_CONTENT_MAX 8192

typedef enum {
	CHAT_ROLE_SYSTEM = 0,
	CHAT_ROLE_USER = 1,
	CHAT_ROLE_ASSISTANT = 2
} chat_role_t;

typedef struct {
	chat_role_t role;
	time_t timestamp;
	char content[CHAT_MSG_CONTENT_MAX];
} chat_msg_t;

typedef struct {
	char id[CHAT_ID_MAX];
	char user[CHAT_USER_MAX];
	char title[CHAT_TITLE_MAX];
	char model[CHAT_MODEL_MAX];
	time_t created_at;
	time_t updated_at;
	size_t msg_count;
	chat_msg_t messages[CHAT_MAX_MESSAGES];
} chat_session_t;

typedef struct {
	char id[CHAT_ID_MAX];
	char title[CHAT_TITLE_MAX];
	time_t updated_at;
	size_t msg_count;
} chat_session_summary_t;

#endif
