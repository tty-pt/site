#include "bud/bud.h"
#include "bud/bud_jsx.h"
#include "bud/bud_app.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../common/viewer_zoom.h"

#include <transp/music.h>
#include <transp/spelling.h>

#include "../../common/ux/site_ui.c"

#include "../fields.h"

static app_state_t app_state = { 0 };
static bud_node *g_main = NULL;

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
	hyle_bud_state_apply_len(
	        &app_state.cache, song_fields, data, (size_t)data_len);
	bud_state_apply_len(
	        &app_state, song_app_fields, data, (size_t)data_len);
	bud_json_data_len(
	        data, (size_t)data_len, app_state.chord_html,
	        sizeof(app_state.chord_html));
	bud_app_set_state();
}

void wasm_init(const char *json, int len)
{
	size_t jlen = len >= 0 ? (size_t)len : 0;
	hyle_bud_state_apply_len(&app_state.cache, song_fields, json, jlen);
	bud_state_apply_len(&app_state, song_app_fields, json, jlen);
}

/* ── Page builder helpers ──────────────────────────── */

static bud_node *render_key_options(void)
{
	bud_node *opts = bud_fragment();
	int cur_t = ((app_state.transpose % 12) + 12) % 12;
	for (int i = 0; i < 12; i++) {
		const char *name = key_name(
		        i, app_state.original_key, app_state.use_latin);
		bud_append(
		        opts, bud_tpl("<option value='%d' %b>%s</option>", i,
		                      (i == cur_t) ? "selected" : NULL, name));
	}
	return opts;
}

static bud_node *render_transpose_form(bud_node *key_options)
{
	return bud_tpl(
	        "<form id='transpose-form' method='GET' action='%s' "
	        "data-song-id='%s' data-action='/api/song/%s/transpose?h=1'>"
	        "  <label>Key: <select name='t' %bind>%node</select></label>"
	        "  %node"
	        "  <label>Zoom <input type='range' name='z' min='" STR(
	                VIEWER_ZOOM_MIN) "' "
	                                 "max='" STR(
	                                         VIEWER_ZOOM_MAX) "' step='10' "
	                                                          "value='%d' "
	                                                          "data-detail-"
	                                                          "viewer-zoom="
	                                                          "'1' %bind "
	                                                          "%bind/></"
	                                                          "label>"
	                                                          "  <button "
	                                                          "type='"
	                                                          "submit' "
	                                                          "class='btn' "
	                                                          "data-wasm-"
	                                                          "hide='1'>"
	                                                          "Apply</"
	                                                          "button>"
	                                                          "</form>",
	        app_state.path, app_state.cache.id, app_state.cache.id,
	        "change", bud_api_action_handler, key_options,
	        site_ui_checkbox(
	                "l", "Latin", app_state.use_latin,
	                bud_api_action_handler),
	        app_state.zoom, "input", on_zoom_change, "change",
	        on_zoom_change);
}

static bud_node *render_chord_viewer(void)
{
	char zoom_style[64];
	snprintf(
	        zoom_style, sizeof(zoom_style),
	        "width:100%%;max-width:100%%;--chord-zoom:%d", app_state.zoom);

	bud_node *media_slot = site_ui_render_media_slot(
	        app_state.cache.yt, app_state.cache.audio, app_state.cache.pdf);

	char meta_html[1024] = { 0 };
	if (app_state.cache.type[0]) {
		snprintf(
		        meta_html + strlen(meta_html),
		        sizeof(meta_html) - strlen(meta_html),
		        "<div class='italic whitespace-pre-wrap text-xs "
		        "text-muted'>%s</div>",
		        app_state.cache.type);
	}
	if (app_state.cache.author[0]) {
		snprintf(
		        meta_html + strlen(meta_html),
		        sizeof(meta_html) - strlen(meta_html),
		        "<div class='text-xs text-muted'>%s</div>",
		        app_state.cache.author);
	}

	return bud_tpl(
	        "<div id='main' data-song-id='%s' data-use-latin='%d' "
	        "data-show-media='%d' "
	        "data-yt='%s' data-audio='%s' data-pdf='%s' "
	        "data-original-key='%d' "
	        "data-save-url='%s' data-detail-viewer-controls='song' "
	        "data-is-owner='%d' "
	        "data-transpose='%d' data-zoom='%d' style='%s' "
	        "data-type-display='%s' data-author='%s'>"
	        "  <div class='flex flex-col gap-4 w-full max-w-xl'>"
	        "    <div id='song-detail-body' class='flex justify-between "
	        "items-center w-full max-w-xl text-xs text-muted gap-2' "
	        "data-detail-viewer-scope='1'>"
	        "      <div class='flex flex-col gap-1 min-w-0'>%raw</div>"
	        "      <div class='flex justify-end items-center gap-2 "
	        "flex-shrink-0 ml-auto' data-song-media='1'>%node</div>"
	        "    </div>"
	        "    <div class='detail-viewer-scroll w-full max-w-xl' "
	        "data-detail-viewer-scroll='1'>"
	        "      <pre id='chord-data' data-detail-viewer-target='1' "
	        "class='whitespace-pre-wrap font-mono p-4 rounded "
	        "chord-data'>%raw</pre>"
	        "    </div>"
	        "  </div>"
	        "</div>",
	        app_state.cache.id, app_state.use_latin ? 1 : 0,
	        app_state.show_media ? 1 : 0, app_state.cache.yt,
	        app_state.cache.audio, app_state.cache.pdf,
	        app_state.original_key, app_state.save_url,
	        app_state.is_owner ? 1 : 0, app_state.transpose, app_state.zoom,
	        zoom_style, app_state.cache.type, app_state.cache.author,
	        meta_html, media_slot, app_state.chord_html);
}

bud_node *bud_app_render(void)
{
	bud_node *main_node = render_chord_viewer();
	g_main = main_node;
	bud_node *key_options = render_key_options();
	bud_node *transpose_form = render_transpose_form(key_options);

	bud_node *item_menu = site_ui_item_menu(
	        "song", app_state.cache.id, app_state.is_owner);

	bud_node *menu_items =
	        bud_tpl("%node %node", transpose_form, item_menu);

	bud_node *inner = site_ui_layout(
	        app_state.cache.title, app_state.path,
	        site_ui_module_icon("song"), app_state.page_user, menu_items,
	        bud_tpl("<div class='center flex flex-col gap-4'>%node</div>",
	                main_node));
	return bud_tpl("<div id='bud-root'>%node</div>", inner);
}
