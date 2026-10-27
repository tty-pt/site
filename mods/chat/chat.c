#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <json-c/json.h>

#include <ttypt/xy-mod.h>
#include <ttypt/axil.h>
#include <ttypt/corm.h>

#include "../common/common.h"
#include "chat-types.h"
#include "ux/chat_ui.h"
#include "ux/chat_ui.c"

#define AUREN_DEFAULT_HOST "127.0.0.1"
#define AUREN_DEFAULT_PORT 4242
#define CHAT_STORAGE_DIR "var/chat/sessions"
#define GENERATING_SENTINEL "[generating...]"

static pthread_mutex_t g_chat_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_ws_session_map = 0;  /* corm handle: fd (u32) -> session_id (str) */

static void ensure_storage_dir(void)
{
	mkdir("var", 0755);
	mkdir("var/chat", 0755);
	mkdir(CHAT_STORAGE_DIR, 0755);
}

static void get_session_file_path(const char *id, char *out, size_t out_len)
{
	snprintf(out, out_len, "%s/%s.dsv", CHAT_STORAGE_DIR, id);
}

/* Encode string for single-line DSV (escape newlines and pipes) */
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

/* Unescape DSV string */
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

XY_IMPL(int, chat_save_session, const chat_session_t *, s)
{
	if (!s || !s->id[0])
		return -1;
	ensure_storage_dir();

	char path[512];
	get_session_file_path(s->id, path, sizeof(path));

	pthread_mutex_lock(&g_chat_lock);
	FILE *fp = fopen(path, "w");
	if (!fp) {
		pthread_mutex_unlock(&g_chat_lock);
		return -1;
	}

	char esc_title[CHAT_TITLE_MAX * 2];
	char esc_user[CHAT_USER_MAX * 2];
	char esc_model[CHAT_MODEL_MAX * 2];
	dsv_escape(s->title, esc_title, sizeof(esc_title));
	dsv_escape(s->user, esc_user, sizeof(esc_user));
	dsv_escape(s->model, esc_model, sizeof(esc_model));

	/* Header line: META|id|user|title|model|created_at|updated_at */
	fprintf(fp, "META|%s|%s|%s|%s|%ld|%ld\n",
	        s->id, esc_user, esc_title, esc_model,
	        (long)s->created_at, (long)s->updated_at);

	/* Message lines: MSG|role|timestamp|content */
	for (size_t i = 0; i < s->msg_count; i++) {
		const chat_msg_t *m = &s->messages[i];
		char esc_content[CHAT_MSG_CONTENT_MAX * 2];
		dsv_escape(m->content, esc_content, sizeof(esc_content));
		fprintf(fp, "MSG|%d|%ld|%s\n", (int)m->role, (long)m->timestamp, esc_content);
	}

	fclose(fp);
	pthread_mutex_unlock(&g_chat_lock);
	return 0;
}

XY_IMPL(int, chat_get_session, const char *, id, chat_session_t *, out)
{
	if (!id || !id[0] || !out)
		return -1;

	char path[512];
	get_session_file_path(id, path, sizeof(path));

	pthread_mutex_lock(&g_chat_lock);
	FILE *fp = fopen(path, "r");
	if (!fp) {
		pthread_mutex_unlock(&g_chat_lock);
		return -1;
	}

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
	pthread_mutex_unlock(&g_chat_lock);
	return 0;
}

static int compare_summaries(const void *a, const void *b)
{
	const chat_session_summary_t *sa = (const chat_session_summary_t *)a;
	const chat_session_summary_t *sb = (const chat_session_summary_t *)b;
	if (sb->updated_at > sa->updated_at) return 1;
	if (sb->updated_at < sa->updated_at) return -1;
	return 0;
}

