#include <ttypt/ndx-mod.h>
#include <ttypt/ndc.h>

#include <string.h>

#include "../common/common.h"
#include "../index/index.h"
#define SSR_IMPL
#include "ssr.h"
#include "ssr_ffi.h"

static int
ssr_get_handler(int fd, char *body) {
	return core_get(fd, body);
}

static int
dispatch_render_result(int fd, struct RenderResult result) {
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

NDX_LISTENER(int, ssr_render_song_detail,
             int, fd,
             const struct SongDetailRenderFfi *, req) {
	return dispatch_render_result(fd, ssr_render_song_detail_ffi(req));
}

NDX_LISTENER(int, ssr_render_poem_detail,
             int, fd,
             const struct PoemRenderFfi *, req) {
	return dispatch_render_result(fd, ssr_render_poem_detail_ffi(req));
}

NDX_LISTENER(int, ssr_render_poem_edit,
             int, fd,
             const struct PoemRenderFfi *, req) {
	return dispatch_render_result(fd, ssr_render_poem_edit_ffi(req));
}

NDX_LISTENER(int, ssr_render_delete,
             int, fd,
             const char *, module,
             const char *, id,
             const char *, title) {
	static __thread char query[512];
	struct ModuleEntryFfi modules_snap[64];
	size_t modules_len;
	ndc_env_get(fd, query, "QUERY_STRING");
	SSR_FILL_MODULES(modules_snap, modules_len);
	struct DeleteRenderFfi req = {
	    .module = module,
	    .title = title,
	    .id = id,
	    .query = query,
	    .remote_user = get_request_user(fd),
	    .modules = modules_snap,
	    .modules_len = modules_len,
	};
	return dispatch_render_result(fd, ssr_render_delete_ffi(&req));
}

NDX_LISTENER(int, ssr_render_songbook_detail,
             int, fd,
             const struct SongbookDetailRenderFfi *, req) {
	return dispatch_render_result(fd, ssr_render_songbook_detail_ffi(req));
}

NDX_LISTENER(int, ssr_render_choir_detail,
             int, fd,
             const struct ChoirDetailRenderFfi *, req) {
	return dispatch_render_result(fd, ssr_render_choir_detail_ffi(req));
}

NDX_LISTENER(int, ssr_render,
             int, fd,
             const char *, method,
             const char *, path,
             const char *, query,
             const char *, body,
             size_t, body_len,
             const char *, remote_user) {
	struct ModuleEntryFfi modules_snap[64];
	size_t modules_len;
	SSR_FILL_MODULES(modules_snap, modules_len);
	struct PageRenderFfi req = {
	    .method = method ? method : "",
	    .path = path ? path : "/",
	    .query = query ? query : "",
	    .body = (const unsigned char *)body,
	    .body_len = body_len,
	    .remote_user = remote_user ? remote_user : "",
	    .modules = modules_snap,
	    .modules_len = modules_len,
	};
	return dispatch_render_result(fd, ssr_render_page_ffi(&req));
}

void ndx_install(void) {
	ndc_register_handler("GET:/", ssr_get_handler);
	ndc_register_handler("GET:/auth/login", ssr_get_handler);
	ndc_register_handler("GET:/auth/register", ssr_get_handler);
}
