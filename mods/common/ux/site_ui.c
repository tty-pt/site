#ifndef SITE_UI_C
#define SITE_UI_C
#include "site_ui.h"
#include "bud/bud_app.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "bud/bud_jsx.h"

static void url_encode(const char *src, char *dst, size_t dst_len)
{
	static const char hex[] = "0123456789ABCDEF";
	if (!src || !dst || dst_len == 0)
		return;
	while (*src && dst_len > 1) {
		unsigned char c = (unsigned char)*src;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '_' ||
		    c == '.' || c == '~')
		{
			*dst++ = c;
			dst_len--;
		} else {
			if (dst_len < 4)
				break;
			*dst++ = '%';
			*dst++ = hex[c >> 4];
			*dst++ = hex[c & 15];
			dst_len -= 3;
		}
		src++;
	}
	*dst = '\0';
}

void parent_path(const char *path, char *buf, size_t len)
{
	const char *slash;
	if (!path || !buf || len == 0)
		return;
	slash = strrchr(path, '/');
	if (!slash || slash == path) {
		snprintf(buf, len, "/");
		return;
	}
	if (*(slash + 1) == '\0') {
		/* Path ends with /; skip it and find the previous / */
		const char *p = slash - 1;
		while (p > path && *p != '/')
			p--;
		if (*p == '/') {
			slash = p;
		} else {
			snprintf(buf, len, "/");
			return;
		}
	}
	size_t n = (size_t)(slash - path) + 1;
	if (n >= len)
		n = len - 1;
	memcpy(buf, path, n);
	buf[n] = '\0';
}

const char *site_ui_module_icon(const char *module) {
	if (!module || !module[0])
		return "\xf0\x9f\x8f\xa0";
	if (strcmp(module, "song") == 0)
		return "\xf0\x9f\x8e\xb5";
	if (strcmp(module, "poem") == 0)
		return "\xf0\x9f\x93\x9d";
	if (strcmp(module, "gig") == 0)
		return "\xf0\x9f\x8e\xa4";
	if (strcmp(module, "grp") == 0)
		return "\xf0\x9f\x91\xa5";
	return "\xf0\x9f\x8f\xa0";
}

const char *site_ui_module_display(const char *module) {
	if (module && strcmp(module, "grp") == 0)
		return "group";
	return module;
}

void site_ui_item_path(
        const char *module, const char *id, char *buf, size_t len)
{
	snprintf(buf, len, "/%s/%s", module, id);
}

void item_action_path(
        const char *module, const char *id, const char *action, char *buf,
        size_t len)
{
	snprintf(buf, len, "/%s/%s/%s", module, id, action);
}

void site_ui_collection_path(const char *module, char *buf, size_t len)
{
	snprintf(buf, len, "/%s/", module);
}

void auth_path(const char *action, char *buf, size_t len)
{
	snprintf(buf, len, "/auth/%s", action);
}

void login_href(const char *ret, char *buf, size_t len)
{
	char encoded[1024];
	if (!ret || !*ret) {
		snprintf(buf, len, "/auth/login");
		return;
	}
	url_encode(ret, encoded, sizeof(encoded));
	snprintf(buf, len, "/auth/login?ret=%s", encoded);
}

/* ── Zoom helpers (WASM-safe) ──────────────────────── */

void ui_apply_zoom(bud_node *main_node, bud_node *zoom_label, int zoom)
{
	char style[64], zoom_str[16], zoom_pct[16];
	snprintf(
	        style, sizeof(style),
	        "width:100%%;max-width:100%%;--chord-zoom:%d", zoom);
	snprintf(zoom_str, sizeof(zoom_str), "%d", zoom);
	snprintf(zoom_pct, sizeof(zoom_pct), "%d%%", zoom);
	bud_patch_attr(main_node, "style", style);
	bud_patch_attr(main_node, "data-zoom", zoom_str);
	if (zoom_label)
		bud_patch_text(zoom_label, zoom_pct);
}

