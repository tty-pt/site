#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <limits.h>

#include <ttypt/axil.h>
#include <ttypt/xy-mod.h>

#include "common_internal.h"
#include <hyle/schema.h>
#include "hyle-bud/hyle-bud.h"
#include "ux/site_ui.h"
#include "bud/bud_jsx.h"
#include "../mpfd/mpfd.h"
#include "../auth/auth.h"

static void set_html_security_headers(int fd)
{
	axil_header_set(fd, "X-Content-Type-Options", "nosniff");
	axil_header_set(fd, "X-Frame-Options", "DENY");
	axil_header_set(
	        fd, "Referrer-Policy", "strict-origin-when-cross-origin");
	axil_header_set(
	        fd, "Content-Security-Policy",
	        "default-src 'self'; "
	        "script-src 'self' 'wasm-unsafe-eval'; "
	        "style-src 'self' 'unsafe-inline'; "
	        "img-src 'self' data:; "
	        "media-src 'self'; "
	        "frame-src 'self' https://www.youtube.com; "
	        "frame-ancestors 'none'");
}

XY_IMPL(int, respond_error, int, fd, int, status, const char *, msg)
{
	char accept[256] = { 0 };

	axil_header_get(fd, "Accept", accept, sizeof(accept));
	if (strstr(accept, "text/html") || strstr(accept, "*/*") || accept[0] == '\0') {
		char status_str[16];
		char msg_with_space[256];
		char uri[512] = { 0 };
		char *html;
		bud_node *body;

		snprintf(status_str, sizeof(status_str), "%d", status);
		const char *lang = i18n_resolve_locale(fd);
		site_ui_set_locale(lang);
		const char *trans_msg = msg ? ui_t(msg) : ui_t("Error");
		snprintf(
		        msg_with_space, sizeof(msg_with_space), " %s",
		        trans_msg);
		axil_env_get(fd, uri, sizeof(uri), "DOCUMENT_URI");

		body = site_ui_layout(
		        trans_msg, uri, "!", get_request_user(fd),
		        NULL,
		        bud_tpl("<p><strong>%s</strong>%s</p>", status_str,
		                msg_with_space));

		html = site_ui_page_lang(
		        trans_msg, uri, "!", get_request_user(fd),
		        NULL, NULL, body, lang);
		if (html) {
			set_html_security_headers(fd);
			axil_header_set(
			        fd, "Content-Type", "text/html; charset=utf-8");
			axil_respond(fd, status, html);
			bud_free_string(html);
			return 1;
		}
	}

	return axil_respond_plain(fd, status, msg);
}

XY_IMPL(int, bad_request, int, fd, const char *, msg)
{
	return respond_error(fd, 400, msg ? msg : "Bad request");
}

XY_IMPL(int, server_error, int, fd, const char *, msg)
{
	return respond_error(fd, 500, msg ? msg : "Internal server error");
}

XY_IMPL(int, not_found, int, fd, const char *, msg)
{
	return respond_error(fd, 404, msg ? msg : "Not found");
}

XY_IMPL(const char *, require_user, int, fd)
{
	const char *user = get_request_user(fd);
	if (!user || !user[0]) {
		require_login(fd, user);
		return NULL;
	}
	return user;
}

XY_IMPL(int, respond_html, int, fd, const char *, html)
{
	if (html) {
		set_html_security_headers(fd);
		axil_header_set(fd, "Content-Type", "text/html; charset=utf-8");
		axil_respond(fd, 200, html);
		bud_free_string((char *)html);
		return 0;
	}
	return server_error(fd, "Internal Server Error");
}

XY_IMPL(int, respond_json, int, fd, int, status, const char *, msg)
{
	axil_header_set(fd, "Content-Type", "application/json");
	axil_respond(fd, status, msg);
	return 1;
}

XY_IMPL(int, redirect_to_item,
	int, fd,
	const char *, module,
	const char *, id)
{
	char loc[256];
	snprintf(loc, sizeof(loc), "/%s/%s", module, id);
	return axil_redirect(fd, loc);
}

XY_IMPL(int, respond_item_file,
	int, fd,
	const char *, item_path,
	const char *, filename,
	const char *, allowed_exts)
{
	char full_path[PATH_MAX];
	char stem[256];
	size_t flen, slen = 0;
	const char *dot;

	if (!item_path || !filename || !filename[0])
		return respond_error(fd, 404, "Not found");

	dot = strrchr(filename, '.');
	flen = strlen(filename);
	slen = dot ? (size_t)(dot - filename) : flen;
	if (slen >= sizeof(stem))
		return respond_error(fd, 404, "Not found");
	memcpy(stem, filename, slen);
	stem[slen] = '\0';
	if (!is_safe_id(stem))
		return respond_error(fd, 404, "Not found");

	item_child_path(item_path, filename, full_path, sizeof(full_path));
	return axil_respond_file(fd, full_path, allowed_exts);
}

XY_IMPL(int, site_ui_respond_item_detail,
	int, fd,
	const item_ctx_t *, ctx,
	const char *, module,
	const char *, title,
	bud_node *, body)
{
	char path[256];
	char page_title[512];
	int is_owner;
	bud_node *layout;
	const char *lang;

	if (!ctx || !module || !body)
		return server_error(fd, "Invalid detail context");

	lang = i18n_resolve_locale(fd);
	site_ui_set_locale(lang);

	is_owner = (ctx->username && ctx->username[0])
	                   ? item_owner_check(ctx->item_path, ctx->username)
	                   : 0;

	snprintf(path, sizeof(path), "/%s/%s", module, ctx->id);
	if (title && title[0])
		snprintf(
		        page_title, sizeof(page_title), "%s: %s", module,
		        title);
	else
		snprintf(
		        page_title, sizeof(page_title), "%s: %s", module,
		        ctx->id);

	layout = site_ui_layout(
	        page_title, path, site_ui_module_icon(module), ctx->username,
	        site_ui_item_menu(module, ctx->id, is_owner), body);

	return site_ui_respond_page(
	        fd, page_title, path, site_ui_module_icon(module),
	        ctx->username, NULL, module, layout);
}

