#include "nd_ui_internal.h"
#include <ttypt/axil-ws.h>
#include <ttypt/lam.h>
#include "nd_seven_shader.h"
#include "nd_seven_ground.h"
#include "nd_seven.h"

nd_client_state_t client_state;
nd_scene_t g_scene;
int g_stage_w = ND_CANVAS_W, g_stage_h = ND_CANVAS_H;
#define ND_STAGE_MIN_W 320
#define ND_STAGE_MIN_H 240
#define ND_STAGE_MAX_W 2560
#define ND_STAGE_MAX_H 1440
int g_ui_initialized = 0;
int g_seven_shaders_ready_ui = 0;

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
	bud_app_set_state();
}

const nd_client_state_t *nd_ui_get_client_state(void)
{
	return &client_state;
}

/* --- Viewport driver registry --- */

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

/* --- Command & keyboard handlers --- */

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

int on_data_cmd(bud_event *ev)
{
	if (ev) bud_event_stop_propagation(ev);
	bud_node *node = ev ? (ev->current_target ? ev->current_target : ev->target) : NULL;
	const char *cmd = node ? bud_get_attr(node, "data-cmd") : NULL;
	if (!cmd && ev && ev->target)
		cmd = bud_get_attr(ev->target, "data-cmd");
	if (!cmd || !cmd[0]) return 0;
	if (client_state.selected_item_ref) {
		client_state.selected_item_ref = 0;
		bud_app_set_state();
	}
	return send_cmd(cmd);
}

int on_cmd_action(bud_event *ev)
{
	const char *cmd = ev ? (const char *)ev->user : NULL;
	if (!cmd || !cmd[0]) return 0;
	return send_cmd(cmd);
}

int nd_ui_seven_active(const nd_client_state_t *st)
{
	return st && (strcmp(st->preferred_driver, "lamina-seven") == 0 || st->layout_mode == ND_LAYOUT_GAME_2D);
}

int on_key_up(bud_event *e)
{
	const char *key = (const char *)e->user;
	if (!key || !*key)
		return 0;
	return nd_seven_key(key, 0);
}