int ui_on_zoom_change(
        bud_event *event, int *zoom_out, bud_node *main_node,
        bud_node *zoom_label)
{
	const char *value = (const char *)event->user;
	if (!value)
		return 0;
	int z = atoi(value);
	if (z < VIEWER_ZOOM_MIN)
		z = VIEWER_ZOOM_MIN;
	else if (z > VIEWER_ZOOM_MAX)
		z = VIEWER_ZOOM_MAX;
	*zoom_out = z;
	ui_apply_zoom(main_node, zoom_label, z);
	return 0;
}

static bud_arg menu_btn(const char *href, const char *icon, const char *label) {
	return lx_el("a", lx_attr("class", "btn"), lx_attr("href", href),
	              lx_el("span", lx_text(icon)),
	              lx_el("span", lx_text(label)));
}

bud_node *site_ui_menu(const char *user, const char *path)
{
	int is_home = (!path || strcmp(path, "/") == 0);
	char up[PATH_MAX] = "";
	char me_href[PATH_MAX] = "";
	char login_buf[1024] = "";
	char reg_href[PATH_MAX] = "";

	if (!is_home)
		parent_path(path, up, sizeof(up));
	if (user && user[0])
		snprintf(me_href, sizeof(me_href), "/%s/", user);
	else {
		login_href(path, login_buf, sizeof(login_buf));
		auth_path("register", reg_href, sizeof(reg_href));
	}

	return lx_frag(is_home ? lx_none()
	                       : menu_btn(up, "⬆️", "go up"),
	               (user && user[0])
	                       ? lx_frag(menu_btn(me_href, "😊", "me"),
	                                 menu_btn("/auth/logout", "🚪", "logout"))
	                       : lx_frag(menu_btn(login_buf, "🔑", "login"),
	                                 menu_btn(reg_href, "📝", "register")))
	        .data.node;
}

bud_node *site_ui_item_menu(const char *module, const char *id, int is_owner)
{
	if (!is_owner)
		return lx_frag(lx_none()).data.node;

	char edit_href[PATH_MAX];
	char del_href[PATH_MAX];
	item_action_path(module, id, "edit", edit_href, sizeof(edit_href));
	item_action_path(module, id, "delete", del_href, sizeof(del_href));

	return lx_frag(menu_btn(edit_href, "✏️", "edit"),
	               menu_btn(del_href, "🗑️", "delete"))
	        .data.node;
}

bud_node *site_ui_form_actions(
        const char *cancel_href, const char *submit_label, bud_node *extra)
{
	return lx_el("div", lx_attr("class", "flex gap-2"),
	             lx_el("button", lx_attr("type", "submit"),
	                   lx_attr("class", "btn btn-primary"),
	                   lx_text(submit_label)),
	             extra ? lx_node(extra) : lx_none(),
	             lx_el("a", lx_attr("href", cancel_href),
	                   lx_attr("class", "btn btn-secondary"),
	                   lx_text("Cancel")))
	        .data.node;
}

bud_node *site_ui_empty_state(const char *message)
{
	return lx_el("p", lx_attr("class", "text-muted"), lx_text(message))
	        .data.node;
}

