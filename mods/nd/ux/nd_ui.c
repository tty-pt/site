#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <bud/bud.h>
#include <bud/bud_app.h>

#include <nd/bcp.h>
#include <nd/ui-slot.h>
#include "nd_viewport.h"

#include <ttypt/axil-ws.h>
#include <ttypt/lam.h>
#include "nd_scene.h"
#include "nd_lamina.h"
#include "nd_ui.h"
#include "../../common/ux/site_ui.c"

static nd_client_state_t client_state;
static nd_scene_t g_scene;
static uint32_t g_lamina_fb[ND_CANVAS_W * ND_CANVAS_H];
static int g_ui_initialized = 0;

static char g_active_modal[32] = {0};

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

/* --- Pluggable Viewport Registry & Drivers --- */

static nd_viewport_driver_t g_viewport_drivers[ND_VIEWPORT_MAX_DRIVERS];
static size_t g_driver_count = 0;
static int g_active_driver_idx = 0;

int nd_register_viewport_driver(const nd_viewport_driver_t *drv)
{
	if (!drv || g_driver_count >= ND_VIEWPORT_MAX_DRIVERS)
		return -1;

	for (size_t i = 0; i < g_driver_count; i++) {
		if (strcmp(g_viewport_drivers[i].id, drv->id) == 0) {
			g_viewport_drivers[i] = *drv;
			return 0;
		}
	}

	g_viewport_drivers[g_driver_count++] = *drv;
	return 0;
}

const nd_viewport_driver_t *nd_get_active_viewport_driver(void)
{
	if (g_driver_count == 0) return NULL;
	if (g_active_driver_idx < 0 || (size_t)g_active_driver_idx >= g_driver_count)
		g_active_driver_idx = 0;
	return &g_viewport_drivers[g_active_driver_idx];
}

