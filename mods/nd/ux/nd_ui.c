#include "nd_ui_internal.h"
#include <ttypt/axil-ws.h>
#include <ttypt/lam.h>
#include "../../common/ux/site_ui.c"

nd_client_state_t client_state;
nd_scene_t g_scene;
uint32_t g_lamina_fb[ND_CANVAS_W * ND_CANVAS_H];
int g_ui_initialized = 0;

char g_active_modal[32] = {0};

void nd_ui_set_active_modal(const char *modal_id)
{
	if (!modal_id || !modal_id[0]) {
		g_active_modal[0] = '\0';
		client_state.active_modal[0] = '\0';
	} else {
		if (g_active_modal != modal_id) {
			snprintf(g_active_modal, sizeof(g_active_modal), "%s", modal_id);
		}
		if (client_state.active_modal != modal_id) {
			snprintf(client_state.active_modal, sizeof(client_state.active_modal), "%s", modal_id);
		}
	}
}

const char *nd_ui_get_active_modal(void)
{
	return g_active_modal;
}

void nd_ui_toggle_modal(const char *modal_id)
{
	if (modal_id && modal_id[0] && strcmp(g_active_modal, modal_id) == 0)
		nd_ui_set_active_modal("");
	else
		nd_ui_set_active_modal(modal_id);
#ifdef __wasm__
	bud_app_set_state();
#endif
}

const nd_client_state_t *nd_ui_get_client_state(void)
{
	return &client_state;
}

/* --- Unified Viewport & Terminal Stage --- */

static bud_node *mount_tty(void)
{
	return lx_n("div",
		lx_attr("id", "term"),
		lx_attr("data-bud-terminal", "1"),
		lx_attr("data-line-mode", "true"),
		lx_attr("style", "border: thin solid #9589c5 !important;"),
		lx_attr("class", "flex-1 h-full min-h-[400px] font-mono text-neutral-200 overflow-hidden relative"));
}

nd_viewport_driver_t g_viewport_drivers[ND_VIEWPORT_MAX_DRIVERS] = {0};
size_t g_driver_count = 0;
int g_active_driver_idx = 0;

int nd_register_viewport_driver(const nd_viewport_driver_t *drv)
{
	if (!drv || !drv->id || g_driver_count >= ND_VIEWPORT_MAX_DRIVERS)
		return -1;
	for (size_t i = 0; i < g_driver_count; i++) {
		if (strcmp(g_viewport_drivers[i].id, drv->id) == 0) {
			g_viewport_drivers[i] = *drv;
			return (int)i;
		}
	}
	int idx = (int)g_driver_count;
	g_viewport_drivers[g_driver_count++] = *drv;
	return idx;
}

const nd_viewport_driver_t *nd_get_active_viewport_driver(void)
{
	if (g_driver_count == 0) return NULL;
	if (g_active_driver_idx >= 0 && (size_t)g_active_driver_idx < g_driver_count)
		return &g_viewport_drivers[g_active_driver_idx];
	return &g_viewport_drivers[0];
}

int nd_set_active_viewport_driver(const char *id)
{
	if (!id) return -1;
	for (size_t i = 0; i < g_driver_count; i++) {
		if (strcmp(g_viewport_drivers[i].id, id) == 0) {
			g_active_driver_idx = (int)i;
			return (int)i;
		}
	}
	return -1;
}

int nd_viewport_driver_count(void)
{
	return (int)g_driver_count;
}

const nd_viewport_driver_t *nd_get_viewport_driver_at(int idx)
{
	if (idx < 0 || (size_t)idx >= g_driver_count) return NULL;
	return &g_viewport_drivers[idx];
}

