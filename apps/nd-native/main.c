#define _POSIX_C_SOURCE 200809L

#ifndef LAM_WEBGL_NATIVE
#define LAM_WEBGL_NATIVE 1
#endif
#ifndef LAM_NATIVE_CLIENT
#define LAM_NATIVE_CLIENT 1
#endif
#ifndef AXIL_WS_NATIVE
#define AXIL_WS_NATIVE 1
#endif

#include <bud/bud.h>
#include <ttypt/axil-ws.h>
#include <ttypt/lam.h>
#include <ttypt/lam-font.h>
#include <ttypt/lam-ui.h>

#include "bud_lamina.h"
#include "axil_ws_native.h"
#include "nd_ui.h"
#include "nd_seven.h"

#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Exports from unmodified mods/nd/ux/nd_ui.c & nd_state_io.c */
extern void wasm_tick(float dt, int width, int height);
extern void wasm_ws_on_open(void);
extern void wasm_ws_on_close(int code);
extern void wasm_ws_on_channel(uint8_t channel, const uint8_t *buf, size_t len);
extern void wasm_term_on_input(const char *buf, size_t len);
extern int on_cycle_viewport(bud_event *e);

/* Minimal stubs for BUD_WASM_MODULE_EXT (native app uses bud_lamina directly). */
int bud_slot_mount(int slot, bud_node *(*render_fn)(void))
{
	(void)slot; (void)render_fn;
	return 0;
}

int bud_slot_update(int slot, bud_node *(*render_fn)(void))
{
	(void)slot; (void)render_fn;
	return 0;
}

int bud_slot_dispatch_any(unsigned int node_id, const char *event_name,
                          int bubbles, void *event_data)
{
	(void)node_id; (void)event_name; (void)bubbles; (void)event_data;
	return 0;
}

typedef struct {
	bud_lamina_t *bl;
	const char   *init_send;
	int           ws_opened;
	int           bcp_packets;
	int           term_bytes;
} nd_native_app_t;

static double monotonic_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static bud_node *render_cb(void *ctx)
{
	(void)ctx;
	return nd_ui_render(nd_ui_get_client_state());
}

static void term_input_cb(void *ctx, const char *data, size_t len)
{
	(void)ctx;
	wasm_term_on_input(data, len);
}

static void on_ws_open(void *user)
{
	nd_native_app_t *app = (nd_native_app_t *)user;
	app->ws_opened = 1;
	wasm_ws_on_open();
	if (app->init_send && app->init_send[0]) {
		char buf[256];
		int n = snprintf(buf, sizeof(buf), "%s\n", app->init_send);
		if (n > 0) ws_send(buf, (size_t)n);
	}
}

static void on_ws_close(void *user, int code)
{
	(void)user;
	wasm_ws_on_close(code);
}

static void on_ws_channel(void *user, uint8_t channel,
                          const uint8_t *data, size_t len, int is_text)
{
	(void)is_text;
	nd_native_app_t *app = (nd_native_app_t *)user;
	if (channel == AXIL_WS_CH_BCP)
		app->bcp_packets++;
	wasm_ws_on_channel(channel, data, len);
}

static void on_ws_term_write(void *user, const char *data, size_t len)
{
	nd_native_app_t *app = (nd_native_app_t *)user;
	app->term_bytes += (int)len;
	bud_lamina_term_write(app->bl, data, len);
}

static uint32_t open_first_font(const char *const *paths, uint32_t px,
                                uint32_t emoji_ref)
{
	for (size_t i = 0; paths[i]; i++) {
		if (access(paths[i], R_OK) == 0) {
			uint32_t f = lam_font_open_ttf(paths[i], px);
			if (f != UINT32_MAX) {
				if (emoji_ref != UINT32_MAX)
					lam_font_add_fallback(f, emoji_ref);
				return f;
			}
		}
	}
	return UINT32_MAX;
}

static int save_framebuffer_ppm(const char *path, int w, int h)
{
	if (!path || w <= 0 || h <= 0) return -1;
	uint8_t *rgba = malloc((size_t)w * (size_t)h * 4);
	if (!rgba) return -1;
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

	FILE *f = fopen(path, "wb");
	if (!f) {
		free(rgba);
		return -1;
	}
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	/* OpenGL origin is bottom-left; flip vertically for standard PPM */
	for (int y = h - 1; y >= 0; y--) {
		const uint8_t *row = rgba + (size_t)y * (size_t)w * 4;
		for (int x = 0; x < w; x++) {
			uint8_t rgb[3] = { row[x * 4], row[x * 4 + 1], row[x * 4 + 2] };
			fwrite(rgb, 1, 3, f);
		}
	}
	fclose(f);
	free(rgba);
	return 0;
}