static size_t list_sessions(chat_session_summary_t *out, size_t max)
{
	ensure_storage_dir();
	DIR *dir = opendir(CHAT_STORAGE_DIR);
	if (!dir)
		return 0;

	size_t count = 0;
	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL && count < max) {
		size_t nlen = strlen(ent->d_name);
		if (nlen > 4 && strcmp(ent->d_name + nlen - 4, ".dsv") == 0) {
			char id[CHAT_ID_MAX] = {0};
			snprintf(id, sizeof(id), "%.*s", (int)(nlen - 4), ent->d_name);
			chat_session_t s;
			if (chat_get_session(id, &s) == 0) {
				chat_session_summary_t *sum = &out[count++];
				snprintf(sum->id, sizeof(sum->id), "%s", s.id);
				snprintf(sum->title, sizeof(sum->title), "%s", s.title);
				sum->updated_at = s.updated_at;
				sum->msg_count = s.msg_count;
			}
		}
	}
	closedir(dir);

	if (count > 1) {
		qsort(out, count, sizeof(chat_session_summary_t), compare_summaries);
	}
	return count;
}

static void generate_session_id(char *out, size_t len)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	snprintf(out, len, "sess_%lx%lx", (long)ts.tv_sec, (long)ts.tv_nsec);
}

/* Call Auren /v1/chat/completions via SSE stream */
static int auren_chat_completion(
        const chat_session_t *s,
        char *resp_out,
        size_t resp_out_max)
{
	const char *host = AUREN_DEFAULT_HOST;
	int port = AUREN_DEFAULT_PORT;
	const char *env_port = getenv("AUREN_PORT");
	if (env_port && *env_port)
		port = atoi(env_port);

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		return -1;

	struct timeval tv;
	tv.tv_sec = 60;
	tv.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons((uint16_t)port);
	inet_pton(AF_INET, host, &sin.sin_addr);

	if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		close(sock);
		return -2;
	}

	struct json_object *root = json_object_new_object();
	const char *model = (s->model[0] && strcmp(s->model, "qwen") != 0) ? s->model : "olmoe";
	json_object_object_add(root, "model", json_object_new_string(model));
	json_object_object_add(root, "stream", json_object_new_boolean(1));
	json_object_object_add(root, "temperature", json_object_new_double(0.7));

	struct json_object *msgs_arr = json_object_new_array();

	int has_system = 0;
	for (size_t i = 0; i < s->msg_count; i++) {
		if (s->messages[i].role == CHAT_ROLE_SYSTEM) {
			has_system = 1;
			break;
		}
	}
	if (!has_system) {
		struct json_object *sys_obj = json_object_new_object();
		json_object_object_add(sys_obj, "role", json_object_new_string("system"));
		json_object_object_add(sys_obj, "content", json_object_new_string(
		        "You are an intelligent, helpful AI assistant built into the site. "
		        "Format your responses cleanly and helpfully using standard Markdown."));
		json_object_array_add(msgs_arr, sys_obj);
	}

	for (size_t i = 0; i < s->msg_count; i++) {
		const chat_msg_t *m = &s->messages[i];
		if (strcmp(m->content, GENERATING_SENTINEL) == 0)
			continue;

		const char *rstr = "user";
		if (m->role == CHAT_ROLE_ASSISTANT) rstr = "assistant";
		else if (m->role == CHAT_ROLE_SYSTEM) rstr = "system";

		struct json_object *m_obj = json_object_new_object();
		json_object_object_add(m_obj, "role", json_object_new_string(rstr));
		json_object_object_add(m_obj, "content", json_object_new_string(m->content));
		json_object_array_add(msgs_arr, m_obj);
	}
	json_object_object_add(root, "messages", msgs_arr);

	const char *payload_str = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
	size_t payload_len = strlen(payload_str);

	char req_header[1024];
	snprintf(req_header, sizeof(req_header),
	         "POST /v1/chat/completions HTTP/1.1\r\n"
	         "Host: %s:%d\r\n"
	         "User-Agent: axil-chat/1.0\r\n"
	         "Content-Type: application/json\r\n"
	         "Content-Length: %zu\r\n"
	         "Connection: close\r\n\r\n",
	         host, port, payload_len);

	if (send(sock, req_header, strlen(req_header), 0) < 0 ||
	    send(sock, payload_str, payload_len, 0) < 0)
	{
		json_object_put(root);
		close(sock);
		return -3;
	}
	json_object_put(root);

	/* Stream read line by line */
	FILE *stream = fdopen(sock, "r");
	if (!stream) {
		close(sock);
		return -4;
	}

	char line[4096];
	size_t out_len = 0;
	resp_out[0] = '\0';

	while (fgets(line, sizeof(line), stream)) {
		if (strncmp(line, "data: [DONE]", 12) == 0) {
			break;
		}
		if (strncmp(line, "data: ", 6) == 0) {
			struct json_object *obj = json_tokener_parse(line + 6);
			if (obj) {
				struct json_object *choices;
				if (json_object_object_get_ex(obj, "choices", &choices) &&
				    json_object_array_length(choices) > 0)
				{
					struct json_object *c0 = json_object_array_get_idx(choices, 0);
					struct json_object *delta;
					if (json_object_object_get_ex(c0, "delta", &delta)) {
						struct json_object *content;
						if (json_object_object_get_ex(delta, "content", &content)) {
							const char *part = json_object_get_string(content);
							if (part) {
								size_t plen = strlen(part);
								if (out_len + plen < resp_out_max - 1) {
									memcpy(resp_out + out_len, part, plen);
									out_len += plen;
									resp_out[out_len] = '\0';
								}
							}
						}
					}
				}
				json_object_put(obj);
			}
		}
	}
	fclose(stream);

	/* Trim leading whitespace */
	char *start = resp_out;
	while (*start == '\n' || *start == '\r' || *start == ' ')
		start++;
	if (start != resp_out) {
		memmove(resp_out, start, strlen(start) + 1);
	}

	return 0;
}