static bud_node *mount_seven_stage(void)
{
	/* A different tag than the terminal's <div>: swapping stages must replace
	 * the element (patch-replace). Patching the terminal's div in place would
	 * keep its data-bud-terminal attribute, and with it the live xterm, which
	 * then swallows every key press. */
	return lx_n("nd-stage",
		lx_attr("id", "game-stage"),
		lx_attr("class", "relative flex-1 flex items-center justify-center min-w-0 min-h-0 overflow-hidden"),
		lx_node(lx_n("canvas",
			lx_attr("id", "seven-stage-canvas"),
			lx_attr("data-bud-canvas", "1"),
			lx_attr("width", "640"),
			lx_attr("height", "480"),
			lx_attr("class", "w-[640px] h-[480px] rounded-lg border-2 border-amber-900/60 shadow-2xl block select-none bg-neutral-950"))));
}

static const nd_viewport_driver_t s_seven_driver = {
	.id = "lamina-seven",
	.label = "🐑 Seven Ways",
	.required_capability = "lamina",
	.mount = mount_seven_stage,
};

static const nd_viewport_driver_t s_tty_driver = {
	.id = "tty",
	.label = "📺 Terminal",
	.required_capability = "axil-tty",
	.mount = mount_tty,
};

void nd_ui_init_defaults(void)
{
	if (g_ui_initialized) return;
	g_ui_initialized = 1;
	nd_scene_init(&g_scene);
	nd_register_viewport_driver(&s_tty_driver);
	nd_register_viewport_driver(&s_seven_driver);
}

/* --- UI glue registry (tenant modules register via ND_UI_GLUE) --- */

#define ND_UI_MAX_GLUE 16
static const nd_ui_glue_t *g_glue[ND_UI_MAX_GLUE];
static size_t g_glue_count;

void nd_ui_glue_register(const nd_ui_glue_t *glue)
{
	if (glue && g_glue_count < ND_UI_MAX_GLUE)
		g_glue[g_glue_count++] = glue;
}

void nd_ui_glue_contribute(int slot, void *parent, const void *state)
{
	int filled = bud_slot_fill(slot, (bud_node *)parent, state);
	if (filled <= 0) {
		for (size_t i = 0; i < g_glue_count; i++)
			if (g_glue[i]->contribute)
				g_glue[i]->contribute(slot, parent, state);
	}
}

bud_node *nd_ui_glue_render_slot(int slot, const void *state)
{
	bud_node *slot_node = bud_slot_render(slot, state);
	if (slot_node)
		return slot_node;
	for (size_t i = 0; i < g_glue_count; i++) {
		bud_node *n = g_glue[i]->render_slot ? g_glue[i]->render_slot(slot, state) : NULL;
		if (n)
			return n;
	}
	return NULL;
}

void nd_ui_glue_state_default(void *state)
{
	for (size_t i = 0; i < g_glue_count; i++)
		if (g_glue[i]->state_default)
			g_glue[i]->state_default(state);
}

void nd_ui_glue_state_serialize(const void *state, char *buf, size_t cap)
{
	for (size_t i = 0; i < g_glue_count; i++)
		if (g_glue[i]->state_serialize)
			g_glue[i]->state_serialize(state, buf, cap);
}

void nd_ui_glue_scene_sync(void *scene, const void *state)
{
	for (size_t i = 0; i < g_glue_count; i++)
		if (g_glue[i]->scene_sync)
			g_glue[i]->scene_sync(scene, state);
}

/* Command sending helpers */
static int send_cmd(const char *cmd)
{
	if (!cmd) return 0;
	size_t len = strlen(cmd);
	char buf[128];
	if (len + 2 > sizeof(buf)) return 0;
	memcpy(buf, cmd, len);
	buf[len] = '\n';
	ws_send(buf, len + 1);
	return 1;
}

int nd_ui_send_cmd(const char *cmd)
{
	return send_cmd(cmd);
}

static int on_data_cmd(bud_event *ev)
{
	if (ev) bud_event_stop_propagation(ev);
	const char *cmd = bud_get_attr(ev->target, "data-cmd");
	if (!cmd || !cmd[0]) return 0;
	return send_cmd(cmd);
}

static int on_cmd_action(bud_event *ev)
{
	const char *cmd = ev ? (const char *)ev->user : NULL;
	if (!cmd || !cmd[0]) return 0;
	return send_cmd(cmd);
}