static void parse_base_url(const char *url, char *host_out, size_t hcap,
                           uint16_t *port_out, char *path_out, size_t pcap)
{
	snprintf(host_out, hcap, "127.0.0.1");
	*port_out = 8080;
	snprintf(path_out, pcap, "/nd");
	if (!url || !url[0]) return;

	const char *p = url;
	if (strncmp(p, "ws://", 5) == 0) p += 5;
	else if (strncmp(p, "http://", 7) == 0) p += 7;

	const char *slash = strchr(p, '/');
	const char *hend = slash ? slash : (p + strlen(p));
	const char *colon = memchr(p, ':', (size_t)(hend - p));
	if (colon) {
		size_t hl = (size_t)(colon - p);
		if (hl >= hcap) hl = hcap - 1;
		memcpy(host_out, p, hl);
		host_out[hl] = '\0';
		int port = atoi(colon + 1);
		if (port > 0 && port <= 65535) *port_out = (uint16_t)port;
	} else if (hend > p) {
		size_t hl = (size_t)(hend - p);
		if (hl >= hcap) hl = hcap - 1;
		memcpy(host_out, p, hl);
		host_out[hl] = '\0';
	}
	if (slash)
		snprintf(path_out, pcap, "%s", slash);
}

/* Native desktop shell: omit web <main class="main"> / <nav class="menu"> chrome
 * so NeverDark's #main fills 100% of the native window without CSS overrides. */
bud_node *site_ui_layout(const char *title, const char *path,
                         const char *icon, const char *user,
                         bud_node *menu_items, bud_node *children)
{
	(void)title; (void)path; (void)icon; (void)user;
	if (menu_items) bud_free(menu_items);
	return children;
}

