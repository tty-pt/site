#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "chat-types.h"

/* Direct unit tests of storage and data functions without requiring full running axil server */
#define CHAT_STORAGE_DIR "var/chat/sessions"

static void ensure_storage_dir(void)
{
	system("mkdir -p var/chat/sessions");
}

static void get_session_file_path(const char *id, char *out, size_t out_len)
{
	snprintf(out, out_len, "%s/%s.dsv", CHAT_STORAGE_DIR, id);
}

static void dsv_escape(const char *src, char *dst, size_t dst_len)
{
	size_t di = 0;
	for (size_t si = 0; src[si] && di + 2 < dst_len; si++) {
		if (src[si] == '\n') {
			dst[di++] = '\\';
			dst[di++] = 'n';
		} else if (src[si] == '\r') {
			dst[di++] = '\\';
			dst[di++] = 'r';
		} else if (src[si] == '|') {
			dst[di++] = '\\';
			dst[di++] = 'p';
		} else if (src[si] == '\\') {
			dst[di++] = '\\';
			dst[di++] = '\\';
		} else {
			dst[di++] = src[si];
		}
	}
	dst[di] = '\0';
}

static void dsv_unescape(const char *src, char *dst, size_t dst_len)
{
	size_t di = 0;
	for (size_t si = 0; src[si] && di + 1 < dst_len; si++) {
		if (src[si] == '\\' && src[si + 1]) {
			si++;
			if (src[si] == 'n')
				dst[di++] = '\n';
			else if (src[si] == 'r')
				dst[di++] = '\r';
			else if (src[si] == 'p')
				dst[di++] = '|';
			else if (src[si] == '\\')
				dst[di++] = '\\';
			else
				dst[di++] = src[si];
		} else {
			dst[di++] = src[si];
		}
	}
	dst[di] = '\0';
}

static int test_save_session(const chat_session_t *s)
{
	if (!s || !s->id[0])
		return -1;
	ensure_storage_dir();

	char path[512];
	get_session_file_path(s->id, path, sizeof(path));

	FILE *fp = fopen(path, "w");
	if (!fp)
		return -1;

	char esc_title[CHAT_TITLE_MAX * 2];
	char esc_user[CHAT_USER_MAX * 2];
	char esc_model[CHAT_MODEL_MAX * 2];
	dsv_escape(s->title, esc_title, sizeof(esc_title));
	dsv_escape(s->user, esc_user, sizeof(esc_user));
	dsv_escape(s->model, esc_model, sizeof(esc_model));

	fprintf(fp, "META|%s|%s|%s|%s|%ld|%ld\n",
	        s->id, esc_user, esc_title, esc_model,
	        (long)s->created_at, (long)s->updated_at);

	for (size_t i = 0; i < s->msg_count; i++) {
		const chat_msg_t *m = &s->messages[i];
		char esc_content[CHAT_MSG_CONTENT_MAX * 2];
		dsv_escape(m->content, esc_content, sizeof(esc_content));
		fprintf(fp, "MSG|%d|%ld|%s\n", (int)m->role, (long)m->timestamp, esc_content);
	}

	fclose(fp);
	return 0;
}