static int on_item_select(bud_event *ev)
{
	bud_node *node = ev ? (ev->current_target ? ev->current_target : ev->target) : NULL;
	const char *ref_str = node ? bud_get_attr(node, "data-item-ref") : NULL;
	if (!ref_str || !ref_str[0]) return 0;

	uint32_t ref = (uint32_t)strtoul(ref_str, NULL, 10);
	client_state.selected_item_ref = (client_state.selected_item_ref == ref) ? 0 : ref;
#ifdef __wasm__
	bud_app_set_state();
#endif
	return 0;
}

static int on_item_click(bud_event *ev)
{
	bud_node *node = ev ? (ev->current_target ? ev->current_target : ev->target) : NULL;
	const char *ref_str = node ? bud_get_attr(node, "data-ref") : NULL;
	if (!ref_str || !ref_str[0]) return 0;

	uint32_t ref = (uint32_t)strtoul(ref_str, NULL, 10);
	client_state.target_ref = (client_state.target_ref == ref) ? 0 : ref;
#ifdef __wasm__
	bud_app_set_state();
#endif
	return 0;
}

static int on_btn_action(bud_event *ev)
{
	if (!ev || !ev->target) return 0;
	bud_event_stop_propagation(ev);
	const char *action = bud_get_attr(ev->target, "data-action");
	const char *ref_str = bud_get_attr(ev->target, "data-ref");
	if (!action || !ref_str) return 0;

	char cmd[64];
	snprintf(cmd, sizeof(cmd), "%s #%s\n", action, ref_str);
	ws_send(cmd, strlen(cmd));
	return 0;
}

static bud_node *render_viewport_slot(const nd_client_state_t *st)
{
	const char *req_drv = (st && st->preferred_driver[0]) ? st->preferred_driver : "tty";
	bud_node *vport = NULL;

	for (size_t i = 0; i < g_driver_count; i++) {
		if (strcmp(g_viewport_drivers[i].id, req_drv) == 0 && g_viewport_drivers[i].mount) {
			vport = g_viewport_drivers[i].mount();
			break;
		}
	}
	if (!vport) {
		vport = mount_tty();
	}

	bud_node *overlay = nd_ui_glue_render_slot(ND_UI_SLOT_HUD_OVERLAYS, st);
	if (overlay) {
		bud_node *wrap = bud_element("div");
		bud_set_attr(wrap, "class", "relative flex-1 flex flex-col min-w-0 min-h-0");
		bud_append(wrap, vport);
		bud_append(wrap, overlay);
		return wrap;
	}
	return vport;
}

static int on_dismiss_target(bud_event *ev)
{
	if (ev) bud_event_stop_propagation(ev);
	client_state.target_ref = 0;
#ifdef __wasm__
	bud_app_set_state();
#endif
	return 0;
}

