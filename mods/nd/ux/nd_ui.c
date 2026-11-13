#include "nd_ui_internal.h"
#include <ttypt/axil-ws.h>
#include <ttypt/lam.h>
#include "site_ui.h"
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

static void seven_stage_redraw(void);

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

typedef struct {
	int x;
	int y;
	int w;
	int h;
	int minimized;
} nd_hud_win_t;

static nd_hud_win_t s_win[ND_WIN_COUNT] = {
	{ -1, -1, 0, 0, 0 },
	{ -1, -1, 0, 0, 0 },
	{ -1, -1, 0, 0, 0 },
	{ -1, -1, 0, 0, 0 },
	{ -1, -1, 0, 0, 0 },
	{ -1, -1, 0, 0, 0 }
};

static int s_chat_expanded = 0;
static uint32_t s_prev_target_ref = 0;
static uint32_t s_last_target_ref = 0;
static int s_target_win_pinned = 0;
static int s_popper_top_px = 62;

/* Drag / Resize interaction state:
 * mode bitmask:
 *   1 = N (top), 2 = E (right), 4 = S (bottom), 8 = W (left), 16 = MOVE
 */
#define ND_DRAG_N    1
#define ND_DRAG_E    2
#define ND_DRAG_S    4
#define ND_DRAG_W    8
#define ND_DRAG_MOVE 16

static int s_drag_win = -1;
static int s_drag_mode = 0;
static int s_drag_start_mx = 0;
static int s_drag_start_my = 0;
static int s_drag_start_x = 0;
static int s_drag_start_y = 0;
static int s_drag_start_w = 0;
static int s_drag_start_h = 0;

static const char *const s_win_persist_keys[ND_WIN_COUNT] = {
	"nd-win-chat",
	"nd-win-title",
	"nd-win-map",
	"nd-win-contents",
	"nd-win-actions",
	"nd-win-target"
};

static const struct {
	const char *id_suf;
	const char *icon;
	const char *label;
} s_win_meta[ND_WIN_COUNT] = {
	{ "chat",     "💬", "Open Chat & Console" },
	{ "title",    "📍", "Open Room Title" },
	{ "map",      "🗺️", "Open Minimap" },
	{ "contents", "📋", "Open Room Contents" },
	{ "actions",  "⚡", "Open Actions" },
	{ "target",   "🖼", "Open Look-At" }
};

static void get_win_limits(int win_id, int *min_w, int *min_h, int *def_w, int *def_h, int vw, int vh)
{
	int eff_vw = (vw > 0) ? vw : 1080;
	int eff_vh = (vh > 0) ? vh : 720;
	(void)eff_vh;
	switch (win_id) {
	case ND_WIN_CHAT:
		*min_w = 260;
		*min_h = 110;
		*def_w = eff_vw / 2;
		if (*def_w < 360) *def_w = 360;
		*def_h = s_chat_expanded ? 420 : 180;
		break;
	case ND_WIN_TITLE:
		*min_w = 160;
		*min_h = 30;
		*def_w = 280;
		*def_h = 36;
		break;
	case ND_WIN_MAP:
		*min_w = 140;
		*min_h = 140;
		*def_w = 220;
		*def_h = 220;
		break;
	case ND_WIN_CONTENTS:
		*min_w = 200;
		*min_h = 120;
		*def_w = 300;
		*def_h = 240;
		break;
	case ND_WIN_TARGET:
		*min_w = 140;
		*min_h = 140;
		*def_w = 220;
		*def_h = 220;
		break;
	case ND_WIN_ACTIONS:
	default:
		*min_w = 160;
		*min_h = 46;
		*def_w = 240;
		*def_h = 54;
		break;
	}
}

static void get_win_default_pos(int win_id, int vw, int vh, int cur_w, int cur_h, int *dx, int *dy)
{
	int eff_vw = (vw > 0) ? vw : 1080;
	int eff_vh = (vh > 0) ? vh : 720;
	switch (win_id) {
	case ND_WIN_CHAT:
		*dx = 16;
		*dy = eff_vh - cur_h - 16;
		if (*dy < 26) *dy = 26;
		break;
	case ND_WIN_TITLE:
		*dx = (eff_vw - cur_w) / 2;
		if (*dx < 8) *dx = 8;
		*dy = 26;
		break;
	case ND_WIN_MAP:
		*dx = eff_vw - cur_w - 16;
		if (*dx < 8) *dx = 8;
		*dy = 28;
		break;
	case ND_WIN_TARGET:
		*dx = eff_vw - cur_w - 16;
		if (*dx < 8) *dx = 8;
		*dy = 256;
		break;
	case ND_WIN_CONTENTS:
		*dx = eff_vw - cur_w - 16;
		if (*dx < 8) *dx = 8;
		*dy = 256;
		break;
	case ND_WIN_ACTIONS:
	default:
		*dx = eff_vw - cur_w - 16;
		if (*dx < 8) *dx = 8;
		*dy = eff_vh - cur_h - 16;
		if (*dy < 26) *dy = 26;
		break;
	}
}

void nd_ui_set_win_rect(int win_id, int x, int y, int w, int h)
{
	if (win_id < 0 || win_id >= ND_WIN_COUNT) return;
	s_win[win_id].x = x;
	s_win[win_id].y = y;
	s_win[win_id].w = w > 0 ? w : 0;
	s_win[win_id].h = h > 0 ? h : 0;
}

void nd_ui_get_win_rect(int win_id, int *x, int *y, int *w, int *h)
{
	if (win_id < 0 || win_id >= ND_WIN_COUNT) return;
	if (x) *x = s_win[win_id].x;
	if (y) *y = s_win[win_id].y;
	if (w) *w = s_win[win_id].w;
	if (h) *h = s_win[win_id].h;
}

void nd_ui_set_win_minimized(int win_id, int minimized)
{
	if (win_id < 0 || win_id >= ND_WIN_COUNT) return;
	s_win[win_id].minimized = minimized ? 1 : 0;
}

int nd_ui_get_win_minimized(int win_id)
{
	if (win_id < 0 || win_id >= ND_WIN_COUNT) return 0;
	return s_win[win_id].minimized;
}

void nd_ui_reset_win(int win_id)
{
	if (win_id < 0 || win_id >= ND_WIN_COUNT) return;
	s_win[win_id].x = -1;
	s_win[win_id].y = -1;
	s_win[win_id].w = 0;
	s_win[win_id].h = 0;
	s_win[win_id].minimized = 0;
	if (win_id == ND_WIN_CHAT)
		s_chat_expanded = 0;
}

