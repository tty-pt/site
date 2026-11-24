#ifndef LAM_WEBGL_NATIVE
#define LAM_WEBGL_NATIVE 1
#endif
#ifndef LAM_NATIVE_CLIENT
#define LAM_NATIVE_CLIENT 1
#endif

#include "bud_lamina.h"
#include "term_ansi.h"

#include <ttypt/lam.h>
#include <ttypt/lam-font.h>
#include <ttypt/webgl.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	int  clicks;
	int  dblclicks;
	int  mousedowns;
	int  mousemoves;
	int  mouseups;
	int  wheels;
	int  keydowns;
	int  slider_val;
	char last_key[32];
	char last_input[128];
	char last_change[128];
	char last_term_cmd[128];
	int  counter;
} test_app_t;

static test_app_t g_app;

static int on_btn_click(bud_event *ev)
{
	(void)ev;
	g_app.clicks++;
	g_app.counter++;
	bud_app_set_state();
	return 0;
}

static int on_stage_mousedown(bud_event *ev)
{
	(void)ev;
	g_app.mousedowns++;
	return 0;
}

static int on_stage_dblclick(bud_event *ev)
{
	(void)ev;
	g_app.dblclicks++;
	return 0;
}

static int on_stage_wheel(bud_event *ev)
{
	const char *s = ev ? (const char *)ev->user : NULL;
	assert(s && s[0]);
	int dx = 0, dy = 0, mode = -1;
	assert(sscanf(s, "%d,%d,%d", &dx, &dy, &mode) == 3);
	g_app.wheels++;
	return 1;
}

static int on_win_mousemove(bud_event *ev)
{
	(void)ev;
	g_app.mousemoves++;
	return 0;
}

static int on_win_mouseup(bud_event *ev)
{
	(void)ev;
	g_app.mouseups++;
	return 0;
}

static int on_win_keydown(bud_event *ev)
{
	const char *k = ev ? (const char *)ev->user : NULL;
	if (k) snprintf(g_app.last_key, sizeof(g_app.last_key), "%s", k);
	g_app.keydowns++;
	return 0;
}

static int on_slider_input(bud_event *ev)
{
	const char *v = ev ? (const char *)ev->user : NULL;
	if (v) g_app.slider_val = atoi(v);
	return 0;
}

static int on_text_input(bud_event *ev)
{
	const char *v = ev ? (const char *)ev->user : NULL;
	if (v) snprintf(g_app.last_input, sizeof(g_app.last_input), "%s", v);
	return 0;
}

static int on_text_change(bud_event *ev)
{
	const char *v = ev ? (const char *)ev->user : NULL;
	if (v) snprintf(g_app.last_change, sizeof(g_app.last_change), "%s", v);
	return 0;
}

static void on_term_submit(void *ctx, const char *data, size_t len)
{
	(void)ctx;
	size_t n = len < sizeof(g_app.last_term_cmd) - 1 ? len : sizeof(g_app.last_term_cmd) - 1;
	memcpy(g_app.last_term_cmd, data, n);
	g_app.last_term_cmd[n] = '\0';
}

static bud_node *render_test_app(void *ctx)
{
	test_app_t *app = (test_app_t *)ctx;

	bud_node *root = bud_element("div");
	bud_set_attr(root, "id", "bud-root");
	bud_set_attr(root, "class", "root");

	bud_node *stage = bud_element("canvas");
	bud_set_attr(stage, "id", "seven-stage-canvas");
	bud_set_attr(stage, "data-bud-key", "seven-stage-canvas");
	bud_set_attr(stage, "data-bud-webgl", "1");
	bud_set_attr(stage, "class", "stage");
	bud_bind(stage, "mousedown", 0, on_stage_mousedown);
	bud_bind(stage, "wheel", 0, on_stage_wheel);
	bud_bind(stage, "dblclick", 0, on_stage_dblclick);
	bud_append(root, stage);

	bud_node *btn = bud_element("button");
	bud_set_attr(btn, "id", "action-btn");
	bud_set_attr(btn, "data-bud-key", "action-btn");
	bud_set_attr(btn, "class", "btn");
	bud_append(btn, bud_textf("Count: %d", app->counter));
	bud_bind(btn, "click", 1, on_btn_click);
	bud_append(root, btn);

	bud_node *slider = bud_element("input");
	bud_set_attr(slider, "id", "res-slider");
	bud_set_attr(slider, "data-bud-key", "res-slider");
	bud_set_attr(slider, "type", "range");
	bud_set_attr(slider, "min", "0");
	bud_set_attr(slider, "max", "100");
	bud_set_attr(slider, "step", "1");
	bud_set_attr(slider, "value", "50");
	bud_set_attr(slider, "class", "slider");
	bud_bind(slider, "input", 1, on_slider_input);
	bud_bind(slider, "change", 1, on_slider_input);
	bud_append(root, slider);

	bud_node *txt = bud_element("input");
	bud_set_attr(txt, "id", "name-input");
	bud_set_attr(txt, "data-bud-key", "name-input");
	bud_set_attr(txt, "type", "text");
	bud_set_attr(txt, "placeholder", "Enter name");
	bud_set_attr(txt, "class", "text-in");
	bud_bind(txt, "input", 1, on_text_input);
	bud_bind(txt, "change", 1, on_text_change);
	bud_append(root, txt);

	bud_node *term = bud_element("div");
	bud_set_attr(term, "id", "term");
	bud_set_attr(term, "data-bud-key", "term");
	bud_set_attr(term, "data-bud-terminal", "1");
	bud_set_attr(term, "class", "term");
	bud_append(root, term);

	bud_bind(root, "mousemove@window", 1, on_win_mousemove);
	bud_bind(root, "mouseup@window", 1, on_win_mouseup);
	bud_bind(root, "keydown@window", 1, on_win_keydown);
	return root;
}