static bud_node *render_room_contents(const nd_client_state_t *st)
{
	bud_node *wrap = bud_element("nd-contents");
	bud_set_attr(wrap, "id", "contents");
	bud_set_attr(wrap, "class", "nd-contents w-full");

	uint32_t active_loc = (st && st->target_ref) ? st->target_ref : (st ? st->room.ref : 0);
	int is_inventory = (st && st->target_ref && st->me && st->target_ref == st->me);

	int any = 0;
	if (st) {
		for (size_t i = 0; i < st->entity_count; i++) {
			const nd_entity_desc_t *e = &st->entities[i];
			if (!is_inventory && st->me && e->ref == st->me)
				continue;
			if (active_loc && e->location != active_loc)
				continue;
			any = 1;

			bud_node *row = bud_element("nd-item");
			char ref_buf[32];
			snprintf(ref_buf, sizeof(ref_buf), "%u", e->ref);
			bud_set_attr(row, "data-ref", ref_buf);
			bud_set_attr(row, "data-item-ref", ref_buf);
			bud_set_attr(row, "title", e->name);

			int is_selected = (st && st->selected_item_ref == e->ref);
			char rcls[160];
			snprintf(rcls, sizeof(rcls),
				"nd-row cursor-pointer select-none %s",
				is_selected ? "ctb" : "");
			bud_set_attr(row, "class", rcls);
			bud_bind(row, "click", 1, on_item_select);

			/* Avatar thumbnail: the colored glyph underneath, art image on
			 * top, so a missing image still leaves the glyph visible. */
			bud_node *av = bud_element("nd-avatar");
			char glyph[2] = { e->ch ? e->ch : '?', '\0' };
			char gclass[48];
			snprintf(gclass, sizeof(gclass), "nd-glyph cf%u", e->fg < 8 ? e->fg + 8 : e->fg);
			bud_node *g = bud_element("span");
			bud_set_attr(g, "class", gclass);
			bud_append(g, bud_text(glyph));
			bud_append(av, g);
			if (e->art[0]) {
				char art_url[128];
				snprintf(art_url, sizeof(art_url), "/nd/art/%s", e->art);
				bud_node *img = bud_element("img");
				bud_set_attr(img, "src", art_url);
				bud_set_attr(img, "alt", "");
				bud_append(av, img);
			}
			bud_append(row, av);

			/* Name */
			bud_node *name_span = bud_element("span");
			bud_set_attr(name_span, "class", "nd-name grow pointer-events-none");
			bud_append(name_span, bud_text(e->name));
			bud_append(row, name_span);

			/* Action Dock: floating popup capsule when selected */
			bud_node *act_dock = bud_element("div");
			bud_set_attr(act_dock, "class", is_selected ? "nd-item-popup flex items-center gap-1" : "hidden");

			char look_cmd[48], get_cmd[48], drop_cmd[48];
			snprintf(look_cmd, sizeof(look_cmd), "look #%s", ref_buf);
			snprintf(get_cmd, sizeof(get_cmd), "get #%s", ref_buf);
			snprintf(drop_cmd, sizeof(drop_cmd), "drop #%s", ref_buf);

			/* Look button */
			bud_node *look = bud_element("button");
			bud_set_attr(look, "type", "button");
			bud_set_attr(look, "data-action", "look");
			bud_set_attr(look, "data-cmd", look_cmd);
			bud_set_attr(look, "class", "nd-pad btn text-xs");
			bud_set_attr(look, "title", "Look");
			bud_append(look, bud_text("👁"));
			bud_bind(look, "click", 1, on_data_cmd);
			bud_append(act_dock, look);

			/* Get button: shown if ACT_GET (4) is set or item is gettable */
			if ((e->actions & 4) || (e->location != st->me && e->type != 1 && e->actions == 0)) {
				bud_node *get = bud_element("button");
				bud_set_attr(get, "type", "button");
				bud_set_attr(get, "data-action", "get");
				bud_set_attr(get, "data-cmd", get_cmd);
				bud_set_attr(get, "class", "nd-pad btn text-xs");
				bud_set_attr(get, "title", "Get");
				bud_append(get, bud_text("🖐"));
				bud_bind(get, "click", 1, on_data_cmd);
				bud_append(act_dock, get);
			}

			/* Drop button: shown if ACT_DROP (8) is set or item is in inventory */
			if ((e->actions & 8) || (e->location == st->me)) {
				bud_node *drop = bud_element("button");
				bud_set_attr(drop, "type", "button");
				bud_set_attr(drop, "data-action", "drop");
				bud_set_attr(drop, "data-cmd", drop_cmd);
				bud_set_attr(drop, "class", "nd-pad btn text-xs");
				bud_set_attr(drop, "title", "Drop");
				bud_append(drop, bud_text("🪣"));
				bud_bind(drop, "click", 1, on_data_cmd);
				bud_append(act_dock, drop);
			}

			bud_append(row, act_dock);
			bud_append(wrap, row);
		}
	}

	if (!any) {
		bud_node *empty = bud_element("div");
		bud_set_attr(empty, "class", "nd-empty italic select-none");
		bud_append(empty, bud_text(is_inventory ? "Inventory is empty." : "Nothing here."));
		bud_append(wrap, empty);
	}

	return wrap;
}

