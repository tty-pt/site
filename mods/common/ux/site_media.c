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
	char zoom_pct[16];
	snprintf(zoom_pct, sizeof(zoom_pct), "%d%%", zoom);

	bud_node *zoom_text_node = bud_text(zoom_pct);
	if (zoom_text_out)
		*zoom_text_out = zoom_text_node;

	if (on_zoom_change) {
		return bud_tpl(
		        "<div class='viewer-controls' "
		        "data-detail-viewer-controls='%s' "
		        "data-detail-viewer-save-url='%s'>"
		        "  <label>%s"
		        "    <input type='range' min='" STR(
		                VIEWER_ZOOM_MIN) "' max='" STR(VIEWER_ZOOM_MAX) "' step='10' value='%d' data-detail-viewer-zoom='1' %bind %bind/>"
		                                                                "  </label>"
		                                                                "  <p class='text-xs text-muted' data-detail-viewer-zoom-label='1'>%node</p>"
		                                                                "  <label>"
		                                                                "    <input type='checkbox' checked data-detail-viewer-wrap='1'/>"
		                                                                "    %s"
		                                                                "  </label>"
		                                                                "</div>",
		        module ? module : "", save_url ? save_url : "", ui_t("Zoom"), zoom,
		        "input", on_zoom_change, "change", on_zoom_change,
		        zoom_text_node, ui_t("Wrap lines"));
	}

	return bud_tpl(
	        "<div class='viewer-controls' data-detail-viewer-controls='%s' "
	        "data-detail-viewer-save-url='%s'>"
	        "  <label>%s"
	        "    <input type='range' min='" STR(
	                VIEWER_ZOOM_MIN) "' max='" STR(VIEWER_ZOOM_MAX) "' "
	                                                                "step='"
	                                                                "10' "
	                                                                "value="
	                                                                "'%d' "
	                                                                "data-"
	                                                                "detail"
	                                                                "-viewe"
	                                                                "r-"
	                                                                "zoom='"
	                                                                "1'/>"
	                                                                "  "
	                                                                "</"
	                                                                "label>"
	                                                                "  <p "
	                                                                "class="
	                                                                "'text-"
	                                                                "xs "
	                                                                "text-"
	                                                                "muted'"
	                                                                " data-"
	                                                                "detail"
	                                                                "-viewe"
	                                                                "r-"
	                                                                "zoom-"
	                                                                "label="
	                                                                "'1'>%"
	                                                                "node</"
	                                                                "p>"
	                                                                "  "
	                                                                "<label"
	                                                                ">"
	                                                                "    "
	                                                                "<input"
	                                                                " type="
	                                                                "'check"
	                                                                "box' "
	                                                                "checke"
	                                                                "d "
	                                                                "data-"
	                                                                "detail"
	                                                                "-viewe"
	                                                                "r-"
	                                                                "wrap='"
	                                                                "1'/>"
	                                                                "    "
	                                                                "%s"
	                                                                "  "
	                                                                "</"
	                                                                "label>"
	                                                                "</"
	                                                                "div>",
	        module ? module : "", save_url ? save_url : "", ui_t("Zoom"), zoom,
	        zoom_text_node, ui_t("Wrap lines"));
}

bud_node *site_ui_checkbox(
        const char *name, const char *label, int checked,
        bud_event_handler_fn on_change)
{
	if (!name || !label)
		return NULL;

	if (on_change) {
		return bud_tpl(
		        "<label>"
		        "  <input type='checkbox' name='%s' value='1' %b "
		        "%bind/>"
		        "  %s"
		        "</label>",
		        name, checked ? "checked" : NULL, "change", on_change,
		        label);
	}

	return bud_tpl(
	        "<label>"
	        "  <input type='checkbox' name='%s' value='1' %b/>"
	        "  %s"
	        "</label>",
	        name, checked ? "checked" : NULL, label);
}

static int is_yt_id_char(char c)
{
	return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	        (c >= '0' && c <= '9') || c == '_' || c == '-');
}