/* Background thread for async Auren query */
typedef struct {
	char session_id[CHAT_ID_MAX];
} auren_worker_arg_t;

static void *auren_worker_thread(void *arg)
{
	auren_worker_arg_t *warg = (auren_worker_arg_t *)arg;
	if (!warg)
		return NULL;

	chat_session_t s;
	if (chat_get_session(warg->session_id, &s) != 0) {
		free(warg);
		return NULL;
	}

	char reply[CHAT_MSG_CONTENT_MAX] = {0};
	int rc = auren_chat_completion(&s, reply, sizeof(reply));

	/* Re-fetch latest session to apply result */
	if (chat_get_session(warg->session_id, &s) == 0) {
		/* Find the pending placeholder or append */
		int found_placeholder = 0;
		for (size_t i = s.msg_count; i > 0; i--) {
			if (s.messages[i - 1].role == CHAT_ROLE_ASSISTANT &&
			    strcmp(s.messages[i - 1].content, GENERATING_SENTINEL) == 0)
			{
				if (rc == 0 && reply[0]) {
					snprintf(s.messages[i - 1].content, sizeof(s.messages[i - 1].content), "%s", reply);
				} else {
					s.messages[i - 1].role = CHAT_ROLE_SYSTEM;
					if (rc == -2)
						snprintf(s.messages[i - 1].content, sizeof(s.messages[i - 1].content),
						         "Unable to reach Auren daemon on port 4242.");
					else
						snprintf(s.messages[i - 1].content, sizeof(s.messages[i - 1].content),
						         "Auren error (code %d).", rc);
				}
				s.messages[i - 1].timestamp = time(NULL);
				found_placeholder = 1;
				break;
			}
		}

		if (!found_placeholder && s.msg_count < CHAT_MAX_MESSAGES) {
			chat_msg_t *am = &s.messages[s.msg_count++];
			am->role = (rc == 0) ? CHAT_ROLE_ASSISTANT : CHAT_ROLE_SYSTEM;
			am->timestamp = time(NULL);
			snprintf(am->content, sizeof(am->content), "%s", (rc == 0) ? reply : "Error contacting Auren");
		}

		s.updated_at = time(NULL);
		chat_save_session(&s);
	}

	free(warg);
	return NULL;
}

static void url_decode_inplace(char *str)
{
	char *p = str, *q = str;
	while (*p) {
		if (*p == '+') {
			*q++ = ' ';
			p++;
		} else if (*p == '%' && p[1] && p[2]) {
			char hex[3] = { p[1], p[2], 0 };
			*q++ = (char)strtol(hex, NULL, 16);
			p += 3;
		} else {
			*q++ = *p++;
		}
	}
	*q = '\0';
}

