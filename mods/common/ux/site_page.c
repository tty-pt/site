#ifndef SITE_PAGE_C
#define SITE_PAGE_C

#include "site_ui.h"
#include "../common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bud/bud_app.h"

static size_t escape_html_into(const char *src, char *dst, size_t dstsize)
{
	size_t w = 0;
	const char *ent;
	size_t elen;

	if (!src)
		src = "";
	while (*src && w < dstsize - 1) {
		switch (*src) {
		case '&':
			ent = "&amp;";
			elen = 5;
			break;
		case '<':
			ent = "&lt;";
			elen = 4;
			break;
		case '>':
			ent = "&gt;";
			elen = 4;
			break;
		case '"':
			ent = "&quot;";
			elen = 6;
			break;
		default:
			dst[w++] = *src++;
			continue;
		}
		if (w + elen >= dstsize)
			break;
		memcpy(dst + w, ent, elen);
		w += elen;
		src++;
	}
	dst[w] = '\0';
	return w;
}

static size_t
escape_json_script_into(const char *src, char *dst, size_t dstsize)
{
	static const char hex[] = "0123456789abcdef";
	size_t w;
	unsigned char c;

	if (!src)
		src = "";
	w = 0;
	while (*src && w + 1 < dstsize) {
		c = (unsigned char)*src++;
		if (c == '"' || c == '\\') {
			if (w + 2 >= dstsize)
				break;
			dst[w++] = '\\';
			dst[w++] = (char)c;
		} else if (c == '\n' || c == '\r' || c == '\t') {
			if (w + 2 >= dstsize)
				break;
			dst[w++] = '\\';
			dst[w++] = c == '\n' ? 'n' : (c == '\r' ? 'r' : 't');
		} else if (c < 0x20 || c == '<') {
			if (w + 6 >= dstsize)
				break;
			dst[w++] = '\\';
			dst[w++] = 'u';
			dst[w++] = '0';
			dst[w++] = '0';
			dst[w++] = hex[c >> 4];
			dst[w++] = hex[c & 15];
		} else {
			dst[w++] = (char)c;
		}
	}
	dst[w] = '\0';
	return w;
}