int nd_ui_get_chat_expanded(void)
{
	return s_chat_expanded;
}

void nd_ui_set_chat_expanded(int expanded)
{
	s_chat_expanded = expanded ? 1 : 0;
}

int nd_ui_get_chat_minimized(void)
{
	return s_win[ND_WIN_CHAT].minimized;
}

void nd_ui_set_chat_minimized(int minimized)
{
	s_win[ND_WIN_CHAT].minimized = minimized ? 1 : 0;
}

int nd_ui_get_chat_width(void)
{
	return s_win[ND_WIN_CHAT].w;
}

int nd_ui_get_chat_height(void)
{
	return s_win[ND_WIN_CHAT].h;
}

void nd_ui_set_chat_size(int width, int height)
{
	s_win[ND_WIN_CHAT].w = width > 0 ? width : 0;
	s_win[ND_WIN_CHAT].h = height > 0 ? height : 0;
}

static int parse_style_px(const char *style, const char *prop, int def_val)
{
	if (!style || !prop) return def_val;
	const char *p = strstr(style, prop);
	while (p) {
		if (p == style || p[-1] == ';' || p[-1] == ' ') {
			return (int)strtol(p + strlen(prop), NULL, 10);
		}
		p = strstr(p + 1, prop);
	}
	return def_val;
}

void nd_ui_restore_persist(const char *key, const char *val)
{
	if (!key || !val) return;
	for (int i = 0; i < ND_WIN_COUNT; i++) {
		if (strcmp(key, s_win_persist_keys[i]) == 0) {
			int min_w, min_h, def_w, def_h;
			get_win_limits(i, &min_w, &min_h, &def_w, &def_h, 0, 0);
			int rx = parse_style_px(val, "left:", -1);
			int ry = parse_style_px(val, "top:", -1);
			int rw = parse_style_px(val, "width:", 0);
			int rh = parse_style_px(val, "height:", 0);
			if (rx >= 0 && ry >= 0 && ry < 24) ry = 24;
			if (rw > 0 && rw < min_w) rw = min_w;
			if (rh > 0 && rh < min_h) rh = min_h;
			s_win[i].x = rx;
			s_win[i].y = ry;
			s_win[i].w = rw;
			s_win[i].h = rh;
			if (strstr(val, "hud-win-minimized") || strstr(val, "chat-minimized"))
				s_win[i].minimized = 1;
			if (i == ND_WIN_CHAT && strstr(val, "chat-expanded"))
				s_chat_expanded = 1;
			bud_app_set_state();
			return;
		}
	}
}

static int on_restore_persist(bud_event *ev)
{
	const char *payload = ev ? (const char *)ev->user : NULL;
	if (!payload || !*payload) return 0;
	const char *sep = strchr(payload, '|');
	if (!sep) return 0;
	char key[48];
	size_t klen = (size_t)(sep - payload);
	if (klen >= sizeof(key)) klen = sizeof(key) - 1;
	memcpy(key, payload, klen);
	key[klen] = '\0';
	nd_ui_restore_persist(key, sep + 1);
	return 0;
}

static void parse_mouse_coords(bud_event *ev, int *cx, int *cy, int *vw, int *vh, int *wx, int *wy, int *ww, int *wh)
{
	*cx = 0;
	*cy = 0;
	*vw = 0;
	*vh = 0;
	if (wx) *wx = -1;
	if (wy) *wy = -1;
	if (ww) *ww = 0;
	if (wh) *wh = 0;
	const char *s = ev ? (const char *)ev->user : NULL;
	if (!s || !*s)
		return;
	int rx = -1, ry = -1, rw = 0, rh = 0;
	int n = sscanf(s, "%d,%d,%d,%d,%d,%d,%d,%d", cx, cy, vw, vh, &rx, &ry, &rw, &rh);
	if (n >= 8) {
		if (wx) *wx = rx;
		if (wy) *wy = ry;
		if (ww) *ww = rw;
		if (wh) *wh = rh;
	}
}

static int resolve_event_win(bud_event *ev, int *mode_out)
{
	if (mode_out) *mode_out = 0;
	if (!ev) return -1;
	bud_node *cur = ev->current_target ? ev->current_target : ev->target;
	int win_id = -1;
	int mode = 0;
	while (cur) {
		const char *m_str = bud_get_attr(cur, "data-win-drag");
		if (m_str && m_str[0] && mode == 0)
			mode = (int)strtol(m_str, NULL, 10);
		const char *w_str = bud_get_attr(cur, "data-win-id");
		if (w_str && w_str[0]) {
			win_id = (int)strtol(w_str, NULL, 10);
			break;
		}
		cur = bud_node_parent(cur);
	}
	if (mode_out) *mode_out = mode;
	return (win_id >= 0 && win_id < ND_WIN_COUNT) ? win_id : -1;
}

static int start_win_drag(bud_event *ev, int win_id, int mode)
{
	if (ev) bud_event_stop_propagation(ev);
	if (win_id < 0 || win_id >= ND_WIN_COUNT) return 0;
	if (s_win[win_id].minimized && mode != ND_DRAG_MOVE) return 0;

	int cx = 0, cy = 0, vw = 0, vh = 0, wx = -1, wy = -1, ww = 0, wh = 0;
	parse_mouse_coords(ev, &cx, &cy, &vw, &vh, &wx, &wy, &ww, &wh);

	int min_w, min_h, def_w, def_h;
	get_win_limits(win_id, &min_w, &min_h, &def_w, &def_h, vw, vh);
	int cur_w = (ww > 0) ? ww : ((s_win[win_id].w > 0) ? s_win[win_id].w : def_w);
	int cur_h = (wh > 0) ? wh : ((s_win[win_id].h > 0) ? s_win[win_id].h : def_h);
	if (cur_w < min_w) cur_w = min_w;
	if (cur_h < min_h) cur_h = min_h;
	int dx = 0, dy = 0;
	get_win_default_pos(win_id, vw, vh, cur_w, cur_h, &dx, &dy);
	int cur_x = (wx >= 0) ? wx : ((s_win[win_id].x >= 0) ? s_win[win_id].x : dx);
	int cur_y = (wy >= 0) ? wy : ((s_win[win_id].y >= 0) ? s_win[win_id].y : dy);
	if (cur_x < 0) cur_x = 0;
	if (cur_y < 24) cur_y = 24;

	/* Lock live coordinates & dimensions immediately so dragging/resizing never snaps */
	s_win[win_id].x = cur_x;
	s_win[win_id].y = cur_y;
	if (win_id != ND_WIN_TITLE || mode != ND_DRAG_MOVE) {
		s_win[win_id].w = cur_w;
		s_win[win_id].h = cur_h;
	}
	client_state.selected_item_ref = 0;

	s_drag_win = win_id;
	s_drag_mode = mode;
	s_drag_start_mx = cx;
	s_drag_start_my = cy;
	s_drag_start_x = cur_x;
	s_drag_start_y = cur_y;
	s_drag_start_w = cur_w;
	s_drag_start_h = cur_h;
	bud_app_set_state();
	return 0;
}

