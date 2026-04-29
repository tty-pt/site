#include <ttypt/ndx-mod.h>
#include <ttypt/ndc.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../common/common.h"
#include "../index/index.h"
#define SSR_IMPL
#include "ssr.h"

struct render_request {
	const char *method;
	const char *path;
	const char *query;
	const unsigned char *body;
	size_t body_len;
	const char *remote_user;
	const char *forwarded_host;
	const char *modules_header;
};

struct render_result {
	uint16_t status;
	char *content_type;
	char *location;
	char *body;
};

extern struct render_result ssr_render_ffi(const struct render_request *request);
extern struct render_result ssr_render_item_ffi(
	const char *module,
	const char *action,
	const char *id,
	const char *query,
	const char *json,
	const char *remote_user,
	const char *modules_header);
extern void ssr_free_result_ffi(struct render_result *result);

static int
ssr_get_handler(int fd, char *body)
{
	return core_get(fd, body);
}

NDX_LISTENER(int, ssr_render_item,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, action,
	const char *, json)
{
	char query[512] = {0};
	ndc_env_get(fd, query, "QUERY_STRING");

	struct render_result result = ssr_render_item_ffi(
		module, action, id, query, json,
		get_request_user(fd),
		index_get_modules_header(0));

	if (result.location && result.location[0]) {
		ndc_header_set(fd, "Location", result.location);
		ndc_respond(fd, result.status ? result.status : 303, "");
		ssr_free_result_ffi(&result);
		return 0;
	}

	ndc_header_set(fd, "Content-Type",
		result.content_type ? result.content_type : "text/html; charset=utf-8");
	ndc_respond(fd, result.status ? result.status : 200,
		result.body ? result.body : "");
	ssr_free_result_ffi(&result);
	return 0;
}

NDX_LISTENER(int, ssr_render,
	int, fd,
	const char *, method,
	const char *, path,
	const char *, query,
	const char *, body,
	size_t, body_len,
	const char *, remote_user,
	const char *, forwarded_host,
	const char *, modules_header)
{
	struct render_request request = {
		.method = method ? method : "",
		.path = path ? path : "/",
		.query = query ? query : "",
		.body = (const unsigned char *)body,
		.body_len = body_len,
		.remote_user = remote_user ? remote_user : "",
		.forwarded_host = forwarded_host ? forwarded_host : "",
		.modules_header = modules_header ? modules_header : "",
	};
	struct render_result result = ssr_render_ffi(&request);

	if (result.location && result.location[0]) {
		ndc_header_set(fd, "Location", result.location);
		ndc_respond(fd, result.status ? result.status : 303, "");
		ssr_free_result_ffi(&result);
		return 0;
	}

	ndc_header_set(fd, "Content-Type",
		result.content_type ? result.content_type : "text/html; charset=utf-8");
	ndc_respond(fd, result.status ? result.status : 200,
		result.body ? result.body : "");
	ssr_free_result_ffi(&result);
	return 0;
}

void
ndx_install(void)
{
	ndc_register_handler("GET:/", ssr_get_handler);
	ndc_register_handler("GET:/auth/login", ssr_get_handler);
	ndc_register_handler("GET:/auth/register", ssr_get_handler);
}