static const char *TEST_CSS =
	"#bud-root { position: relative; width: 800px; height: 600px; display: block; }\n"
	"#seven-stage-canvas { position: absolute; left: 0; top: 0; width: 800px; height: 600px; z-index: 0; }\n"
	"#action-btn { position: absolute; left: 20px; top: 20px; width: 120px; height: 40px; background: #223344; color: #ffffff; z-index: 10; }\n"
	"#action-btn:hover { background: #335577; }\n"
	"#res-slider { position: absolute; left: 20px; top: 80px; width: 200px; height: 24px; z-index: 10; }\n"
	"#name-input { position: absolute; left: 20px; top: 120px; width: 200px; height: 32px; z-index: 10; }\n"
	"#term { position: absolute; left: 20px; top: 360px; width: 400px; height: 200px; z-index: 10; }\n";

static void test_term_ansi_unit(void)
{
	term_ansi_t *t = term_ansi_create();
	assert(t != NULL);

	/* Feed Telnet IAC negotiation + ANSI colors + UTF-8 */
	const char raw[] =
		"\xFF\xFB\x01"                  /* IAC WILL ECHO */
		"\xFF\xFA\x1F\x00\x50\x00\x18\xFF\xF0" /* IAC SB NAWS ... IAC SE */
		"\033[32mHello\033[0m São\n"
		"\033[38;2;255;128;64mOrange\033[0m\n";
	term_ansi_write(t, raw, sizeof(raw) - 1);

	char line[128];
	term_ansi_get_line(t, 2, line, sizeof(line));
	assert(strcmp(line, "Hello São") == 0);
	assert(term_ansi_get_cell_fg(t, 2, 0) == LAM_RGB(0x2f, 0x7e, 0x25));

	term_ansi_get_line(t, 1, line, sizeof(line));
	assert(strcmp(line, "Orange") == 0);
	assert(term_ansi_get_cell_fg(t, 1, 0) == LAM_RGB(255, 128, 64));

	/* Interactive input line */
	term_ansi_input_char(t, 'l');
	term_ansi_input_char(t, 'o');
	term_ansi_input_char(t, 'o');
	term_ansi_input_char(t, 'x');
	term_ansi_input_key(t, 259, 0, NULL, 0); /* Backspace */
	term_ansi_input_char(t, 'k');
	char cmd[64] = {0};
	assert(term_ansi_input_key(t, 257, 0, cmd, sizeof(cmd)) == 1); /* Enter */
	assert(strcmp(cmd, "look\n") == 0);

	term_ansi_destroy(t);
	printf("  test_term_ansi_unit: PASS\n");
}