int nd_set_active_viewport_driver(const char *id)
{
	if (!id) return -1;
	for (size_t i = 0; i < g_driver_count; i++) {
		if (strcmp(g_viewport_drivers[i].id, id) == 0) {
			g_active_driver_idx = (int)i;
			return 0;
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

/* --------------------------------------------------------------------------
 * NeverDark UI Decoupling & Viewport Architecture
 *
 * NeverDark strictly partitions responsibilities between backend simulation
 * and client presentation:
 *
 * 1. Backend Simulation & Game Logic (axil-nd, nd-* modules):
 *    - Simulates spatial movement, combat, vitals, inventory, and biomes.
 *    - Emits declarative Binary Client Protocol (#b) streams and ANSI text.
 *    - Contains ZERO UI or DOM rendering code.
 *
 * 2. Client Presentation & Viewports:
 *    - Bud UI Shell (mods/nd/ux/nd_ui.c): Pure-C isomorphic HTML DOM HUD
 *      (character vitals, paper doll equipment modal, grimoire, chat/command).
 *    - liblamina Viewports: Standalone 2D immediate-mode graphics surface
 *      (tilemaps, sprites, bitmap fonts, immediate-mode canvas via lam_canvas_flush).
 *      On native systems, liblamina renders via OpenGL/GLFW or Linux /dev/fb0.
 *    - Custom Native Clients: Modders can code custom client surfaces for
 *      their area of space without touching game logic or backend modules.
 * -------------------------------------------------------------------------- */

/* TTY Viewport Driver */
static bud_node *mount_tty(void)
{
	bud_node *term = bud_element("div");
	bud_set_attr(term, "id", "term");
	bud_set_attr(term, "data-bud-terminal", "1");
	bud_set_attr(term, "data-line-mode", "true");
	bud_set_attr(term, "style", "border: thin solid #9589c5 !important;");
	bud_set_attr(term, "class", "flex-1 h-full min-h-[400px] font-mono text-neutral-200 overflow-hidden relative");
	return term;
}

static const nd_viewport_driver_t g_driver_tty = {
	.id = "tty",
	.label = "📺 TTY",
	.required_capability = "axil-tty",
	.mount = mount_tty,
	.init = NULL,
	.on_frame = NULL,
	.on_resize = NULL,
	.destroy = NULL
};

/* Lamina 2D Canvas Driver */
static bud_node *mount_lamina(void)
{
	bud_node *canvas = bud_element("canvas");
	bud_set_attr(canvas, "id", "viewport");
	bud_set_attr(canvas, "data-bud-canvas", "1");
	bud_set_attr(canvas, "width", "640");
	bud_set_attr(canvas, "height", "480");
	bud_set_attr(canvas, "class", "flex-1 min-h-[400px] bg-black rounded border border-neutral-800 shadow-inner");
	return canvas;
}

static void on_frame_lamina(const uint8_t *buf, size_t len)
{
	(void)buf; (void)len;
	nd_scene_sync(&g_scene, &client_state);
	nd_lamina_render_scene(g_lamina_fb, ND_CANVAS_W, ND_CANVAS_H, &g_scene);
	lam_canvas_flush(g_lamina_fb, ND_CANVAS_W, ND_CANVAS_H);
}

static const nd_viewport_driver_t g_driver_lamina = {
	.id = "lamina-2d",
	.label = "🎮 Lamina (2D)",
	.required_capability = "lamina",
	.mount = mount_lamina,
	.init = NULL,
	.on_frame = on_frame_lamina,
	.on_resize = NULL,
	.destroy = NULL
};

/* Bud DOM Viewport Driver (nd-driver-bud) */
static bud_node *mount_bud(void)
{
	bud_node *panel = bud_element("div");
	bud_set_attr(panel, "id", "bud-viewport");
	bud_set_attr(panel, "data-bud-viewport", "bud");
	bud_set_attr(panel, "class", "flex-1 h-full min-h-[400px] bg-neutral-900 border border-emerald-800 rounded p-4 font-mono text-neutral-200 overflow-y-auto relative flex flex-col gap-3");

	bud_node *hdr = bud_element("div");
	bud_set_attr(hdr, "class", "text-xl font-bold text-emerald-400 border-b border-emerald-900/50 pb-2");
	bud_append(hdr, bud_text(client_state.room.name[0] ? client_state.room.name : "NeverDark World"));
	bud_append(panel, hdr);

	bud_node *desc = bud_element("p");
	bud_set_attr(desc, "class", "text-sm text-neutral-300 leading-relaxed");
	bud_append(desc, bud_text(client_state.room.desc[0] ? client_state.room.desc : "Exploring spacetime coordinates..."));
	bud_append(panel, desc);

	if (client_state.entity_count > 0) {
		bud_node *ents = bud_element("div");
		bud_set_attr(ents, "class", "flex flex-wrap gap-2 mt-2");
		for (size_t i = 0; i < client_state.entity_count; i++) {
			const nd_entity_desc_t *e = &client_state.entities[i];
			bud_node *ebadge = bud_element("span");
			bud_set_attr(ebadge, "class", "px-2 py-1 bg-neutral-800 border border-neutral-700 rounded text-xs text-neutral-200");
			bud_append(ebadge, bud_text(e->name[0] ? e->name : "entity"));
			bud_append(ents, ebadge);
		}
		bud_append(panel, ents);
	}

	return panel;
}

static const nd_viewport_driver_t g_driver_bud = {
	.id = "bud",
	.label = "🌿 Bud (DOM)",
	.required_capability = "bud",
	.mount = mount_bud,
	.init = NULL,
	.on_frame = NULL,
	.on_resize = NULL,
	.destroy = NULL
};

void nd_ui_init_defaults(void)
{
	if (g_ui_initialized)
		return;
	g_ui_initialized = 1;

	nd_scene_init(&g_scene);

	nd_register_viewport_driver(&g_driver_tty);
	nd_register_viewport_driver(&g_driver_lamina);
	nd_register_viewport_driver(&g_driver_bud);
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

/* --- Serialization --- */

void nd_state_serialize(const nd_client_state_t *st, char *buf, size_t cap)
{
	if (!st || !buf || cap == 0)
		return;

	char esc_map[1200] = {0};
	size_t em = 0;
	for (size_t i = 0; st->map_buf[i] && em + 4 < sizeof(esc_map); i++) {
		if (st->map_buf[i] == '\n') {
			esc_map[em++] = '\\';
			esc_map[em++] = 'n';
		} else if (st->map_buf[i] == '"' || st->map_buf[i] == '\\') {
			esc_map[em++] = '\\';
			esc_map[em++] = st->map_buf[i];
		} else {
			esc_map[em++] = st->map_buf[i];
		}
	}

	int offset = snprintf(buf, cap,
		"{\"me\":%u,\"auth_failed\":%d,\"user\":\"%s\",\"map_buf\":\"%s\","
		"\"active_modal\":\"%s\","
		"\"selected_item_ref\":%u,"
		"\"preferred_driver\":\"%s\","
		"\"active_viewport\":%d,\"tod\":%u,"
		"\"room\":{\"ref\":%u,\"name\":\"%s\",\"desc\":\"%s\",\"exits\":%u},"
		"\"room_ref\":%u,\"room_name\":\"%s\",\"room_desc\":\"%s\",\"room_exits\":%u,"
		"\"gauges\":[",
		st->me, st->auth_failed, st->user, esc_map,
		st->active_modal,
		st->selected_item_ref,
		st->preferred_driver,
		g_active_driver_idx, st->tod,
		st->room.ref, st->room.name, st->room.desc, st->room.exits,
		st->room.ref, st->room.name, st->room.desc, st->room.exits);

	if (offset > 0 && (size_t)offset < cap) {
		size_t gcur = (size_t)offset;
		int first = 1;
		for (size_t i = 0; i < ND_MAX_GAUGES && gcur + 96 < cap; i++) {
			if (!st->gauges[i].name[0])
				continue;
			int gn = snprintf(buf + gcur, cap - gcur,
				"%s{\"n\":\"%s\",\"val\":%u,\"max\":%u}",
				first ? "" : ",", st->gauges[i].name,
				st->gauges[i].bar.val, st->gauges[i].bar.max);
			if (gn > 0) gcur += (size_t)gn;
			first = 0;
		}
		int en = snprintf(buf + gcur, cap - gcur, "],\"entities\":[");
		if (en > 0) offset = (int)(gcur + (size_t)en);
	}

	if (offset < 0 || (size_t)offset >= cap)
		return;

	size_t cur = (size_t)offset;
	for (size_t i = 0; i < st->entity_count && cur + 128 < cap; i++) {
		const nd_entity_desc_t *e = &st->entities[i];
		int n = snprintf(buf + cur, cap - cur,
			"%s{\"ref\":%u,\"name\":\"%s\",\"ch\":\"%c\",\"fg\":%u,\"loc\":%u}",
			(i > 0) ? "," : "",
			e->ref, e->name, e->ch ? e->ch : '?', e->fg, e->location);
		if (n > 0) cur += (size_t)n;
	}

	if (cur + 3 < cap) {
		buf[cur++] = ']';

		/* Actions array */
		if (cur + 16 < cap) {
			int an = snprintf(buf + cur, cap - cur, ",\"actions\":[");
			if (an > 0) cur += (size_t)an;
			for (size_t i = 0; i < st->action_count && cur + 128 < cap; i++) {
				const nd_action_desc_t *a = &st->actions[i];
				int n = snprintf(buf + cur, cap - cur,
					"%s{\"id\":%u,\"label\":\"%s\",\"icon\":\"%s\"}",
					(i > 0) ? "," : "",
					a->id, a->label, a->icon);
				if (n > 0) cur += (size_t)n;
			}
			if (cur + 1 < cap) {
				buf[cur++] = ']';
			}
		}

		/* Allow tenant modules to contribute state slices */
		char slice_buf[1024] = {0};
		nd_ui_glue_state_serialize(st, slice_buf, sizeof(slice_buf));
		size_t slen = strlen(slice_buf);
		if (slen > 0 && cur + slen + 16 < cap) {
			int n = snprintf(buf + cur, cap - cur, ",\"slices\":{%s}", slice_buf);
			if (n > 0) cur += (size_t)n;
		}

		buf[cur++] = '}';
		buf[cur] = '\0';
	}
}

static int send_cmd(const char *cmd)
{
	if (!cmd)
		return 0;
	size_t len = strlen(cmd);
	char buf[128];
	if (len + 2 > sizeof(buf))
		return 0;
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
	if (ev)
		bud_event_stop_propagation(ev);
	const char *cmd = bud_get_attr(ev->target, "data-cmd");
	if (!cmd || !cmd[0])
		return 0;
	return send_cmd(cmd);
}

static int on_cycle_viewport(bud_event *ev);

static int on_action_dispatch(bud_event *ev)
{
	const char *action = ev ? ev->type : NULL;
	const char *val = ev ? (const char *)ev->user : NULL;
	if (!action)
		return 0;
	if (strcmp(action, "item-select") == 0 && val && val[0]) {
		uint32_t ref = (uint32_t)strtoul(val, NULL, 10);
		client_state.selected_item_ref = (client_state.selected_item_ref == ref) ? 0 : ref;
#ifdef __wasm__
		bud_app_set_state();
#endif
		return 0;
	}
	if (strcmp(action, "cycle-viewport") == 0) {
		return on_cycle_viewport(ev);
	}
	return 0;
}

static int on_cmd_action(bud_event *ev)
{
	const char *cmd = ev ? (const char *)ev->user : NULL;
	if (!cmd || !cmd[0])
		return 0;
	return send_cmd(cmd);
}

static int on_item_select(bud_event *ev)
{
	bud_node *node = ev ? (ev->current_target ? ev->current_target : ev->target) : NULL;
	const char *ref_str = node ? bud_get_attr(node, "data-item-ref") : NULL;
	if (!ref_str || !ref_str[0])
		return 0;

	uint32_t ref = (uint32_t)strtoul(ref_str, NULL, 10);
	client_state.selected_item_ref = (client_state.selected_item_ref == ref) ? 0 : ref;
#ifdef __wasm__
	bud_app_set_state();
#endif
	return 0;
}

static int on_move_k(bud_event *ev) { (void)ev; send_cmd("k"); return 0; }
static int on_move_K(bud_event *ev) { (void)ev; send_cmd("K"); return 0; }
static int on_move_h(bud_event *ev) { (void)ev; send_cmd("h"); return 0; }
static int on_move_l(bud_event *ev) { (void)ev; send_cmd("l"); return 0; }
static int on_move_j(bud_event *ev) { (void)ev; send_cmd("j"); return 0; }
static int on_move_J(bud_event *ev) { (void)ev; send_cmd("J"); return 0; }

/* Generic dock-button click: dispatch the action id carried by the button to
 * whichever module registered for it. The shell never names the modules. */
static int on_dock_action(bud_event *ev)
{
	if (ev)
		bud_event_stop_propagation(ev);
	const char *id = (ev && ev->target) ? bud_get_attr(ev->target, "data-nd-action") : NULL;
	if (id && id[0])
		on_nd_ui_action(id);
	return 0;
}

static int on_action_look(bud_event *ev) { (void)ev; send_cmd("look"); return 0; }
static int on_action_inv(bud_event *ev) { (void)ev; send_cmd("inventory"); return 0; }

static int on_cycle_viewport(bud_event *ev)
{
	(void)ev;
	if (g_driver_count > 1) {
		g_active_driver_idx = (g_active_driver_idx + 1) % (int)g_driver_count;
		const nd_viewport_driver_t *cur = nd_get_active_viewport_driver();
		if (cur && cur->init)
			cur->init();
#ifdef __wasm__
		bud_app_set_state();
#endif
	}
	return 0;
}

static bud_node *make_btn(const char *label, const char *title, bud_event_handler_fn handler)
{
	bud_node *btn = bud_element("button");
	bud_set_attr(btn, "type", "button");
	bud_set_attr(btn, "class", "nd-pad btn round border border-neutral-700 hover:bg-neutral-800 text-neutral-200 shadow-md text-base select-none");
	if (title && title[0])
		bud_set_attr(btn, "title", title);
	bud_append(btn, bud_text(label));
	if (handler)
		bud_bind(btn, "click", 1, handler);
	return btn;
}

bud_node *nd_ui_dock_button(const char *icon, const char *title, const char *action_id)
{
	bud_node *btn = make_btn(icon ? icon : "", title, on_dock_action);
	if (action_id && action_id[0])
		bud_set_attr(btn, "data-nd-action", action_id);
	return btn;
}

static bud_node *render_direction_keypad(void)
{
	bud_node *grid = bud_element("div");
	bud_set_attr(grid, "id", "directions");
	bud_set_attr(grid, "class", "nd-keypad text-center select-none");

	bud_append(grid, bud_element("div"));
	bud_append(grid, make_btn("↑", "North (k)", on_move_k));
	bud_append(grid, make_btn("↗", "Northeast (K)", on_move_K));

	bud_append(grid, make_btn("←", "West (h)", on_move_h));
	bud_append(grid, bud_element("div"));
	bud_append(grid, make_btn("→", "East (l)", on_move_l));

	bud_append(grid, bud_element("div"));
	bud_append(grid, make_btn("↓", "South (j)", on_move_j));
	bud_append(grid, make_btn("↘", "Southeast (J)", on_move_J));

	return grid;
}

static bud_node *render_gauges_slot(const nd_client_state_t *st)
{
	bud_node *node = nd_ui_glue_render_slot(ND_UI_SLOT_GAUGES, st);
	if (node)
		return node;

	/* No module claimed the gauges slot: render nothing. */
	return bud_element("div");
}

static bud_node *render_action_bar(const nd_client_state_t *st)
{
	bud_node *dock = bud_element("div");
	bud_set_attr(dock, "id", "g-actions");
	bud_set_attr(dock, "class", "flex flex-wrap gap-1 items-center justify-center");

	/* Tenant modules contribute their own dock buttons */
	nd_ui_glue_contribute(ND_UI_SLOT_ACTIONS, dock, st);

	/* Shell-level commands */
	bud_append(dock, make_btn("📦", "Inventory", on_action_inv));
	bud_append(dock, make_btn("👁", "Look", on_action_look));

	/* Dynamic viewport cycle button if multiple drivers registered */
	if (g_driver_count > 1) {
		int next_idx = (g_active_driver_idx + 1) % (int)g_driver_count;
		bud_node *vbtn = make_btn(g_viewport_drivers[next_idx].label, "Switch Viewport", on_cycle_viewport);
		bud_set_attr(vbtn, "data-bud-action", "cycle-viewport");
		bud_append(dock, vbtn);
	}

	return dock;
}

static bud_node *render_modals_slot(const nd_client_state_t *st)
{
	bud_node *slot = bud_element("div");
	bud_set_attr(slot, "id", "modals-slot");

	/* Tenant modules contribute their own modals */
	nd_ui_glue_contribute(ND_UI_SLOT_MODALS, slot, st);

	return slot;
}

static bud_node *render_viewport_slot(const nd_client_state_t *st)
{
	const nd_viewport_driver_t *drv = nd_get_active_viewport_driver();
	bud_node *vport = (drv && drv->mount) ? drv->mount() : mount_tty();

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

static bud_node *render_room_contents(const nd_client_state_t *st)
{
	bud_node *wrap = bud_element("nd-contents");
	bud_set_attr(wrap, "id", "contents");
	bud_set_attr(wrap, "class", "nd-contents w-full min-h-0 overflow-y-auto max-h-36");

	int any = 0;
	if (st) {
		for (size_t i = 0; i < st->entity_count; i++) {
			const nd_entity_desc_t *e = &st->entities[i];
			if (st->me && e->ref == st->me)
				continue;
			if (st->room.ref && e->location != st->room.ref)
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
				"flex items-center gap-2 p-1 border-b border-neutral-800 text-xs cursor-pointer select-none %s",
				is_selected ? "ctb bg-neutral-800/80 border-[#9589c5]/60" : "hover:bg-neutral-900/60");
			bud_set_attr(row, "class", rcls);
			bud_bind(row, "click", 1, on_item_select);

			/* ANSI Glyph */
			char glyph[8] = {0};
			snprintf(glyph, sizeof(glyph), "%c", e->ch ? e->ch : '?');
			bud_node *g = bud_element("span");
			char gclass[64];
			snprintf(gclass, sizeof(gclass), "cf%u font-bold pointer-events-none", e->fg & 0xF);
			bud_set_attr(g, "class", gclass);
			bud_append(g, bud_text(glyph));
			bud_append(row, g);

			/* Entity Name */
			bud_node *name_span = bud_element("span");
			bud_set_attr(name_span, "class", "grow truncate pointer-events-none");
			bud_append(name_span, bud_text(e->name));
			bud_append(row, name_span);

			/* Dynamic Contextual Action Bar */
			bud_node *act_dock = bud_element("div");
			bud_set_attr(act_dock, "class", is_selected ? "flex items-center gap-1 flex-shrink-0" : "hidden");

			/* 👁 Look Action (universal) */
			bud_node *look = bud_element("button");
			bud_set_attr(look, "type", "button");
			snprintf(ref_buf, sizeof(ref_buf), "look #%u", e->ref);
			bud_set_attr(look, "data-cmd", ref_buf);
			bud_set_attr(look, "class", "nd-pad btn round text-xs");
			bud_append(look, bud_text("👁"));
			bud_bind(look, "click", 1, on_data_cmd);
			bud_append(act_dock, look);

			/* 🖐️ Get Action (ACT_GET = 4, only if in room / not in player inventory) */
			if (e->location != st->me && (e->actions & 4)) {
				bud_node *get = bud_element("button");
				bud_set_attr(get, "type", "button");
				snprintf(ref_buf, sizeof(ref_buf), "get #%u", e->ref);
				bud_set_attr(get, "data-cmd", ref_buf);
				bud_set_attr(get, "class", "nd-pad btn round text-xs");
				bud_append(get, bud_text("🖐️"));
				bud_bind(get, "click", 1, on_data_cmd);
				bud_append(act_dock, get);
			}

			/* 🪣 Drop Action (ACT_DROP = 8, only if in player inventory) */
			if (e->location == st->me && (e->actions & 8)) {
				bud_node *drop = bud_element("button");
				bud_set_attr(drop, "type", "button");
				snprintf(ref_buf, sizeof(ref_buf), "drop #%u", e->ref);
				bud_set_attr(drop, "data-cmd", ref_buf);
				bud_set_attr(drop, "class", "nd-pad btn round text-xs");
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
		bud_set_attr(empty, "class", "text-neutral-500 text-xs px-2 py-1 italic select-none");
		bud_append(empty, bud_text("nothing here"));
		bud_append(wrap, empty);
	}

	return wrap;
}

static void nd_ansi_to_html(const char *src, char *dst, size_t cap)
{
	if (!dst || cap == 0)
		return;
	dst[0] = '\0';
	if (!src || !src[0])
		return;

	size_t w = 0;
	int state = 0; /* 0: normal, 1: esc, 2: csi */
	int fg = 7;
	int bg = 0;
	int bold = 0;
	int span_open = 0;
	int params[16] = {0};
	int nparams = 0;
	int cur_val = 0;
	int has_val = 0;

	for (size_t i = 0; src[i] && w + 40 < cap; i++) {
		unsigned char c = (unsigned char)src[i];
		if (state == 0) {
			if (c == '\x1b') {
				state = 1;
				continue;
			}
			if (c == '<') {
				if (w + 4 < cap) { memcpy(dst + w, "&lt;", 4); w += 4; }
			} else if (c == '>') {
				if (w + 4 < cap) { memcpy(dst + w, "&gt;", 4); w += 4; }
			} else if (c == '&') {
				if (w + 5 < cap) { memcpy(dst + w, "&amp;", 5); w += 5; }
			} else if (c == '\r' || c == '\0' || c == '\x7f' || c == '\v' || c == '\f') {
				continue;
			} else {
				if (!span_open && (fg != 7 || bg != 0 || bold)) {
					int sn = snprintf(dst + w, cap - w, "<span class=\"cf%d c%d\">", fg, bg);
					if (sn > 0) { w += (size_t)sn; span_open = 1; }
				}
				dst[w++] = c;
			}
		} else if (state == 1) {
			if (c == '[') {
				state = 2;
				nparams = 0;
				cur_val = 0;
				has_val = 0;
			} else {
				state = 0;
			}
		} else if (state == 2) {
			if (c >= '0' && c <= '9') {
				cur_val = cur_val * 10 + (c - '0');
				has_val = 1;
			} else if (c == ';') {
				if (nparams < 16) {
					params[nparams++] = has_val ? cur_val : 0;
				}
				cur_val = 0;
				has_val = 0;
			} else if (c == 'm') {
				if (has_val || nparams > 0) {
					if (nparams < 16)
						params[nparams++] = cur_val;
				} else {
					if (nparams < 16)
						params[nparams++] = 0;
				}

				for (int p = 0; p < nparams; p++) {
					int val = params[p];
					if (val == 0) {
						fg = 7;
						bg = 0;
						bold = 0;
					} else if (val == 1) {
						bold = 1;
						if (fg < 8) fg += 8;
					} else if (val == 22) {
						bold = 0;
						if (fg >= 8) fg -= 8;
					} else if (val >= 30 && val <= 37) {
						fg = (bold ? 8 : 0) + (val - 30);
					} else if (val == 39) {
						fg = bold ? 15 : 7;
					} else if (val >= 40 && val <= 47) {
						bg = val - 40;
					} else if (val == 49) {
						bg = 0;
					}
				}

				if (span_open) {
					if (w + 7 < cap) {
						memcpy(dst + w, "</span>", 7);
						w += 7;
						span_open = 0;
					}
				}
				if (fg != 7 || bg != 0 || bold) {
					int sn = snprintf(dst + w, cap - w, "<span class=\"cf%d c%d\">", fg, bg);
					if (sn > 0) {
						w += (size_t)sn;
						span_open = 1;
					}
				}
				state = 0;
			} else {
				state = 0;
			}
		}
	}

	if (span_open && w + 7 < cap) {
		memcpy(dst + w, "</span>", 7);
		w += 7;
	}
	dst[w < cap ? w : cap - 1] = '\0';
}

static bud_node *render_room_map(const nd_client_state_t *st)
{
	bud_node *map = bud_element("nd-map");
	bud_set_attr(map, "id", "room-map");
	bud_set_attr(map, "class", "font-mono whitespace-pre text-[10px] leading-tight text-green-400 bg-black/60 p-2 rounded border border-neutral-700/60 overflow-hidden select-none text-center shadow-inner");
	if (st && st->map_buf[0]) {
		char html[8192];
		nd_ansi_to_html(st->map_buf, html, sizeof(html));
		bud_append(map, bud_raw(html));
	} else if (st && st->room.desc[0]) {
		bud_append(map, bud_text(st->room.desc));
	} else {
		bud_append(map, bud_text("+---+---+---+\n|   |   |   |\n+---#   #---+\n|   # @ #   |\n+---#   #---+\n|   |   |   |\n+---+---+---+"));
	}
	return map;
}

bud_node *nd_ui_render(const nd_client_state_t *st)
{
	nd_ui_init_defaults();

	bud_node *vport = render_viewport_slot(st);
	bud_node *keypad = render_direction_keypad();
	bud_node *map = render_room_map(st);
	bud_node *contents = render_room_contents(st);
	bud_node *gauges = render_gauges_slot(st);
	bud_node *actions = render_action_bar(st);
	bud_node *modals = render_modals_slot(st);

	/* Target card if entity targeted */
	int has_target = (st && st->target_ref);
	bud_node *target_card = bud_element("nd-look-at");
	bud_set_attr(target_card, "id", "target-card");
	char tcls[160];
	snprintf(tcls, sizeof(tcls), "%sw-full p-2 bg-neutral-800/80 rounded border border-[#9589c5] text-xs font-bold text-neutral-100 flex items-center gap-2",
		has_target ? "" : "hidden ");
	bud_set_attr(target_card, "class", tcls);
	bud_append(target_card, bud_text("🎯 Target #"));
	char tref[16] = "0";
	if (has_target)
		snprintf(tref, sizeof(tref), "%u", st->target_ref);
	bud_append(target_card, bud_text(tref));

	bud_node *cb = bud_element("div");
	bud_set_attr(cb, "id", "cb");
	bud_set_attr(cb, "class", "flex flex-col gap-2 items-center w-full flex-1 min-h-0 overflow-y-auto");
	bud_append(cb, target_card);
	bud_append(cb, map);
	bud_append(cb, keypad);
	bud_append(cb, contents);

	bud_node *rp_bottom = bud_element("div");
	bud_set_attr(rp_bottom, "class", "w-full flex-shrink-0 flex flex-col gap-2 pt-2 border-t border-neutral-700/40 items-center");
	bud_append(rp_bottom, gauges);
	bud_append(rp_bottom, actions);

	bud_node *rp = bud_element("div");
	bud_set_attr(rp, "id", "right-panel");
	bud_set_attr(rp, "class", "flex flex-col gap-2 w-80 max-w-[340px] h-full flex-shrink-0 backdrop-blur-[4px] bg-black/40 p-2 border border-neutral-800/80 justify-between select-none overflow-hidden");
	bud_append(rp, cb);
	bud_append(rp, rp_bottom);

	bud_node *holder = bud_element("div");
	bud_set_attr(holder, "id", "holder");
	char hcls[128];
	snprintf(hcls, sizeof(hcls), "nd terminal-mode flex flex-1 gap-2 p-2 min-h-0 h-full overflow-hidden %s",
		(st && st->target_ref) ? "targetting" : "");
	bud_set_attr(holder, "class", hcls);
	bud_append(holder, vport);
	bud_append(holder, rp);

	bud_node *main_box = bud_element("div");
	bud_set_attr(main_box, "id", "main");
	char mcls[160];
	snprintf(mcls, sizeof(mcls), "w-full h-full flex flex-col box-border %s",
		(st && st->tod == 1) ? "night" : "day");
	bud_set_attr(main_box, "class", mcls);
	bud_append(main_box, holder);
	bud_append(main_box, modals);

	const char *user = (st && st->user[0]) ? st->user : client_state.user;
	bud_node *layout = site_ui_layout("NeverDark", "/nd", "🕹️", user, NULL, main_box);

	bud_node *root = bud_tpl(
		"<div id='bud-root' data-capabilities='axil-ws axil-tty lamina bud xylem' "
		"class='w-full h-[calc(100vh-64px)] box-border font-[\"Lucida_Console\",Monaco,monospace]'>%node</div>",
		layout);
	bud_bind(root, "cycle-viewport", 1, on_action_dispatch);
	bud_bind(root, "item-select", 1, on_action_dispatch);
	bud_bind(root, "cmd", 1, on_cmd_action);
	return root;
}

ISO_EXPORT(void, wasm_canvas_render, void)
{
	const nd_viewport_driver_t *drv = nd_get_active_viewport_driver();
	if (drv && drv->on_frame) {
		drv->on_frame(NULL, 0);
	}
}

ISO_EXPORT(void, wasm_tick, float dt, int width, int height)
{
	(void)dt;
	(void)width;
	(void)height;
	const nd_viewport_driver_t *drv = nd_get_active_viewport_driver();
	if (drv && drv->on_frame) {
		drv->on_frame(NULL, 0);
	}
}

ISO_EXPORT(bud_node *, bud_app_render, void)
{
	return nd_ui_render(&client_state);
}

/* Compact JSON sequential scanner for entities array */
static void parse_json_entities(const char *json, nd_client_state_t *st)
{
	if (!json || !st)
		return;
	const char *arr = strstr(json, "\"entities\":");
	if (!arr)
		return;
	arr = strchr(arr, '[');
	if (!arr)
		return;
	arr++;

	size_t count = 0;
	while (*arr && *arr != ']' && count < ND_MAX_ENTITIES) {
		const char *obj = strchr(arr, '{');
		if (!obj)
			break;
		const char *obj_end = strchr(obj, '}');
		if (!obj_end)
			break;

		/* Extract ref */
		const char *p = strstr(obj, "\"ref\":");
		if (p && p < obj_end) {
			st->entities[count].ref = (uint32_t)strtoul(p + 6, NULL, 10);
		}
		/* Extract name */
		p = strstr(obj, "\"name\":\"");
		if (p && p < obj_end) {
			p += 8;
			size_t nlen = 0;
			while (p + nlen < obj_end && p[nlen] != '"' && nlen < sizeof(st->entities[count].name) - 1) {
				st->entities[count].name[nlen] = p[nlen];
				nlen++;
			}
			st->entities[count].name[nlen] = '\0';
		}
		/* Extract ch */
		p = strstr(obj, "\"ch\":\"");
		if (p && p < obj_end) {
			st->entities[count].ch = p[6];
		}
		/* Extract fg */
		p = strstr(obj, "\"fg\":");
		if (p && p < obj_end) {
			st->entities[count].fg = (uint16_t)strtoul(p + 5, NULL, 10);
		}
		/* Extract loc */
		p = strstr(obj, "\"loc\":");
		if (p && p < obj_end) {
			st->entities[count].location = (uint32_t)strtoul(p + 6, NULL, 10);
		}

		count++;
		arr = obj_end + 1;
	}
	st->entity_count = count;
}

static void parse_json_actions(const char *json, nd_client_state_t *st)
{
	if (!json || !st)
		return;
	const char *arr = strstr(json, "\"actions\":");
	if (!arr)
		return;
	arr = strchr(arr, '[');
	if (!arr)
		return;
	arr++;

	size_t count = 0;
	while (*arr && *arr != ']' && count < ND_MAX_ACTIONS) {
		const char *obj = strchr(arr, '{');
		if (!obj)
			break;
		const char *obj_end = strchr(obj, '}');
		if (!obj_end)
			break;

		const char *p = strstr(obj, "\"id\":");
		if (p && p < obj_end) {
			st->actions[count].id = (uint32_t)strtoul(p + 5, NULL, 10);
		}
		p = strstr(obj, "\"label\":\"");
		if (p && p < obj_end) {
			p += 9;
			size_t nlen = 0;
			while (p + nlen < obj_end && p[nlen] != '"' && nlen < sizeof(st->actions[count].label) - 1) {
				st->actions[count].label[nlen] = p[nlen];
				nlen++;
			}
			st->actions[count].label[nlen] = '\0';
		}
		p = strstr(obj, "\"icon\":\"");
		if (p && p < obj_end) {
			p += 8;
			size_t nlen = 0;
			while (p + nlen < obj_end && p[nlen] != '"' && nlen < sizeof(st->actions[count].icon) - 1) {
				st->actions[count].icon[nlen] = p[nlen];
				nlen++;
			}
			st->actions[count].icon[nlen] = '\0';
		}

		count++;
		arr = obj_end + 1;
	}
	st->action_count = count;
}

static void parse_gauge_elem(const char *elem, size_t elen, void *user)
{
	nd_client_state_t *st = (nd_client_state_t *)user;
	char name[ND_GAUGE_NAME_MAX] = {0};
	bud_json_str_len(elem, elen, "n", name, sizeof(name));
	if (!name[0])
		return;
	nd_client_gauge_set(st, name,
		(uint16_t)bud_json_int_len(elem, elen, "val", 0),
		(uint16_t)bud_json_int_len(elem, elen, "max", 0));
}

ISO_EXPORT(void, wasm_init, const char *json, int len)
{
	nd_ui_init_defaults();
	memset(&client_state, 0, sizeof(client_state));
	nd_ui_glue_state_default(&client_state);

	if (json && len > 0) {
		client_state.me = (uint32_t)bud_json_int(json, "me", 0);
		client_state.auth_failed = (int)bud_json_int(json, "auth_failed", 0);
		bud_json_str(json, "user", client_state.user, sizeof(client_state.user));
		bud_json_str(json, "map_buf", client_state.map_buf, sizeof(client_state.map_buf));
		client_state.tod = (uint32_t)bud_json_int(json, "tod", 0);
		client_state.selected_item_ref = (uint32_t)bud_json_int(json, "selected_item_ref", 0);
		bud_json_str(json, "active_modal", client_state.active_modal, sizeof(client_state.active_modal));
		nd_ui_set_active_modal(client_state.active_modal);
		bud_json_str(json, "preferred_driver", client_state.preferred_driver, sizeof(client_state.preferred_driver));

		int v_idx = (int)bud_json_int(json, "active_viewport", 0);
		if (v_idx >= 0 && (size_t)v_idx < g_driver_count) {
			g_active_driver_idx = v_idx;
		}

		/* Rehydrate room info */
		client_state.room.ref = (uint32_t)bud_json_int(json, "room_ref", 0);
		client_state.room.exits = (uint32_t)bud_json_int(json, "room_exits", 0);
		bud_json_str(json, "room_name", client_state.room.name, sizeof(client_state.room.name));
		bud_json_str(json, "room_desc", client_state.room.desc, sizeof(client_state.room.desc));

		/* Rehydrate generic gauges, then let each module restore its own state */
		bud_json_array_for_each_key_len(json, (size_t)len, "gauges", parse_gauge_elem, &client_state);
		on_nd_state_deserialize(json, &client_state);

		/* Rehydrate entities and actions lists */
		parse_json_entities(json, &client_state);
		parse_json_actions(json, &client_state);
	}

	/* Immediately synchronize scene graph before initial render frame */
	nd_scene_sync(&g_scene, &client_state);

	ws_connect("/nd", 3);
}

/*
 * Negotiate active viewport driver based on genuine regional driver metadata
 * delivered via BCP (opcode ND_BCP_REGION) from nd-region persistence.
 */
static void nd_negotiate_regional_driver(const nd_client_state_t *st)
{
	if (!st) return;

	if (st->preferred_driver[0]) {
		nd_set_active_viewport_driver(st->preferred_driver);
	}
}

ISO_EXPORT(void, wasm_term_on_input, const char *buf, size_t len)
{
	if (buf && len > 0)
		ws_send(buf, len);
}

ISO_EXPORT(void, wasm_ws_on_open, void)
{
	/* WebSocket connected */
}

ISO_EXPORT(void, wasm_ws_on_close, int code)
{
	(void)code;
}

ISO_EXPORT(void, wasm_ws_on_channel, uint8_t channel, const uint8_t *buf, size_t len)
{
	if (!buf || len == 0)
		return;

	if (channel == 1 || (buf[0] == ND_BCP_MAGIC_0 && buf[1] == ND_BCP_MAGIC_1)) {
		int ret = nd_bcp_decode(buf, len, &client_state);
		if (ret > 0) {
			nd_negotiate_regional_driver(&client_state);
			nd_scene_sync(&g_scene, &client_state);
			bud_app_set_state();
			const nd_viewport_driver_t *drv = nd_get_active_viewport_driver();
			if (drv && drv->on_frame) {
				drv->on_frame(NULL, 0);
			}
		}
		return;
	}

	if (channel == 0) {
		/* Raw text channel is handled directly by axil-tty-bridge */
		return;
	}
}

ISO_EXPORT(void, wasm_ws_on_message, const uint8_t *buf, size_t len)
{
	if (!buf || len == 0)
		return;

	if (buf[0] == ND_BCP_MAGIC_0 && buf[1] == ND_BCP_MAGIC_1) {
		wasm_ws_on_channel(1, buf, len);
	} else {
		wasm_ws_on_channel(0, buf, len);
	}
}