int main(int argc, char **argv)
{
	const char *url = getenv("ND_URL");
	if (!url || !url[0]) url = "ws://127.0.0.1:8080/nd";
	const char *cookie = getenv("ND_COOKIE");
	const char *htdocs = getenv("ND_HTDOCS");
	if (!htdocs || !htdocs[0]) htdocs = "htdocs";
	const char *driver = getenv("ND_DRIVER");
	int driver_explicit = (driver && driver[0]) ? 1 : 0;
	if (!driver || !driver[0]) driver = "lamina-seven";
	const char *screenshot = NULL;
	const char *init_send = NULL;
	int max_frames = 0;
	int win_w = 1280, win_h = 800;
	int size_explicit = 0;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--url") == 0 && i + 1 < argc) {
			url = argv[++i];
		} else if (strcmp(argv[i], "--cookie") == 0 && i + 1 < argc) {
			cookie = argv[++i];
		} else if (strcmp(argv[i], "--htdocs") == 0 && i + 1 < argc) {
			htdocs = argv[++i];
		} else if (strcmp(argv[i], "--driver") == 0 && i + 1 < argc) {
			driver = argv[++i];
			driver_explicit = 1;
		} else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
			max_frames = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
			screenshot = argv[++i];
		} else if (strcmp(argv[i], "--send") == 0 && i + 1 < argc) {
			init_send = argv[++i];
		} else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
			int w = 0, h = 0;
			if (sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w >= 320 && h >= 240) {
				win_w = w;
				win_h = h;
				size_explicit = 1;
			}
		} else if (argv[i][0] != '-') {
			url = argv[i];
		}
	}

	/* 1. Initialize liblamina GLFW + OpenGL 3.3 Core window */
	lam_init();
	lam_set_title("NeverDark");

	float dpi_scale = lam_content_scale();
	int scale_override = 0;
	const char *env_scale = getenv("ND_SCALE");
	if (env_scale && env_scale[0]) {
		float sc = (float)atof(env_scale);
		if (sc >= 1.0f && sc <= 4.0f) {
			dpi_scale = sc;
			scale_override = 1;
		}
	}
	lam_set_dpi_scale(dpi_scale);

	uint32_t actual_w = 0, actual_h = 0;
	lam_size(&actual_w, &actual_h);
	if (size_explicit || (actual_w == 1024 && actual_h == 768)) {
		lam_resize((uint32_t)win_w, (uint32_t)win_h);
		lam_size(&actual_w, &actual_h);
	}
	if (actual_w > 0 && actual_h > 0) {
		win_w = (int)lroundf((float)actual_w / dpi_scale);
		win_h = (int)lroundf((float)actual_h / dpi_scale);
		if (win_w < 1) win_w = 1;
		if (win_h < 1) win_h = 1;
	}

	/* 2. Open proportional sans + monospace TTF fonts with emoji fallback */
	static const char *const sans_paths[] = {
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/TTF/DejaVuSans.ttf",
		"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
		NULL
	};
	static const char *const sans_bold_paths[] = {
		"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
		"/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
		"/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
		NULL
	};
	static const char *const mono_paths[] = {
		"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
		"/usr/share/fonts/TTF/DejaVuSansMono.ttf",
		"/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
		NULL
	};
	static const char *const mono_bold_paths[] = {
		"/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
		"/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf",
		NULL
	};
	const char *emoji_path = "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf";
	uint32_t f_emoji = UINT32_MAX;
	if (access(emoji_path, R_OK) == 0)
		f_emoji = lam_font_open_ttf(emoji_path, 15);

	uint32_t f_sans      = open_first_font(sans_paths, 15, f_emoji);
	uint32_t f_sans_bold = open_first_font(sans_bold_paths, 15, f_emoji);
	uint32_t f_mono      = open_first_font(mono_paths, 13, f_emoji);
	uint32_t f_mono_bold = open_first_font(mono_bold_paths, 13, f_emoji);
	if (f_sans_bold == UINT32_MAX) f_sans_bold = f_sans;
	if (f_mono_bold == UINT32_MAX) f_mono_bold = f_mono;

	/* 3. Create libbud-lamina bridge and load canonical web stylesheets */
	bud_lamina_t *bl = bud_lamina_create(win_w, win_h);
	if (!bl) {
		fprintf(stderr, "nd-native: failed to initialize bud-lamina bridge\n");
		return 1;
	}
	bud_lamina_set_active_global(bl);
	bud_lamina_set_fonts(bl, f_sans, f_sans_bold, f_mono, f_mono_bold);
	bud_lamina_set_asset_root(bl, htdocs);
	bud_lamina_set_dark_mode(bl, 1);

	char css_path[768];
	snprintf(css_path, sizeof(css_path), "%s/styles.css", htdocs);
	int r1 = bud_lamina_load_css_file(bl, css_path);
	snprintf(css_path, sizeof(css_path), "%s/hyle.css", htdocs);
	int r2 = bud_lamina_load_css_file(bl, css_path);
	snprintf(css_path, sizeof(css_path), "%s/nd.css", htdocs);
	int r3 = bud_lamina_load_css_file(bl, css_path);
	fprintf(stderr, "nd-native: loaded CSS (styles.css=%d, hyle.css=%d, nd.css=%d rules)\n",
	        r1, r2, r3);

	nd_native_app_t app;
	memset(&app, 0, sizeof(app));
	app.bl = bl;
	app.init_send = init_send;

	bud_lamina_set_render(bl, render_cb, &app);
	bud_lamina_set_term_input(bl, term_input_cb, &app);
	bud_lamina_attach_input(bl);

	/* 4. Configure native WebSocket client & boot unmodified NeverDark UX */
	char host[256], path[512];
	uint16_t port = 8080;
	parse_base_url(url, host, sizeof(host), &port, path, sizeof(path));

	axil_ws_native_cbs_t ws_cbs = {
		.on_open = on_ws_open,
		.on_close = on_ws_close,
		.on_channel = on_ws_channel,
		.on_term_write = on_ws_term_write,
		.user = &app
	};
	axil_ws_native_set_callbacks(&ws_cbs);
	axil_ws_native_set_base(host, port);
	if (cookie && cookie[0])
		axil_ws_native_set_cookie(cookie);

	/* Select preferred viewport driver (preserved across ALLOW_CHOICE regions) */
	char init_json[256];
	unsigned init_layout = (strcmp(driver, "lamina-seven") == 0)
	                       ? (unsigned)ND_LAYOUT_GAME_2D
	                       : (unsigned)ND_LAYOUT_STANDARD_HYBRID;
	snprintf(init_json, sizeof(init_json),
	         "{\"preferred_driver\":\"%s\",\"preferred_map_driver\":\"lamina\",\"layout_mode\":%u}",
	         driver, init_layout);
	nd_stl_init(init_json, (int)strlen(init_json));

	/* If CLI passed a custom path other than "/nd", connect to it explicitly */
	if (strcmp(path, "/nd") != 0)
		ws_connect(path, strlen(path));

	fprintf(stderr, "nd-native: connecting to ws://%s:%u%s (%dx%d @ %.2fx, fb=%ux%u)\n",
	        host, (unsigned)port, path, win_w, win_h, dpi_scale,
	        (unsigned)actual_w, (unsigned)actual_h);

	/* 5. Main 60fps desktop loop */
	double prev_t = monotonic_sec();
	int frame = 0;
	int driver_applied = 0;

	while (lam_running()) {
		double now = monotonic_sec();
		float dt = (float)(now - prev_t);
		if (dt < 0.001f) dt = 0.001f;
		if (dt > 0.1f)   dt = 0.1f;
		prev_t = now;

		lam_poll();
		axil_ws_native_poll(0);

		if (driver_explicit && !driver_applied && app.bcp_packets > 0) {
			const nd_client_state_t *cur = nd_ui_get_client_state();
			int want_seven = (strcmp(driver, "lamina-seven") == 0);
			int is_seven = cur && (strcmp(cur->preferred_driver, "lamina-seven") == 0 ||
			                       cur->layout_mode == ND_LAYOUT_GAME_2D);
			if (want_seven != is_seven) {
				for (int tries = 0; tries < 4 && want_seven != is_seven; tries++) {
					on_cycle_viewport(NULL);
					cur = nd_ui_get_client_state();
					is_seven = cur && (strcmp(cur->preferred_driver, "lamina-seven") == 0 ||
					                   cur->layout_mode == ND_LAYOUT_GAME_2D);
				}
				bud_lamina_mark_dirty(bl);
				driver_applied = 1;
			}
		}

		if (!scale_override) {
			float cur_scale = lam_content_scale();
			if (fabsf(cur_scale - dpi_scale) > 0.01f) {
				dpi_scale = cur_scale;
				lam_set_dpi_scale(dpi_scale);
			}
		}

		uint32_t cw = 0, ch = 0;
		lam_size(&cw, &ch);
		if (cw > 0 && ch > 0) {
			int lw = (int)lroundf((float)cw / dpi_scale);
			int lh = (int)lroundf((float)ch / dpi_scale);
			if (lw < 1) lw = 1;
			if (lh < 1) lh = 1;
			if (lw != win_w || lh != win_h) {
				win_w = lw;
				win_h = lh;
				bud_lamina_resize(bl, win_w, win_h);
			}
		}

		/* Rebuild Bud->Lamina DOM only when dirty */
		bud_lamina_sync(bl);

		/* Run unmodified NeverDark simulation & 3D WebGL2 + 2D minimap tick */
		int sw = win_w, sh = win_h;
		if (bud_lamina_webgl_begin(bl)) {
			bud_lamina_webgl_size(bl, &sw, &sh);
			wasm_tick(dt, sw, sh);
			bud_lamina_webgl_end(bl);
		} else {
			wasm_tick(dt, sw, sh);
		}

		/* Sync again if wasm_tick updated shader-ready UI state */
		bud_lamina_sync(bl);

		/* Composite full UI tree (including 3D stage, minimap, HUDs, ANSI terminal) */
		lam_clear(0xFF0A0D12u);
		bud_lamina_render(bl);

		frame++;
		if (max_frames > 0 && frame >= max_frames) {
			if (screenshot) {
				uint32_t fbw = 0, fbh = 0;
				lam_size(&fbw, &fbh);
				save_framebuffer_ppm(screenshot,
				                     fbw ? (int)fbw : win_w,
				                     fbh ? (int)fbh : win_h);
			}
			lam_flush();
			break;
		}
		lam_flush();

		/* Pace to ~60Hz if frame finished very quickly */
		double elapsed = monotonic_sec() - now;
		if (elapsed < 0.015) {
			struct timespec slp;
			double rem = 0.015 - elapsed;
			slp.tv_sec = 0;
			slp.tv_nsec = (long)(rem * 1e9);
			nanosleep(&slp, NULL);
		}
	}

	const nd_client_state_t *st = nd_ui_get_client_state();
	fprintf(stderr,
	        "nd-native: exit frames=%d ws_opened=%d bcp=%d term_bytes=%d shaders_ready=%d room=\"%s\" (#%u)\n",
	        frame, app.ws_opened, app.bcp_packets, app.term_bytes,
	        nd_seven_shaders_ready(),
	        st ? st->room.name : "", st ? st->room.ref : 0);

	ws_close();
	bud_lamina_destroy(bl);
	if (f_sans != UINT32_MAX) lam_font_close(f_sans);
	if (f_sans_bold != UINT32_MAX && f_sans_bold != f_sans) lam_font_close(f_sans_bold);
	if (f_mono != UINT32_MAX) lam_font_close(f_mono);
	if (f_mono_bold != UINT32_MAX && f_mono_bold != f_mono) lam_font_close(f_mono_bold);
	if (f_emoji != UINT32_MAX) lam_font_close(f_emoji);
	lam_deinit();
	return 0;
}
