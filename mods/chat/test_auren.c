#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include "chat-types.h"

static void json_escape(const char *src, char *dst, size_t dst_len)
{
	size_t di = 0;
	for (size_t si = 0; src[si] && di + 4 < dst_len; si++) {
		unsigned char c = (unsigned char)src[si];
		if (c == '"') {
			dst[di++] = '\\';
			dst[di++] = '"';
		} else if (c == '\\') {
			dst[di++] = '\\';
			dst[di++] = '\\';
		} else if (c == '\n') {
			dst[di++] = '\\';
			dst[di++] = 'n';
		} else if (c == '\r') {
			dst[di++] = '\\';
			dst[di++] = 'r';
		} else if (c == '\t') {
			dst[di++] = '\\';
			dst[di++] = 't';
		} else {
			dst[di++] = (char)c;
		}
	}
	dst[di] = '\0';
}

static int auren_chat_completion(
        const chat_session_t *s,
        char *resp_out,
        size_t resp_out_max)
{
	const char *host = "127.0.0.1";
	int port = 4242;

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) return -1;

	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons((uint16_t)port);
	inet_pton(AF_INET, host, &sin.sin_addr);

	if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		close(sock);
		return -2;
	}

	size_t payload_cap = 65536;
	char *payload = malloc(payload_cap);
	const char *model = s->model[0] ? s->model : "olmoe";
	int written = snprintf(payload, payload_cap, "{\"model\":\"%s\",\"messages\":[", model);

	for (size_t i = 0; i < s->msg_count; i++) {
		const chat_msg_t *m = &s->messages[i];
		const char *rstr = "user";
		if (m->role == CHAT_ROLE_ASSISTANT) rstr = "assistant";
		else if (m->role == CHAT_ROLE_SYSTEM) rstr = "system";

		char esc_msg[CHAT_MSG_CONTENT_MAX * 2];
		json_escape(m->content, esc_msg, sizeof(esc_msg));

		int add = snprintf(payload + written, payload_cap - (size_t)written,
		                   "%s{\"role\":\"%s\",\"content\":\"%s\"}",
		                   i ? "," : "", rstr, esc_msg);
		written += add;
	}
	written += snprintf(payload + written, payload_cap - (size_t)written, "],\"max_tokens\":15,\"stream\":false}");

	char req_header[1024];
	snprintf(req_header, sizeof(req_header),
	         "POST /v1/chat/completions HTTP/1.1\r\n"
	         "Host: %s:%d\r\n"
	         "User-Agent: axil-chat-test/1.0\r\n"
	         "Content-Type: application/json\r\n"
	         "Content-Length: %zu\r\n"
	         "Connection: close\r\n\r\n",
	         host, port, strlen(payload));

	send(sock, req_header, strlen(req_header), 0);
	send(sock, payload, strlen(payload), 0);
	free(payload);

	size_t resp_cap = 65536;
	char *raw_resp = malloc(resp_cap);
	size_t total_recv = 0;
	ssize_t n;
	while ((n = recv(sock, raw_resp + total_recv, resp_cap - 1 - total_recv, 0)) > 0) {
		total_recv += (size_t)n;
	}
	raw_resp[total_recv] = '\0';
	close(sock);

	char *body = strstr(raw_resp, "\r\n\r\n");
	if (!body) body = strstr(raw_resp, "\n\n");
	if (!body) {
		free(raw_resp);
		return -4;
	}
	body += (body[0] == '\r') ? 4 : 2;

	char *content_marker = strstr(body, "\"content\":");
	if (!content_marker) {
		free(raw_resp);
		return -5;
	}
	char *val_start = strchr(content_marker + 10, '"');
	if (!val_start) {
		free(raw_resp);
		return -5;
	}
	val_start++;

	size_t out_idx = 0;
	for (char *p = val_start; *p && out_idx + 1 < resp_out_max; p++) {
		if (*p == '"') break;
		if (*p == '\\' && *(p + 1)) {
			p++;
			if (*p == 'n') resp_out[out_idx++] = '\n';
			else if (*p == 'r') resp_out[out_idx++] = '\r';
			else if (*p == 't') resp_out[out_idx++] = '\t';
			else if (*p == '"') resp_out[out_idx++] = '"';
			else if (*p == '\\') resp_out[out_idx++] = '\\';
			else resp_out[out_idx++] = *p;
		} else {
			resp_out[out_idx++] = *p;
		}
	}
	resp_out[out_idx] = '\0';
	free(raw_resp);
	return 0;
}

int main(void)
{
	printf("[test] Testing active Auren daemon connection on port 4242...\n");

	chat_session_t s;
	memset(&s, 0, sizeof(s));
	strncpy(s.model, "olmoe", sizeof(s.model) - 1);
	s.msg_count = 1;
	s.messages[0].role = CHAT_ROLE_USER;
	strncpy(s.messages[0].content, "Reply with 'AUREN_ACTIVE_OK' only.", sizeof(s.messages[0].content) - 1);

	char resp[1024] = {0};
	int rc = auren_chat_completion(&s, resp, sizeof(resp));
	if (rc == -2) {
		printf("[test] Auren not reachable on 4242 (skipped live query)\n");
		return 0;
	}
	assert(rc == 0);
	printf("[test] Auren response received: '%s'\n", resp);
	assert(strlen(resp) > 0);
	printf("[test] Active Auren connection PASS\n");
	return 0;
}
