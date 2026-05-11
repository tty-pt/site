#include "../fields.h"

static app_state_t app_state = { 0 };
static bud_node *g_main = NULL;
static bud_node *g_zoom_label = NULL;
static bud_node *g_chord_raw = NULL;
static bud_node *g_chord_pre = NULL;
static bud_node *g_key_options[23] = { NULL };

#define BUD_LOG(msg)                                                           \
	do {                                                                   \
		const char *_m = (msg);                                        \
		if (bud_host_log_fn)                                           \
			bud_host_log_fn(_m, strlen(_m));                       \
	} while (0)

static void fetch_transpose(void)
{
	if (!bud_host_fetch_fn)
		return;
	char url[1024];
	snprintf(
	        url,
	        sizeof(url),
	        "/api/song/%s/transpose?t=%d&h=1%s%s%s",
	        app_state.cache.id,
	        app_state.transpose,
	        app_state.use_bemol ? "&b=1" : "",
	        app_state.use_latin ? "&l=1" : "",
	        app_state.show_media ? "&m=1" : "");
	bud_host_fetch_fn(url, strlen(url), 1);
}

extern void wasm_mark_dirty(void);
extern void wasm_flush(void);
extern void (*bud_host_set_location_fn)(const char *url, size_t len);

void wasm_fetch_callback(int request_id, const char *data, int data_len)
{
	(void)request_id;
	(void)data_len;
	bud_json_data(data, app_state.chord_html, sizeof(app_state.chord_html));
	if (g_chord_raw)
		bud_raw_set_text(g_chord_raw, app_state.chord_html);

	extern void bud_patch_innerhtml(unsigned int node_id, const char *html);
	if (g_chord_pre)
		bud_patch_innerhtml(
		        bud_node_id(g_chord_pre), app_state.chord_html);

	for (int i = -11; i <= 11; i++) {
		const char *name = key_name(
		        i,
		        app_state.original_key,
		        app_state.use_bemol,
		        app_state.use_latin);
		if (g_key_options[i + 11])
			bud_patch_text(g_key_options[i + 11], name);
	}
}

void wasm_load_chords(void)
{
	fetch_transpose();
}

void wasm_init(const char *json, int len)
{
	(void)len;
	bud_state_apply(&app_state, song_fields, json);
}

static int on_transpose_change(bud_event *event)
{
	const char *value = (const char *)event->user;
	const char *name = bud_get_attr(event->target, "name");
	BUD_LOG("otc: called");
	if (!name || !value) {
		if (!name)
			BUD_LOG("otc: name is NULL");
		if (!value)
			BUD_LOG("otc: value is NULL");
		return 0;
	}
	BUD_LOG("otc: name and value ok");
	if (strcmp(name, "t") == 0)
		app_state.transpose = atoi(value);
	else if (strcmp(name, "b") == 0)
		app_state.use_bemol = atoi(value);
	else if (strcmp(name, "l") == 0)
		app_state.use_latin = atoi(value);
	else if (strcmp(name, "m") == 0)
		app_state.show_media = atoi(value);

	BUD_LOG("otc: calling fetch_transpose");
	fetch_transpose();
	if (bud_host_set_location_fn) {
		char url[1024];
		snprintf(
		        url,
		        sizeof(url),
		        "%s?t=%d&b=%d&l=%d&m=%d",
		        app_state.path,
		        app_state.transpose,
		        app_state.use_bemol ? 1 : 0,
		        app_state.use_latin ? 1 : 0,
		        app_state.show_media ? 1 : 0);
		bud_host_set_location_fn(url, strlen(url));
	}
	BUD_LOG("otc: done");
	return 0;
}

static int on_zoom_change(bud_event *event)
{
	return ui_on_zoom_change(event, &app_state.zoom, g_main, g_zoom_label);
}

