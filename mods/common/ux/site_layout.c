#ifndef SITE_LAYOUT_C
#define SITE_LAYOUT_C

#include "site_ui.h"

#include <limits.h>
#include <stdio.h>

#include "bud/bud_app.h"

static bud_node *menu_btn(const char *href, const char *icon, const char *label)
{
	return bud_tpl(
	        "<a class='btn' href='%s'>"
	        "  <span>%s</span>"
	        "  <span>%s</span>"
	        "</a>",
	        href ? href : "", icon ? icon : "", label ? label : "");
}

bud_node *site_ui_menu(const char *user, const char *path)
{
	char me_href[PATH_MAX] = "";
	char login_buf[1024] = "";
	char reg_href[PATH_MAX] = "";

	if (user && user[0]) {
		snprintf(me_href, sizeof(me_href), "/%s/", user);
		return bud_tpl(
		        "%node"
		        "%node",
		        menu_btn(me_href, "👤", "me"),
		        menu_btn("/auth/logout", "🚪", "logout"));
	} else {
		login_href(path, login_buf, sizeof(login_buf));
		auth_path("register", reg_href, sizeof(reg_href));
		return bud_tpl(
		        "%node"
		        "%node",
		        menu_btn(login_buf, "🔑", "login"),
		        menu_btn(reg_href, "📝", "register"));
	}
}

bud_node *site_ui_item_menu(const char *module, const char *id, int is_owner)
{
	if (!is_owner)
		return NULL;

	char edit_href[PATH_MAX];
	char del_href[PATH_MAX];
	item_action_path(module, id, "edit", edit_href, sizeof(edit_href));
	item_action_path(module, id, "delete", del_href, sizeof(del_href));

	return bud_tpl(
	        "%node"
	        "%node",
	        menu_btn(edit_href, "✏️", "edit"),
	        menu_btn(del_href, "🗑️", "delete"));
}

bud_node *site_ui_empty_state(const char *message)
{
	return bud_tpl("<p class='text-muted'>%s</p>", message ? message : "");
}

bud_node *site_ui_layout(
        const char *title, const char *path, const char *icon, const char *user,
        bud_node *menu_items, bud_node *children)
{
	(void)title;
	(void)icon;

	bud_node *local_items =
	        menu_items ? bud_tpl("<div class='menu-separator'></div>"
	                             "<div class='module-menu'>%node</div>",
	                             menu_items)
	                   : NULL;

	bud_node *panel = bud_tpl(
	        "<span class='functions flex-1 fixed top-0 right-0 z-50 h-full "
	        "overflow-y-auto text-sm capitalize flex flex-col p-4'>"
	        "  <div class='relative z-20 flex flex-col gap-2'>"
	        "    %node"
	        "    %node"
	        "  </div>"
	        "  <label for='menu-functions' class='absolute inset-0 z-10 "
	        "cursor-pointer' aria-label='Close Menu'></label>"
	        "</span>",
	        site_ui_menu(user, path), local_items);

	return bud_tpl(
	        "<main class='main'>%node</main>"
	        "<nav class='menu'>%node</nav>",
	        children, panel);
}

bud_node *site_ui_form_page(
        const char *user, const char *title, const char *path, const char *icon,
        const char *heading, const char *extra_head, bud_node *children)
{
	(void)extra_head;
	bud_node *center = bud_tpl(
	        "<div class='center'>"
	        "  %node"
	        "  %node"
	        "</div>",
	        (heading && heading[0]) ? bud_tpl("<h1>%s</h1>", heading)
	                                : NULL,
	        children);

	return site_ui_layout(title, path, icon, user, NULL, center);
}

#endif
