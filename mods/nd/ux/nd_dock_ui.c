#include "nd_ui_internal.h"
#include <ttypt/axil-ws.h>

int on_dock_action(bud_event *ev)
{
	if (!ev || !ev->target) return 0;
	const char *action = bud_get_attr(ev->target, "data-nd-action");
	if (!action) return 0;

	if (!strcmp(action, "stats")) {
		nd_ui_toggle_modal("attr");
	} else if (!strcmp(action, "equipment")) {
		nd_ui_toggle_modal("equip");
	} else if (!strcmp(action, "spells")) {
		nd_ui_toggle_modal("spell");
	} else {
		char cmd[64];
		snprintf(cmd, sizeof(cmd), "%s\n", action);
		ws_send(cmd, strlen(cmd));
	}
	return 0;
}

static int on_move_k(bud_event *e) { (void)e; ws_send("k\n", 2); return 0; }
static int on_move_j(bud_event *e) { (void)e; ws_send("j\n", 2); return 0; }
static int on_move_h(bud_event *e) { (void)e; ws_send("h\n", 2); return 0; }
static int on_move_l(bud_event *e) { (void)e; ws_send("l\n", 2); return 0; }
static int on_move_K(bud_event *e) { (void)e; ws_send("K\n", 2); return 0; }
static int on_move_J(bud_event *e) { (void)e; ws_send("J\n", 2); return 0; }

static int on_action_stats(bud_event *e) { (void)e; nd_ui_toggle_modal("attr"); return 0; }
static int on_action_equip(bud_event *e) { (void)e; nd_ui_toggle_modal("equip"); return 0; }
static int on_action_spells(bud_event *e) { (void)e; nd_ui_toggle_modal("spell"); return 0; }
static int on_action_inv(bud_event *e) { (void)e; ws_send("inventory\n", 10); return 0; }
static int on_action_look(bud_event *e) { (void)e; ws_send("look\n", 5); return 0; }

static int on_cycle_viewport(bud_event *e)
{
	(void)e;
	if (g_driver_count > 1) {
		g_active_driver_idx = (g_active_driver_idx + 1) % (int)g_driver_count;
		bud_app_set_state();
	}
	return 0;
}

bud_node *make_btn(const char *label, const char *title, bud_event_handler_fn handler)
{
	return lx_n("button",
		lx_attr("type", "button"),
		lx_attr("class", "nd-pad btn round border border-neutral-700 hover:bg-neutral-800 text-neutral-200 shadow-md text-base select-none"),
		(title && title[0]) ? lx_attr("title", title) : lx_none(),
		handler ? lx_bind("click", 1, handler) : lx_none(),
		lx_text(label ? label : ""));
}

bud_node *nd_ui_dock_button(const char *icon, const char *title, const char *action_id)
{
	bud_node *btn = make_btn(icon ? icon : "", title, on_dock_action);
	if (action_id && action_id[0])
		bud_set_attr(btn, "data-nd-action", action_id);
	return btn;
}

bud_node *render_direction_keypad(void)
{
	return lx_n("div",
		lx_attr("id", "directions"),
		lx_attr("class", "nd-keypad text-center select-none"),
		lx_node(bud_element("div")),
		lx_node(make_btn("↑", "North (k)", on_move_k)),
		lx_node(make_btn("↗", "Northeast (K)", on_move_K)),
		lx_node(make_btn("←", "West (h)", on_move_h)),
		lx_node(bud_element("div")),
		lx_node(make_btn("→", "East (l)", on_move_l)),
		lx_node(bud_element("div")),
		lx_node(make_btn("↓", "South (j)", on_move_j)),
		lx_node(make_btn("↘", "Southeast (J)", on_move_J)));
}

bud_node *render_action_bar(const nd_client_state_t *st)
{
	bud_node *dock = bud_element("div");
	bud_set_attr(dock, "id", "g-actions");
	bud_set_attr(dock, "class", "flex flex-wrap gap-1 items-center justify-center");

	/* Tenant modules contribute their own dock buttons */
	nd_ui_glue_contribute(ND_UI_SLOT_ACTIONS, dock, st);

	bud_append(dock, make_btn("📦", "Inventory", on_action_inv));
	bud_append(dock, make_btn("👁", "Look", on_action_look));

	int allow_choice = (st && (st->pres_flags & ND_REGION_ALLOW_CHOICE) && !(st->pres_flags & ND_REGION_LOCKED));
	if (g_driver_count > 1 && allow_choice) {
		int next_idx = (g_active_driver_idx + 1) % (int)g_driver_count;
		bud_node *vbtn = make_btn(g_viewport_drivers[next_idx].label, "Switch Viewport", on_cycle_viewport);
		bud_set_attr(vbtn, "data-bud-action", "cycle-viewport");
		bud_append(dock, vbtn);
	}

	return dock;
}

bud_node *render_gauges_slot(const nd_client_state_t *st)
{
	bud_node *node = nd_ui_glue_render_slot(ND_UI_SLOT_GAUGES, st);
	if (node)
		return node;
	return bud_element("div");
}