bud_node *site_ui_viewer_controls(
        const char *module, int zoom, const char *save_url,
        bud_event_handler_fn on_zoom_change, bud_node **zoom_text_out)
{
	char zoom_str[16];
	char zoom_pct[16];
	snprintf(zoom_str, sizeof(zoom_str), "%d", zoom);
	snprintf(zoom_pct, sizeof(zoom_pct), "%d%%", zoom);

	bud_node *zoom_text_node = bud_text(zoom_pct);
	if (zoom_text_out)
		*zoom_text_out = zoom_text_node;

	bud_arg zoom_input = lx_none();
	if (on_zoom_change)
		zoom_input =
		        lx_frag(lx_bind("input", 0, on_zoom_change),
		                lx_bind("change", 0, on_zoom_change));

	return lx_el("div", lx_attr("class", "viewer-controls"),
	             lx_attr("data-detail-viewer-controls", module),
	             lx_attr("data-detail-viewer-save-url", save_url),
	             lx_el("label", lx_text("Zoom"),
	                   lx_el("input", lx_attr("type", "range"),
	                         lx_attr("min", STR(VIEWER_ZOOM_MIN)),
	                         lx_attr("max", STR(VIEWER_ZOOM_MAX)),
	                         lx_attr("step", "10"),
	                         lx_attr("value", zoom_str),
	                         lx_attr("data-detail-viewer-zoom", "1"),
	                         zoom_input)),
	             lx_el("p", lx_attr("class", "text-xs text-muted"),
	                   lx_attr("data-detail-viewer-zoom-label", "1"),
	                   lx_node(zoom_text_node)),
	             lx_el("label",
	                   lx_el("input", lx_attr("type", "checkbox"),
	                         lx_attr("checked", "checked"),
	                         lx_attr("data-detail-viewer-wrap", "1")),
	                   lx_text("Wrap lines")))
	        .data.node;
}

/* ── Shared checkbox builder ─────────────────────────── */

bud_node *site_ui_checkbox(
        const char *name, const char *label, int checked,
        bud_event_handler_fn on_change)
{
	if (!name || !label)
		return lx_none().data.node;

	bud_arg bind = lx_none();
	if (on_change)
		bind = lx_bind("change", 0, on_change);

	return lx_el("label",
	             lx_el("input", lx_attr("type", "checkbox"),
	                   lx_attr("name", name), lx_attr("value", "1"), bind,
	                   checked ? lx_attr("checked", "") : lx_none()),
	             lx_text(label))
	        .data.node;
}

/* ── Shared media slot renderer ──────────────────────── */

bud_node *
site_ui_render_media_slot(const char *yt, const char *audio, const char *pdf)
{
	char src[1024];
	bud_node *inner = bud_fragment();
	int has_media = 0;
	if (!inner)
		return NULL;

	if (yt && yt[0]) {
		snprintf(
		        src, sizeof(src), "https://www.youtube.com/embed/%s",
		        yt);
		bud_append(
		        inner,
		        lx_el("div",
		              lx_attr("class", "flex flex-col gap-4 w-full"),
		              lx_el("iframe", lx_attr("src", src),
		                    lx_attr("class", "w-full aspect-video "
		                                     "border-none"),
		                    lx_attr("title", "YouTube video player"),
		                    lx_attr("allow",
		                            "accelerometer; autoplay; "
		                            "clipboard-write; encrypted-media; "
		                            "gyroscope; picture-in-picture; "
		                            "web-share"),
		                    lx_attr("referrerpolicy",
		                            "strict-origin-when-cross-origin"),
		                    lx_attr("allowfullscreen", "")))
		                .data.node);
		has_media = 1;
	}

	if (audio && audio[0]) {
		bud_append(
		        inner,
		        lx_el("div",
		              lx_attr("class", "flex flex-col gap-4 w-full"),
		              lx_el("audio", lx_attr("controls", ""),
		                    lx_attr("class", "w-full"),
		                    lx_el("source", lx_attr("src", audio),
		                          lx_attr("type", "audio/mpeg"))))
		                .data.node);
		has_media = 1;
	}

	if (pdf && pdf[0]) {
		bud_append(
		        inner,
		        lx_el("div",
		              lx_attr("class", "flex flex-col gap-4 w-full"),
		              lx_el("a", lx_attr("href", pdf),
		                    lx_attr("target", "_blank"),
		                    lx_attr("rel", "noopener"),
		                    lx_attr("class", "text-blue-600"),
		                    lx_text("View PDF")))
		                .data.node);
		has_media = 1;
	}

	if (!has_media)
		return NULL;

	return lx_el("div",
	             lx_attr("class", "media-slot flex flex-col gap-4 w-full"),
	             lx_node(inner))
	        .data.node;
}