static int on_win_drag_down(bud_event *ev)
{
	if (ev && ev->target) {
		const char *tag = bud_node_tag(ev->target);
		if (tag && strcmp(tag, "button") == 0)
			return 0;
	}
	int mode = 0;
	int win_id = resolve_event_win(ev, &mode);
	if (win_id < 0 || mode == 0) return 0;
	return start_win_drag(ev, win_id, mode);
}

static int on_win_drag_move(bud_event *ev)
{
	if (s_drag_win < 0 || s_drag_win >= ND_WIN_COUNT || !s_drag_mode) return 0;
	int cx = 0, cy = 0, vw = 0, vh = 0;
	parse_mouse_coords(ev, &cx, &cy, &vw, &vh, NULL, NULL, NULL, NULL);
	if (cx == 0 && cy == 0 && vw == 0 && vh == 0) return 0;

	int win_id = s_drag_win;
	int min_w, min_h, def_w, def_h;
	get_win_limits(win_id, &min_w, &min_h, &def_w, &def_h, vw, vh);
	int max_w = (vw > 40) ? (vw - 16) : 1800;
	int max_h = (vh > 40) ? (vh - 16) : 1200;
	if (max_w < min_w) max_w = min_w;
	if (max_h < min_h) max_h = min_h;

	int dx = cx - s_drag_start_mx;
	int dy = cy - s_drag_start_my;

	if (s_drag_mode == ND_DRAG_MOVE) {
		int nx = s_drag_start_x + dx;
		int ny = s_drag_start_y + dy;
		int lim_x = (vw > 80) ? (vw - 60) : 1800;
		int lim_y = (vh > 50) ? (vh - 36) : 1200;
		if (nx < 0) nx = 0;
		if (ny < 24) ny = 24;
		if (nx > lim_x) nx = lim_x;
		if (ny > lim_y) ny = lim_y;
		s_win[win_id].x = nx;
		s_win[win_id].y = ny;
	} else {
		int nx = s_drag_start_x;
		int ny = s_drag_start_y;
		int nw = s_drag_start_w;
		int nh = s_drag_start_h;

		if (s_drag_mode & ND_DRAG_E) {
			nw = s_drag_start_w + dx;
			if (nw < min_w) nw = min_w;
			if (nw > max_w) nw = max_w;
			s_win[win_id].w = nw;
		}
		if (s_drag_mode & ND_DRAG_S) {
			nh = s_drag_start_h + dy;
			if (nh < min_h) nh = min_h;
			if (nh > max_h) nh = max_h;
			s_win[win_id].h = nh;
		}
		if (s_drag_mode & ND_DRAG_W) {
			int right_edge = s_drag_start_x + s_drag_start_w;
			nw = s_drag_start_w - dx;
			if (nw < min_w) nw = min_w;
			if (nw > max_w) nw = max_w;
			nx = right_edge - nw;
			if (nx < 0) {
				nx = 0;
				nw = right_edge;
				if (nw < min_w) nw = min_w;
			}
			s_win[win_id].x = nx;
			s_win[win_id].w = nw;
		}
		if (s_drag_mode & ND_DRAG_N) {
			int bottom_edge = s_drag_start_y + s_drag_start_h;
			nh = s_drag_start_h - dy;
			if (nh < min_h) nh = min_h;
			if (nh > max_h) nh = max_h;
			ny = bottom_edge - nh;
			if (ny < 24) {
				ny = 24;
				nh = bottom_edge - 24;
				if (nh < min_h) nh = min_h;
			}
			s_win[win_id].y = ny;
			s_win[win_id].h = nh;
		}
	}
	bud_app_set_state();
	return 0;
}

static int on_win_drag_up(bud_event *ev)
{
	(void)ev;
	if (s_drag_win < 0 && !s_drag_mode) return 0;
	s_drag_win = -1;
	s_drag_mode = 0;
	bud_app_set_state();
	return 0;
}

static int on_win_minimize(bud_event *ev)
{
	if (ev) bud_event_stop_propagation(ev);
	int win_id = resolve_event_win(ev, NULL);
	if (win_id < 0) return 0;
	s_drag_win = -1;
	s_drag_mode = 0;
	client_state.selected_item_ref = 0;
	s_win[win_id].minimized = !s_win[win_id].minimized;
	if (win_id == ND_WIN_TARGET && s_win[win_id].minimized) {
		if (client_state.target_ref)
			s_last_target_ref = client_state.target_ref;
		client_state.target_ref = 0;
		s_target_win_pinned = 0;
	}
	bud_app_set_state();
	return 0;
}

static int on_win_restore(bud_event *ev)
{
	if (ev) bud_event_stop_propagation(ev);
	int win_id = resolve_event_win(ev, NULL);
	if (win_id < 0) return 0;
	s_drag_win = -1;
	s_drag_mode = 0;
	s_win[win_id].minimized = 0;
	if (win_id == ND_WIN_TARGET) {
		s_target_win_pinned = 1;
		if (!client_state.target_ref && s_last_target_ref)
			client_state.target_ref = s_last_target_ref;
	}
	bud_app_set_state();
	return 0;
}

static int on_win_reset(bud_event *ev)
{
	if (ev) bud_event_stop_propagation(ev);
	int win_id = resolve_event_win(ev, NULL);
	if (win_id < 0) return 0;
	s_drag_win = -1;
	s_drag_mode = 0;
	client_state.selected_item_ref = 0;
	nd_ui_reset_win(win_id);
	bud_app_set_state();
	return 0;
}

static int on_toggle_chat(bud_event *ev)
{
	if (ev) bud_event_stop_propagation(ev);
	s_drag_win = -1;
	s_drag_mode = 0;
	s_win[ND_WIN_CHAT].h = 0;
	if (s_win[ND_WIN_CHAT].minimized) {
		s_win[ND_WIN_CHAT].minimized = 0;
		s_chat_expanded = 1;
	} else {
		s_chat_expanded = !s_chat_expanded;
	}
	bud_app_set_state();
	return 0;
}

