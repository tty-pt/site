#ifndef SITE_CHROME_C
#define SITE_CHROME_C

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bud/bud_app.h"
#include "bud/bud_jsx.h"
#include "site_chrome.h"

#define NAV_SCROLL_TOP 24
#define NAV_SCROLL_HIDE_AT 48
#define NAV_SCROLL_DELTA 4

static bud_node *site_chrome_nav_bar;
static int site_chrome_scroll_y;
static int site_chrome_scroll_ready;
static int site_chrome_hidden;

static void site_chrome_parent_path(const char *path, char *buf, size_t len)
{
	const char *slash;
	const char *p;
	size_t n;

	if (!path || !buf || len == 0)
		return;
	slash = strrchr(path, '/');
	if (!slash || slash == path) {
		snprintf(buf, len, "/");
		return;
	}
	if (*(slash + 1) == '\0') {
		p = slash - 1;
		while (p > path && *p != '/')
			p--;
		if (*p != '/') {
			snprintf(buf, len, "/");
			return;
		}
		slash = p;
	}
	n = (size_t)(slash - path) + 1;
	if (n >= len)
		n = len - 1;
	memcpy(buf, path, n);
	buf[n] = '\0';
}

static void site_chrome_set_hidden(int hidden)
{
	const char *classes;

	if (!site_chrome_nav_bar || site_chrome_hidden == hidden)
		return;
	site_chrome_hidden = hidden;
	classes = hidden ? "nav-bar nav-bar-hidden" : "nav-bar";
	bud_set_attr(site_chrome_nav_bar, "class", classes);
	bud_patch_attr(site_chrome_nav_bar, "class", classes);
}

static int site_chrome_on_scroll(bud_event *event)
{
	const char *value;
	int y;
	int delta;

	value = event ? (const char *)event->user : NULL;
	if (!value || !site_chrome_nav_bar)
		return 0;
	y = atoi(value);
	if (!site_chrome_scroll_ready) {
		site_chrome_scroll_y = y;
		site_chrome_scroll_ready = 1;
		return 0;
	}
	delta = y - site_chrome_scroll_y;
	if (y < NAV_SCROLL_TOP || delta < -NAV_SCROLL_DELTA)
		site_chrome_set_hidden(0);
	else if (y > NAV_SCROLL_HIDE_AT && delta > NAV_SCROLL_DELTA)
		site_chrome_set_hidden(1);
	if (delta > NAV_SCROLL_DELTA || delta < -NAV_SCROLL_DELTA)
		site_chrome_scroll_y = y;
	return 0;
}

bud_node *site_ui_chrome(const site_ui_chrome_state *state)
{
	const char *path;
	const char *icon;
	char up[SITE_CHROME_PATH_MAX];
	bud_arg back;
	bud_node *bar;

	path = state ? state->path : "";
	icon = state && state->icon[0] ? state->icon : "\xf0\x9f\x8f\xa0";
	up[0] = '\0';
	if (path[0] && strcmp(path, "/") != 0)
		site_chrome_parent_path(path, up, sizeof(up));
	back = up[0] ? lx_el("a", lx_attr("class", "btn nav-back"),
	                     lx_attr("href", up), lx_attr("aria-label", "Back"),
	                     lx_el("span", lx_attr("aria-hidden", "true"),
	                           lx_text("\xe2\x86\xa9\xef\xb8\x8f")))
	             : lx_none();

	bar = lx_el("header", lx_attr("class", "nav-bar"),
	            lx_attr("data-site-chrome", "1"),
	            lx_bind("scroll@window", 0, site_chrome_on_scroll),
	            lx_el("span", lx_attr("class", "nav-bar-slot"), back),
	            lx_el("h1", lx_attr("class", "nav-bar-title"),
	                  lx_text(state ? state->title : "")),
	            lx_el("span", lx_attr("class", "nav-bar-slot"),
	                  lx_el("label", lx_attr("for", "menu-functions"),
	                        lx_attr("class", "menu-toggle btn"),
	                        lx_attr("aria-label", "Menu"),
	                        lx_attr("data-menu-toggle", "1"),
	                        lx_text(icon))))
	              .data.node;
	site_chrome_nav_bar = bar;
	site_chrome_scroll_y = 0;
	site_chrome_scroll_ready = 0;
	site_chrome_hidden = 0;

	return lx_el("div", lx_attr("id", "chrome-root"),
	             lx_el("input", lx_attr("id", "menu-functions"),
	                   lx_attr("name", "functions"),
	                   lx_attr("type", "checkbox"),
	                   lx_attr("class", "menu-control"),
	                   lx_attr("aria-label", "Menu")),
	             lx_el("label", lx_attr("for", "menu-functions"),
	                   lx_attr("class", "menu-overlay"),
	                   lx_attr("aria-label", "Close Menu")),
	             lx_node(bar))
	        .data.node;
}

#endif