static int extract_yt_id(const char *s, char out_id[12])
{
	if (!s)
		return 0;
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
		s++;
	if (!*s)
		return 0;

	/* Check if it is already a raw 11-char ID */
	int is_raw_id = 1;
	for (int i = 0; i < 11; i++) {
		if (!is_yt_id_char(s[i])) {
			is_raw_id = 0;
			break;
		}
	}
	if (is_raw_id) {
		const char *tail = s + 11;
		while (*tail == ' ' || *tail == '\t' || *tail == '\r' ||
		       *tail == '\n')
			tail++;
		if (*tail == '\0') {
			memcpy(out_id, s, 11);
			out_id[11] = '\0';
			return 1;
		}
	}

	/* Check for URL format */
	const char *p = s;
	if (strncmp(p, "http://", 7) == 0)
		p += 7;
	else if (strncmp(p, "https://", 8) == 0)
		p += 8;
	else
		return 0;

	const char *host_start = p;
	const char *slash = strchr(host_start, '/');
	size_t host_len =
	        slash ? (size_t)(slash - host_start) : strlen(host_start);
	if (host_len == 0)
		return 0;

	int is_yt_host = 0;
	if (host_len == 8 && strncmp(host_start, "youtu.be", 8) == 0)
		is_yt_host = 1;
	else if (host_len == 11 && strncmp(host_start, "youtube.com", 11) == 0)
		is_yt_host = 1;
	else if (
	        host_len == 15 &&
	        strncmp(host_start, "www.youtube.com", 15) == 0)
		is_yt_host = 1;
	else if (
	        host_len == 13 && strncmp(host_start, "m.youtube.com", 13) == 0)
		is_yt_host = 1;
	else if (
	        host_len == 17 &&
	        strncmp(host_start, "music.youtube.com", 17) == 0)
		is_yt_host = 1;

	if (!is_yt_host || !slash)
		return 0;

	const char *path = slash;

	/* youtu.be/<id> */
	if (host_len == 8 && strncmp(host_start, "youtu.be", 8) == 0) {
		const char *id_start = path + 1;
		for (int i = 0; i < 11; i++) {
			if (!is_yt_id_char(id_start[i]))
				return 0;
		}
		char term = id_start[11];
		if (term == '\0' || term == '?' || term == '&' || term == '/' ||
		    term == '#' || term == ' ' || term == '\t' ||
		    term == '\r' || term == '\n')
		{
			memcpy(out_id, id_start, 11);
			out_id[11] = '\0';
			return 1;
		}
		return 0;
	}

	/* youtube.com/watch?... v=<id> */
	const char *v_param = strstr(path, "v=");
	if (v_param) {
		if (v_param > path &&
		    (v_param[-1] == '?' || v_param[-1] == '&'))
		{
			const char *id_start = v_param + 2;
			for (int i = 0; i < 11; i++) {
				if (!is_yt_id_char(id_start[i]))
					return 0;
			}
			char term = id_start[11];
			if (term == '\0' || term == '&' || term == '#' ||
			    term == ' ' || term == '\t' || term == '\r' ||
			    term == '\n')
			{
				memcpy(out_id, id_start, 11);
				out_id[11] = '\0';
				return 1;
			}
		}
	}

	/* /embed/<id>, /v/<id>, /shorts/<id> */
	const char *prefixes[] = { "/embed/", "/v/", "/shorts/" };
	for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
		const char *pos = strstr(path, prefixes[i]);
		if (pos) {
			const char *id_start = pos + strlen(prefixes[i]);
			int valid = 1;
			for (int j = 0; j < 11; j++) {
				if (!is_yt_id_char(id_start[j])) {
					valid = 0;
					break;
				}
			}
			if (valid) {
				char term = id_start[11];
				if (term == '\0' || term == '?' ||
				    term == '&' || term == '/' || term == '#' ||
				    term == ' ' || term == '\t' ||
				    term == '\r' || term == '\n')
				{
					memcpy(out_id, id_start, 11);
					out_id[11] = '\0';
					return 1;
				}
			}
		}
	}

	return 0;
}

static int safe_url(const char *s)
{
	if (!s)
		return 0;
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
		s++;
	if (strncmp(s, "https://", 8) != 0)
		return 0;
	for (const char *p = s; *p; p++)
		if (*p == '"' || *p == '\'' || *p == '<' || *p == '>' ||
		    *p == '\\' || *p == '\n' || *p == '\r')
			return 0;
	return 1;
}

