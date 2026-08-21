#include <stdio.h>
#include <string.h>

#include <ttypt/axil.h>
#include <ttypt/xy-mod.h>

#include "common_internal.h"
#include "ux/site_ui.h"
#include "bud/bud_jsx.h"
#include "../mpfd/mpfd.h"
#include "../auth/auth.h"

XY_IMPL(int, parse_transpose_qs,
	const char *, qs,
	int *, transpose,
	int *, flags,
	int *, show_media);

int parse_transpose_qs(
        const char *qs, int *transpose, int *flags, int *show_media)
{
	*transpose = 0;
	*flags = 0;
	*show_media = 0;
	if (!qs || !*qs)
		return 0;
	char copy[1024];
	snprintf(copy, sizeof(copy), "%s", qs);
	axil_query_parse(copy);
	char buf[32];
	if (axil_query_param("t", buf, sizeof(buf)) > 0)
		*transpose = atoi(buf);
	if (axil_query_param("b", buf, sizeof(buf)) >= 0 && atoi(buf) != 0)
		*flags |= TPARAM_BEMOL;
	if (axil_query_param("l", buf, sizeof(buf)) >= 0 && atoi(buf) != 0)
		*flags |= TPARAM_LATIN;
	if (axil_query_param("h", buf, sizeof(buf)) >= 0 && atoi(buf) != 0)
		*flags |= TPARAM_HTML;
	if (axil_query_param("m", buf, sizeof(buf)) >= 0 && atoi(buf) != 0)
		*show_media = 1;
	return 0;
}

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
	        "frame-src https://www.youtube.com; "
	        "frame-ancestors 'none'");
}

XY_IMPL(int, respond_error, int, fd, int, status, const char *, msg)
{
	char accept[256] = { 0 };

	axil_header_get(fd, "Accept", accept, sizeof(accept));
	if (strstr(accept, "text/html")) {
		char status_str[16];
		char uri[512] = { 0 };
		char *html;
		bud_node *body;

		snprintf(status_str, sizeof(status_str), "%d", status);
		axil_env_get(fd, uri, sizeof(uri), "DOCUMENT_URI");

		body = site_ui_layout(
		        msg ? msg : status_str, uri, "!", get_request_user(fd),
		        NULL,
		        lx_el("p", lx_el("strong", lx_text(status_str)),
		              lx_text(" "), lx_text(msg ? msg : "Error"))
		                .data.node);

		html = site_ui_page(
		        msg ? msg : status_str, uri, "!", get_request_user(fd),
		        NULL, NULL, body);
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
		respond_error(fd, 401, "Unauthorized");
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
	return respond_html(
	        fd, site_ui_page(
	                    title, path, icon, user, extra_head, module, body));
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
	bud_node *page =
	        site_ui_form_page(user, title, action, icon, NULL, form);
	return site_ui_respond_page(
	        fd, title, action, icon, user, NULL, module, page);
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
