#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <json-c/json.h>

#include <ttypt/xy-mod.h>
#include <ttypt/axil.h>

#include "../common/common.h"
#include "auren-types.h"
#include "ux/auren_ui.h"

static int render_auren_page(int fd, const auren_demo_state_t *st)
{
	char user[ENV_VALUE_LEN] = {0};
	axil_env_get(fd, user, sizeof(user), "REMOTE_USER");

	bud_node *auren_view = auren_ui_render_view(st);

	char extra_head[1024];
	snprintf(extra_head, sizeof(extra_head),
	         "<link rel=\"stylesheet\" href=\"/auren.css\">\n"
	         "<script type=\"module\" src=\"/auren-client.js\"></script>\n");

	return site_ui_respond_page(
	        fd, "Auren Browser LLM", "/auren", "🧠", user, extra_head, "auren", auren_view);
}

static int auren_generate_api_handler(int fd, char *body)
{
	if (!body || !body[0]) {
		axil_respond(fd, 400, "{\"error\":\"Missing request body\"}");
		return 1;
	}

	struct json_object *req = json_tokener_parse(body);
	if (!req) {
		axil_respond(fd, 400, "{\"error\":\"Invalid JSON\"}");
		return 1;
	}

	const char *prompt_str = "";
	struct json_object *p_obj = NULL;
	if (json_object_object_get_ex(req, "prompt", &p_obj) && p_obj) {
		prompt_str = json_object_get_string(p_obj);
	}

	/* Connect to local Auren daemon on 127.0.0.1:4242 */
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		json_object_put(req);
		axil_respond(fd, 502, "{\"error\":\"Socket creation failed\"}");
		return 1;
	}

	struct sockaddr_in saddr;
	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(4242);
	inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);

	if (connect(sock, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
		close(sock);
		json_object_put(req);
		axil_respond(fd, 502, "{\"error\":\"Auren daemon unreachable on port 4242\"}");
		return 1;
	}

	/* Build chat payload for Auren daemon */
	struct json_object *auren_req = json_object_new_object();
	json_object_object_add(auren_req, "model", json_object_new_string("qwen"));
	struct json_object *msgs_arr = json_object_new_array();
	struct json_object *user_msg = json_object_new_object();
	json_object_object_add(user_msg, "role", json_object_new_string("user"));
	json_object_object_add(user_msg, "content", json_object_new_string(prompt_str ? prompt_str : ""));
	json_object_array_add(msgs_arr, user_msg);
	json_object_object_add(auren_req, "messages", msgs_arr);

	const char *payload = json_object_to_json_string_ext(auren_req, JSON_C_TO_STRING_PLAIN);
	size_t payload_len = strlen(payload);

	char http_req[1024];
	snprintf(http_req, sizeof(http_req),
	         "POST /v1/chat/completions HTTP/1.1\r\n"
	         "Host: 127.0.0.1:4242\r\n"
	         "Content-Type: application/json\r\n"
	         "Content-Length: %zu\r\n"
	         "Connection: close\r\n\r\n",
	         payload_len);

	send(sock, http_req, strlen(http_req), 0);
	send(sock, payload, payload_len, 0);
	json_object_put(auren_req);
	json_object_put(req);

	/* Read HTTP response */
	FILE *fp = fdopen(sock, "r");
	if (!fp) {
		close(sock);
		axil_respond(fd, 502, "{\"error\":\"Socket stream error\"}");
		return 1;
	}

	char line[4096];
	int in_body = 0;
	char resp_buf[16384] = {0};
	size_t resp_len = 0;

	while (fgets(line, sizeof(line), fp)) {
		if (!in_body) {
			if (strcmp(line, "\r\n") == 0 || strcmp(line, "\n") == 0) {
				in_body = 1;
			}
			continue;
		}
		size_t llen = strlen(line);
		if (resp_len + llen < sizeof(resp_buf) - 1) {
			memcpy(resp_buf + resp_len, line, llen);
			resp_len += llen;
			resp_buf[resp_len] = '\0';
		}
	}
	fclose(fp);

	/* Extract assistant content from JSON */
	const char *answer = "";
	struct json_object *res_json = json_tokener_parse(resp_buf);
	if (res_json) {
		struct json_object *choices;
		if (json_object_object_get_ex(res_json, "choices", &choices) &&
		    json_object_array_length(choices) > 0)
		{
			struct json_object *c0 = json_object_array_get_idx(choices, 0);
			struct json_object *msg_obj;
			if (json_object_object_get_ex(c0, "message", &msg_obj)) {
				struct json_object *cnt;
				if (json_object_object_get_ex(msg_obj, "content", &cnt)) {
					answer = json_object_get_string(cnt);
				}
			}
		}
	}

	struct json_object *out_obj = json_object_new_object();
	json_object_object_add(out_obj, "response", json_object_new_string(answer ? answer : ""));
	const char *out_str = json_object_to_json_string_ext(out_obj, JSON_C_TO_STRING_PLAIN);

	axil_header_set(fd, "Content-Type", "application/json");
	axil_respond(fd, 200, out_str);
	json_object_put(out_obj);
	if (res_json) json_object_put(res_json);

	return 0;
}

static int auren_route_handler(int fd, char *body)
{
	(void)body;
	char uri[1024] = {0};
	char method[32] = {0};
	axil_env_get(fd, uri, sizeof(uri), "DOCUMENT_URI");
	axil_env_get(fd, method, sizeof(method), "REQUEST_METHOD");

	char *q = strchr(uri, '?');
	if (q) *q = '\0';

	if (strcmp(uri, "/auren/") == 0) {
		axil_header_set(fd, "Location", "/auren");
		axil_respond(fd, 301, "");
		return 0;
	}

	auren_demo_state_t st;
	memset(&st, 0, sizeof(st));
	snprintf(st.model, sizeof(st.model), "smollm2-135m-instruct");
	snprintf(st.status, sizeof(st.status), "ready");
	st.webgpu_available = 1;
	st.cached = 0;
	st.percent = 100;
	st.streaming = 0;
	st.msg_count = 0;

	return render_auren_page(fd, &st);
}

XY_MODULE_API void xy_install(void)
{
	xy_load("./mods/common/common");
	axil_register_handler("/auren", auren_route_handler);
	axil_register_handler("/auren/", auren_route_handler);
	axil_register_handler("POST:/auren/generate", auren_generate_api_handler);
}
