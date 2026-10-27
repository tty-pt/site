#ifndef ND_UI_INTERNAL_H
#define ND_UI_INTERNAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <bud/bud.h>
#include <bud/bud_jsx.h>
#include <bud/bud_app.h>

#include <nd/bcp.h>
#include <nd/ui-slot.h>
#include "nd_viewport.h"
#include "nd_scene.h"
#include "nd_lamina.h"
#include "nd_ui.h"

extern nd_client_state_t client_state;
extern nd_scene_t g_scene;
extern uint32_t g_lamina_fb[ND_CANVAS_W * ND_CANVAS_H];
extern int g_ui_initialized;
extern char g_active_modal[32];
extern nd_viewport_driver_t g_viewport_drivers[ND_VIEWPORT_MAX_DRIVERS];
extern size_t g_driver_count;
extern int g_active_driver_idx;

/* Map UI */
bud_node *render_room_map(const nd_client_state_t *st);
void nd_map_canvas_redraw(const nd_client_state_t *st);

/* Dock & Keypad UI */
bud_node *make_btn(const char *label, const char *title, bud_event_handler_fn handler);
bud_node *render_direction_keypad(void);
bud_node *render_action_bar(const nd_client_state_t *st);
bud_node *render_gauges_slot(const nd_client_state_t *st);
int on_dock_action(bud_event *ev);

/* State I/O */
void parse_json_entities(const char *json, nd_client_state_t *st);
void parse_json_actions(const char *json, nd_client_state_t *st);

#endif /* ND_UI_INTERNAL_H */