/* WebSocket streaming worker — sends tokens from auren back to client */
static void *ws_worker_thread(void *arg)
{
	typedef struct {
		socket_t ws_fd;
		char session_id[CHAT_ID_MAX];
	} ws_worker_arg_t;

	ws_worker_arg_t *warg = (ws_worker_arg_t *)arg;
	if (!warg)
		return NULL;

	chat_session_t s;
	if (chat_get_session(warg->session_id, &s) != 0) {
		free(warg);
		return NULL;
	}

	const char *host = AUREN_DEFAULT_HOST;
	int port = AUREN_DEFAULT_PORT;
	const char *env_port = getenv("AUREN_PORT");
	if (env_port && *env_port)
		port = atoi(env_port);

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		free(warg);
		return NULL;
	}

	struct timeval tv;
	tv.tv_sec = 5;  /* Reduced from 60s to 5s for better responsiveness when auren unavailable */
	tv.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons((uint16_t)port);
	inet_pton(AF_INET, host, &sin.sin_addr);

	if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		close(sock);
		free(warg);
		return NULL;
	}

	/* Build auren request (same as auren_chat_completion) */
	struct json_object *root = json_object_new_object();
	const char *model = (s.model[0] && strcmp(s.model, "qwen") != 0) ? s.model : "olmoe";
	json_object_object_add(root, "model", json_object_new_string(model));
	json_object_object_add(root, "stream", json_object_new_boolean(1));
	json_object_object_add(root, "temperature", json_object_new_double(0.7));

	struct json_object *msgs_arr = json_object_new_array();

	/* System message */
	struct json_object *sys_obj = json_object_new_object();
	json_object_object_add(sys_obj, "role", json_object_new_string("system"));
	json_object_object_add(sys_obj, "content", json_object_new_string(
	        "You are an intelligent, helpful AI assistant. Format your responses cleanly using Markdown."));
	json_object_array_add(msgs_arr, sys_obj);

	/* Add session messages (exclude placeholder messages) */
	for (size_t i = 0; i < s.msg_count; i++) {
		const chat_msg_t *m = &s.messages[i];
		if (strcmp(m->content, "[generating...]") == 0)
			continue;

		const char *rstr = "user";
		if (m->role == CHAT_ROLE_ASSISTANT) rstr = "assistant";
		else if (m->role == CHAT_ROLE_SYSTEM) rstr = "system";

		struct json_object *m_obj = json_object_new_object();
		json_object_object_add(m_obj, "role", json_object_new_string(rstr));
		json_object_object_add(m_obj, "content", json_object_new_string(m->content));
		json_object_array_add(msgs_arr, m_obj);
	}
	json_object_object_add(root, "messages", msgs_arr);

	const char *payload_str = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
	size_t payload_len = strlen(payload_str);

	char req_header[1024];
	snprintf(req_header, sizeof(req_header),
	         "POST /v1/chat/completions HTTP/1.1\r\n"
	         "Host: %s:%d\r\n"
	         "User-Agent: axil-chat/1.0\r\n"
	         "Content-Type: application/json\r\n"
	         "Content-Length: %zu\r\n"
	         "Connection: close\r\n\r\n",
	         host, port, payload_len);

	if (send(sock, req_header, strlen(req_header), 0) < 0 ||
	    send(sock, payload_str, payload_len, 0) < 0)
	{
		json_object_put(root);
		close(sock);
		free(warg);
		return NULL;
	}
	json_object_put(root);

	/* Stream tokens back to WS client */
	FILE *stream = fdopen(sock, "r");
	if (!stream) {
		close(sock);
		free(warg);
		return NULL;
	}

	char line[4096];
	char accumulated[CHAT_MSG_CONTENT_MAX] = {0};

	while (fgets(line, sizeof(line), stream)) {
		if (strncmp(line, "data: [DONE]", 12) == 0) {
			break;
		}
		if (strncmp(line, "data: ", 6) == 0) {
			struct json_object *obj = json_tokener_parse(line + 6);
			if (obj) {
				struct json_object *choices;
				if (json_object_object_get_ex(obj, "choices", &choices) &&
				    json_object_array_length(choices) > 0)
				{
					struct json_object *c0 = json_object_array_get_idx(choices, 0);
					struct json_object *delta;
					if (json_object_object_get_ex(c0, "delta", &delta)) {
						struct json_object *content;
						if (json_object_object_get_ex(delta, "content", &content)) {
							const char *token = json_object_get_string(content);
							if (token) {
								/* Send token back to WS client */
								struct json_object *msg = json_object_new_object();
								json_object_object_add(msg, "token", json_object_new_string(token));
								json_object_object_add(msg, "done", json_object_new_boolean(0));
								const char *msg_str = json_object_to_json_string_ext(msg, JSON_C_TO_STRING_PLAIN);
								axil_ws_write(warg->ws_fd, (const void *)msg_str, strlen(msg_str));
								json_object_put(msg);

								/* Also accumulate for session save */
								size_t acc_len = strlen(accumulated);
								size_t token_len = strlen(token);
								if (acc_len + token_len < sizeof(accumulated) - 1) {
									memcpy(accumulated + acc_len, token, token_len);
									accumulated[acc_len + token_len] = '\0';
								}
							}
						}
					}
				}
				json_object_put(obj);
			}
		}
	}
	fclose(stream);

	/* Send completion message */
	struct json_object *done_msg = json_object_new_object();
	json_object_object_add(done_msg, "done", json_object_new_boolean(1));
	const char *done_str = json_object_to_json_string_ext(done_msg, JSON_C_TO_STRING_PLAIN);
	axil_ws_write(warg->ws_fd, (const void *)done_str, strlen(done_str));
	json_object_put(done_msg);

	/* Update session with final response */
	if (chat_get_session(warg->session_id, &s) == 0) {
		for (size_t i = s.msg_count; i > 0; i--) {
			if (s.messages[i - 1].role == CHAT_ROLE_ASSISTANT &&
			    strcmp(s.messages[i - 1].content, "[generating...]") == 0)
			{
				if (accumulated[0]) {
					snprintf(s.messages[i - 1].content, sizeof(s.messages[i - 1].content), "%s", accumulated);
				}
				s.messages[i - 1].timestamp = time(NULL);
				break;
			}
		}
		s.updated_at = time(NULL);
		chat_save_session(&s);
	}

	free(warg);
	return NULL;
}