char *site_ui_page(
        const char *title, const char *path, const char *icon, const char *user,
        const char *extra_head, const char *module, bud_node *body)
{
	char *body_html;
	char *chrome_html;
	char *page;
	char module_attr[320];
	char title_esc[512];
	char state_title[SITE_CHROME_TITLE_MAX * 6];
	char state_path[SITE_CHROME_PATH_MAX * 6];
	char state_icon[SITE_CHROME_ICON_MAX * 6];
	char state_user[SITE_CHROME_USER_MAX * 6];
	site_ui_chrome_state state;
	bud_node *chrome;
	int len;

#if __has_include("version.gen.h")
#include "version.gen.h"
#else
#define SITE_CSS_V "?v=22"
#define SITE_CLIENT_V "?v=1"
#define SITE_FRAGMENTS_V "?v=1"
#endif

	if (!body)
		return NULL;
	body_html = bud_render_hydrated_html(body);
	if (!body_html)
		return NULL;
	memset(&state, 0, sizeof(state));
	snprintf(state.title, sizeof(state.title), "%s", title ? title : "");
	snprintf(state.path, sizeof(state.path), "%s", path ? path : "");
	snprintf(state.icon, sizeof(state.icon), "%s", icon ? icon : "");
	snprintf(state.user, sizeof(state.user), "%s", user ? user : "");
	chrome = site_ui_chrome(&state);
	chrome_html = chrome ? bud_render_hydrated_html(chrome) : NULL;
	if (!chrome_html) {
		bud_free_string(body_html);
		return NULL;
	}

	escape_html_into(title, title_esc, sizeof(title_esc));
	escape_json_script_into(state.title, state_title, sizeof(state_title));
	escape_json_script_into(state.path, state_path, sizeof(state_path));
	escape_json_script_into(state.icon, state_icon, sizeof(state_icon));
	escape_json_script_into(state.user, state_user, sizeof(state_user));

	if (module && module[0]) {
		snprintf(
		        module_attr, sizeof(module_attr),
		        " data-modules=\"site_chrome %s\"", module);
	} else {
		snprintf(
		        module_attr, sizeof(module_attr),
		        " data-modules=\"site_chrome\"");
	}

	if (!extra_head)
		extra_head = "";

	len = snprintf(
	        NULL, 0,
	        "<!DOCTYPE html>\n<html lang=\"pt\">\n<head>\n"
	        "<meta charset=\"utf-8\">\n"
	        "<meta name=\"viewport\" content=\"width=device-width, "
	        "initial-scale=1.0\">\n"
	        "<title>%s</title>\n"
	        "<link rel=\"stylesheet\" href=\"/styles.css" SITE_CSS_V "\">\n"
	        "<link rel=\"stylesheet\" href=\"/hyle.css" SITE_CSS_V "\">\n"
	        "%s</head>\n<body style=\"margin:0\"%s>\n"
	        "<script type=\"application/json\" id=\"chrome-state\">"
	        "{\"title\":\"%s\",\"path\":\"%s\",\"icon\":\"%s\","
	        "\"user\":\"%s\"}</script>\n%s\n%s\n"
	        SITE_UI_FRAGMENTS_SCRIPT "\n"
	        "<script type=\"module\" src=\"/bud-client.js" SITE_CLIENT_V
	        "\"></script>\n</body>\n</html>\n",
	        title_esc, extra_head, module_attr, state_title, state_path,
	        state_icon, state_user, chrome_html, body_html);

	page = (char *)malloc((size_t)len + 1);
	if (!page) {
		bud_free_string(chrome_html);
		bud_free_string(body_html);
		return NULL;
	}

	snprintf(
	        page, (size_t)len + 1,
	        "<!DOCTYPE html>\n<html lang=\"pt\">\n<head>\n"
	        "<meta charset=\"utf-8\">\n"
	        "<meta name=\"viewport\" content=\"width=device-width, "
	        "initial-scale=1.0\">\n"
	        "<title>%s</title>\n"
	        "<link rel=\"stylesheet\" href=\"/styles.css" SITE_CSS_V "\">\n"
	        "<link rel=\"stylesheet\" href=\"/hyle.css" SITE_CSS_V "\">\n"
	        "%s</head>\n<body style=\"margin:0\"%s>\n"
	        "<script type=\"application/json\" id=\"chrome-state\">"
	        "{\"title\":\"%s\",\"path\":\"%s\",\"icon\":\"%s\","
	        "\"user\":\"%s\"}</script>\n%s\n%s\n"
	        SITE_UI_FRAGMENTS_SCRIPT "\n"
	        "<script type=\"module\" src=\"/bud-client.js" SITE_CLIENT_V
	        "\"></script>\n</body>\n</html>\n",
	        title_esc, extra_head, module_attr, state_title, state_path,
	        state_icon, state_user, chrome_html, body_html);

	bud_free_string(chrome_html);
	bud_free_string(body_html);
	return page;
}

char *
site_ui_state_head(const char *json)
{
	size_t len;
	char *head;
	if (!json || !json[0])
		return NULL;
	len = strlen(json) + 128;
	head = (char *)malloc(len);
	if (!head)
		return NULL;
	snprintf(
	        head, len,
	        "<script type=\"application/json\" id=\"bud-state\">%s</script>",
	        json);
	return head;
}

int
site_ui_respond_with_state(
        int fd, const char *title, const char *path, const char *icon,
        const char *user, const char *state_json, const char *module,
        bud_node *body)
{
	char *head = site_ui_state_head(state_json);
	char *page = site_ui_page(title, path, icon, user, head, module, body);
	int rc = 0;
	if (page)
		rc = respond_html(fd, page);
	else
		axil_respond(fd, 500, "Internal Server Error");
	free(head);
	/* respond_html takes ownership of page via axil_respond copy; free */
	free(page);
	return rc;
}

#endif