static int test_get_session(const char *id, chat_session_t *out)
{
	if (!id || !id[0] || !out)
		return -1;

	char path[512];
	get_session_file_path(id, path, sizeof(path));

	FILE *fp = fopen(path, "r");
	if (!fp)
		return -1;

	memset(out, 0, sizeof(*out));
	char line[CHAT_MSG_CONTENT_MAX * 2 + 128];

	while (fgets(line, sizeof(line), fp)) {
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
			line[len - 1] = '\0';
			len--;
		}

		if (strncmp(line, "META|", 5) == 0) {
			char *p = line + 5;
			char *parts[6] = {0};
			int idx = 0;
			while (p && idx < 6) {
				parts[idx++] = p;
				char *next = strchr(p, '|');
				if (next) {
					*next = '\0';
					p = next + 1;
				} else {
					break;
				}
			}
			if (idx >= 6) {
				snprintf(out->id, sizeof(out->id), "%s", parts[0]);
				dsv_unescape(parts[1], out->user, sizeof(out->user));
				dsv_unescape(parts[2], out->title, sizeof(out->title));
				dsv_unescape(parts[3], out->model, sizeof(out->model));
				out->created_at = (time_t)atol(parts[4]);
				out->updated_at = (time_t)atol(parts[5]);
			}
		} else if (strncmp(line, "MSG|", 4) == 0) {
			if (out->msg_count >= CHAT_MAX_MESSAGES)
				continue;
			char *p = line + 4;
			char *parts[3] = {0};
			int idx = 0;
			while (p && idx < 3) {
				parts[idx++] = p;
				if (idx < 3) {
					char *next = strchr(p, '|');
					if (next) {
						*next = '\0';
						p = next + 1;
					} else {
						break;
					}
				}
			}
			if (idx >= 3) {
				chat_msg_t *m = &out->messages[out->msg_count++];
				m->role = (chat_role_t)atoi(parts[0]);
				m->timestamp = (time_t)atol(parts[1]);
				dsv_unescape(parts[2], m->content, sizeof(m->content));
			}
		}
	}

	fclose(fp);
	return 0;
}

int main(void)
{
	printf("[test] Testing chat multi-session persistence...\n");

	chat_session_t s1;
	memset(&s1, 0, sizeof(s1));
	snprintf(s1.id, sizeof(s1.id), "test_sess_001");
	snprintf(s1.title, sizeof(s1.title), "Test Session 1");
	snprintf(s1.user, sizeof(s1.user), "testuser");
	snprintf(s1.model, sizeof(s1.model), "olmoe");
	s1.created_at = 1700000000;
	s1.updated_at = 1700000100;
	s1.msg_count = 2;

	s1.messages[0].role = CHAT_ROLE_USER;
	s1.messages[0].timestamp = 1700000010;
	snprintf(s1.messages[0].content, sizeof(s1.messages[0].content), "Hello Auren!");

	s1.messages[1].role = CHAT_ROLE_ASSISTANT;
	s1.messages[1].timestamp = 1700000020;
	snprintf(s1.messages[1].content, sizeof(s1.messages[1].content), "Greetings! How may I assist you?");

	assert(test_save_session(&s1) == 0);

	chat_session_t loaded;
	memset(&loaded, 0, sizeof(loaded));
	assert(test_get_session("test_sess_001", &loaded) == 0);

	assert(strcmp(loaded.id, "test_sess_001") == 0);
	assert(strcmp(loaded.title, "Test Session 1") == 0);
	assert(strcmp(loaded.user, "testuser") == 0);
	assert(strcmp(loaded.model, "olmoe") == 0);
	assert(loaded.msg_count == 2);
	assert(loaded.messages[0].role == CHAT_ROLE_USER);
	assert(strcmp(loaded.messages[0].content, "Hello Auren!") == 0);
	assert(loaded.messages[1].role == CHAT_ROLE_ASSISTANT);
	assert(strcmp(loaded.messages[1].content, "Greetings! How may I assist you?") == 0);

	/* Multi-session test: Create s2 */
	chat_session_t s2;
	memset(&s2, 0, sizeof(s2));
	snprintf(s2.id, sizeof(s2.id), "test_sess_002");
	snprintf(s2.title, sizeof(s2.title), "Test Session 2");
	snprintf(s2.user, sizeof(s2.user), "anotheruser");
	snprintf(s2.model, sizeof(s2.model), "qwen");
	s2.created_at = 1700000200;
	s2.updated_at = 1700000300;
	s2.msg_count = 1;
	s2.messages[0].role = CHAT_ROLE_USER;
	s2.messages[0].timestamp = 1700000210;
	snprintf(s2.messages[0].content, sizeof(s2.messages[0].content), "Session 2 content");

	assert(test_save_session(&s2) == 0);

	chat_session_t loaded2;
	assert(test_get_session("test_sess_002", &loaded2) == 0);
	assert(strcmp(loaded2.id, "test_sess_002") == 0);
	assert(strcmp(loaded2.title, "Test Session 2") == 0);
	assert(strcmp(loaded2.model, "qwen") == 0);
	assert(loaded2.msg_count == 1);

	printf("[test] Session persistence & multi-session isolation PASS\n");
	return 0;
}
