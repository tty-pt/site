#include <ttypt/ndx-mod.h>
#include <ttypt/ndc.h>

#include <string.h>

#include "../common/common.h"
#include "../index/index.h"
#define SSR_IMPL
#include "ssr.h"
#include "ssr_ffi.h"

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

	struct RenderResult result = ssr_render_item_ffi(
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
	const char *, modules_header)
{
	struct RenderRequest request = {
		.method          = method          ? method          : "",
		.path            = path            ? path            : "/",
		.query           = query           ? query           : "",
		.body            = (const unsigned char *)body,
		.body_len        = body_len,
		.remote_user     = remote_user     ? remote_user     : "",
		.modules_header  = modules_header  ? modules_header  : "",
	};
	struct RenderResult result = ssr_render_ffi(&request);

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