bud_node *bud_app_render(void)
{
	char transpose_str[16];
	char orig_key_str[16];
	char zoom_str[16];
	char zoom_style[64];
	snprintf(
	        transpose_str,
	        sizeof(transpose_str),
	        "%d",
	        app_state.transpose);
	snprintf(
	        orig_key_str,
	        sizeof(orig_key_str),
	        "%d",
	        app_state.original_key);
	snprintf(zoom_str, sizeof(zoom_str), "%d", app_state.zoom);
	snprintf(
	        zoom_style,
	        sizeof(zoom_style),
	        "width:100%%;max-width:100%%;--chord-zoom:%d",
	        app_state.zoom);

	char bemol_val[2] = { app_state.use_bemol ? '1' : '0', '\0' };
	char latin_val[2] = { app_state.use_latin ? '1' : '0', '\0' };
	char media_val[2] = { app_state.show_media ? '1' : '0', '\0' };
	char owner_val[2] = { app_state.is_owner ? '1' : '0', '\0' };

	int has_media =
	        (app_state.cache.yt[0] != '\0' ||
	         app_state.cache.audio[0] != '\0' ||
	         app_state.cache.pdf[0] != '\0');

	bud_node *media_slot = NULL;
	if (app_state.show_media && has_media) {
		bud_node *yt_slot = NULL;
		if (app_state.cache.yt[0]) {
			char yt_src[1024];
			snprintf(
			        yt_src,
			        sizeof(yt_src),
			        "https://www.youtube.com/embed/%s",
			        app_state.cache.yt);
			yt_slot = lx_el("div",
			                lx_attr("id", "media-container"),
			                lx_attr("class",
			                        "flex flex-col gap-4 w-full "
			                        "max-w-xl"),
			                lx_el("iframe",
			                      lx_attr("src", yt_src),
			                      lx_attr("class",
			                              "w-full aspect-video "
			                              "border-none"),
			                      lx_attr("allowfullscreen", "")))
			                  .data.node;
		}

		bud_node *audio_slot = NULL;
		if (app_state.cache.audio[0]) {
			audio_slot = lx_el("div",
			                   lx_attr("id", "media-container"),
			                   lx_attr("class",
			                           "flex flex-col gap-4 w-full "
			                           "max-w-xl"),
			                   lx_el("audio",
			                         lx_attr("controls", ""),
			                         lx_attr("class", "w-full"),
			                         lx_el("source",
			                               lx_attr("src",
			                                       app_state.cache
			                                               .audio),
			                               lx_attr("type",
			                                       "audio/mpeg"))))
			                     .data.node;
		}

		bud_node *pdf_slot = NULL;
		if (app_state.cache.pdf[0]) {
			pdf_slot =
			        lx_el("div",
			              lx_attr("id", "media-container"),
			              lx_attr("class",
			                      "flex flex-col gap-4 w-full "
			                      "max-w-xl"),
			              lx_el("a",
			                    lx_attr("href",
			                            app_state.cache.pdf),
			                    lx_attr("target", "_blank"),
			                    lx_attr("rel", "noopener"),
			                    lx_attr("class", "text-blue-600"),
			                    lx_text("View PDF")))
			                .data.node;
		}

		media_slot =
		        lx_el("div",
		              lx_attr("id", "media-slot"),
		              lx_attr("class",
		                      "flex flex-col gap-4 w-full max-w-xl"),
		              yt_slot ? lx_node(yt_slot) : lx_none(),
		              audio_slot ? lx_node(audio_slot) : lx_none(),
		              pdf_slot ? lx_node(pdf_slot) : lx_none())
		                .data.node;
	}

	g_chord_raw = bud_raw(app_state.chord_html);
	g_chord_pre = lx_el("pre",
	                    lx_attr("id", "chord-data"),
	                    lx_attr("data-detail-viewer-target", "1"),
	                    lx_attr("class",
	                            "whitespace-pre-wrap font-mono p-4 rounded "
	                            "chord-data"),
	                    lx_node(g_chord_raw))
	                      .data.node;

	g_main = lx_el("div",
	               lx_attr("id", "main"),
	               lx_attr("data-song-id", app_state.cache.id),
	               lx_attr("data-transpose", transpose_str),
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
	               lx_attr("data-zoom", zoom_str),
	               lx_attr("style", zoom_style),
	               lx_attr("data-type-display", app_state.cache.type),
	               lx_attr("data-author", app_state.cache.author),
	               lx_el("div",
	                     lx_attr("class",
	                             "flex flex-col gap-4 w-full max-w-xl"),
	                     lx_el("div",
	                           lx_attr("class",
	                                   "detail-viewer-scroll w-full "
	                                   "max-w-xl"),
	                           lx_attr("data-detail-viewer-scroll", "1"),
	                           lx_node(g_chord_pre)),
	                     media_slot ? lx_node(media_slot) : lx_none()))
	                 .data.node;

	bud_node *detail_body = NULL;
	if (app_state.cache.type[0] || app_state.cache.author[0]) {
		detail_body =
		        lx_el("div",
		              lx_attr("id", "song-detail-body"),
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
		                    lx_el("div",
		                          lx_attr("class", "text-right"),
		                          lx_text(app_state.cache.author[0]
		                                          ? app_state.cache
		                                                    .author
		                                          : "N/A"))))
		                .data.node;
	}

	bud_node *key_options = NULL;
	for (int i = -11; i <= 11; i++) {
		char val_str[16];
		snprintf(val_str, sizeof(val_str), "%d", i);
		bud_node *opt =
		        lx_el("option",
		              lx_attr("value", val_str),
		              i == app_state.transpose ? lx_attr("selected", "")
		                                       : lx_none(),
		              lx_text(key_name(
		                      i,
		                      app_state.original_key,
		                      app_state.use_bemol,
		                      app_state.use_latin)))
		                .data.node;
		g_key_options[i + 11] = opt;
		if (!key_options) {
			key_options = lx_frag(lx_node(opt)).data.node;
		} else {
			bud_append(key_options, opt);
		}
	}

	bud_node *transpose_form =
	        lx_el("form",
	              lx_attr("id", "transpose-form"),
	              lx_attr("method", "GET"),
	              lx_attr("action", app_state.path),
	              lx_attr("data-song-id", app_state.cache.id),
	              lx_el("label",
	                    lx_text("Key:"),
	                    lx_el("select",
	                          lx_attr("name", "t"),
	                          lx_bind("change", 0, on_transpose_change),
	                          lx_node(key_options))),
	              lx_el("label",
	                    lx_el("input",
	                          lx_attr("type", "checkbox"),
	                          lx_attr("name", "b"),
	                          lx_bind("change", 0, on_transpose_change),
	                          app_state.use_bemol ? lx_attr("checked", "")
	                                              : lx_none()),
	                    lx_text("Flats (♭)")),
	              lx_el("label",
	                    lx_el("input",
	                          lx_attr("type", "checkbox"),
	                          lx_attr("name", "l"),
	                          lx_bind("change", 0, on_transpose_change),
	                          app_state.use_latin ? lx_attr("checked", "")
	                                              : lx_none()),
	                    lx_text("Latin")),
	              lx_el("label",
	                    lx_el("input",
	                          lx_attr("type", "checkbox"),
	                          lx_attr("name", "m"),
	                          lx_bind("change", 0, on_transpose_change),
	                          app_state.show_media ? lx_attr("checked", "")
	                                               : lx_none()),
	                    lx_text("Video")),
	              lx_el("button",
	                    lx_attr("type", "submit"),
	                    lx_attr("class", "btn"),
	                    lx_text("Apply")))
	                .data.node;

	bud_node *zoom_ctrl = site_ui_viewer_controls(
	        "song",
	        app_state.zoom,
	        app_state.save_url,
	        on_zoom_change,
	        &g_zoom_label);
	bud_node *item_menu = site_ui_item_menu(
	        "song", app_state.cache.id, app_state.is_owner);

	bud_node *menu_items =
	        lx_frag(lx_node(transpose_form),
	                zoom_ctrl ? lx_node(zoom_ctrl) : lx_none(),
	                item_menu ? lx_node(item_menu) : lx_none())
	                .data.node;

	bud_node *inner = site_ui_layout(
	        app_state.cache.title,
	        app_state.path,
	        "🎸",
	        app_state.page_user,
	        menu_items,
	        lx_frag(lx_el("div",
	                      lx_attr("class", "center flex flex-col gap-4"),
	                      lx_node(g_main),
	                      detail_body ? lx_node(detail_body) : lx_none()))
	                .data.node);
	return lx_el("div", lx_attr("id", "bud-root"), lx_node(inner))
	        .data.node;
}