static int valid_yt_id(const char *s)
{
	if (!s || strlen(s) != 11)
		return 0;
	for (int i = 0; i < 11; i++)
		if (!((s[i] >= 'A' && s[i] <= 'Z') ||
		      (s[i] >= 'a' && s[i] <= 'z') ||
		      (s[i] >= '0' && s[i] <= '9') ||
		      s[i] == '_' || s[i] == '-'))
			return 0;
	return 1;
}

static int safe_url(const char *s)
{
	if (!s || strncmp(s, "https://", 8) != 0)
		return 0;
	for (const char *p = s; *p; p++)
		if (*p == '"' || *p == '\'' || *p == '<' || *p == '>' ||
		    *p == '\\' || *p == '\n' || *p == '\r')
			return 0;
	return 1;
}

int site_ui_build_media_html(
        const char *yt, const char *audio, const char *pdf, char *out,
        size_t out_sz)
{
	char buf[8192];
	int pos = 0;
	int has = 0;

	if (yt && yt[0] && !valid_yt_id(yt))
		yt = NULL;
	if (audio && audio[0] && !safe_url(audio))
		audio = NULL;
	if (pdf && pdf[0] && !safe_url(pdf))
		pdf = NULL;

#define APPEND(...)                                                            \
	do {                                                                   \
		int r = snprintf(                                              \
		        buf + pos, sizeof(buf) - (size_t)pos, __VA_ARGS__);    \
		if (r > 0)                                                     \
			pos += r;                                              \
		if ((size_t)pos >= sizeof(buf))                                \
			goto done;                                             \
	} while (0)

	if (yt && yt[0]) {
		char src[1024];
		snprintf(
		        src, sizeof(src), "https://www.youtube.com/embed/%s",
		        yt);
		APPEND("<div class=\"flex flex-col gap-4 w-full\">"
		       "<iframe src=\"%s\" class=\"w-full aspect-video "
		       "border-none\" title=\"YouTube video player\" "
		       "allow=\"accelerometer; autoplay; clipboard-write; "
		       "encrypted-media; gyroscope; picture-in-picture; "
		       "web-share\" referrerpolicy=\"strict-origin-when-cross-origin\" "
		       "allowfullscreen></iframe></div>",
		       src);
		has = 1;
	}
	if (audio && audio[0]) {
		APPEND("<div class=\"flex flex-col gap-4 w-full\">"
		       "<audio controls class=\"w-full\">"
		       "<source src=\"%s\" type=\"audio/mpeg\">"
		       "</audio></div>",
		       audio);
		has = 1;
	}
	if (pdf && pdf[0]) {
		APPEND("<div class=\"flex flex-col gap-4 w-full\">"
		       "<a href=\"%s\" target=\"_blank\" rel=\"noopener\" "
		       "class=\"text-blue-600\">View PDF</a></div>",
		       pdf);
		has = 1;
	}
#undef APPEND
done:
	if (has)
		snprintf(out, out_sz, "%s", buf);
	else
		out[0] = '\0';
	return has;
}