int on_key_down(bud_event *e)
{
	const char *key = (const char *)e->user;
	if (!key || !*key)
		return 0;

	if (strcmp(key, "Escape") == 0) {
		int changed = 0;
		const char *active = nd_ui_get_active_modal();
		if (active && active[0]) {
			nd_ui_set_active_modal("");
			changed = 1;
		}
		if (client_state.selected_item_ref) {
			client_state.selected_item_ref = 0;
			changed = 1;
		}
		if (changed) {
			bud_app_set_state();
			return 1;
		}
	}

	if (nd_ui_seven_active(&client_state)) {
		if (strcmp(key, "+") == 0 || strcmp(key, "=") == 0 || strcmp(key, "]") == 0 || strcmp(key, "PageUp") == 0) {
			nd_seven_zoom_in();
			nd_ui_seven_stage_redraw();
			bud_app_set_state();
			return 1;
		} else if (strcmp(key, "-") == 0 || strcmp(key, "_") == 0 || strcmp(key, "[") == 0 || strcmp(key, "PageDown") == 0) {
			nd_seven_zoom_out();
			nd_ui_seven_stage_redraw();
			bud_app_set_state();
			return 1;
		} else if (strcmp(key, "0") == 0 || strcmp(key, ")") == 0) {
			nd_seven_set_zoom(1.0f);
			nd_ui_seven_stage_redraw();
			bud_app_set_state();
			return 1;
		} else if (strcmp(key, ",") == 0 || strcmp(key, "<") == 0) {
			nd_seven_detail_out();
			nd_ui_seven_stage_redraw();
			bud_app_set_state();
			return 1;
		} else if (strcmp(key, ".") == 0 || strcmp(key, ">") == 0) {
			nd_seven_detail_in();
			nd_ui_seven_stage_redraw();
			bud_app_set_state();
			return 1;
		} else if (strcmp(key, "q") == 0 || strcmp(key, "Q") == 0) {
			nd_seven_orbit_cam(12.0f, 0.0f);
			return 1;
		} else if (strcmp(key, "e") == 0 || strcmp(key, "E") == 0) {
			nd_seven_orbit_cam(-12.0f, 0.0f);
			return 1;
		} else if (strcmp(key, "r") == 0 || strcmp(key, "R") == 0 || strcmp(key, "Home") == 0) {
			nd_seven_reset_cam();
			nd_seven_set_zoom(1.0f);
			bud_app_set_state();
			return 1;
		}
		if (strcmp(key, " ") == 0 || strcmp(key, "Space") == 0) {
			char act[64];
			if (nd_seven_action_execute(&client_state, act, sizeof(act)) > 0) {
				ws_send(act, strlen(act));
				return 1;
			}
		}
		{
			char tp_cmd[64];
			int tp_len = nd_seven_god_teleport_step(&client_state, key, tp_cmd, sizeof(tp_cmd));
			if (tp_len > 0) {
				ws_send(tp_cmd, (size_t)tp_len);
				nd_ui_seven_stage_redraw();
				return 1;
			}
		}
		if (nd_seven_key(key, 1))
			return 1;
	}
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

/* --- Seven stage rendering & WASM tick loop --- */

static int seven_stage_fit(int width, int height)
{
	int w = width < ND_STAGE_MIN_W ? ND_STAGE_MIN_W : width > ND_STAGE_MAX_W ? ND_STAGE_MAX_W : width;
	int h = height < ND_STAGE_MIN_H ? ND_STAGE_MIN_H : height > ND_STAGE_MAX_H ? ND_STAGE_MAX_H : height;

	g_stage_w = w;
	g_stage_h = h;
	return 1;
}

void nd_ui_seven_stage_redraw(void)
{
	if (!seven_stage_fit(g_stage_w, g_stage_h))
		return;

	int64_t wx = 0, wy = 0;
	nd_seven_world_pos(&client_state, &wx, &wy);
	float wz = nd_seven_ground_elevation_f((int32_t)wx, (int32_t)wy);
	float zoom = nd_seven_get_zoom();
	float eff_cam_dist = ND_SEVEN_CAM_DIST / zoom;

	uint16_t world = (uint16_t)client_state.map_data.pos[3];
	float yaw_deg = nd_seven_get_yaw_deg();
	float eff_pitch = nd_seven_eval_effective_pitch(nd_seven_get_pitch_deg(), eff_cam_dist);
	float pitch_deg = nd_seven_clamp_cam_pitch(world, (float)wx, (float)wy, wz,
	                                           eff_cam_dist, yaw_deg, eff_pitch);

	lam_cam_set_3d((float)wx, (float)wy, wz, eff_cam_dist, pitch_deg, yaw_deg, ND_SEVEN_CAM_FOV);
	nd_seven_render((uint32_t)g_stage_w, (uint32_t)g_stage_h, &client_state);
	lam_cam_set_2d();

	int ready_now = nd_seven_shaders_ready();
	if (ready_now != g_seven_shaders_ready_ui) {
		g_seven_shaders_ready_ui = ready_now;
		bud_app_set_state();
	}
}

int g_map_dirty = 0;

ISO_EXPORT(void, wasm_canvas_render, void)
{
	if (nd_ui_seven_active(&client_state))
		nd_ui_seven_stage_redraw();
	nd_map_canvas_redraw(&client_state);
}

ISO_EXPORT(void, wasm_tick, float dt, int width, int height)
{
	static uint32_t s_tick_counter = 0;
	static int s_prev_seven_active = 0;
	int now_seven = nd_ui_seven_active(&client_state);

	if (now_seven && width > 0 && height > 0)
		seven_stage_fit(width, height);
	s_tick_counter++;

	/* Phase 2: Reconciliation (Ingest completed in wasm_ws_msg) */
	if (now_seven != s_prev_seven_active) {
		/* Driver handshake: entering or exiting Seven stage */
		nd_seven_reset_input();
		nd_win_cancel_orbit();
		if (now_seven)
			nd_seven_sync_origin(&client_state);
		s_prev_seven_active = now_seven;
	}

	nd_seven_sync_origin(&client_state);
	sync_room_objects(&client_state);

	int map_on = nd_map_canvas_enabled(&client_state);

	/* Phase 3 & 4: Simulation + Presentation at capped FPS */
	if (now_seven) {
		static float s_render_accum_dt = 0.0f;
		int fps_cap = nd_seven_get_fps_cap();
		float min_dt = (fps_cap == 30) ? 0.031f : (fps_cap == 60 ? 0.0155f : 0.0f);
		s_render_accum_dt += (dt > 0.0f ? dt : 0.016f);
		if (min_dt <= 0.0f || s_render_accum_dt >= min_dt || g_map_dirty) {
			float sim_dt = (s_render_accum_dt > 0.0f) ? s_render_accum_dt : (dt > 0.0f ? dt : 0.016f);
			s_render_accum_dt = 0.0f;
			/* Run simulation tick at the capped frame rate */
			int mv = nd_seven_tick(&client_state, sim_dt);
			if (mv) {
				char m[2] = { (char)mv, '\n' };
				ws_send(m, 2);
			}
			nd_ui_seven_stage_redraw();
		}
		int map_vis = map_on && !nd_ui_get_win_minimized(ND_WIN_MAP) &&
		              client_state.target_ref == 0;
		if (map_vis) {
			static int s_last_map_lx = -999999;
			static int s_last_map_ly = -999999;
			static int s_last_map_dir = -1;
			static uint32_t s_last_map_tod = 0xFFFFFFFFu;
			int cur_lx = nd_seven_get_lx();
			int cur_ly = nd_seven_get_ly();
			int cur_dir = nd_seven_get_dir();
			int moved = (cur_lx != s_last_map_lx || cur_ly != s_last_map_ly || cur_dir != s_last_map_dir);
			int tod_changed = (client_state.tod != s_last_map_tod);
			if (g_map_dirty || tod_changed || (moved && (s_tick_counter % 4) == 0)) {
				nd_map_canvas_redraw(&client_state);
				g_map_dirty = 0;
				s_last_map_lx = cur_lx;
				s_last_map_ly = cur_ly;
				s_last_map_dir = cur_dir;
				s_last_map_tod = client_state.tod;
			}
		}
	} else if (map_on && (g_map_dirty || (s_tick_counter % 3) == 0)) {
		/* Server frames only mark the map dirty (nd_state_io.c); the
		 * redraw happens here, never inside the websocket handler. */
		nd_map_canvas_redraw(&client_state);
		g_map_dirty = 0;
	}

	if (!now_seven && !map_on) {
		g_map_dirty = 0;
		nd_seven_ground_cache_forget();
	}
}

#include "../../common/site_wasm_app.h"

static bud_node *nd_stl_render(void)
{
	return nd_ui_render(&client_state);
}

BUD_WASM_MODULE_EXT(nd, nd_stl_init, nd_stl_render, NULL, wasm_tick, NULL, NULL);
