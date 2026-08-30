#ifndef SITE_CHROME_C
#define SITE_CHROME_C

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bud/bud_app.h"
#include "bud/bud_jsx.h"
#include "site_chrome.h"
#include "../../i18n/i18n_dict.h"

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
	bud_node *back = NULL;
	bud_node *bar;

	path = state ? state->path : "";
	icon = state && state->icon[0] ? state->icon : "🏠";
	up[0] = '\0';
	if (path[0] && strcmp(path, "/") != 0)
		site_chrome_parent_path(path, up, sizeof(up));
	if (up[0]) {
		const char *back_label = i18n_t(state ? state->lang : NULL, "Back");
		back = bud_tpl(
		        "<a class='btn nav-back' href='%s' aria-label='%s'>"
		        "  <span aria-hidden='true'>↩️</span>"
		        "</a>",
		        up, back_label);
	}

	const char *menu_label = i18n_t(state ? state->lang : NULL, "Menu");
	const char *close_menu_label = i18n_t(state ? state->lang : NULL, "Close Menu");

	bar =
	        bud_tpl("<header class='nav-bar' data-site-chrome='1' %bind>"
	                "  <span class='nav-bar-slot'>%node</span>"
	                "  <h1 class='nav-bar-title'>%s</h1>"
	                "  <span class='nav-bar-slot'>"
	                "    <label for='menu-functions' class='menu-toggle "
	                "btn' aria-label='%s' data-menu-toggle='1'>%s</label>"
	                "  </span>"
	                "</header>",
	                "scroll@window", site_chrome_on_scroll, back,
	                state ? state->title : "", menu_label, icon);
	site_chrome_nav_bar = bar;
	site_chrome_scroll_y = 0;
	site_chrome_scroll_ready = 0;
	site_chrome_hidden = 0;

	return bud_tpl(
	        "<div id='chrome-root'>"
	        "  <input id='menu-functions' name='functions' type='checkbox' "
	        "class='menu-control' aria-label='%s'/>"
	        "  <label for='menu-functions' class='menu-overlay' "
	        "aria-label='%s'></label>"
	        "  %node"
	        "</div>",
	        menu_label, close_menu_label,
	        bar);
}

#endif