static void format_win_style(int win_id, char *buf, size_t cap)
{
	buf[0] = '\0';
	if (win_id < 0 || win_id >= ND_WIN_COUNT) return;
	const nd_hud_win_t *w = &s_win[win_id];
	size_t len = 0;
	if (w->x >= 0 && w->y >= 0) {
		int n = snprintf(buf + len, cap - len, "left:%dpx;top:%dpx;right:auto;bottom:auto;transform:none;", w->x, w->y);
		if (n > 0) len += (size_t)n;
	}
	if (!w->minimized) {
		if (w->w > 0 && len + 24 < cap) {
			int n = snprintf(buf + len, cap - len, "width:%dpx;", w->w);
			if (n > 0) len += (size_t)n;
		}
		if (w->h > 0 && len + 24 < cap) {
			int n = snprintf(buf + len, cap - len, "height:%dpx;", w->h);
			if (n > 0) len += (size_t)n;
		}
	}
}

static void append_win_chrome(bud_node *win, int win_id, const char *id_prefix, const char *title_txt, int has_expand)
{
	char wid_str[16];
	snprintf(wid_str, sizeof(wid_str), "%d", win_id);
	bud_set_attr(win, "data-win-id", wid_str);
	bud_set_attr(win, "data-bud-persist-key", s_win_persist_keys[win_id]);

	char style_buf[160];
	format_win_style(win_id, style_buf, sizeof(style_buf));
	if (style_buf[0])
		bud_set_attr(win, "style", style_buf);

	static const struct { const char *suf; const char *cls; const char *mode; } handles[] = {
		{ "resize-n",  "chat-resize-handle chat-resize-n",  "1" },
		{ "resize-e",  "chat-resize-handle chat-resize-e",  "2" },
		{ "resize-s",  "chat-resize-handle chat-resize-s",  "4" },
		{ "resize-w",  "chat-resize-handle chat-resize-w",  "8" },
		{ "resize-ne", "chat-resize-handle chat-resize-ne", "3" },
		{ "resize-nw", "chat-resize-handle chat-resize-nw", "9" },
		{ "resize-se", "chat-resize-handle chat-resize-se", "6" },
		{ "resize-sw", "chat-resize-handle chat-resize-sw", "12" }
	};

	for (size_t i = 0; i < sizeof(handles) / sizeof(handles[0]); i++) {
		bud_node *h = bud_element("div");
		char hid[64];
		snprintf(hid, sizeof(hid), "%s-%s", id_prefix, handles[i].suf);
		bud_set_attr(h, "id", hid);
		bud_set_attr(h, "data-bud-key", hid);
		bud_set_attr(h, "class", handles[i].cls);
		bud_set_attr(h, "data-win-drag", handles[i].mode);
		bud_bind(h, "mousedown", 0, on_win_drag_down);
		bud_append(win, h);
	}

	/* Auto-hiding hover toolbar (draggable header + micro-icon controls) */
	bud_node *hdr = bud_element("div");
	char hdr_id[64];
	snprintf(hdr_id, sizeof(hdr_id), "%s-toolbar", id_prefix);
	bud_set_attr(hdr, "id", hdr_id);
	bud_set_attr(hdr, "data-bud-key", hdr_id);
	bud_set_attr(hdr, "class", "hud-win-toolbar chat-header select-none");
	bud_set_attr(hdr, "data-win-drag", "16");
	bud_bind(hdr, "mousedown", 0, on_win_drag_down);

	bud_node *title = bud_element("div");
	bud_set_attr(title, "class", "hud-win-title flex items-center gap-1.5 font-bold text-neutral-200 pointer-events-none truncate");
	bud_append(title, bud_text(title_txt));
	bud_append(hdr, title);

	bud_node *ctrls = bud_element("div");
	bud_set_attr(ctrls, "class", "chat-controls flex items-center gap-1");

	int is_min = s_win[win_id].minimized;
	(void)is_min;

	if (has_expand) {
		bud_node *exp_btn = bud_element("button");
		bud_set_attr(exp_btn, "type", "button");
		char exp_id[64];
		snprintf(exp_id, sizeof(exp_id), "%s-toggle-btn", id_prefix);
		bud_set_attr(exp_btn, "id", exp_id);
		bud_set_attr(exp_btn, "class", "chat-ctrl-btn");
		bud_set_attr(exp_btn, "title", s_chat_expanded ? "Compact" : "Expand");
		bud_append(exp_btn, bud_text(s_chat_expanded ? "⤡" : "⤢"));
		bud_bind(exp_btn, "click", 1, on_toggle_chat);
		bud_append(ctrls, exp_btn);
	}

	bud_node *rst_btn = bud_element("button");
	bud_set_attr(rst_btn, "type", "button");
	char rst_id[64];
	snprintf(rst_id, sizeof(rst_id), "%s-reset-btn", id_prefix);
	bud_set_attr(rst_btn, "id", rst_id);
	bud_set_attr(rst_btn, "class", "chat-ctrl-btn");
	bud_set_attr(rst_btn, "title", "Reset Position & Size");
	bud_append(rst_btn, bud_text("↺"));
	bud_bind(rst_btn, "click", 1, on_win_reset);
	bud_append(ctrls, rst_btn);

	bud_node *min_btn = bud_element("button");
	bud_set_attr(min_btn, "type", "button");
	char min_id[64];
	snprintf(min_id, sizeof(min_id), "%s-min-btn", id_prefix);
	bud_set_attr(min_btn, "id", min_id);
	bud_set_attr(min_btn, "class", "chat-ctrl-btn");
	bud_set_attr(min_btn, "title", "Close");
	bud_append(min_btn, bud_text("✕"));
	bud_bind(min_btn, "click", 1, on_win_minimize);
	bud_append(ctrls, min_btn);

	bud_append(hdr, ctrls);
	bud_append(win, hdr);
}