static bud_node *render_target_card(const nd_client_state_t *st)
{
	uint32_t t_ref = st ? st->target_ref : 0;
	if (!t_ref) return bud_element("div");

	const nd_entity_desc_t *target = NULL;
	if (st) {
		for (size_t i = 0; i < st->entity_count; i++) {
			if (st->entities[i].ref == t_ref) {
				target = &st->entities[i];
				break;
			}
		}
	}
	if (!target) return bud_element("div");

	bud_node *card = bud_element("div");
	bud_set_attr(card, "id", "target-card");
	bud_set_attr(card, "class", "target-card");
	char tip[96];
	if (st && st->me && target->ref == st->me)
		snprintf(tip, sizeof(tip), "%s #%u (You - Inventory)", target->name, target->ref);
	else
		snprintf(tip, sizeof(tip), "%s #%u", target->name, target->ref);
	bud_set_attr(card, "title", tip);

	/* Map-sized square: art as full-cover background, name overlaid. */
	bud_node *art = bud_element("div");
	bud_set_attr(art, "class", "target-art");
	bud_node *badge = bud_element("div");
	char gclass[48];
	snprintf(gclass, sizeof(gclass), "target-glyph cf%u", target->fg < 8 ? target->fg + 8 : target->fg);
	bud_set_attr(badge, "class", gclass);
	char ch_str[2] = { target->ch ? target->ch : '?', '\0' };
	bud_append(badge, bud_text(ch_str));
	bud_append(art, badge);
	if (target->art[0]) {
		char art_url[128];
		snprintf(art_url, sizeof(art_url), "/nd/art/%s", target->art);
		bud_node *img = bud_element("img");
		bud_set_attr(img, "src", art_url);
		bud_set_attr(img, "alt", "");
		bud_append(art, img);
	}

	bud_node *name_row = bud_element("div");
	bud_set_attr(name_row, "class", "target-title");
	bud_append(name_row, bud_text(target->name));
	bud_append(art, name_row);

	bud_node *close_btn = bud_element("button");
	bud_set_attr(close_btn, "type", "button");
	bud_set_attr(close_btn, "class", "target-close");
	bud_set_attr(close_btn, "title", "Clear Target");
	bud_append(close_btn, bud_text("✕"));
	bud_bind(close_btn, "click", 1, on_dismiss_target);
	bud_append(art, close_btn);

	bud_append(card, art);

	return card;
}

static bud_node *render_modals_slot(const nd_client_state_t *st)
{
	bud_node *slot = bud_element("div");
	bud_set_attr(slot, "id", "modals-slot");
	nd_ui_glue_contribute(ND_UI_SLOT_MODALS, slot, st);
	return slot;
}

static int nd_ui_seven_active(const nd_client_state_t *st)
{
	return st && strcmp(st->preferred_driver, "lamina-seven") == 0;
}

static int on_key_up(bud_event *e)
{
	const char *key = (const char *)e->user;
	if (!key || !*key || !nd_ui_seven_active(&client_state))
		return 0;
	return nd_seven_key(key, 0);
}

static int on_key_down(bud_event *e)
{
	const char *key = (const char *)e->user;
	if (!key || !*key)
		return 0;

	if (nd_ui_seven_active(&client_state) && nd_seven_key(key, 1))
		return 1;
	if (strcmp(key, "k") == 0 || strcmp(key, "ArrowUp") == 0) {
		ws_send("k\n", 2);
		return 1;
	} else if (strcmp(key, "K") == 0) {
		ws_send("K\n", 2);
		return 1;
	} else if (strcmp(key, "j") == 0 || strcmp(key, "ArrowDown") == 0) {
		ws_send("j\n", 2);
		return 1;
	} else if (strcmp(key, "J") == 0) {
		ws_send("J\n", 2);
		return 1;
	} else if (strcmp(key, "h") == 0 || strcmp(key, "ArrowLeft") == 0) {
		ws_send("h\n", 2);
		return 1;
	} else if (strcmp(key, "l") == 0 || strcmp(key, "ArrowRight") == 0) {
		ws_send("l\n", 2);
		return 1;
	} else if (strcmp(key, "x") == 0) {
		ws_send("x\n", 2);
		return 1;
	} else if (strcmp(key, "X") == 0) {
		ws_send("X0\n", 3);
		return 1;
	} else if (strcmp(key, "o") == 0) {
		ws_send("look\n", 5);
		return 1;
	} else if (strcmp(key, "i") == 0) {
		ws_send("inventory\n", 10);
		return 1;
	}
	return 0;
}