XY_IMPL(int, site_ui_respond_page,
	int, fd,
	const char *, title,
	const char *, path,
	const char *, icon,
	const char *, user,
	const char *, extra_head,
	const char *, module,
	bud_node *, body)
{
	const char *lang = i18n_resolve_locale(fd);
	site_ui_set_locale(lang);
	return respond_html(
	        fd, site_ui_page_lang(
	                    title, path, icon, user, extra_head, module, body, lang));
}

XY_IMPL(int, site_ui_respond_form_page,
	int, fd,
	const char *, user,
	const char *, title,
	const char *, action,
	const char *, icon,
	const char *, module,
	bud_node *, form)
{
	bud_node *page = site_ui_form_page(
	        user, title, action, icon, NULL, SITE_UI_FRAGMENTS_SCRIPT,
	        form);
	return site_ui_respond_page(
	        fd, title, action, icon, user, SITE_UI_FRAGMENTS_SCRIPT, module,
	        page);
}

XY_IMPL(int, csrf_check_mpfd, int, fd)
{
	char csrf_submitted[33] = { 0 };
	mpfd_get("csrf_token", csrf_submitted, sizeof(csrf_submitted));
	if (csrf_validate(fd, csrf_submitted))
		return respond_error(fd, 403, "Forbidden");
	return 0;
}

XY_IMPL(int, csrf_check_query, int, fd, char *, body)
{
	axil_query_parse(body);
	char csrf_submitted[33] = { 0 };
	axil_query_param("csrf_token", csrf_submitted, sizeof(csrf_submitted));
	if (csrf_validate(fd, csrf_submitted))
		return respond_error(fd, 403, "Forbidden");
	return 0;
}

XY_IMPL(const char *, csrf_setup, int, fd)
{
	static __thread char csrf_token[33];
	csrf_set_cookie(fd, csrf_token, sizeof(csrf_token));
	return csrf_token;
}

XY_IMPL(int, site_ui_respond_add_page,
	int, fd,
	const char *, user,
	const char *, module,
	const char *, icon,
	bud_node *, form)
{
	char title[64], action[256];
	snprintf(
	        title, sizeof(title), "Add %s", site_ui_module_display(module));
	snprintf(action, sizeof(action), "/%s/add", module);
	return site_ui_respond_form_page(
	        fd, user, title, action, icon, module, form);
}

XY_IMPL(int, site_ui_respond_isomorphic,
	int, fd,
	const item_ctx_t *, ctx,
	const char *, module,
	const char *, title,
	const char *, state_json,
	const char *, wasm_module,
	bud_node *, body)
{
	char path[256];
	char page_title[512];
	char state_script[16384];

	if (!ctx || !module || !body)
		return server_error(fd, "Invalid isomorphic context");

	snprintf(path, sizeof(path), "/%s/%s", module, ctx->id);
	if (title && title[0])
		snprintf(
		        page_title, sizeof(page_title), "%s: %s", module,
		        title);
	else
		snprintf(
		        page_title, sizeof(page_title), "%s: %s", module,
		        ctx->id);

	if (state_json && state_json[0])
		snprintf(
		        state_script, sizeof(state_script),
		        "<script type=\"application/json\" "
		        "id=\"bud-state\">%s</script>",
		        state_json);
	else
		state_script[0] = '\0';

	return site_ui_respond_page(
	        fd, page_title, path, site_ui_module_icon(module),
	        ctx->username ? ctx->username : "",
	        state_script[0] ? state_script : NULL,
	        wasm_module ? wasm_module : module, body);
}

XY_IMPL(int, site_entity_register, const site_entity_def_t *, def)
{
	if (!def || !def->name)
		return -1;
	return 0;
}

XY_IMPL(int, site_ui_respond_edit_page,
	int, fd,
	const char *, user,
	const char *, module,
	const char *, icon,
	const char *, title,
	const char *, id,
	bud_node *, form)
{
	char page_title[256], action[256];
	snprintf(page_title, sizeof(page_title), "Edit %s", title);
	snprintf(action, sizeof(action), "/%s/%s/edit", module, id);
	return site_ui_respond_form_page(
	        fd, user, page_title, action, icon, module, form);
}

XY_IMPL(int, register_standard_item_handlers,
	const char *, module_name,
	const standard_item_handlers_t *, handlers)
{
	char buf[256];

	if (handlers->add_get) {
		snprintf(buf, sizeof(buf), "GET:/%s/add", module_name);
		axil_register_handler(buf, handlers->add_get);
	}
	if (handlers->add_post) {
		snprintf(buf, sizeof(buf), "POST:/%s/add", module_name);
		axil_register_handler(buf, handlers->add_post);
	}
	if (handlers->detail) {
		snprintf(buf, sizeof(buf), "GET:/%s/:id", module_name);
		axil_register_handler(buf, handlers->detail);
	}
	if (handlers->edit_get) {
		snprintf(buf, sizeof(buf), "GET:/%s/:id/edit", module_name);
		axil_register_handler(buf, handlers->edit_get);
	}
	if (handlers->edit_post) {
		snprintf(buf, sizeof(buf), "POST:/%s/:id/edit", module_name);
		axil_register_handler(buf, handlers->edit_post);
	}
	return 0;
}