static int render_chat_page(int fd, const chat_session_t *s, const char *err)
{
	char user[ENV_VALUE_LEN] = {0};
	axil_env_get(fd, user, sizeof(user), "REMOTE_USER");

	chat_session_summary_t summaries[32];
	size_t summary_count = list_sessions(summaries, 32);

	bud_node *chat_view = chat_ui_render_view(s, summaries, summary_count, user, err);

	char extra_head[1024];
	snprintf(extra_head, sizeof(extra_head),
	         "<link rel=\"stylesheet\" href=\"/chat.css\">\n"
	         "<script src=\"/chat.js\" defer></script>\n");

	return site_ui_respond_page(
	        fd, "Chat", "/chat", "💬", user, extra_head, "chat", chat_view);
}

/* Route helper: match pattern /chat/:id and /chat/:id/:action */
static int parse_chat_route(const char *uri, char *id_out, size_t id_len, char *action_out, size_t act_len)
{
	if (strncmp(uri, "/chat/", 6) != 0)
		return -1;

	const char *p = uri + 6;
	const char *slash = strchr(p, '/');

	if (!slash) {
		snprintf(id_out, id_len, "%s", p);
		if (action_out && act_len) action_out[0] = '\0';
		return 0;
	}

	snprintf(id_out, id_len, "%.*s", (int)(slash - p), p);
	if (action_out && act_len)
		snprintf(action_out, act_len, "%s", slash + 1);
	return 0;
}

