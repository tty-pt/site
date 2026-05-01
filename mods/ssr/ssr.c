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

static void
fill_item_context(int fd, const char *id, struct ItemContext *ictx,
	struct ModuleEntryFfi *modules_snap, size_t *modules_len_out)
{
	static __thread char query[512];
	size_t i, modules_len;

	ndc_env_get(fd, query, "QUERY_STRING");
	modules_len = index_get_module_count(0);
	if (modules_len > 64) modules_len = 64;
	for (i = 0; i < modules_len; i++) {
		modules_snap[i].id    = index_get_module_id(i);
		modules_snap[i].title = index_get_module_title(i);
		modules_snap[i].flags = index_get_module_flags(i);
	}
	*modules_len_out = modules_len;

	ictx->id          = id;
	ictx->query       = query;
	ictx->remote_user = get_request_user(fd);
	ictx->modules     = modules_snap;
	ictx->modules_len = modules_len;
}

static int
dispatch_render_result(int fd, struct RenderResult result)
{
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

NDX_LISTENER(int, ssr_render_item,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, action,
	const char *, json)
{
	struct ModuleEntryFfi modules_snap[64];
	size_t modules_len;
	struct ItemContext ictx;
	fill_item_context(fd, id, &ictx, modules_snap, &modules_len);

	struct RenderItemRequest request = {
		.module      = module,
		.action      = action,
		.id          = id,
		.query       = ictx.query,
		.json        = json,
		.remote_user = ictx.remote_user,
		.modules     = modules_snap,
		.modules_len = modules_len,
	};
	return dispatch_render_result(fd, ssr_render_item_ffi(&request));
}

NDX_LISTENER(int, ssr_render_song_detail,
	int, fd,
	const struct SongItemFfi *, payload)
{
	struct ModuleEntryFfi modules_snap[64];
	size_t modules_len;
	struct ItemContext ictx;
	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");
	fill_item_context(fd, id, &ictx, modules_snap, &modules_len);
	return dispatch_render_result(fd, ssr_render_song_detail_ffi(payload, &ictx));
}

NDX_LISTENER(int, ssr_render_poem_detail,
	int, fd,
	const struct PoemItemFfi *, payload)
{
	struct ModuleEntryFfi modules_snap[64];
	size_t modules_len;
	struct ItemContext ictx;
	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");
	fill_item_context(fd, id, &ictx, modules_snap, &modules_len);
	return dispatch_render_result(fd, ssr_render_poem_detail_ffi(payload, &ictx));
}

NDX_LISTENER(int, ssr_render_poem_edit,
	int, fd,
	const struct PoemItemFfi *, payload)
{
	struct ModuleEntryFfi modules_snap[64];
	size_t modules_len;
	struct ItemContext ictx;
	char id[128] = {0};
	ndc_env_get(fd, id, "PATTERN_PARAM_ID");
	fill_item_context(fd, id, &ictx, modules_snap, &modules_len);
	return dispatch_render_result(fd, ssr_render_poem_edit_ffi(payload, &ictx));
}

NDX_LISTENER(int, ssr_render_delete,
	int, fd,
	const char *, module,
	const char *, id,
	const char *, title)
{
	struct ModuleEntryFfi modules_snap[64];
	size_t modules_len;
	struct ItemContext ictx;
	struct DeleteItemFfi payload = { .title = title };
	fill_item_context(fd, id, &ictx, modules_snap, &modules_len);
	return dispatch_render_result(fd, ssr_render_delete_ffi(module, &payload, &ictx));
}

NDX_LISTENER(int, ssr_render,
	int, fd,
	const char *, method,
	const char *, path,
	const char *, query,
	const char *, body,
	size_t, body_len,
	const char *, remote_user)
{
	ModuleEntryFfi modules_snap[64];
	size_t i, modules_len = index_get_module_count(0);
	if (modules_len > 64) modules_len = 64;
	for (i = 0; i < modules_len; i++) {
		modules_snap[i].id    = index_get_module_id(i);
		modules_snap[i].title = index_get_module_title(i);
		modules_snap[i].flags = index_get_module_flags(i);
	}

	struct RenderRequest request = {
		.method      = method      ? method      : "",
		.path        = path        ? path        : "/",
		.query       = query       ? query       : "",
		.body        = (const unsigned char *)body,
		.body_len    = body_len,
		.remote_user = remote_user ? remote_user : "",
		.modules     = modules_snap,
		.modules_len = modules_len,
	};
	return dispatch_render_result(fd, ssr_render_ffi(&request));
}

void
ndx_install(void)
{
	ndc_register_handler("GET:/", ssr_get_handler);
	ndc_register_handler("GET:/auth/login", ssr_get_handler);
	ndc_register_handler("GET:/auth/register", ssr_get_handler);
}