bud_node *site_ui_layout(
        const char *title, const char *path, const char *icon, const char *user,
        bud_node *menu_items, bud_node *children)
{
	return lx_frag(lx_el("main", lx_attr("class", "main"),
	                     lx_el("h1", lx_text(title)),
	                     children ? lx_node(children) : lx_none()),
	               lx_el("nav", lx_attr("class", "menu"),
	                     lx_el("input", lx_attr("id", "menu-functions"),
	                           lx_attr("name", "functions"),
	                           lx_attr("type", "checkbox"),
	                           lx_attr("class", "hidden")),
	                     lx_el("label", lx_attr("for", "menu-functions"),
	                           lx_attr("class", "menu-overlay"),
	                           lx_attr("aria-label", "Close Menu")),
	                     lx_el("span",
	                           lx_attr("class",
	                                   "functions flex-1 fixed right-0 "
	                                   "z-50 h-full overflow-y-auto "
	                                   "text-sm capitalize flex flex-col "
	                                   "p-4"),
	                           lx_el("div",
	                                 lx_attr("class",
	                                         "relative z-20 flex flex-col "
	                                         "gap-2"),
lx_el("strong", lx_text(icon), lx_text(" "), lx_text(title)),
	                                 lx_node(site_ui_menu(
	                                         user, path)),
                                 menu_items
	                                         ? lx_frag(lx_el("div",
	                                                         lx_attr("clas"
	                                                                 "s",
	                                                                 "menu-"
	                                                                 "separ"
	                                                                 "ato"
	                                                                 "r")),
	                                                   lx_el("div",
	                                                         lx_attr("clas"
	                                                                 "s",
	                                                                 "modul"
	                                                                 "e-"
	                                                                 "men"
	                                                                 "u"),
	                                                         lx_node(menu_items)))
	                                         : lx_none()),
	                           lx_el("label",
	                                 lx_attr("for", "menu-functions"),
	                                 lx_attr("class",
	                                         "absolute inset-0 z-10 "
	                                         "cursor-pointer"),
	                                 lx_attr("aria-label", "Close Menu"))),
	                     lx_el("span",
	                           lx_attr("class",
	                                   "fixed top-0 right-0 z-30 p-2 flex "
	                                   "items-center gap-4"),
	                           lx_el("label",
	                                 lx_attr("for", "menu-functions"),
	                                 lx_attr("class",
	                                         "menu-toggle flex "
	                                         "items-center justify-center "
	                                         "cursor-pointer text-base "
	                                         "btn"),
	                                 lx_attr("aria-label", "Menu"),
	                                 lx_attr("data-menu-toggle", "1"),
	                                 lx_text(icon)))))
	        .data.node;
}

bud_node *site_ui_form_page(
        const char *user, const char *title, const char *path, const char *icon,
        const char *heading, bud_node *children)
{
	bud_node *center =
	        lx_el("div", lx_attr("class", "center"),
	              (heading && heading[0]) ? lx_el("h1", lx_text(heading))
	                                      : lx_none(),
	              children ? lx_node(children) : lx_none())
	                .data.node;

	return site_ui_layout(title, path, icon, user, NULL, center);
}

bud_node *site_ui_delete_confirm(
        const char *module, const char *id, const char *title,
        const char *csrf_token)
{
	char action_path[PATH_MAX];
	char cancel_path[PATH_MAX];
	item_action_path(
	        module, id, "delete", action_path, sizeof(action_path));
	site_ui_item_path(module, id, cancel_path, sizeof(cancel_path));

	return lx_el("div", lx_attr("class", "center"),
	             lx_el("p", lx_text("Are you sure you want to delete "),
	                   lx_el("strong",
	                         lx_text((title && title[0]) ? title : id)),
	                   lx_text("?")),
	             lx_el("form", lx_attr("method", "POST"),
	                   lx_attr("action", action_path),
	                   lx_attr("enctype", "multipart/form-data"),
	                   lx_el("input", lx_attr("type", "hidden"),
	                         lx_attr("name", "csrf_token"),
	                         lx_attr("value", csrf_token)),
	                   lx_node(site_ui_form_actions(
	                           cancel_path, "Delete", NULL))))
	        .data.node;
}

bud_node *site_ui_add_form(
        const char *module, const char *csrf_token, int has_error,
        const char *error_msg)
{
	char action[PATH_MAX];
	char cancel_href[PATH_MAX];
	snprintf(action, sizeof(action), "/%s/add", module);
	snprintf(cancel_href, sizeof(cancel_href), "/%s/", module);

	return lx_frag((has_error && error_msg)
	                       ? lx_el("p", lx_attr("class", "text-error"),
	                               lx_text(error_msg))
	                       : lx_none(),
	               lx_el("form", lx_attr("action", action),
	                     lx_attr("method", "POST"),
	                     lx_attr("enctype", "multipart/form-data"),
	                     lx_attr("class", "flex flex-col gap-4"),
	                     lx_el("input", lx_attr("type", "hidden"),
	                           lx_attr("name", "csrf_token"),
	                           lx_attr("value", csrf_token)),
	                     lx_el("label", lx_text("Title:"),
	                           lx_el("input", lx_attr("name", "title"))),
	                     lx_node(site_ui_form_actions(
	                             cancel_href, "Add", NULL))))
	        .data.node;
}