/* Unified Chat Dispatcher */
static int chat_route_handler(int fd, char *body)
{
	char uri[1024] = {0};
	char method[32] = {0};
	axil_env_get(fd, uri, sizeof(uri), "DOCUMENT_URI");
	axil_env_get(fd, method, sizeof(method), "REQUEST_METHOD");

	/* Strip trailing query string if in uri */
	char *q = strchr(uri, '?');
	if (q) *q = '\0';

	int is_post = (strcmp(method, "POST") == 0);

	/* 1. GET /chat or GET /chat/ */
	if (!is_post && (strcmp(uri, "/chat") == 0 || strcmp(uri, "/chat/") == 0)) {
		chat_session_summary_t summaries[32];
		size_t count = list_sessions(summaries, 32);
		if (count > 0) {
			char loc[128];
			snprintf(loc, sizeof(loc), "/chat/%s", summaries[0].id);
			axil_header_set(fd, "Location", loc);
			axil_respond(fd, 303, "");
			return 0;
		}
		return render_chat_page(fd, NULL, NULL);
	}

	/* 2. POST /chat/new */
	if (is_post && strcmp(uri, "/chat/new") == 0) {
		chat_session_t s;
		memset(&s, 0, sizeof(s));
		generate_session_id(s.id, sizeof(s.id));
		strncpy(s.title, "New Conversation", sizeof(s.title) - 1);
		strncpy(s.model, "olmoe", sizeof(s.model) - 1);
		s.created_at = time(NULL);
		s.updated_at = s.created_at;
		chat_save_session(&s);

		char loc[128];
		snprintf(loc, sizeof(loc), "/chat/%s", s.id);
		axil_header_set(fd, "Location", loc);
		axil_respond(fd, 303, "");
		return 0;
	}

	/* 3. Parse /chat/:id and /chat/:id/:action */
	char sess_id[CHAT_ID_MAX] = {0};
	char action[64] = {0};
	if (parse_chat_route(uri, sess_id, sizeof(sess_id), action, sizeof(action)) != 0 || !sess_id[0]) {
		axil_respond(fd, 404, "Not Found");
		return 1;
	}

	/* WebSocket upgrade: GET /chat/:id with Sec-WebSocket-Key */
	if (!is_post && action[0] == '\0') {
		char ws_key[ENV_VALUE_LEN] = {0};
		if (axil_env_get(fd, ws_key, sizeof(ws_key), "HTTP_SEC_WEBSOCKET_KEY") == 0) {
			/* WebSocket upgrade requested — register session for this fd */
			if (g_ws_session_map) {
				corm_put(g_ws_session_map, &fd, (void *)sess_id);
			}
			/* WebSocket upgrade */
			if (axil_ws_upgrade(fd) < 0) {
				axil_respond(fd, 400, "WebSocket upgrade failed");
				return 1;
			}
			/* axil now routes frames through on_axil_parse */
			return 0;
		}
	}

	/* POST /chat/:id/delete */
	if (is_post && strcmp(action, "delete") == 0) {
		char path[512];
		get_session_file_path(sess_id, path, sizeof(path));
		unlink(path);
		axil_header_set(fd, "Location", "/chat");
		axil_respond(fd, 303, "");
		return 0;
	}

	/* POST /chat/:id/send or POST /chat/send */
	if (is_post && (strcmp(action, "send") == 0 || strcmp(sess_id, "send") == 0)) {
		char target_id[CHAT_ID_MAX] = {0};
		if (strcmp(sess_id, "send") != 0) {
			snprintf(target_id, sizeof(target_id), "%s", sess_id);
		}

		chat_session_t s;
		int is_new = 0;
		if (!target_id[0] || chat_get_session(target_id, &s) != 0) {
			memset(&s, 0, sizeof(s));
			generate_session_id(s.id, sizeof(s.id));
			strncpy(s.title, "New Conversation", sizeof(s.title) - 1);
			strncpy(s.model, "olmoe", sizeof(s.model) - 1);
			s.created_at = time(NULL);
			s.updated_at = s.created_at;
			is_new = 1;
		} else if (strcmp(s.model, "qwen") == 0) {
			strncpy(s.model, "olmoe", sizeof(s.model) - 1);
		}

		char prompt[CHAT_MSG_CONTENT_MAX] = {0};
		if (body && *body) {
			char *p = strstr(body, "prompt=");
			if (p) {
				p += 7;
				char *end = strchr(p, '&');
				if (end)
					snprintf(prompt, sizeof(prompt), "%.*s", (int)(end - p), p);
				else
					snprintf(prompt, sizeof(prompt), "%s", p);
				url_decode_inplace(prompt);
			}
		}

		if (!prompt[0]) {
			char loc[128];
			snprintf(loc, sizeof(loc), "/chat/%s", s.id);
			axil_header_set(fd, "Location", loc);
			axil_respond(fd, 303, "");
			return 0;
		}

		if (s.msg_count == 0 || is_new || strcmp(s.title, "New Conversation") == 0) {
			snprintf(s.title, sizeof(s.title), "%.*s", 32, prompt);
		}

		/* Append user message */
		if (s.msg_count < CHAT_MAX_MESSAGES) {
			chat_msg_t *um = &s.messages[s.msg_count++];
			um->role = CHAT_ROLE_USER;
			um->timestamp = time(NULL);
			snprintf(um->content, sizeof(um->content), "%s", prompt);
		}

		/* Append generating placeholder */
		if (s.msg_count < CHAT_MAX_MESSAGES) {
			chat_msg_t *gm = &s.messages[s.msg_count++];
			gm->role = CHAT_ROLE_ASSISTANT;
			gm->timestamp = time(NULL);
			snprintf(gm->content, sizeof(gm->content), "%s", GENERATING_SENTINEL);
		}

		s.updated_at = time(NULL);
		chat_save_session(&s);

		/* Launch background worker thread for Auren inference — NEVER BLOCKS AXIL */
		auren_worker_arg_t *warg = malloc(sizeof(*warg));
		if (warg) {
			snprintf(warg->session_id, sizeof(warg->session_id), "%s", s.id);
			pthread_t tid;
			if (pthread_create(&tid, NULL, auren_worker_thread, warg) == 0) {
				pthread_detach(tid);
			} else {
				free(warg);
			}
		}

		char loc[128];
		snprintf(loc, sizeof(loc), "/chat/%s", s.id);
		axil_header_set(fd, "Location", loc);
		axil_respond(fd, 303, "");
		return 0;
	}

	/* GET /chat/:id */
	if (!is_post && action[0] == '\0') {
		chat_session_t s;
		if (chat_get_session(sess_id, &s) != 0) {
			axil_respond(fd, 404, "Chat Session Not Found");
			return 1;
		}
		return render_chat_page(fd, &s, NULL);
	}

	axil_respond(fd, 404, "Not Found");
	return 1;
}

