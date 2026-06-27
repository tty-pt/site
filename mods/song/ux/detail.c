#include "bud/bud.h"
#include "bud/bud_jsx.h"
#include "bud/bud_app.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../common/viewer_zoom.h"

#include "music.c"

#include "../../common/ux/site_ui.c"

#include "../fields.h"

static app_state_t app_state = { 0 };
static bud_node *g_main = NULL;

static bud_node *g_chord_raw = NULL;
static bud_node *g_chord_pre = NULL;
static bud_node *g_key_options[23] = { NULL };
static bud_node *g_media_node = NULL;

static void toggle_media(void)
{
	if (!g_media_node)
		return;
	extern void bud_patch_innerhtml(unsigned int node_id, const char *html);
	if (app_state.show_media) {
		char html[8192];
		site_ui_build_media_html(
		        app_state.cache.yt, app_state.cache.audio,
		        app_state.cache.pdf, html, sizeof(html));
		bud_patch_innerhtml(
		        bud_node_id(g_media_node), html[0] ? html : "");
	} else {
		bud_patch_innerhtml(bud_node_id(g_media_node), "");
	}
}

extern void wasm_mark_dirty(void);
extern void wasm_flush(void);

static int on_zoom_change(bud_event *event)
{
	const char *value = (const char *)event->user;
	int z;
	char zv[16];

	if (!value)
		return 0;

	z = atoi(value);
	if (z < VIEWER_ZOOM_MIN)
		z = VIEWER_ZOOM_MIN;
	if (z > VIEWER_ZOOM_MAX)
		z = VIEWER_ZOOM_MAX;
	app_state.zoom = z;
	ui_apply_zoom(g_main, NULL, z);

	snprintf(zv, sizeof(zv), "%d", z);
	bud_set_attr(event->target, "value", zv);

	return bud_api_action_handler(event);
}

void wasm_fetch_callback(int request_id, const char *data, int data_len)
{
	(void)request_id;
	(void)data_len;
	bud_state_apply(&app_state, song_fields, data);
	bud_json_data(data, app_state.chord_html, sizeof(app_state.chord_html));
	if (g_chord_raw)
		bud_raw_set_text(g_chord_raw, app_state.chord_html);

	extern void bud_patch_innerhtml(unsigned int node_id, const char *html);
	if (g_chord_pre)
		bud_patch_innerhtml(
		        bud_node_id(g_chord_pre), app_state.chord_html);

	for (int i = -11; i <= 11; i++) {
		const char *name = key_name(
		        i, app_state.original_key, app_state.use_bemol,
		        app_state.use_latin);
		if (g_key_options[i + 11])
			bud_patch_text(g_key_options[i + 11], name);
	}
	toggle_media();
}

void wasm_init(const char *json, int len)
{
	(void)len;
	bud_state_apply(&app_state, song_fields, json);
}

/* ── Page builder helpers ──────────────────────────── */

static bud_node *render_key_options(void)
{
	bud_node *key_opts = NULL;
	for (int i = -11; i <= 11; i++) {
		char val_str[16];
		snprintf(val_str, sizeof(val_str), "%d", i);
		bud_node *opt =
		        lx_el("option", lx_attr("value", val_str),
		              i == app_state.transpose ? lx_attr("selected", "")
		                                       : lx_none(),
		              lx_text(key_name(
		                      i, app_state.original_key,
		                      app_state.use_bemol,
		                      app_state.use_latin)))
		                .data.node;
		g_key_options[i + 11] = opt;
		if (!key_opts)
			key_opts = lx_frag(lx_node(opt)).data.node;
		else
			bud_append(key_opts, opt);
	}
	return key_opts;
}

static bud_node *render_transpose_form(bud_node *key_options)
{
	char zoom_str[16];
	snprintf(zoom_str, sizeof(zoom_str), "%d", app_state.zoom);

	char api_action[256];
	snprintf(
	        api_action, sizeof(api_action), "/api/song/%s/transpose?h=1",
	        app_state.cache.id);

	return lx_el("form", lx_attr("id", "transpose-form"),
	             lx_attr("method", "GET"),
	             lx_attr("action", app_state.path),
	             lx_attr("data-song-id", app_state.cache.id),
	             lx_attr("data-action", api_action),
	             lx_el("label", lx_text("Key:"),
	                   lx_el("select", lx_attr("name", "t"),
	                         lx_bind("change", 0, bud_api_action_handler),
	                         lx_node(key_options))),
	             lx_node(site_ui_checkbox(
	                     "b", "Flats (\xe2\x99\xad)", app_state.use_bemol,
	                     bud_api_action_handler)),
	             lx_node(site_ui_checkbox(
	                     "l", "Latin", app_state.use_latin,
	                     bud_api_action_handler)),
	             lx_node(site_ui_checkbox(
	                     "m", "Video", app_state.show_media,
	                     bud_api_action_handler)),
	             lx_el("label", lx_text("Zoom"),
	                   lx_el("input", lx_attr("type", "range"),
	                         lx_attr("name", "z"),
	                         lx_attr("min", STR(VIEWER_ZOOM_MIN)),
	                         lx_attr("max", STR(VIEWER_ZOOM_MAX)),
	                         lx_attr("step", "10"),
	                         lx_attr("value", zoom_str),
	                         lx_attr("data-detail-viewer-zoom", "1"),
	                         lx_bind("input", 0, on_zoom_change),
	                         lx_bind("change", 0, on_zoom_change))),
	             lx_el("button", lx_attr("type", "submit"),
	                   lx_attr("class", "btn"),
	                   lx_attr("data-wasm-hide", "1"), lx_text("Apply")))
	        .data.node;
}

