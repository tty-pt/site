#ifndef ND_UI_INTERNAL_H
#define ND_UI_INTERNAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <bud/bud.h>
#include <bud/bud_jsx.h>
#include <bud/bud_reconcile.h>
#include <bud/bud_app.h>

#include <nd/bcp.h>
#include <nd/ui-slot.h>
#include "nd_viewport.h"
#include "nd_scene.h"
#include "nd_lamina.h"
#include "nd_seven.h"
#include "nd_ui.h"

extern nd_client_state_t client_state;
extern nd_scene_t g_scene;
extern int g_stage_w, g_stage_h;
extern int g_ui_initialized;
extern char g_active_modal[32];
extern nd_viewport_driver_t g_viewport_drivers[ND_VIEWPORT_MAX_DRIVERS];
extern size_t g_driver_count;
extern int g_active_driver_idx;
extern int g_user_driver_override;
extern int g_seven_shaders_ready_ui;

/* Core runtime helpers (nd_ui.c) */
int nd_ui_seven_active(const nd_client_state_t *st);
void nd_ui_seven_stage_redraw(void);
int on_data_cmd(bud_event *ev);
int on_cmd_action(bud_event *ev);
int on_key_up(bud_event *e);
int on_key_down(bud_event *e);

/* Floating HUD window manager & orbit input (nd_win.c) */
const char *nd_win_persist_key(int win_id);
void nd_win_meta(int win_id, const char **id_suf, const char **icon, const char **label);
int nd_win_is_dragging(int win_id);
void nd_win_format_style(int win_id, char *buf, size_t cap);
void nd_win_sync_target(const nd_client_state_t *st);
void nd_win_cancel_orbit(void);
int nd_win_get_popper_top_px(void);
int on_restore_persist(bud_event *ev);
int on_win_drag_down(bud_event *ev);
int on_win_drag_move(bud_event *ev);
int on_win_drag_up(bud_event *ev);
int on_stage_orbit_down(bud_event *ev);
int on_stage_orbit_wheel(bud_event *ev);
int on_stage_orbit_dblclick(bud_event *ev);
int on_win_minimize(bud_event *ev);
int on_win_restore(bud_event *ev);
int on_win_reset(bud_event *ev);
int on_toggle_chat(bud_event *ev);
int on_item_select(bud_event *ev);
int on_dismiss_target(bud_event *ev);

/* Map UI (nd_map_ui.cx) */
bud_node *render_room_map(const nd_client_state_t *st);
void nd_map_canvas_redraw(const nd_client_state_t *st);
int nd_map_canvas_enabled(const nd_client_state_t *st);
/* Set by the websocket handler; the next wasm_tick redraws the map. The
 * handler itself never renders or builds chunks. */
extern int g_map_dirty;

/* Dock & Keypad UI (nd_dock_ui.cx) */
bud_node *make_btn(const char *label, const char *title, bud_event_handler_fn handler);
bud_node *render_direction_keypad(void);
bud_node *render_action_bar(const nd_client_state_t *st);
bud_node *render_gauges_slot(const nd_client_state_t *st);
int on_dock_action(bud_event *ev);
int on_cycle_viewport(bud_event *ev);

/* Settings modal UI (nd_settings_ui.cx) */
bud_node *render_settings_modal(const nd_client_state_t *st);

/* Floating HUD chrome & panels (nd_hud_ui.cx) */
void append_win_chrome(bud_node *win, int win_id, const char *id_prefix, const char *title_txt, int has_expand);
bud_node *render_rpg_chat_terminal(const nd_client_state_t *st);
bud_node *render_room_contents(const nd_client_state_t *st);
bud_node *render_item_popper(const nd_client_state_t *st);
bud_node *render_target_card(const nd_client_state_t *st);
bud_node *render_closed_windows_dock(void);
bud_node *render_room_header(const nd_client_state_t *st);

/* Shell mounts & layout assembly (nd_shell_ui.cx) */
bud_node *mount_tty(void);
bud_node *mount_seven_stage(void);

/* State I/O (nd_state_io.c) */
void parse_json_entities(const char *json, nd_client_state_t *st);
void parse_json_actions(const char *json, nd_client_state_t *st);

#endif /* ND_UI_INTERNAL_H */