/* API Handlers (JSON) */
static int chat_api_sessions_handler(int fd, char *body)
{
	(void)body;
	chat_session_summary_t summaries[32];
	size_t count = list_sessions(summaries, 32);

	struct json_object *root = json_object_new_object();
	struct json_object *sessions_arr = json_object_new_array();

	for (size_t i = 0; i < count; i++) {
		struct json_object *s_obj = json_object_new_object();
		json_object_object_add(s_obj, "id", json_object_new_string(summaries[i].id));
		json_object_object_add(s_obj, "title", json_object_new_string(summaries[i].title));
		json_object_object_add(s_obj, "updated_at", json_object_new_int64(summaries[i].updated_at));
		json_object_object_add(s_obj, "msg_count", json_object_new_int64((int64_t)summaries[i].msg_count));
		json_object_array_add(sessions_arr, s_obj);
	}
	json_object_object_add(root, "sessions", sessions_arr);

	const char *json_out = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
	axil_header_set(fd, "Content-Type", "application/json");
	axil_respond(fd, 200, json_out);

	json_object_put(root);
	return 0;
}

/* Handler for WebSocket frames from chat clients */
XY_IMPL(int, on_axil_parse, socket_t, fd, unsigned char *, input, int, nread)
{
	/* Check if this is a WebSocket connection we own (not HTTP) */
	if (!(axil_flags(fd) & DF_WEBSOCKET))
		return nread;  /* Not our WS, let HTTP routing handle it */

	if (!input || nread <= 0)
		return 0;

	/* Try to parse as JSON command from client */
	char *frame_str = strndup((const char *)input, nread);
	if (!frame_str)
		return 0;

	struct json_object *cmd = json_tokener_parse(frame_str);
	free(frame_str);

	if (!cmd) {
		/* Silently ignore malformed JSON on WebSocket */
		return 0;
	}

	struct json_object *prompt_obj = NULL;
	const char *prompt = NULL;
	if (json_object_object_get_ex(cmd, "prompt", &prompt_obj)) {
		prompt = json_object_get_string(prompt_obj);
	}

	if (!prompt || !prompt[0]) {
		json_object_put(cmd);
		return 0;
	}

	/* Find or create session for this WS fd */
	char sess_id[CHAT_ID_MAX] = {0};
	void *sess_id_ptr = corm_get(g_ws_session_map, &fd);
	if (sess_id_ptr) {
		strncpy(sess_id, (const char *)sess_id_ptr, CHAT_ID_MAX - 1);
	}

	if (!sess_id[0]) {
		/* No session mapped to this WS yet — reject or create new */
		json_object_put(cmd);
		return 0;
	}

	/* Load or create session */
	chat_session_t s;
	if (chat_get_session(sess_id, &s) != 0) {
		memset(&s, 0, sizeof(s));
		strncpy(s.id, sess_id, sizeof(s.id) - 1);
		strncpy(s.title, "WebSocket Conversation", sizeof(s.title) - 1);
		strncpy(s.model, "olmoe", sizeof(s.model) - 1);
		s.created_at = time(NULL);
		s.updated_at = s.created_at;
	}

	/* Update title if first message */
	if (s.msg_count == 0 || strcmp(s.title, "WebSocket Conversation") == 0) {
		snprintf(s.title, sizeof(s.title), "%.*s", 32, prompt);
	}

	/* Append user message */
	if (s.msg_count < CHAT_MAX_MESSAGES) {
		chat_msg_t *um = &s.messages[s.msg_count++];
		um->role = CHAT_ROLE_USER;
		um->timestamp = time(NULL);
		snprintf(um->content, sizeof(um->content), "%s", prompt);
	}

	/* Append assistant placeholder (empty, will be filled by streaming) */
	if (s.msg_count < CHAT_MAX_MESSAGES) {
		chat_msg_t *am = &s.messages[s.msg_count++];
		am->role = CHAT_ROLE_ASSISTANT;
		am->timestamp = time(NULL);
		am->content[0] = '\0';
	}

	s.updated_at = time(NULL);
	chat_save_session(&s);

	json_object_put(cmd);

	/* Send acknowledgment back to client */
	struct json_object *ack = json_object_new_object();
	json_object_object_add(ack, "status", json_object_new_string("processing"));
	const char *ack_str = json_object_to_json_string_ext(ack, JSON_C_TO_STRING_PLAIN);
	axil_ws_write(fd, (const void *)ack_str, strlen(ack_str));
	json_object_put(ack);

	/* Launch streaming worker */
	typedef struct {
		socket_t ws_fd;
		char session_id[CHAT_ID_MAX];
	} ws_worker_arg_t;

	ws_worker_arg_t *warg = malloc(sizeof(*warg));
	if (warg) {
		warg->ws_fd = fd;
		strncpy(warg->session_id, s.id, sizeof(warg->session_id) - 1);
		/* Create streaming worker thread */
		pthread_t tid;
		if (pthread_create(&tid, NULL, ws_worker_thread, warg) == 0) {
			pthread_detach(tid);
		} else {
			free(warg);
		}
	}

	return 0;  /* Frame handled */
}

XY_MODULE_API void xy_install(void)
{
	xy_load("./mods/common/common");
	ensure_storage_dir();

	/* Initialize WebSocket session mapping (fd -> session_id) */
	g_ws_session_map = corm_open(NULL, NULL, CM_U32, CM_STR, 0x1F, 0);

	axil_register_handler("/chat", chat_route_handler);
	axil_register_handler("/chat/*", chat_route_handler);
	axil_register_handler("GET:/api/chat/sessions", chat_api_sessions_handler);
}