/* ── Generic form field builder ───────────────────── */

bud_node *site_ui_form_fields(
        const form_field_t *fields, const char **values, const char *csrf_token)
{
	bud_node *frag = bud_fragment();
	if (!frag)
		return NULL;

	bud_append(
	        frag, lx_el("input", lx_attr("type", "hidden"),
	                    lx_attr("name", "csrf_token"),
	                    lx_attr("value", csrf_token))
	                      .data.node);

	for (const form_field_t *f = fields; f->name; f++) {
		const char *val = values ? values[f - fields] : NULL;
		if (f->type == 2) {
			bud_append(
			        frag,
			        lx_el("label", lx_text(f->label),
			              lx_el("input", lx_attr("type", "file"),
			                    lx_attr("name", f->name)))
			                .data.node);
		} else if (f->type == 1) {
			bud_node *ta =
			        lx_el("textarea", lx_attr("name", f->name),
			              lx_attr("class", "font-mono w-full"))
			                .data.node;
			const char *raw = val ? val : "";
			size_t raw_len = strlen(raw);
			char *safe = malloc(raw_len + 1);
			if (safe) {
				size_t w = 0;
				for (size_t i = 0; i < raw_len; i++) {
					if (raw[i] == '<' && i + 1 < raw_len && raw[i + 1] == '/') {
						i++;
						continue;
					}
					safe[w++] = raw[i];
				}
				safe[w] = '\0';
				bud_append(ta, bud_raw(safe));
				free(safe);
			} else {
				bud_append(ta, bud_raw(raw));
			}
			bud_append(
			        frag,
			        lx_el("label", lx_text(f->label), lx_node(ta))
			                .data.node);
		} else {
			bud_append(
			        frag,
			        lx_el("label", lx_text(f->label),
			              lx_el("input", lx_attr("type", "text"),
			                    lx_attr("name", f->name),
			                    (val && val[0])
			                            ? lx_attr("value", val)
			                            : lx_none()))
			                .data.node);
		}
	}
	return frag;
}

static size_t escape_html_into(const char *src, char *dst, size_t dstsize)
{
	size_t w = 0;
	const char *ent;
	size_t elen;

	if (!src) src = "";
	while (*src && w < dstsize - 1) {
		switch (*src) {
		case '&':  ent = "&amp;";  elen = 5; break;
		case '<':  ent = "&lt;";   elen = 4; break;
		case '>':  ent = "&gt;";   elen = 4; break;
		case '"':  ent = "&quot;"; elen = 6; break;
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

char *site_ui_page(
        const char *title, const char *extra_head, const char *module,
        bud_node *body)
{
	char *body_html;
	char *page;
	char module_attr[256];
	char client_script[512];
	char title_esc[512];
	int len;

#define SITE_CSS_V "?v=18"

	if (!body)
		return NULL;
	body_html = bud_render_hydrated_html(body);
	if (!body_html)
		return NULL;

	escape_html_into(title, title_esc, sizeof(title_esc));

	if (module && module[0]) {
		snprintf(
		        module_attr, sizeof(module_attr),
		        " data-modules=\"%s\"", module);
		snprintf(
		        client_script, sizeof(client_script),
		        "<script type=\"module\" "
		        "src=\"/bud-client.js\"></script>\n");
	} else {
		module_attr[0] = '\0';
		client_script[0] = '\0';
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
	        "%s</head>\n<body style=\"margin:0\"%s>\n%s\n"
	        "%s</body>\n</html>\n",
	        title_esc, extra_head, module_attr, body_html, client_script);

	page = (char *)malloc((size_t)len + 1);
	if (!page) {
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
	        "%s</head>\n<body style=\"margin:0\"%s>\n%s\n"
	        "%s</body>\n</html>\n",
	        title_esc, extra_head, module_attr, body_html, client_script);

	bud_free_string(body_html);
	return page;
}
#endif
