#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

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
	st.percent = 0;
	st.streaming = 0;
	st.msg_count = 0;

	return render_auren_page(fd, &st);
}

XY_MODULE_API void xy_install(void)
{
	xy_load("./mods/common/common");
	axil_register_handler("/auren", auren_route_handler);
	axil_register_handler("/auren/", auren_route_handler);
}