static void render_chord_viewer(void)
{
	char transpose_str[16];
	char zoom_str[16];
	char zoom_style[64];
	snprintf(
	        transpose_str, sizeof(transpose_str), "%d",
	        app_state.transpose);
	snprintf(zoom_str, sizeof(zoom_str), "%d", app_state.zoom);
	snprintf(
	        zoom_style, sizeof(zoom_style),
	        "width:100%%;max-width:100%%;--chord-zoom:%d", app_state.zoom);

	char orig_key_str[16];
	snprintf(
	        orig_key_str, sizeof(orig_key_str), "%d",
	        app_state.original_key);

	char bemol_val[2] = { app_state.use_bemol ? '1' : '0', '\0' };
	char latin_val[2] = { app_state.use_latin ? '1' : '0', '\0' };
	char media_val[2] = { app_state.show_media ? '1' : '0', '\0' };
	char owner_val[2] = { app_state.is_owner ? '1' : '0', '\0' };

	bud_node *media_slot = NULL;
	if (app_state.show_media)
		media_slot = site_ui_render_media_slot(
		        app_state.cache.yt, app_state.cache.audio,
		        app_state.cache.pdf);

	g_chord_raw = bud_raw(app_state.chord_html);
	g_chord_pre = lx_el("pre", lx_attr("id", "chord-data"),
	                    lx_attr("data-detail-viewer-target", "1"),
	                    lx_attr("class",
	                            "whitespace-pre-wrap font-mono p-4 rounded "
	                            "chord-data"),
	                    lx_node(g_chord_raw))
	                      .data.node;

	bud_node *g_main_inner =
	        lx_el("div",
	              lx_attr("class", "flex flex-col gap-4 w-full max-w-xl"),
	              lx_el("div",
	                    lx_attr("class", "detail-viewer-scroll w-full "
	                                     "max-w-xl"),
	                    lx_attr("data-detail-viewer-scroll", "1"),
	                    lx_node(g_chord_pre)))
	                .data.node;

	g_media_node =
	        lx_el("div",
	              lx_attr("class", "flex flex-col gap-4 w-full max-w-xl"),
	              lx_attr("data-song-media", "1"),
	              media_slot ? lx_node(media_slot) : lx_none())
	                .data.node;

	/* Detail body with type/author */
	bud_node *detail_body = NULL;
	if (app_state.cache.type[0] || app_state.cache.author[0]) {
		detail_body =
		        lx_el("div", lx_attr("id", "song-detail-body"),
		              lx_attr("class", "contents"),
		              lx_attr("data-detail-viewer-scope", "1"),
		              lx_el("div",
		                    lx_attr("class",
		                            "flex justify-between items-start "
		                            "w-full max-w-xl text-xs "
		                            "text-muted"),
		                    lx_el("div",
		                          lx_attr("class",
		                                  "italic whitespace-pre-wrap"),
		                          lx_text(app_state.cache.type)),
		                    lx_el("div", lx_attr("class", "text-right"),
		                          lx_text(app_state.cache.author[0]
		                                          ? app_state.cache
		                                                    .author
		                                          : "N/A"))))
		                .data.node;
		bud_append(g_main_inner, detail_body);
	}

	g_main = lx_el("div", lx_attr("id", "main"),
	               lx_attr("data-song-id", app_state.cache.id),
	               lx_attr("data-chord-data", app_state.chord_html),
	               lx_attr("data-use-bemol", bemol_val),
	               lx_attr("data-use-latin", latin_val),
	               lx_attr("data-show-media", media_val),
	               lx_attr("data-yt", app_state.cache.yt),
	               lx_attr("data-audio", app_state.cache.audio),
	               lx_attr("data-pdf", app_state.cache.pdf),
	               lx_attr("data-original-key", orig_key_str),
	               lx_attr("data-save-url", app_state.save_url),
	               lx_attr("data-detail-viewer-controls", "song"),
	               lx_attr("data-is-owner", owner_val),
	               lx_attr("data-transpose", transpose_str),
	               lx_attr("data-zoom", zoom_str),
	               lx_attr("style", zoom_style),
	               lx_attr("data-type-display", app_state.cache.type),
	               lx_attr("data-author", app_state.cache.author),
	               lx_node(g_main_inner), lx_node(g_media_node))
	                 .data.node;
}

bud_node *bud_app_render(void)
{
	render_chord_viewer();
	bud_node *key_options = render_key_options();
	bud_node *transpose_form = render_transpose_form(key_options);

	bud_node *item_menu = site_ui_item_menu(
	        "song", app_state.cache.id, app_state.is_owner);

	bud_node *menu_items =
	        lx_frag(lx_node(transpose_form),
	                item_menu ? lx_node(item_menu) : lx_none())
	                .data.node;

	bud_node *inner = site_ui_layout(
	        app_state.cache.title, app_state.path, "🎸",
	        app_state.page_user, menu_items,
	        lx_frag(lx_el("div",
	                      lx_attr("class", "center flex flex-col gap-4"),
	                      lx_node(g_main)))
	                .data.node);
	return lx_el("div", lx_attr("id", "bud-root"), lx_node(inner))
	        .data.node;
}
