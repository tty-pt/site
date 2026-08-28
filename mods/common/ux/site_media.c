#ifndef SITE_MEDIA_C
#define SITE_MEDIA_C

#include "site_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../viewer_zoom.h"
#include "bud/bud_app.h"
#include "bud/bud_jsx.h"

void ui_apply_zoom(bud_node *main_node, bud_node *zoom_label, int zoom)
{
	char style[64], zoom_str[16], zoom_pct[16];
	snprintf(
	        style, sizeof(style),
	        "width:100%%;max-width:100%%;--chord-zoom:%d", zoom);
	snprintf(zoom_str, sizeof(zoom_str), "%d", zoom);
	snprintf(zoom_pct, sizeof(zoom_pct), "%d%%", zoom);
	bud_patch_attr(main_node, "style", style);
	bud_patch_attr(main_node, "data-zoom", zoom_str);
	if (zoom_label)
		bud_patch_text(zoom_label, zoom_pct);
}

int ui_on_zoom_change(
        bud_event *event, int *zoom_out, bud_node *main_node,
        bud_node *zoom_label)
{
	const char *value = (const char *)event->user;
	if (!value)
		return 0;
	int z = atoi(value);
	if (z < VIEWER_ZOOM_MIN)
		z = VIEWER_ZOOM_MIN;
	else if (z > VIEWER_ZOOM_MAX)
		z = VIEWER_ZOOM_MAX;
	*zoom_out = z;
	ui_apply_zoom(main_node, zoom_label, z);
	return 0;
}

bud_node *site_ui_viewer_controls(
        const char *module, int zoom, const char *save_url,
        bud_event_handler_fn on_zoom_change, bud_node **zoom_text_out)
{
	char zoom_str[16];
	char zoom_pct[16];
	snprintf(zoom_str, sizeof(zoom_str), "%d", zoom);
	snprintf(zoom_pct, sizeof(zoom_pct), "%d%%", zoom);

	bud_node *zoom_text_node = bud_text(zoom_pct);
	if (zoom_text_out)
		*zoom_text_out = zoom_text_node;

	bud_arg zoom_input = lx_none();
	if (on_zoom_change)
		zoom_input =
		        lx_frag(lx_bind("input", 0, on_zoom_change),
		                lx_bind("change", 0, on_zoom_change));

	return lx_el("div", lx_attr("class", "viewer-controls"),
	             lx_attr("data-detail-viewer-controls", module),
	             lx_attr("data-detail-viewer-save-url", save_url),
	             lx_el("label", lx_text("Zoom"),
	                   lx_el("input", lx_attr("type", "range"),
	                         lx_attr("min", STR(VIEWER_ZOOM_MIN)),
	                         lx_attr("max", STR(VIEWER_ZOOM_MAX)),
	                         lx_attr("step", "10"),
	                         lx_attr("value", zoom_str),
	                         lx_attr("data-detail-viewer-zoom", "1"),
	                         zoom_input)),
	             lx_el("p", lx_attr("class", "text-xs text-muted"),
	                   lx_attr("data-detail-viewer-zoom-label", "1"),
	                   lx_node(zoom_text_node)),
	             lx_el("label",
	                   lx_el("input", lx_attr("type", "checkbox"),
	                         lx_attr("checked", "checked"),
	                         lx_attr("data-detail-viewer-wrap", "1")),
	                   lx_text("Wrap lines")))
	        .data.node;
}

bud_node *site_ui_checkbox(
        const char *name, const char *label, int checked,
        bud_event_handler_fn on_change)
{
	if (!name || !label)
		return lx_none().data.node;

	bud_arg bind = lx_none();
	if (on_change)
		bind = lx_bind("change", 0, on_change);

	return lx_el("label",
	             lx_el("input", lx_attr("type", "checkbox"),
	                   lx_attr("name", name), lx_attr("value", "1"), bind,
	                   checked ? lx_attr("checked", "") : lx_none()),
	             lx_text(label))
	        .data.node;
}

static int valid_yt_id(const char *s);
static int safe_url(const char *s);