static bud_node *render_rpg_chat_terminal(const nd_client_state_t *st)
{
	(void)st;
	bud_node *chat = bud_element("div");
	bud_set_attr(chat, "id", "rpg-chat");
	bud_set_attr(chat, "data-bud-key", "rpg-chat");
	int is_min = s_win[ND_WIN_CHAT].minimized;
	int is_dragging = (s_drag_win == ND_WIN_CHAT && s_drag_mode != 0);
	char chat_cls[160];
	snprintf(chat_cls, sizeof(chat_cls), "hud-win rpg-chat %s%s pointer-events-auto",
		is_min
			? "chat-minimized hud-win-minimized"
			: (s_chat_expanded ? "chat-expanded" : "chat-compact"),
		is_dragging ? " chat-resizing hud-win-dragging" : "");
	bud_set_attr(chat, "class", chat_cls);

	append_win_chrome(chat, ND_WIN_CHAT, "chat", "💬 Chat & Console", 1);

	/* Embedded xterm container */
	bud_node *term = lx_n("div",
		lx_attr("id", "term"),
		lx_attr("data-bud-key", "term"),
		lx_attr("data-bud-terminal", "1"),
		lx_attr("data-line-mode", "true"),
		lx_attr("class", "hud-win-body flex-1 w-full h-full min-h-0 font-mono text-neutral-200 overflow-hidden relative bg-transparent"));
	bud_append(chat, term);

	return chat;
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
	 * then swallows every key press.
	 *
	 * The stage fills its holder: the canvas is resized by the lamina bridge
	 * to whatever frame size the stage draws (data-bud-canvas-fit), 1:1 with
	 * the screen, so the sprite scale never changes. */
	bud_node *stage = lx_n("nd-stage",
		lx_attr("id", "game-stage"),
		lx_attr("class", "absolute inset-0 min-w-0 min-h-0 overflow-hidden bg-neutral-950"),
		lx_node(lx_n("canvas",
			lx_attr("id", "seven-stage-canvas"),
			lx_attr("data-bud-canvas", "1"),
			lx_attr("data-bud-canvas-fit", "1"),
			lx_attr("width", "640"),
			lx_attr("height", "480"),
			lx_attr("class", "absolute inset-0 w-full h-full block select-none bg-neutral-950"))));

	return stage;
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

	const char *y_str = ev ? (const char *)ev->user : NULL;
	if (y_str && y_str[0] && ((y_str[0] >= '0' && y_str[0] <= '9') || y_str[0] == '-')) {
		int py = (int)strtol(y_str, NULL, 10);
		if (py < 8) py = 8;
		s_popper_top_px = py;
	} else {
		s_popper_top_px = 62;
	}

	uint32_t ref = (uint32_t)strtoul(ref_str, NULL, 10);
	client_state.selected_item_ref = (client_state.selected_item_ref == ref) ? 0 : ref;
	bud_app_set_state();
	return 0;
}

static int on_item_click(bud_event *ev)
{
	bud_node *node = ev ? (ev->current_target ? ev->current_target : ev->target) : NULL;
	const char *ref_str = node ? bud_get_attr(node, "data-ref") : NULL;
	if (!ref_str || !ref_str[0]) return 0;

	uint32_t ref = (uint32_t)strtoul(ref_str, NULL, 10);
	client_state.target_ref = (client_state.target_ref == ref) ? 0 : ref;
	bud_app_set_state();
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

	bud_node *wrap = bud_element("div");
	bud_set_attr(wrap, "id", "viewport-container");
	bud_set_attr(wrap, "class", "relative flex-1 flex flex-col min-w-0 min-h-0");
	bud_set_attr(wrap, "data-bud-key", "viewport-container");
	bud_append(wrap, vport);

	bud_node *overlay = nd_ui_glue_render_slot(ND_UI_SLOT_HUD_OVERLAYS, st);
	if (overlay) {
		bud_append(wrap, overlay);
	}
	return wrap;
}

static int on_dismiss_target(bud_event *ev)
{
	if (ev) bud_event_stop_propagation(ev);
	if (client_state.target_ref)
		s_last_target_ref = client_state.target_ref;
	client_state.target_ref = 0;
	client_state.selected_item_ref = 0;
	s_target_win_pinned = 0;
	bud_app_set_state();
	return 0;
}

static bud_node *render_item_popper(const nd_client_state_t *st)
{
	uint32_t sel_ref = st ? st->selected_item_ref : 0;
	if (!sel_ref || !st)
		return NULL;

	int is_inventory = (st->target_ref && st->me && st->target_ref == st->me);
	uint32_t container_target = (st->target_ref && !is_inventory) ? st->target_ref : 0;

	const nd_entity_desc_t *e = NULL;
	for (size_t i = 0; i < st->entity_count; i++) {
		const nd_entity_desc_t *cand = &st->entities[i];
		if (st->me && cand->ref == st->me)
			continue;
		if (is_inventory) {
			if (!st->me || cand->location != st->me)
				continue;
		} else if (container_target) {
			if (cand->location != container_target)
				continue;
		} else {
			if (st->me && cand->location == st->me)
				continue;
		}
		if (cand->ref == sel_ref) {
			e = cand;
			break;
		}
	}
	if (!e)
		return NULL;

	char ref_buf[32];
	snprintf(ref_buf, sizeof(ref_buf), "%u", e->ref);

	bud_node *act_dock = bud_element("div");
	bud_set_attr(act_dock, "id", "contents-item-popper");
	bud_set_attr(act_dock, "data-bud-key", "contents-item-popper");
	bud_set_attr(act_dock, "class", "nd-item-popup nd-item-popper");
	char pstyle[64];
	int top_px = s_popper_top_px > 0 ? s_popper_top_px : 62;
	snprintf(pstyle, sizeof(pstyle), "top:%dpx;right:10px;", top_px);
	bud_set_attr(act_dock, "style", pstyle);

	char look_cmd[48], get_cmd[48], drop_cmd[48];
	snprintf(look_cmd, sizeof(look_cmd), "look #%s", ref_buf);
	snprintf(get_cmd, sizeof(get_cmd), "get #%s", ref_buf);
	snprintf(drop_cmd, sizeof(drop_cmd), "drop #%s", ref_buf);

	/* Look menu item */
	bud_node *look = bud_element("button");
	bud_set_attr(look, "type", "button");
	bud_set_attr(look, "data-action", "look");
	bud_set_attr(look, "data-cmd", look_cmd);
	bud_set_attr(look, "class", "nd-popper-item btn text-xs");
	char look_title[64];
	snprintf(look_title, sizeof(look_title), "Look %s", e->name);
	bud_set_attr(look, "title", look_title);
	bud_append(look, bud_text("👁️ Look"));
	bud_bind(look, "click", 1, on_data_cmd);
	bud_append(act_dock, look);

	/* Get menu item: shown if ACT_GET (4) is set or item is gettable */
	if ((e->actions & 4) || (e->location != st->me && !(e->flags & (ND_EF_PLAYER | ND_EF_CREATURE)) && e->type != 1 && e->actions == 0)) {
		bud_node *get = bud_element("button");
		bud_set_attr(get, "type", "button");
		bud_set_attr(get, "data-action", "get");
		bud_set_attr(get, "data-cmd", get_cmd);
		bud_set_attr(get, "class", "nd-popper-item btn text-xs");
		bud_set_attr(get, "title", "Get");
		bud_append(get, bud_text("🖐 Get"));
		bud_bind(get, "click", 1, on_data_cmd);
		bud_append(act_dock, get);
	}

	/* Drop menu item: shown if ACT_DROP (8) is set or item is in inventory */
	if ((e->actions & 8) || (e->location == st->me)) {
		bud_node *drop = bud_element("button");
		bud_set_attr(drop, "type", "button");
		bud_set_attr(drop, "data-action", "drop");
		bud_set_attr(drop, "data-cmd", drop_cmd);
		bud_set_attr(drop, "class", "nd-popper-item btn text-xs");
		bud_set_attr(drop, "title", "Drop");
		bud_append(drop, bud_text("🪣 Drop"));
		bud_bind(drop, "click", 1, on_data_cmd);
		bud_append(act_dock, drop);
	}

	return act_dock;
}

