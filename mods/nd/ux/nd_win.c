#include "nd_ui_internal.h"
#include <math.h>

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
static int s_orbit_dragging = 0;
static int s_orbit_last_mx = 0;
static int s_orbit_last_my = 0;

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

const char *nd_win_persist_key(int win_id)
{
	if (win_id < 0 || win_id >= ND_WIN_COUNT)
		return "";
	return s_win_persist_keys[win_id];
}

void nd_win_meta(int win_id, const char **id_suf, const char **icon, const char **label)
{
	if (win_id < 0 || win_id >= ND_WIN_COUNT) {
		if (id_suf) *id_suf = "";
		if (icon) *icon = "";
		if (label) *label = "";
		return;
	}
	if (id_suf) *id_suf = s_win_meta[win_id].id_suf;
	if (icon) *icon = s_win_meta[win_id].icon;
	if (label) *label = s_win_meta[win_id].label;
}

int nd_win_is_dragging(int win_id)
{
	return (s_drag_win == win_id && s_drag_mode != 0);
}

void nd_win_sync_target(const nd_client_state_t *st)
{
	if (st && st->target_ref) {
		if (st->target_ref != s_prev_target_ref)
			s_win[ND_WIN_MAP].minimized = 0;
		s_last_target_ref = st->target_ref;
	}
	s_prev_target_ref = st ? st->target_ref : 0;
}

void nd_win_cancel_orbit(void)
{
	s_orbit_dragging = 0;
}

int nd_win_get_popper_top_px(void)
{
	return s_popper_top_px;
}

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
	if (strcmp(key, "nd-settings-quality") == 0) {
		int res_pct = parse_style_px(val, "--nd-res:", -1);
		int det_pct = parse_style_px(val, "--nd-det:", -1);
		int cq = parse_style_px(val, "--nd-cloud:", -1);
		int msaa = parse_style_px(val, "--nd-msaa:", -1);
		int drs = parse_style_px(val, "--nd-drs:", -1);
		int fps = parse_style_px(val, "--nd-fps:", -1);
		int zoom_pct = parse_style_px(val, "--nd-zoom:", -1);
		if (res_pct >= 25 && res_pct <= 100)
			nd_seven_set_res_scale((float)res_pct * 0.01f);
		if (det_pct >= 20 && det_pct <= 250)
			nd_seven_set_detail_scale((float)det_pct * 0.01f);
		if (cq >= ND_CLOUD_QUALITY_OFF && cq <= ND_CLOUD_QUALITY_HIGH)
			nd_seven_set_cloud_quality(cq);
		if (msaa == 0 || msaa == 1 || msaa == 2 || msaa == 4)
			nd_seven_set_msaa(msaa);
		if (drs == 0 || drs == 1)
			nd_seven_set_drs(drs);
		if (fps >= 0)
			nd_seven_set_fps_cap(fps);
		if (zoom_pct >= 10 && zoom_pct <= 250)
			nd_seven_set_zoom((float)zoom_pct * 0.01f);
		nd_ui_seven_stage_redraw();
		bud_app_set_state();
		return;
	}
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

int on_restore_persist(bud_event *ev)
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

int on_win_drag_down(bud_event *ev)
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

int on_stage_orbit_down(bud_event *ev)
{
	if (!nd_ui_seven_active(&client_state))
		return 0;
	int cx = 0, cy = 0, vw = 0, vh = 0;
	parse_mouse_coords(ev, &cx, &cy, &vw, &vh, NULL, NULL, NULL, NULL);
	s_orbit_dragging = 1;
	s_orbit_last_mx = cx;
	s_orbit_last_my = cy;
	return 0;
}

int on_stage_orbit_wheel(bud_event *ev)
{
	if (!nd_ui_seven_active(&client_state))
		return 0;
	const char *s = ev ? (const char *)ev->user : NULL;
	if (!s || !*s)
		return 0;
	int dx = 0, dy = 0, mode = 0;
	if (sscanf(s, "%d,%d,%d", &dx, &dy, &mode) < 2)
		return 0;
	(void)dx;
	float scale = (mode == 1) ? 33.0f : ((mode == 2) ? 300.0f : 1.0f);
	float delta_px = (float)dy * scale;
	if (delta_px > 600.0f) delta_px = 600.0f;
	if (delta_px < -600.0f) delta_px = -600.0f;
	float cur_zoom = nd_seven_get_zoom();
	float next_zoom = cur_zoom * expf(-delta_px * 0.0012f);
	nd_seven_set_zoom(next_zoom);
	return 1;
}

int on_stage_orbit_dblclick(bud_event *ev)
{
	(void)ev;
	if (!nd_ui_seven_active(&client_state))
		return 0;
	nd_seven_reset_cam();
	return 1;
}

int on_win_drag_move(bud_event *ev)
{
	if (s_drag_win < 0 || s_drag_win >= ND_WIN_COUNT || !s_drag_mode) {
		if (!s_orbit_dragging)
			return 0;
		int cx = 0, cy = 0, vw = 0, vh = 0, rx = -1, ry = -1, rw = 0, rh = 0, btn = 0, btns = 1;
		const char *s = ev ? (const char *)ev->user : NULL;
		if (s && *s) {
			sscanf(s, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
			       &cx, &cy, &vw, &vh, &rx, &ry, &rw, &rh, &btn, &btns);
			(void)rx; (void)ry; (void)rw; (void)rh; (void)btn;
		}
		if (btns == 0) {
			s_orbit_dragging = 0;
			return 0;
		}
		int mdx = cx - s_orbit_last_mx;
		int mdy = cy - s_orbit_last_my;
		s_orbit_last_mx = cx;
		s_orbit_last_my = cy;
		if (mdx != 0 || mdy != 0)
			nd_seven_orbit_cam((float)(-mdx) * 0.32f, (float)(-mdy) * 0.25f);
		return 0;
	}
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

int on_win_drag_up(bud_event *ev)
{
	(void)ev;
	if (s_orbit_dragging)
		s_orbit_dragging = 0;
	if (s_drag_win < 0 && !s_drag_mode) return 0;
	s_drag_win = -1;
	s_drag_mode = 0;
	bud_app_set_state();
	return 0;
}

int on_win_minimize(bud_event *ev)
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

int on_win_restore(bud_event *ev)
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

int on_win_reset(bud_event *ev)
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

int on_toggle_chat(bud_event *ev)
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

void nd_win_format_style(int win_id, char *buf, size_t cap)
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

int on_item_select(bud_event *ev)
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

int on_dismiss_target(bud_event *ev)
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