static int clean_url(const char *in, char *out, size_t out_sz)
{
	if (!in)
		return 0;
	while (*in == ' ' || *in == '\t' || *in == '\r' || *in == '\n')
		in++;
	if (!*in || !safe_url(in))
		return 0;
	snprintf(out, out_sz, "%s", in);
	char *end = out + strlen(out) - 1;
	while (end >= out &&
	       (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
	{
		*end = '\0';
		end--;
	}
	return out[0] != '\0';
}

bud_node *
site_ui_render_media_slot(const char *yt, const char *audio, const char *pdf)
{
	char yt_id[12] = { 0 };
	int has_yt = extract_yt_id(yt, yt_id);

	char yt_url[1024] = { 0 };
	if (has_yt) {
		snprintf(
		        yt_url, sizeof(yt_url),
		        "https://www.youtube.com/watch?v=%s", yt_id);
	}

	char audio_url[1024] = { 0 };
	int has_audio = clean_url(audio, audio_url, sizeof(audio_url));

	char pdf_url[1024] = { 0 };
	int has_pdf = clean_url(pdf, pdf_url, sizeof(pdf_url));

	int has_btns = has_yt || has_pdf;
	if (!has_btns && !has_audio)
		return NULL;

	bud_node *btn_row =
	        has_btns
	                ? bud_tpl("<div class='media-buttons flex flex-row "
	                          "items-center justify-end gap-2'>"
	                          "  %node"
	                          "  %node"
	                          "</div>",
	                          has_yt ? bud_tpl("<a href='%s' "
	                                           "target='_blank' "
	                                           "rel='noopener noreferrer' "
	                                           "class='btn btn-icon "
	                                           "btn-sm' title='%s' "
	                                           "aria-label='%s'>\xe2\x96\xb6</a>",
	                                           yt_url, ui_t("Watch on YouTube"), ui_t("Watch on YouTube"))
	                                 : NULL,
	                          has_pdf ? bud_tpl("<a href='%s' "
	                                            "target='_blank' "
	                                            "rel='noopener noreferrer' "
	                                            "class='btn btn-icon "
	                                            "btn-sm' title='%s' "
	                                            "aria-label='%s'>\xf0\x9f\x93\x84</a>",
	                                            pdf_url, ui_t("View PDF"), ui_t("View PDF"))
	                                  : NULL)
	                : NULL;

	bud_node *audio_slot =
	        has_audio ? bud_tpl("<div class='audio-slot flex flex-col "
	                            "gap-2 w-full'>"
	                            "  <audio controls class='w-full'>"
	                            "    <source src='%s' type='audio/mpeg'/>"
	                            "  </audio>"
	                            "</div>",
	                            audio_url)
	                  : NULL;

	return bud_tpl(
	        "<div class='media-slot flex flex-col gap-2 w-full'>"
	        "  %node"
	        "  %node"
	        "</div>",
	        btn_row, audio_slot);
}

int site_ui_build_media_html(
        const char *yt, const char *audio, const char *pdf, char *out,
        size_t out_sz)
{
	char buf[8192];
	int pos = 0;
	int has = 0;

	char yt_id[12] = { 0 };
	int has_yt = extract_yt_id(yt, yt_id);

	char yt_url[1024] = { 0 };
	if (has_yt) {
		snprintf(
		        yt_url, sizeof(yt_url),
		        "https://www.youtube.com/watch?v=%s", yt_id);
	}

	char audio_url[1024] = { 0 };
	int has_audio = clean_url(audio, audio_url, sizeof(audio_url));

	char pdf_url[1024] = { 0 };
	int has_pdf = clean_url(pdf, pdf_url, sizeof(pdf_url));

#define APPEND(...)                                                            \
	do {                                                                   \
		int r = snprintf(                                              \
		        buf + pos, sizeof(buf) - (size_t)pos, __VA_ARGS__);    \
		if (r > 0)                                                     \
			pos += r;                                              \
		if ((size_t)pos >= sizeof(buf))                                \
			goto done;                                             \
	} while (0)

	int has_btns = has_yt || has_pdf;
	if (has_btns) {
		APPEND("<div class=\"media-buttons flex flex-row items-center "
		       "justify-end gap-2\">");
		if (has_yt) {
			APPEND("<a href=\"%s\" target=\"_blank\" "
			       "rel=\"noopener noreferrer\" "
			       "class=\"btn btn-icon btn-sm\" "
			       "title=\"Watch on YouTube\" "
			       "aria-label=\"Watch on YouTube\">"
			       "\xe2\x96\xb6</a>",
			       yt_url);
		}
		if (has_pdf) {
			APPEND("<a href=\"%s\" target=\"_blank\" "
			       "rel=\"noopener noreferrer\" "
			       "class=\"btn btn-icon btn-sm\" "
			       "title=\"View PDF\" "
			       "aria-label=\"View PDF\">"
			       "\xf0\x9f\x93\x84</a>",
			       pdf_url);
		}
		APPEND("</div>");
		has = 1;
	}
	if (has_audio) {
		APPEND("<div class=\"audio-slot flex flex-col gap-2 w-full\">"
		       "<audio controls class=\"w-full\">"
		       "<source src=\"%s\" type=\"audio/mpeg\">"
		       "</audio></div>",
		       audio_url);
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