static void test_reconcile_events_and_widgets(void)
{
	memset(&g_app, 0, sizeof(g_app));
	bud_lamina_t *bl = bud_lamina_create(800, 600);
	assert(bl != NULL);
	bud_lamina_set_active_global(bl);

	uint32_t f_sans = lam_font_open_ttf("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16);
	uint32_t f_mono = lam_font_open_ttf("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 14);
	bud_lamina_set_fonts(bl, f_sans, f_sans, f_mono, f_mono);

	assert(bud_lamina_load_css(bl, TEST_CSS, strlen(TEST_CSS)) >= 6);
	bud_lamina_set_render(bl, render_test_app, &g_app);
	bud_lamina_set_term_input(bl, on_term_submit, &g_app);

	/* 1. Initial sync + layout */
	assert(bud_lamina_sync(bl) == 1);
	assert(bud_lamina_is_dirty(bl) == 0);

	lam_ui_div_t *btn = bud_lamina_find_by_id(bl, "action-btn");
	assert(btn != NULL);
	assert(lam_ui_get_x(btn) == 20 && lam_ui_get_y(btn) == 20);
	assert(lam_ui_get_width(btn) == 120 && lam_ui_get_height(btn) == 40);
	assert(lam_ui_get_style(btn)->background_color == LAM_RGB(0x22, 0x33, 0x44));

	/* 2. Hover re-styles button without full rebuild */
	lam_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = LAM_EV_MOUSE_MOVE;
	ev.x = 50; ev.y = 35;
	bud_lamina_handle_event(bl, &ev);
	assert(lam_ui_get_style(btn)->background_color == LAM_RGB(0x33, 0x55, 0x77));
	assert(g_app.mousemoves == 1);

	/* 3. Click button -> bud_app_set_state marks dirty -> sync rebuilds */
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 0; ev.action = 1; ev.x = 50; ev.y = 35;
	bud_lamina_handle_event(bl, &ev);
	ev.action = 0;
	bud_lamina_handle_event(bl, &ev);
	assert(g_app.clicks == 1);
	assert(g_app.mouseups == 1);
	assert(bud_lamina_is_dirty(bl) == 1);
	assert(bud_lamina_sync(bl) == 1);

	/* 4. Stage mousedown + dblclick + wheel */
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 0; ev.action = 1; ev.x = 500; ev.y = 200;
	bud_lamina_handle_event(bl, &ev);
	ev.action = 0;
	bud_lamina_handle_event(bl, &ev);
	ev.action = 1;
	bud_lamina_handle_event(bl, &ev);
	ev.action = 0;
	bud_lamina_handle_event(bl, &ev);
	assert(g_app.mousedowns == 2);
	assert(g_app.dblclicks == 1);

	ev.type = LAM_EV_SCROLL; ev.x = 500; ev.y = 200; ev.dx = 0; ev.dy = 2.0f;
	bud_lamina_handle_event(bl, &ev);
	assert(g_app.wheels == 1);

	/* 5. Range slider drag at 75% (x=20 + 150 = 170) */
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 0; ev.action = 1; ev.x = 170; ev.y = 92;
	bud_lamina_handle_event(bl, &ev);
	assert(g_app.slider_val == 75);
	ev.action = 0; ev.x = 170; ev.y = 92;
	bud_lamina_handle_event(bl, &ev);
	assert(g_app.slider_val == 75);

	/* 6. Text input focus + typing + Backspace + Enter */
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 0; ev.action = 1; ev.x = 60; ev.y = 135;
	bud_lamina_handle_event(bl, &ev);
	ev.action = 0;
	bud_lamina_handle_event(bl, &ev);

	ev.type = LAM_EV_CHAR; ev.code = 'A'; bud_lamina_handle_event(bl, &ev);
	ev.type = LAM_EV_CHAR; ev.code = 'b'; bud_lamina_handle_event(bl, &ev);
	ev.type = LAM_EV_CHAR; ev.code = 'x'; bud_lamina_handle_event(bl, &ev);
	ev.type = LAM_EV_KEY; ev.code = 259; ev.action = 1; bud_lamina_handle_event(bl, &ev); /* Backspace */
	ev.type = LAM_EV_CHAR; ev.code = 'c'; bud_lamina_handle_event(bl, &ev);
	assert(strcmp(g_app.last_input, "Abc") == 0);
	ev.type = LAM_EV_KEY; ev.code = 257; ev.action = 1; bud_lamina_handle_event(bl, &ev); /* Enter */
	assert(strcmp(g_app.last_change, "Abc") == 0);

	/* Escape blurs input AND dispatches Escape to window; subsequent 'w' also reaches app */
	ev.type = LAM_EV_KEY; ev.code = 256; ev.action = 1; bud_lamina_handle_event(bl, &ev);
	assert(g_app.keydowns == 1);
	assert(strcmp(g_app.last_key, "Escape") == 0);
	ev.type = LAM_EV_KEY; ev.code = 87; ev.action = 1; ev.mods = 0; /* 'w' */
	bud_lamina_handle_event(bl, &ev);
	assert(g_app.keydowns == 2);
	assert(strcmp(g_app.last_key, "w") == 0);
	ev.type = LAM_EV_KEY; ev.code = 340; ev.action = 1; ev.mods = 1; /* Left Shift */
	bud_lamina_handle_event(bl, &ev);
	assert(g_app.keydowns == 3);
	assert(strcmp(g_app.last_key, "Shift") == 0);
	ev.type = LAM_EV_KEY; ev.code = 344; ev.action = 1; ev.mods = 1; /* Right Shift */
	bud_lamina_handle_event(bl, &ev);
	assert(g_app.keydowns == 4);
	assert(strcmp(g_app.last_key, "Shift") == 0);

	/* 7. Click #term -> type command -> Enter submits via term_input_fn */
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 0; ev.action = 1; ev.x = 100; ev.y = 420;
	bud_lamina_handle_event(bl, &ev);
	ev.action = 0;
	bud_lamina_handle_event(bl, &ev);
	assert(bud_lamina_is_term_focused(bl) == 1);

	bud_lamina_term_write(bl, "\033[36mWelcome to NeverDark\033[0m\n", 30);
	ev.type = LAM_EV_CHAR; ev.code = 'n'; bud_lamina_handle_event(bl, &ev);
	ev.type = LAM_EV_KEY; ev.code = 257; ev.action = 1; bud_lamina_handle_event(bl, &ev);
	assert(strcmp(g_app.last_term_cmd, "n\n") == 0);

	/* 8. WebGL surface begin/end + full UI render + 100 rebuild cycles (leak stress) */
	int sw = 0, sh = 0;
	assert(bud_lamina_webgl_size(bl, &sw, &sh) == 1);
	assert(sw == 800 && sh == 600);
	assert(bud_lamina_webgl_begin(bl) == 1);
	lam_glClearColor(0.1f, 0.3f, 0.5f, 1.0f);
	lam_glClear(GL_COLOR_BUFFER_BIT);
	bud_lamina_webgl_end(bl);

	for (int i = 0; i < 100; i++) {
		g_app.counter = i;
		bud_lamina_mark_dirty(bl);
		assert(bud_lamina_sync(bl) == 1);
		bud_lamina_render(bl);
	}

	bud_lamina_destroy(bl);
	if (f_sans != UINT32_MAX) lam_font_close(f_sans);
	if (f_mono != UINT32_MAX) lam_font_close(f_mono);
	printf("  test_reconcile_events_and_widgets: PASS (100 cycles + widgets + WebGL)\n");
}

typedef struct {
	int toolbar_downs;
	int toolbar_wx, toolbar_wy, toolbar_ww, toolbar_wh;
	int toolbar_btn;
	unsigned toolbar_btns;
	int btn_clicks;
	int item_clicks;
	int item_pop_y;
	int last_md_btn;
	unsigned last_md_btns;
} mouse_test_app_t;

static mouse_test_app_t g_mapp;

static int on_mt_toolbar_down(bud_event *ev)
{
	g_mapp.toolbar_downs++;
	const char *s = ev ? (const char *)ev->user : NULL;
	if (s) {
		int cx = 0, cy = 0, vw = 0, vh = 0;
		sscanf(s, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%u",
		       &cx, &cy, &vw, &vh,
		       &g_mapp.toolbar_wx, &g_mapp.toolbar_wy,
		       &g_mapp.toolbar_ww, &g_mapp.toolbar_wh,
		       &g_mapp.toolbar_btn, &g_mapp.toolbar_btns);
	}
	return 1;
}

static int on_mt_btn_click(bud_event *ev)
{
	(void)ev;
	g_mapp.btn_clicks++;
	return 1;
}

static int on_mt_item_click(bud_event *ev)
{
	g_mapp.item_clicks++;
	const char *s = ev ? (const char *)ev->user : NULL;
	if (s) g_mapp.item_pop_y = atoi(s);
	return 1;
}

static int on_mt_canvas_down(bud_event *ev)
{
	const char *s = ev ? (const char *)ev->user : NULL;
	if (s) {
		int a, b, c, d, e, f, g, h;
		sscanf(s, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%u",
		       &a, &b, &c, &d, &e, &f, &g, &h,
		       &g_mapp.last_md_btn, &g_mapp.last_md_btns);
	}
	return 1;
}

static bud_node *render_mouse_test(void *ctx)
{
	(void)ctx;
	bud_node *root = bud_element("div");
	bud_set_attr(root, "id", "holder");

	bud_node *cvs = bud_element("canvas");
	bud_set_attr(cvs, "id", "seven-stage-canvas");
	bud_set_attr(cvs, "data-bud-webgl", "1");
	bud_bind(cvs, "mousedown", 0, on_mt_canvas_down);
	bud_append(root, cvs);

	bud_node *win = bud_element("div");
	bud_set_attr(win, "id", "hud-win-contents");
	bud_set_attr(win, "class", "hud-win");
	bud_set_attr(win, "data-win-id", "2");

	bud_node *tb = bud_element("div");
	bud_set_attr(tb, "id", "contents-toolbar");
	bud_set_attr(tb, "class", "hud-win-toolbar");
	bud_set_attr(tb, "data-win-drag", "16");
	bud_bind(tb, "mousedown", 0, on_mt_toolbar_down);
	bud_append(win, tb);

	bud_node *row = bud_element("nd-item");
	bud_set_attr(row, "id", "item-42");
	bud_set_attr(row, "data-item-ref", "42");
	bud_bind(row, "click", 1, on_mt_item_click);
	bud_node *lbl = bud_element("span");
	bud_set_attr(lbl, "id", "item-42-lbl");
	bud_append(lbl, bud_text("Lantern"));
	bud_append(row, lbl);
	bud_append(win, row);

	bud_append(root, win);

	bud_node *btn = bud_element("button");
	bud_set_attr(btn, "id", "mt-btn");
	bud_bind(btn, "click", 0, on_mt_btn_click);
	bud_append(btn, bud_text("Act"));
	bud_append(root, btn);

	return root;
}

static void test_mouse_hover_drag_w3c_and_popper(void)
{
	memset(&g_mapp, 0, sizeof(g_mapp));
	g_mapp.last_md_btn = -1;

	bud_lamina_t *bl = bud_lamina_create(800, 600);
	assert(bl != NULL);

	const char *css =
		"#holder { position: relative; width: 800px; height: 600px; }"
		"#seven-stage-canvas { position: absolute; left: 0; top: 0; width: 800px; height: 600px; z-index: 1; }"
		".hud-win { position: absolute; left: 100px; top: 120px; width: 240px; height: 180px; z-index: 20; overflow: visible; }"
		".hud-win-toolbar { position: absolute; top: -24px; left: 0; right: 0; height: 24px; opacity: 0; pointer-events: none; cursor: move; }"
		".hud-win:hover > .hud-win-toolbar { opacity: 1 !important; pointer-events: auto !important; }"
		"#item-42 { position: absolute; left: 10px; top: 30px; width: 200px; height: 28px; }"
		"#item-42-lbl { position: absolute; left: 0; top: 0; width: 100px; height: 28px; }"
		"#mt-btn { position: absolute; left: 500px; top: 100px; width: 80px; height: 32px; z-index: 25; }";

	assert(bud_lamina_load_css(bl, css, strlen(css)) > 0);
	bud_lamina_set_render(bl, render_mouse_test, &g_mapp);
	assert(bud_lamina_sync(bl) == 1);

	/* 1. Hover child inside .hud-win -> ancestor .hud-win gets :hover,
	 * revealing .hud-win-toolbar (top:-24px => y in [96, 120))! */
	lam_event_t ev = {0};
	ev.type = LAM_EV_MOUSE_MOVE; ev.x = 150; ev.y = 160;
	bud_lamina_handle_event(bl, &ev);

	/* Move up onto toolbar at (150, 108): must stay hovered and receive mousedown with win rect (100,120,240,180) */
	ev.x = 150; ev.y = 108;
	bud_lamina_handle_event(bl, &ev);

	ev.type = LAM_EV_MOUSE_BTN; ev.code = 0; ev.action = 1; ev.x = 150; ev.y = 108;
	bud_lamina_handle_event(bl, &ev);
	assert(g_mapp.toolbar_downs == 1);
	assert(g_mapp.toolbar_wx == 100 && g_mapp.toolbar_wy == 120);
	assert(g_mapp.toolbar_ww == 240 && g_mapp.toolbar_wh == 180);
	assert(g_mapp.toolbar_btn == 0 && g_mapp.toolbar_btns == 1);

	/* Hover must persist cleanly across bud_lamina_sync() rebuilds */
	bud_lamina_mark_dirty(bl);
	assert(bud_lamina_sync(bl) == 1);
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 0; ev.action = 0; ev.x = 150; ev.y = 108;
	bud_lamina_handle_event(bl, &ev);

	/* 2. Dragging from canvas (50, 50) onto #mt-btn (540, 115) must NOT trigger button click */
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 0; ev.action = 1; ev.x = 50; ev.y = 50;
	bud_lamina_handle_event(bl, &ev);
	ev.type = LAM_EV_MOUSE_MOVE; ev.x = 540; ev.y = 115;
	bud_lamina_handle_event(bl, &ev);
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 0; ev.action = 0; ev.x = 540; ev.y = 115;
	bud_lamina_handle_event(bl, &ev);
	assert(g_mapp.btn_clicks == 0);

	/* Direct click on #mt-btn MUST trigger click */
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 0; ev.action = 1; ev.x = 540; ev.y = 115;
	bud_lamina_handle_event(bl, &ev);
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 0; ev.action = 0; ev.x = 540; ev.y = 115;
	bud_lamina_handle_event(bl, &ev);
	assert(g_mapp.btn_clicks == 1);

	/* 3. W3C mouse button mapping: GLFW right (1) -> W3C button 2, buttons 2; GLFW middle (2) -> W3C button 1, buttons 4 */
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 1; ev.action = 1; ev.x = 50; ev.y = 50;
	bud_lamina_handle_event(bl, &ev);
	assert(g_mapp.last_md_btn == 2 && g_mapp.last_md_btns == 2);
	ev.action = 0;
	bud_lamina_handle_event(bl, &ev);

	ev.type = LAM_EV_MOUSE_BTN; ev.code = 2; ev.action = 1; ev.x = 50; ev.y = 50;
	bud_lamina_handle_event(bl, &ev);
	assert(g_mapp.last_md_btn == 1 && g_mapp.last_md_btns == 4);
	ev.action = 0;
	bud_lamina_handle_event(bl, &ev);

	/* Right-clicking #mt-btn must NOT trigger click */
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 1; ev.action = 1; ev.x = 540; ev.y = 115;
	bud_lamina_handle_event(bl, &ev);
	ev.action = 0;
	bud_lamina_handle_event(bl, &ev);
	assert(g_mapp.btn_clicks == 1);

	/* 4. Clicking child span inside [data-item-ref="42"] computes exact popper Y = (row_bot - win_top + 4)
	 * win_top=120, row top=150, row height=28 -> row_bot=178 -> 178 - 120 + 4 = 62 */
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 0; ev.action = 1; ev.x = 130; ev.y = 160;
	bud_lamina_handle_event(bl, &ev);
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 0; ev.action = 0; ev.x = 130; ev.y = 160;
	bud_lamina_handle_event(bl, &ev);
	assert(g_mapp.item_clicks == 1);
	assert(g_mapp.item_pop_y == 62);

	bud_lamina_destroy(bl);
	printf("  test_mouse_hover_drag_w3c_and_popper: PASS\n");
}

typedef struct {
	int modal_open;
	int inner_clicks;
	int esc_count;
} modal_test_app_t;

static modal_test_app_t g_modal_app;

static int on_modal_backdrop_click(bud_event *ev)
{
	(void)ev;
	g_modal_app.modal_open = 0;
	return 1;
}

static int on_modal_inner_click(bud_event *ev)
{
	if (ev) bud_event_stop_propagation(ev);
	g_modal_app.inner_clicks++;
	return 1;
}

static int on_modal_keydown(bud_event *ev)
{
	const char *k = ev ? (const char *)ev->user : NULL;
	if (k && strcmp(k, "Escape") == 0) {
		g_modal_app.esc_count++;
		g_modal_app.modal_open = 0;
		return 1;
	}
	return 0;
}

static bud_node *render_modal_test(void *ctx)
{
	(void)ctx;
	bud_node *root = bud_element("div");
	bud_set_attr(root, "id", "mroot");
	bud_bind(root, "keydown@window", 0, on_modal_keydown);

	bud_node *term = bud_element("div");
	bud_set_attr(term, "id", "term");
	bud_set_attr(term, "data-bud-terminal", "1");
	bud_append(root, term);

	bud_node *cvs = bud_element("canvas");
	bud_set_attr(cvs, "id", "seven-stage-canvas");
	bud_set_attr(cvs, "data-bud-webgl", "1");
	bud_append(root, cvs);

	if (g_modal_app.modal_open) {
		bud_node *backdrop = bud_element("div");
		bud_set_attr(backdrop, "id", "test-modal");
		bud_bind(backdrop, "click", 0, on_modal_backdrop_click);

		bud_node *panel = bud_element("div");
		bud_set_attr(panel, "id", "test-modal-panel");
		bud_bind(panel, "click", 0, on_modal_inner_click);
		bud_append(backdrop, panel);
		bud_append(root, backdrop);
	}

	return root;
}

static void test_modal_escape_term_wrap_and_dpi(void)
{
	memset(&g_modal_app, 0, sizeof(g_modal_app));
	g_modal_app.modal_open = 1;

	bud_lamina_t *bl = bud_lamina_create(800, 600);
	assert(bl != NULL);

	const char *css =
		"#mroot { position: relative; width: 800px; height: 600px; }"
		"#seven-stage-canvas { position: absolute; left: 0; top: 0; width: 800px; height: 600px; z-index: 1; }"
		"#term { position: absolute; left: 10px; top: 400px; width: 180px; height: 100px; overflow: hidden; z-index: 10; }"
		"#test-modal { position: fixed; left: 0; top: 0; width: 800px; height: 600px; z-index: 50; }"
		"#test-modal-panel { position: absolute; left: 200px; top: 150px; width: 400px; height: 300px; }";
	assert(bud_lamina_load_css(bl, css, strlen(css)) > 0);
	bud_lamina_set_render(bl, render_modal_test, &g_modal_app);
	assert(bud_lamina_sync(bl) == 1);

	/* 1. Clicking inside modal panel stops propagation -> modal stays open */
	lam_event_t ev = {0};
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 0; ev.action = 1; ev.x = 300; ev.y = 250;
	bud_lamina_handle_event(bl, &ev);
	ev.action = 0;
	bud_lamina_handle_event(bl, &ev);
	assert(g_modal_app.inner_clicks == 1);
	assert(g_modal_app.modal_open == 1);

	/* 2. Clicking outside modal panel (on backdrop) dismisses modal */
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 0; ev.action = 1; ev.x = 50; ev.y = 50;
	bud_lamina_handle_event(bl, &ev);
	ev.action = 0;
	bud_lamina_handle_event(bl, &ev);
	assert(g_modal_app.modal_open == 0);

	/* 3. Focus #term, reopen modal, then press Escape -> blurs #term AND dispatches Escape */
	bud_lamina_mark_dirty(bl);
	assert(bud_lamina_sync(bl) == 1);
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 0; ev.action = 1; ev.x = 60; ev.y = 450;
	bud_lamina_handle_event(bl, &ev);
	ev.action = 0;
	bud_lamina_handle_event(bl, &ev);

	g_modal_app.modal_open = 1;
	bud_lamina_mark_dirty(bl);
	assert(bud_lamina_sync(bl) == 1);

	ev.type = LAM_EV_KEY; ev.code = 256; ev.action = 1; /* GLFW_KEY_ESCAPE */
	bud_lamina_handle_event(bl, &ev);
	assert(g_modal_app.esc_count == 1);
	assert(g_modal_app.modal_open == 0);

	/* 4. Long terminal lines + narrow box soft-wrap & clamp scrollback without overflow */
	const char *long_line =
		"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ\n";
	for (int i = 0; i < 20; i++)
		bud_lamina_term_write(bl, long_line, strlen(long_line));

	uint32_t f_mono = lam_font_open_ttf("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 13);
	if (f_mono != UINT32_MAX)
		bud_lamina_set_fonts(bl, f_mono, f_mono, f_mono, f_mono);

	/* Render at 1x and 2x HiDPI and check WebGL surface DPI scaling */
	lam_set_dpi_scale(1.0f);
	assert(lam_get_dpi_scale() > 0.99f && lam_get_dpi_scale() < 1.01f);
	int sw1 = 0, sh1 = 0;
	assert(bud_lamina_webgl_size(bl, &sw1, &sh1) == 1);
	assert(sw1 == 800 && sh1 == 600);
	bud_lamina_render(bl);

	/* Scroll wheel up heavily inside #term and re-render to test clamping */
	ev.type = LAM_EV_SCROLL; ev.x = 60; ev.y = 450; ev.dy = 100.0;
	bud_lamina_handle_event(bl, &ev);
	bud_lamina_render(bl);

	lam_set_dpi_scale(2.0f);
	assert(lam_get_dpi_scale() > 1.99f && lam_get_dpi_scale() < 2.01f);
	int sw2 = 0, sh2 = 0;
	assert(bud_lamina_webgl_size(bl, &sw2, &sh2) == 1);
	assert(sw2 == 1600 && sh2 == 1200);
	bud_lamina_render(bl);
	lam_set_dpi_scale(1.0f);

	bud_lamina_destroy(bl);
	if (f_mono != UINT32_MAX) lam_font_close(f_mono);
	printf("  test_modal_escape_term_wrap_and_dpi: PASS\n");
}

/* ── Regression: WebGL redraw inside Bud click handler (e.g. Settings preset) ── */

typedef struct {
	int preset_clicks;
	int res_w;
	int res_h;
} webgl_ev_app_t;

static webgl_ev_app_t g_wgl_app;

static int on_preset_click_webgl(bud_event *ev)
{
	if (ev) bud_event_stop_propagation(ev);
	g_wgl_app.preset_clicks++;
	g_wgl_app.res_w = 600; /* 75% Medium resolution scale of 800x600 */
	g_wgl_app.res_h = 450;

	/* Mirror nd_seven_gl_begin_frame + nd_seven_gl_end_frame side effects:
	 * bind fb=0, set scaled viewport, disable GL_BLEND, enable GL_DEPTH_TEST,
	 * and fill the entire scaled surface with bright green. */
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, g_wgl_app.res_w, g_wgl_app.res_h);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glClearColor(0.20f, 0.80f, 0.40f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	return 0;
}

static bud_node *render_webgl_event_test(void *ctx)
{
	(void)ctx;
	bud_node *root = bud_element("div");
	bud_set_attr(root, "id", "wroot");

	bud_node *cvs = bud_element("canvas");
	bud_set_attr(cvs, "id", "seven-stage-canvas");
	bud_set_attr(cvs, "data-bud-webgl", "1");
	bud_append(root, cvs);

	/* Translucent modal backdrop (same as #settings-modal bg-black/60) */
	bud_node *modal = bud_element("div");
	bud_set_attr(modal, "id", "settings-modal");

	bud_node *btn = bud_element("button");
	bud_set_attr(btn, "id", "settings-preset-toggle");
	bud_append(btn, bud_text("High"));
	bud_bind(btn, "click", 1, on_preset_click_webgl);
	bud_append(modal, btn);

	bud_append(root, modal);
	return root;
}

extern unsigned char glIsEnabled(unsigned int cap);
extern void glReadPixels(int x, int y, int width, int height,
                         unsigned int format, unsigned int type, void *pixels);

static void test_webgl_redraw_in_click_preserves_blend(void)
{
	memset(&g_wgl_app, 0, sizeof(g_wgl_app));
	bud_lamina_t *bl = bud_lamina_create(800, 600);
	assert(bl != NULL);

	const char *css =
		"#wroot { position: relative; width: 800px; height: 600px; }"
		"#seven-stage-canvas { position: absolute; left: 0; top: 0; width: 800px; height: 600px; z-index: 1; }"
		"#settings-modal { position: fixed; left: 0; top: 0; width: 800px; height: 600px; background: rgba(0, 0, 0, 0.6); z-index: 50; }"
		"#settings-preset-toggle { position: absolute; left: 300px; top: 250px; width: 160px; height: 40px; background: rgb(40, 50, 60); }";
	assert(bud_lamina_load_css(bl, css, strlen(css)) > 0);
	bud_lamina_set_render(bl, render_webgl_event_test, &g_wgl_app);
	assert(bud_lamina_sync(bl) == 1);

	/* Initial frame establishes baseline 2D compositor GL state (GL_BLEND=1, GL_DEPTH_TEST=0) */
	lam_clear(0xFF0A0D12u);
	assert(glIsEnabled(GL_BLEND) == GL_TRUE);
	assert(glIsEnabled(GL_DEPTH_TEST) == GL_FALSE);

	/* Click #settings-preset-toggle -> triggers WebGL redraw inside bud_lamina_handle_event() */
	lam_event_t ev = {0};
	ev.type = LAM_EV_MOUSE_BTN; ev.code = 0; ev.action = 1; ev.x = 350; ev.y = 270;
	bud_lamina_handle_event(bl, &ev);
	ev.action = 0;
	bud_lamina_handle_event(bl, &ev);
	assert(g_wgl_app.preset_clicks == 1);

	/* Critical invariant: GL_BLEND must still be enabled and GL_DEPTH_TEST disabled */
	assert(glIsEnabled(GL_BLEND) == GL_TRUE);
	assert(glIsEnabled(GL_DEPTH_TEST) == GL_FALSE);

	/* Next frame composites the scaled WebGL surface + translucent rgba(0,0,0,0.6) backdrop */
	lam_clear(0xFF0A0D12u);
	bud_lamina_render(bl);

	/* Read back a pixel in the upper-right quadrant (x=700, y=500 in GL bottom-left coords),
	 * outside the button and outside the 600x450 bottom-left subrect if autofit were broken!
	 * With autofit + alpha blending: green (51, 204, 102) * 0.40 ≈ (20, 82, 41), NOT solid black (0, 0, 0)! */
	uint32_t fbw = 0, fbh = 0;
	lam_size(&fbw, &fbh);
	if (fbw >= 720 && fbh >= 520) {
		uint8_t px[4] = {0, 0, 0, 0};
		glReadPixels(700, 500, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		assert(px[1] >= 55 && px[1] <= 115);
		assert(px[2] >= 25 && px[2] <= 60);
		printf("  test_webgl_redraw_in_click_preserves_blend: PASS (backdrop px=%u,%u,%u,%u)\n",
		       px[0], px[1], px[2], px[3]);
	} else {
		printf("  test_webgl_redraw_in_click_preserves_blend: PASS (state isolated, fb=%ux%u)\n",
		       fbw, fbh);
	}

	bud_lamina_destroy(bl);
}

int main(void)
{
	printf("test_bud_lamina:\n");
	lam_init();

	test_term_ansi_unit();
	test_reconcile_events_and_widgets();
	test_mouse_hover_drag_w3c_and_popper();
	test_modal_escape_term_wrap_and_dpi();
	test_webgl_redraw_in_click_preserves_blend();

	printf("test_bud_lamina: ALL TESTS PASSED\n");
	return 0;
}