static bud_node *render_room_header(const nd_client_state_t *st)
{
	if (!st || (!st->room.name[0] && !st->room.ref))
		return bud_element("div");

	char text[128];
	if (st->room.name[0]) {
		snprintf(text, sizeof(text), "📍 %s (#%u)", st->room.name, st->room.ref);
	} else {
		snprintf(text, sizeof(text), "📍 Room #%u", st->room.ref);
	}

	return lx_n("div",
		lx_attr("id", "room-header"),
		lx_attr("class", "text-center font-mono text-xs text-neutral-300 font-bold px-2 py-0.5 bg-neutral-900/60 rounded border border-neutral-800/60 truncate max-w-full"),
		lx_text(text));
}

bud_node *nd_ui_render(const nd_client_state_t *st)
{
	nd_ui_init_defaults();

	bud_node *vport = render_viewport_slot(st);
	bud_node *map = render_room_map(st);
	bud_node *modals = render_modals_slot(st);

	if (st && st->layout_mode == ND_LAYOUT_GAME_2D) {
		/* Clean 2D Game Layout: render ONLY vport (game canvas) and map (minimap) */
		bud_node *holder = bud_element("div");
		bud_set_attr(holder, "id", "holder");
		bud_set_attr(holder, "class", "nd game-2d-mode flex flex-row gap-4 p-3 min-h-0 h-full overflow-hidden justify-center items-center");
		bud_append(holder, vport);
		if (map)
			bud_append(holder, map);

		bud_node *main_box = bud_element("div");
		bud_set_attr(main_box, "id", "main");
		bud_set_attr(main_box, "class", "flex flex-col h-screen w-screen overflow-hidden bg-neutral-950 text-neutral-100");
		bud_append(main_box, holder);
		bud_append(main_box, modals);

		const char *user = (st && st->user[0]) ? st->user : client_state.user;
		bud_node *layout = site_ui_layout("NeverDark", "/nd", "🕹️", user, NULL, main_box);

		bud_node *root = bud_tpl(
			"<div id='bud-root' data-capabilities='axil-ws axil-tty lamina bud xylem' "
			"class='w-full h-[calc(100vh-64px)] box-border font-[\"Lucida_Console\",Monaco,monospace]'>%node</div>",
			layout);

		bud_bind(root, "cmd", 1, on_cmd_action);
		bud_bind(root, "keydown@window", 0, on_key_down);
		return root;
	}

	bud_node *room_hdr = render_room_header(st);
	bud_node *keypad = render_direction_keypad();
	bud_node *contents = render_room_contents(st);
	bud_node *gauges = render_gauges_slot(st);
	bud_node *actions = render_action_bar(st);
	bud_node *target_card = render_target_card(st);

	bud_node *cb = bud_element("div");
	bud_set_attr(cb, "id", "cb");
	bud_set_attr(cb, "class", "flex flex-col gap-2 items-center w-full flex-1 min-h-0 overflow-y-auto");
	bud_append(cb, target_card);
	bud_append(cb, room_hdr);
	if (map)
		bud_append(cb, map);
	bud_append(cb, keypad);
	bud_append(cb, contents);

	bud_node *rp_bottom = bud_element("div");
	bud_set_attr(rp_bottom, "class", "w-full flex-shrink-0 flex flex-col gap-2 pt-2 border-t border-neutral-700/40 items-center");
	bud_append(rp_bottom, gauges);
	bud_append(rp_bottom, actions);

	bud_node *rp = bud_element("div");
	bud_set_attr(rp, "id", "right-panel");
	const char *rp_class = "flex flex-col gap-2 w-80 max-w-[340px] h-full flex-shrink-0 backdrop-blur-[4px] bg-black/40 p-2 border border-neutral-800/80 justify-between select-none overflow-hidden";
	if (st && st->layout_mode == ND_LAYOUT_STAGE_EXPANDED) {
		rp_class = "flex flex-col gap-2 w-52 max-w-[220px] h-full flex-shrink-0 backdrop-blur-[4px] bg-black/40 p-2 border border-neutral-800/80 justify-between select-none overflow-hidden text-xs";
	}
	bud_set_attr(rp, "class", rp_class);
	bud_append(rp, cb);
	bud_append(rp, rp_bottom);

	bud_node *holder = bud_element("div");
	bud_set_attr(holder, "id", "holder");
	char hcls[160];
	snprintf(hcls, sizeof(hcls), "nd terminal-mode flex flex-1 gap-2 p-2 min-h-0 h-full overflow-hidden %s %s",
		(st && st->target_ref) ? "targetting" : "",
		(st && st->layout_mode == ND_LAYOUT_STAGE_FULLSCREEN) ? "fullscreen-layout" : "");
	bud_set_attr(holder, "class", hcls);
	bud_append(holder, vport);
	if (!st || st->layout_mode != ND_LAYOUT_STAGE_FULLSCREEN) {
		bud_append(holder, rp);
	}

	bud_node *main_box = bud_element("div");
	bud_set_attr(main_box, "id", "main");
	char mcls[128];
	snprintf(mcls, sizeof(mcls), "flex flex-col h-screen w-screen overflow-hidden bg-neutral-950 text-neutral-100 %s",
		(st && st->target_ref) ? "targetting" : "");
	bud_set_attr(main_box, "class", mcls);
	bud_append(main_box, holder);
	bud_append(main_box, modals);

	const char *user = (st && st->user[0]) ? st->user : client_state.user;
	bud_node *layout = site_ui_layout("NeverDark", "/nd", "🕹️", user, NULL, main_box);

	bud_node *root = bud_tpl(
		"<div id='bud-root' data-capabilities='axil-ws axil-tty lamina bud xylem' "
		"class='w-full h-[calc(100vh-64px)] box-border font-[\"Lucida_Console\",Monaco,monospace]'>%node</div>",
		layout);

	bud_bind(root, "cmd", 1, on_cmd_action);
	bud_bind(root, "keydown@window", 0, on_key_down);
	bud_bind(root, "keyup@window", 0, on_key_up);
	return root;
}

static void seven_stage_redraw(void)
{
	nd_seven_render(g_lamina_fb, ND_CANVAS_W, ND_CANVAS_H, &client_state);
	lam_canvas_flush(g_lamina_fb, ND_CANVAS_W, ND_CANVAS_H);
}

ISO_EXPORT(void, wasm_canvas_render, void)
{
	if (nd_ui_seven_active(&client_state))
		seven_stage_redraw();
	nd_map_canvas_redraw(&client_state);
}

ISO_EXPORT(void, wasm_tick, float dt, int width, int height)
{
	(void)width;
	(void)height;
	static uint32_t s_tick_counter = 0;
	s_tick_counter++;
	if (nd_ui_seven_active(&client_state)) {
		int mv = nd_seven_tick(&client_state, dt);
		if (mv) {
			char m[2] = { (char)mv, '\n' };
			ws_send(m, 2);
		}
		if ((s_tick_counter % 2) == 0)
			seven_stage_redraw();
	}
	if ((s_tick_counter % 3) == 0)
		nd_map_canvas_redraw(&client_state);
}

ISO_EXPORT(bud_node *, bud_app_render, void)
{
	return nd_ui_render(&client_state);
}
