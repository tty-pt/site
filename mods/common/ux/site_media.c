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
	char src[1024];
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

	if (yt && yt[0]) {
		snprintf(
		        src, sizeof(src), "https://www.youtube.com/embed/%.11s",
		        yt);
		bud_append(
		        inner,
		        lx_el("div",
		              lx_attr("class", "flex flex-col gap-4 w-full"),
		              lx_el("iframe", lx_attr("src", src),
		                    lx_attr("class", "w-full aspect-video "
		                                     "border-none"),
		                    lx_attr("title", "YouTube video player"),
		                    lx_attr("allow",
		                            "accelerometer; autoplay; "
		                            "clipboard-write; encrypted-media; "
		                            "gyroscope; picture-in-picture; "
		                            "web-share"),
		                    lx_attr("referrerpolicy",
		                            "strict-origin-when-cross-origin"),
		                    lx_attr("allowfullscreen", "")))
		                .data.node);
		has_media = 1;
	}

	if (audio && audio[0]) {
		bud_append(
		        inner,
		        lx_el("div",
		              lx_attr("class", "flex flex-col gap-4 w-full"),
		              lx_el("audio", lx_attr("controls", ""),
		                    lx_attr("class", "w-full"),
		                    lx_el("source", lx_attr("src", audio),
		                          lx_attr("type", "audio/mpeg"))))
		                .data.node);
		has_media = 1;
	}

	if (pdf && pdf[0]) {
		bud_append(
		        inner,
		        lx_el("div",
		              lx_attr("class", "flex flex-col gap-4 w-full"),
		              lx_el("a", lx_attr("href", pdf),
		                    lx_attr("target", "_blank"),
		                    lx_attr("rel", "noopener"),
		                    lx_attr("class", "text-blue-600"),
		                    lx_text("View PDF")))
		                .data.node);
		has_media = 1;
	}

	if (!has_media)
		return NULL;

	return lx_el("div",
	             lx_attr("class", "media-slot flex flex-col gap-4 w-full"),
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

	if (yt && yt[0]) {
		char src[1024];
		snprintf(
		        src, sizeof(src), "https://www.youtube.com/embed/%.11s",
		        yt);
		APPEND("<div class=\"flex flex-col gap-4 w-full\">"
		       "<iframe src=\"%s\" class=\"w-full aspect-video "
		       "border-none\" title=\"YouTube video player\" "
		       "allow=\"accelerometer; autoplay; clipboard-write; "
		       "encrypted-media; gyroscope; picture-in-picture; "
		       "web-share\" "
		       "referrerpolicy=\"strict-origin-when-cross-origin\" "
		       "allowfullscreen></iframe></div>",
		       src);
		has = 1;
	}
	if (audio && audio[0]) {
		APPEND("<div class=\"flex flex-col gap-4 w-full\">"
		       "<audio controls class=\"w-full\">"
		       "<source src=\"%s\" type=\"audio/mpeg\">"
		       "</audio></div>",
		       audio);
		has = 1;
	}
	if (pdf && pdf[0]) {
		APPEND("<div class=\"flex flex-col gap-4 w-full\">"
		       "<a href=\"%s\" target=\"_blank\" rel=\"noopener\" "
		       "class=\"text-blue-600\">View PDF</a></div>",
		       pdf);
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
