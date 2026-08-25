#ifndef SITE_LAYOUT_C
#define SITE_LAYOUT_C

#include "site_ui.h"

#include <limits.h>
#include <stdio.h>

#include "bud/bud_jsx.h"

static bud_arg menu_btn(const char *href, const char *icon, const char *label)
{
	return lx_el(
	        "a", lx_attr("class", "btn"), lx_attr("href", href),
	        lx_el("span", lx_text(icon)), lx_el("span", lx_text(label)));
}

bud_node *site_ui_menu(const char *user, const char *path)
{
	char me_href[PATH_MAX] = "";
	char login_buf[1024] = "";
	char reg_href[PATH_MAX] = "";

	if (user && user[0])
		snprintf(me_href, sizeof(me_href), "/%s/", user);
	else {
		login_href(path, login_buf, sizeof(login_buf));
		auth_path("register", reg_href, sizeof(reg_href));
	}

	return lx_frag((user && user[0])
	                       ? lx_frag(menu_btn(me_href, "😊", "me"),
	                                 menu_btn(
	                                         "/auth/logout", "🚪",
	                                         "logout"))
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

bud_node *site_ui_empty_state(const char *message)
{
	return lx_el("p", lx_attr("class", "text-muted"), lx_text(message))
	        .data.node;
}

bud_node *site_ui_layout(
        const char *title, const char *path, const char *icon, const char *user,
        bud_node *menu_items, bud_node *children)
{
	bud_node *main_node;
	bud_node *panel;
	bud_node *menu;
	bud_arg local_items;

	(void)title;
	(void)icon;
	main_node = lx_el("main", lx_attr("class", "main"),
	                  children ? lx_node(children) : lx_none())
	                    .data.node;
	local_items =
	        menu_items
	                ? lx_frag(lx_el("div",
	                                lx_attr("class", "menu-separator")),
	                          lx_el("div", lx_attr("class", "module-menu"),
	                                lx_node(menu_items)))
	                : lx_none();
	panel = lx_el("span",
	              lx_attr("class",
	                      "functions flex-1 fixed top-0 right-0 z-50 "
	                      "h-full "
	                      "overflow-y-auto text-sm capitalize flex "
	                      "flex-col "
	                      "p-4"),
	              lx_el("div",
	                    lx_attr("class",
	                            "relative z-20 flex flex-col gap-2"),
	                    lx_node(site_ui_menu(user, path)), local_items),
	              lx_el("label", lx_attr("for", "menu-functions"),
	                    lx_attr("class",
	                            "absolute inset-0 z-10 cursor-pointer"),
	                    lx_attr("aria-label", "Close Menu")))
	                .data.node;
	menu = lx_el("nav", lx_attr("class", "menu"), lx_node(panel)).data.node;
	return lx_frag(lx_node(main_node), lx_node(menu)).data.node;
}

bud_node *site_ui_form_page(
        const char *user, const char *title, const char *path, const char *icon,
        const char *heading, const char *extra_head, bud_node *children)
{
	bud_node *center =
	        lx_el("div", lx_attr("class", "center"),
	              (heading && heading[0]) ? lx_el("h1", lx_text(heading))
	                                      : lx_none(),
	              children ? lx_node(children) : lx_none())
	                .data.node;

	(void)extra_head;
	return site_ui_layout(title, path, icon, user, NULL, center);
}

#endif