static bud_node *render_room_contents(const nd_client_state_t *st)
{
	bud_node *wrap = bud_element("nd-contents");
	bud_set_attr(wrap, "id", "contents");
	bud_set_attr(wrap, "class", "nd-contents w-full");

	int is_inventory = (st && st->target_ref && st->me && st->target_ref == st->me);
	uint32_t container_target = (st && st->target_ref && !is_inventory) ? st->target_ref : 0;

	int any = 0;
	if (st) {
		for (size_t i = 0; i < st->entity_count; i++) {
			const nd_entity_desc_t *e = &st->entities[i];
			if (st->me && e->ref == st->me)
				continue;
			if (is_inventory) {
				if (!st->me || e->location != st->me)
					continue;
			} else if (container_target) {
				if (e->location != container_target)
					continue;
			} else {
				if (st->me && e->location == st->me)
					continue;
			}
			any = 1;

			bud_node *row = bud_element("nd-item");
			char ref_buf[32];
			snprintf(ref_buf, sizeof(ref_buf), "%u", e->ref);
			bud_set_attr(row, "data-ref", ref_buf);
			bud_set_attr(row, "data-item-ref", ref_buf);
			char row_key[48];
			snprintf(row_key, sizeof(row_key), "item-%s", ref_buf);
			bud_set_attr(row, "data-bud-key", row_key);
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

			/* Dropdown caret hint */
			bud_node *caret = bud_element("span");
			bud_set_attr(caret, "class", "nd-item-caret pointer-events-none");
			bud_append(caret, bud_text(is_selected ? "▴" : "▾"));
			bud_append(row, caret);

			bud_append(wrap, row);
		}
	}

	if (!any) {
		bud_node *empty = bud_element("div");
		bud_set_attr(empty, "data-bud-key", "empty-contents");
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

static bud_node *render_closed_windows_dock(void)
{
	bud_node *dock = bud_element("div");
	bud_set_attr(dock, "id", "bottom-left-windows");
	bud_set_attr(dock, "data-bud-key", "bottom-left-windows");

	int any_closed = 0;
	for (int w = 0; w < ND_WIN_COUNT; w++) {
		if (w == ND_WIN_ACTIONS || w == ND_WIN_TARGET || !s_win[w].minimized)
			continue;
		any_closed = 1;
		bud_node *btn = bud_element("button");
		bud_set_attr(btn, "type", "button");
		char bid[48];
		snprintf(bid, sizeof(bid), "restore-win-%s", s_win_meta[w].id_suf);
		bud_set_attr(btn, "id", bid);
		bud_set_attr(btn, "class", "nd-pad btn round");
		char wid_str[16];
		snprintf(wid_str, sizeof(wid_str), "%d", w);
		bud_set_attr(btn, "data-win-id", wid_str);
		bud_set_attr(btn, "title", s_win_meta[w].label);
		bud_append(btn, bud_text(s_win_meta[w].icon));
		bud_bind(btn, "click", 1, on_win_restore);
		bud_append(dock, btn);
	}

	bud_set_attr(dock, "class", any_closed ? "hud-closed-dock pointer-events-auto" : "hud-closed-dock hidden");
	return dock;
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
	return st && (strcmp(st->preferred_driver, "lamina-seven") == 0 || st->layout_mode == ND_LAYOUT_GAME_2D);
}

static int on_key_up(bud_event *e)
{
	const char *key = (const char *)e->user;
	if (!key || !*key)
		return 0;
	return nd_seven_key(key, 0);
}

static int on_key_down(bud_event *e)
{
	const char *key = (const char *)e->user;
	if (!key || !*key)
		return 0;

	if (nd_ui_seven_active(&client_state)) {
		if (strcmp(key, "+") == 0 || strcmp(key, "=") == 0 || strcmp(key, "]") == 0 || strcmp(key, "PageUp") == 0) {
			nd_seven_zoom_in();
			seven_stage_redraw();
			return 1;
		} else if (strcmp(key, "-") == 0 || strcmp(key, "_") == 0 || strcmp(key, "[") == 0 || strcmp(key, "PageDown") == 0) {
			nd_seven_zoom_out();
			seven_stage_redraw();
			return 1;
		} else if (strcmp(key, "0") == 0 || strcmp(key, ")") == 0) {
			nd_seven_set_zoom(1.0f);
			seven_stage_redraw();
			return 1;
		}
		if (strcmp(key, " ") == 0 || strcmp(key, "Space") == 0) {
			char act[64];
			if (nd_seven_action_execute(&client_state, act, sizeof(act)) > 0) {
				ws_send(act, strlen(act));
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

/* Per-biome backdrop: the current room's art, falling back to the CSS void. */
static void apply_room_backdrop(bud_node *main_box, const nd_client_state_t *st)
{
	if (!st || !st->room.art[0])
		return;
	char style[160];
	snprintf(style, sizeof(style),
		"background: url('/nd/art/%s') center/cover no-repeat;", st->room.art);
	bud_set_attr(main_box, "style", style);
}

static bud_node *render_room_header(const nd_client_state_t *st)
{
	if (!st || (!st->room.name[0] && !st->room.ref))
		return lx_n("div", lx_attr("id", "room-header"), lx_attr("class", "hidden"));

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
	if (st && st->preferred_driver[0]) {
		for (size_t i = 0; i < g_driver_count; i++) {
			if (strcmp(g_viewport_drivers[i].id, st->preferred_driver) == 0) {
				g_active_driver_idx = (int)i;
				break;
			}
		}
	}

	bud_node *vport = render_viewport_slot(st);
	bud_node *map = render_room_map(st);
	bud_node *modals = render_modals_slot(st);

	int is_seven = (nd_ui_seven_active(st) || (st && st->layout_mode == ND_LAYOUT_GAME_2D));

	if (is_seven) {
		if (st && st->target_ref) {
			if (st->target_ref != s_prev_target_ref)
				s_win[ND_WIN_MAP].minimized = 0;
			s_last_target_ref = st->target_ref;
		}
		s_prev_target_ref = st ? st->target_ref : 0;

		/* Full Seven Stage with floating movable & resizable HUD windows */
		bud_node *holder = bud_element("div");
		bud_set_attr(holder, "id", "holder");
		bud_set_attr(holder, "class", "nd game-2d-mode relative flex flex-col flex-1 min-h-0 w-full overflow-hidden");
		bud_append(holder, vport);

		/* 1. Floating RPG Chat Terminal Window (bottom-left default) */
		bud_node *chat = render_rpg_chat_terminal(st);
		bud_append(holder, chat);

		/* 2. Floating Room Title Window (top-center default, movable & resizable) */
		{
			bud_node *title_win = bud_element("div");
			bud_set_attr(title_win, "id", "room-header");
			bud_set_attr(title_win, "data-bud-key", "room-header");
			int has_room = (st && (st->room.name[0] || st->room.ref));
			int is_min = s_win[ND_WIN_TITLE].minimized;
			int is_drag = (s_drag_win == ND_WIN_TITLE && s_drag_mode != 0);
			char tcls[160];
			snprintf(tcls, sizeof(tcls), "hud-win seven-room-header pointer-events-auto%s%s%s",
				has_room ? "" : " hidden",
				is_min ? " hud-win-minimized" : "",
				is_drag ? " hud-win-dragging" : "");
			bud_set_attr(title_win, "class", tcls);

			append_win_chrome(title_win, ND_WIN_TITLE, "title", "📍 Room Title", 0);

			char text[128] = {0};
			if (st && st->room.name[0]) {
				snprintf(text, sizeof(text), "📍 %s (#%u)", st->room.name, st->room.ref);
			} else if (st && st->room.ref) {
				snprintf(text, sizeof(text), "📍 Room #%u", st->room.ref);
			}
			bud_node *title_body = bud_element("div");
			bud_set_attr(title_body, "id", "room-header-text");
			bud_set_attr(title_body, "data-bud-key", "title-body");
			bud_set_attr(title_body, "class", "hud-win-body seven-room-title-body truncate");
			bud_append(title_body, bud_text(text));
			bud_append(title_win, title_body);
			bud_append(holder, title_win);
		}

		/* Right-hand HUD container (#map-overlay) with independent movable/resizable Map/Look-At & Contents windows */
		bud_node *overlay = bud_element("div");
		bud_set_attr(overlay, "id", "map-overlay");
		bud_set_attr(overlay, "data-bud-key", "map-overlay");
		bud_set_attr(overlay, "class", "absolute inset-0 z-10 opacity-90 pointer-events-none overflow-visible");
		bud_set_attr(overlay, "style", "inset:0;top:12px;right:12px;bottom:76px;max-height:calc(100% - 88px)");

		/* 3. Unified Minimap / Look-At Window (top-right default, swaps to Look-At card when st->target_ref != 0) */
		if (map) {
			bud_node *tc = (st && st->target_ref) ? render_target_card(st) : NULL;
			int has_target_card = (tc && bud_get_attr(tc, "id") != NULL);
			bud_node *map_box = bud_element("div");
			bud_set_attr(map_box, "id", "hud-win-map");
			bud_set_attr(map_box, "data-bud-key", "minimap");
			int is_min = s_win[ND_WIN_MAP].minimized;
			int is_drag = (s_drag_win == ND_WIN_MAP && s_drag_mode != 0);
			char mcls[192];
			snprintf(mcls, sizeof(mcls), "hud-win hud-win-map rpg-hud-panel pointer-events-auto shadow-lg rounded flex-shrink-0%s%s%s",
				has_target_card ? " hud-win-map-target" : "",
				is_min ? " hud-win-minimized" : "",
				is_drag ? " hud-win-dragging" : "");
			bud_set_attr(map_box, "class", mcls);
			append_win_chrome(map_box, ND_WIN_MAP, "map", has_target_card ? "👁️ Look-At" : "🗺️ Minimap", 0);

			bud_node *map_body = bud_element("div");
			bud_set_attr(map_body, "data-bud-key", "map-body");
			bud_set_attr(map_body, "class", "hud-win-body flex flex-col items-center justify-center w-full h-full min-h-0 overflow-hidden");
			if (has_target_card) {
				bud_set_attr(map, "class", "hidden");
				bud_append(map_body, map);
				bud_append(map_body, tc);
			} else {
				if (tc)
					bud_free(tc);
				bud_append(map_body, map);
			}
			bud_append(map_box, map_body);
			bud_append(overlay, map_box);
		}

		/* 4. Floating Room Contents Window (mid-right default, movable & resizable, scrollable body + unclipped top-level Popper menu) */
		bud_node *contents = render_room_contents(st);
		if (contents) {
			bud_node *cnt_box = bud_element("div");
			bud_set_attr(cnt_box, "id", "hud-win-contents");
			bud_set_attr(cnt_box, "data-bud-key", "contents");
			int is_min = s_win[ND_WIN_CONTENTS].minimized;
			int is_drag = (s_drag_win == ND_WIN_CONTENTS && s_drag_mode != 0);
			char ccls[192];
			snprintf(ccls, sizeof(ccls), "hud-win hud-win-contents rpg-hud-panel pointer-events-auto p-2 flex flex-col flex-initial min-h-[120px] max-h-[36vh]%s%s",
				is_min ? " hud-win-minimized" : "",
				is_drag ? " hud-win-dragging" : "");
			bud_set_attr(cnt_box, "class", ccls);
			append_win_chrome(cnt_box, ND_WIN_CONTENTS, "contents", "📦 Room Contents", 0);

			bud_node *cnt_body = bud_element("div");
			bud_set_attr(cnt_body, "data-bud-key", "contents-body");
			bud_set_attr(cnt_body, "class", "hud-win-body flex flex-col flex-1 w-full min-h-0 overflow-hidden");
			bud_append(cnt_body, contents);
			bud_append(cnt_box, cnt_body);

			/* Mount Popper-style action dropdown directly on #hud-win-contents outside the scroll container */
			bud_node *popper = render_item_popper(st);
			if (popper)
				bud_append(cnt_box, popper);

			bud_append(overlay, cnt_box);
		}

		bud_node *gauges = render_gauges_slot(st);
		if (gauges) {
			bud_node *stat_box = bud_element("div");
			bud_set_attr(stat_box, "data-bud-key", "gauges-dock");
			bud_set_attr(stat_box, "class", "rpg-hud-panel pointer-events-auto flex flex-col gap-1 items-end p-1.5 w-full flex-shrink-0");
			bud_append(stat_box, gauges);
			bud_append(overlay, stat_box);
		}

		bud_append(holder, overlay);

		/* 6. Permanent Toolbar-Free Bottom-Right Actions Dock */
		bud_node *actions = render_action_bar(st);
		if (actions) {
			bud_node *act_dock = bud_element("div");
			bud_set_attr(act_dock, "id", "bottom-right-actions");
			bud_set_attr(act_dock, "data-bud-key", "bottom-right-actions");
			bud_set_attr(act_dock, "class", "hud-actions-dock pointer-events-auto");

			bud_node *act_body = bud_element("div");
			bud_set_attr(act_body, "class", "hud-win-body flex items-center justify-center w-full h-full");
			bud_append(act_body, actions);
			bud_append(act_dock, act_body);
			bud_append(holder, act_dock);
		}

		/* Bottom-Left Closed-Windows Launcher Dock (action-style buttons to reopen closed windows) */
		bud_node *closed_dock = render_closed_windows_dock();
		bud_append(holder, closed_dock);

		bud_node *main_box = bud_element("div");
		bud_set_attr(main_box, "id", "main");
		bud_set_attr(main_box, "class", "flex flex-col h-screen w-screen overflow-hidden bg-neutral-950 text-neutral-100");
		bud_append(main_box, holder);
		bud_append(main_box, modals);

		const char *user = (st && st->user[0]) ? st->user : client_state.user;
		bud_node *layout = site_ui_layout("NeverDark", "/nd", "🕹️", user, NULL, main_box);

		bud_node *root = bud_tpl(
			"<div id='bud-root' data-slot='1' data-capabilities='axil-ws axil-tty lamina webgl bud xylem' "
			"class='w-full h-[calc(100vh-64px)] box-border font-[\"Lucida_Console\",Monaco,monospace]'>%node</div>",
			layout);

		bud_bind(root, "cmd", 1, on_cmd_action);
		bud_bind(root, "cycle-viewport", 1, on_cycle_viewport);
		bud_bind(root, "restore-persist", 1, on_restore_persist);
		bud_bind(root, "keydown@window", 0, on_key_down);
		bud_bind(root, "keyup@window", 0, on_key_up);
		bud_bind(root, "mousemove@window", 0, on_win_drag_move);
		bud_bind(root, "mouseup@window", 0, on_win_drag_up);
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
	apply_room_backdrop(main_box, st);
	char mcls[128];
	snprintf(mcls, sizeof(mcls), "flex flex-col h-screen w-screen overflow-hidden bg-neutral-950 text-neutral-100 %s",
		(st && st->target_ref) ? "targetting" : "");
	bud_set_attr(main_box, "class", mcls);
	bud_append(main_box, holder);
	bud_append(main_box, modals);

	const char *user = (st && st->user[0]) ? st->user : client_state.user;
	bud_node *layout = site_ui_layout("NeverDark", "/nd", "🕹️", user, NULL, main_box);

	bud_node *root = bud_tpl(
		"<div id='bud-root' data-slot='1' data-capabilities='axil-ws axil-tty lamina webgl bud xylem' "
		"class='w-full h-[calc(100vh-64px)] box-border font-[\"Lucida_Console\",Monaco,monospace]'>%node</div>",
		layout);

	bud_bind(root, "cmd", 1, on_cmd_action);
	bud_bind(root, "cycle-viewport", 1, on_cycle_viewport);
	bud_bind(root, "keydown@window", 0, on_key_down);
	bud_bind(root, "keyup@window", 0, on_key_up);
	return root;
}

static int seven_stage_fit(int width, int height)
{
	int w = width < ND_STAGE_MIN_W ? ND_STAGE_MIN_W : width > ND_STAGE_MAX_W ? ND_STAGE_MAX_W : width;
	int h = height < ND_STAGE_MIN_H ? ND_STAGE_MIN_H : height > ND_STAGE_MAX_H ? ND_STAGE_MAX_H : height;

	g_stage_w = w;
	g_stage_h = h;
	return 1;
}

static void seven_stage_redraw(void)
{
	if (!seven_stage_fit(g_stage_w, g_stage_h))
		return;

	int64_t wx = 0, wy = 0;
	nd_seven_world_pos(&client_state, &wx, &wy);
	float wz = nd_seven_ground_elevation_f((int32_t)wx, (int32_t)wy);
	float zoom = nd_seven_get_zoom();
	float eff_cam_dist = ND_SEVEN_CAM_DIST / zoom;

	lam_cam_set_3d((float)wx, (float)wy, wz, eff_cam_dist, ND_SEVEN_DIORAMA_PITCH, ND_SEVEN_CAM_YAW, ND_SEVEN_CAM_FOV);
	nd_seven_render((uint32_t)g_stage_w, (uint32_t)g_stage_h, &client_state);
	lam_cam_set_2d();
}

int g_map_dirty = 0;

ISO_EXPORT(void, wasm_canvas_render, void)
{
	if (nd_ui_seven_active(&client_state))
		seven_stage_redraw();
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
		if (now_seven)
			nd_seven_sync_origin(&client_state);
		s_prev_seven_active = now_seven;
	}

	nd_seven_sync_origin(&client_state);
	sync_room_objects(&client_state);

	int map_on = nd_map_canvas_enabled(&client_state);

	/* Phase 3: Simulation */
	if (now_seven) {
		int mv = nd_seven_tick(&client_state, dt);
		if (mv) {
			char m[2] = { (char)mv, '\n' };
			ws_send(m, 2);
		}
		/* Phase 4: Presentation (Pure drawing without state mutation)
		 * Seven stage redraws every tick at native display refresh (60Hz / 120Hz). */
		seven_stage_redraw();
		int map_vis = map_on && !s_win[ND_WIN_MAP].minimized &&
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