bud_node *
site_ui_render_media_slot(const char *yt, const char *audio, const char *pdf)
{
	bud_node *inner = bud_fragment();
	int has_media = 0;
	if (!inner)
		return NULL;
	if (yt && yt[0] && !valid_yt_id(yt))
		yt = NULL;
	if (audio && audio[0] && !safe_url(audio))
		audio = NULL;
	if (pdf && pdf[0] && !safe_url(pdf))
		pdf = NULL;

	int has_btns = (yt && yt[0]) || (pdf && pdf[0]);
	if (has_btns) {
		bud_node *btn_row =
		        lx_el("div",
		              lx_attr("class",
		                      "media-buttons flex flex-row items-center "
		                      "justify-end gap-2"))
		                .data.node;
		if (yt && yt[0]) {
			char yt_url[1024];
			snprintf(
			        yt_url, sizeof(yt_url),
			        "https://www.youtube.com/watch?v=%.11s", yt);
			bud_append(
			        btn_row,
			        lx_el("a",
			              lx_attr("href", yt_url),
			              lx_attr("target", "_blank"),
			              lx_attr("rel", "noopener noreferrer"),
			              lx_attr("class",
			                      "btn btn-icon btn-sm"),
			              lx_attr("title", "Watch on YouTube"),
			              lx_attr("aria-label",
			                      "Watch on YouTube"),
			              lx_text("\xe2\x96\xb6"))
			                .data.node);
		}
		if (pdf && pdf[0]) {
			bud_append(
			        btn_row,
			        lx_el("a",
			              lx_attr("href", pdf),
			              lx_attr("target", "_blank"),
			              lx_attr("rel", "noopener noreferrer"),
			              lx_attr("class",
			                      "btn btn-icon btn-sm"),
			              lx_attr("title", "View PDF"),
			              lx_attr("aria-label", "View PDF"),
			              lx_text("\xf0\x9f\x93\x84"))
			                .data.node);
		}
		bud_append(inner, btn_row);
		has_media = 1;
	}

	if (audio && audio[0]) {
		bud_append(
		        inner,
		        lx_el("div",
		              lx_attr("class", "audio-slot flex flex-col gap-2 w-full"),
		              lx_el("audio", lx_attr("controls", ""),
		                    lx_attr("class", "w-full"),
		                    lx_el("source", lx_attr("src", audio),
		                          lx_attr("type", "audio/mpeg"))))
		                .data.node);
		has_media = 1;
	}

	if (!has_media)
		return NULL;

	return lx_el("div",
	             lx_attr("class", "media-slot flex flex-col gap-2 w-full"),
	             lx_node(inner))
	        .data.node;
}

static int valid_yt_id(const char *s)
{
	if (!s)
		return 0;
	for (int i = 0; i < 11; i++)
		if (!((s[i] >= 'A' && s[i] <= 'Z') ||
		      (s[i] >= 'a' && s[i] <= 'z') ||
		      (s[i] >= '0' && s[i] <= '9') || s[i] == '_' ||
		      s[i] == '-'))
			return 0;
	for (const char *p = s + 11; *p; p++)
		if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
			return 0;
	return 1;
}

static int safe_url(const char *s)
{
	if (!s || strncmp(s, "https://", 8) != 0)
		return 0;
	for (const char *p = s; *p; p++)
		if (*p == '"' || *p == '\'' || *p == '<' || *p == '>' ||
		    *p == '\\' || *p == '\n' || *p == '\r')
			return 0;
	return 1;
}

int site_ui_build_media_html(
        const char *yt, const char *audio, const char *pdf, char *out,
        size_t out_sz)
{
	char buf[8192];
	int pos = 0;
	int has = 0;

	if (yt && yt[0] && !valid_yt_id(yt))
		yt = NULL;
	if (audio && audio[0] && !safe_url(audio))
		audio = NULL;
	if (pdf && pdf[0] && !safe_url(pdf))
		pdf = NULL;

#define APPEND(...)                                                            \
	do {                                                                   \
		int r = snprintf(                                              \
		        buf + pos, sizeof(buf) - (size_t)pos, __VA_ARGS__);    \
		if (r > 0)                                                     \
			pos += r;                                              \
		if ((size_t)pos >= sizeof(buf))                                \
			goto done;                                             \
	} while (0)

	int has_btns = (yt && yt[0]) || (pdf && pdf[0]);
	if (has_btns) {
		APPEND("<div class=\"media-buttons flex flex-row items-center justify-end gap-2\">");
		if (yt && yt[0]) {
			char yt_url[1024];
			snprintf(
			        yt_url, sizeof(yt_url),
			        "https://www.youtube.com/watch?v=%.11s", yt);
			APPEND("<a href=\"%s\" target=\"_blank\" "
			       "rel=\"noopener noreferrer\" "
			       "class=\"btn btn-icon btn-sm\" "
			       "title=\"Watch on YouTube\" "
			       "aria-label=\"Watch on YouTube\">"
			       "\xe2\x96\xb6</a>",
			       yt_url);
		}
		if (pdf && pdf[0]) {
			APPEND("<a href=\"%s\" target=\"_blank\" "
			       "rel=\"noopener noreferrer\" "
			       "class=\"btn btn-icon btn-sm\" "
			       "title=\"View PDF\" "
			       "aria-label=\"View PDF\">"
			       "\xf0\x9f\x93\x84</a>",
			       pdf);
		}
		APPEND("</div>");
		has = 1;
	}
	if (audio && audio[0]) {
		APPEND("<div class=\"audio-slot flex flex-col gap-2 w-full\">"
		       "<audio controls class=\"w-full\">"
		       "<source src=\"%s\" type=\"audio/mpeg\">"
		       "</audio></div>",
		       audio);
		has = 1;
	}
#undef APPEND
done:
	if (has)
		snprintf(out, out_sz, "%s", buf);
	else
		out[0] = '\0';
	return has;
}

#endif
