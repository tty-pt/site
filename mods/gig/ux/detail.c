#include "bud/bud.h"
#include "bud/bud_jsx.h"
#include "bud/bud_app.h"
#include "../fields.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../common/viewer_zoom.h"

#include "../../song/ux/music.c"

#include "../../common/ux/site_ui.c"
#include "../../common/state_macros.h"

#include "../../song/lib/transp/transp_flags.h"

#ifdef __wasm__
__attribute__((import_module("env"), import_name("bud_host_log"))) void
bud_host_log(const char *msg, size_t len);
#endif

#define MAX_SB_SONGS 128
#define MAX_SB_OPTS 16

/* ── WASM app state (gig WASM runtime) ────────────── */

#define SB_SONG_ROW_SCHEMA(F_STR, F_INT, st)                                   \
	F_STR(st, title, 256)                                                  \
	F_STR(st, song_id, 256)                                                \
	F_INT(st, orig_key)                                                    \
	F_INT(st, transpose)                                                   \
	F_INT(st, flags)                                                       \
	F_STR(st, yt, 512)                                                     \
	F_STR(st, audio, 512)                                                  \
	F_STR(st, pdf, 512)                                                    \
	F_STR(st, type, 512)

BUD_STATE_STRUCT_EXT(sb_song_row_data_t, SB_SONG_ROW_SCHEMA, char *chord_html;)

#define SB_SONG_OPTION_SCHEMA(F_STR, F_INT, st)                                \
	F_STR(st, id, 128)                                                     \
	F_STR(st, title, 256)

BUD_STATE_STRUCT(sb_song_option_t, SB_SONG_OPTION_SCHEMA)

typedef struct {
	char sb_id[128];
	char path[256];
	int zoom;
	int latin;
	int show_media;
	int is_owner;
	char title[256];
	char user[64];
	char csrf_token[33];
	char grp_id[128];
	char owner[64];
	int n_songs;
	int n_song_options;
} sb_app_state_t;

static sb_app_state_t sb_app_state = { 0 };
static sb_song_row_data_t g_sb_songs[MAX_SB_SONGS];
static sb_song_option_t g_sb_options[MAX_SB_OPTS];
static bud_node *g_sb_chord_nodes[MAX_SB_SONGS];
static bud_node *g_sb_media_nodes[MAX_SB_SONGS];
static int g_sb_n_chord_nodes;
static bud_node *g_sb_root = NULL;
static bud_node *g_sb_main = NULL;

/* ── Field descriptor table for sb_app_state_t (simple fields) ── */

static const bud_field_desc_t gig_app_fields[] = {
	OVERLAY_STR(id, sb_app_state_t, sb_id, 128),
	OVERLAY_STR(path, sb_app_state_t, path, 256),
	OVERLAY_INT(zoom, sb_app_state_t, zoom),
	OVERLAY_INT(l, sb_app_state_t, latin),
	OVERLAY_INT(m, sb_app_state_t, show_media),
	OVERLAY_INT(owner, sb_app_state_t, is_owner),
	OVERLAY_STR(title, sb_app_state_t, title, 256),
	OVERLAY_STR(user, sb_app_state_t, user, 64),
	OVERLAY_STR(csrf, sb_app_state_t, csrf_token, 33),
	OVERLAY_STR(grp, sb_app_state_t, grp_id, 128),
	OVERLAY_STR(owner_name, sb_app_state_t, owner, 64),
	FIELD_END
};

BUD_STATE_FIELDS(sb_song_row_data_t, sb_song_row_fields, SB_SONG_ROW_SCHEMA)
BUD_STATE_FIELDS(sb_song_option_t, sb_song_option_fields, SB_SONG_OPTION_SCHEMA)

/* ── WASM state init (called by JS bridge before bud_app_mount) ── */

void wasm_init(const char *json, int len)
{
	(void)len;
	memset(&sb_app_state, 0, sizeof(sb_app_state));

	bud_state_apply(&sb_app_state, gig_app_fields, json);

	if (sb_app_state.zoom < VIEWER_ZOOM_MIN ||
	    sb_app_state.zoom > VIEWER_ZOOM_MAX)
		sb_app_state.zoom = VIEWER_ZOOM_DEFAULT;

	bud_state_apply_array(
	        json, "songs", g_sb_songs, sizeof(sb_song_row_data_t),
	        &sb_app_state.n_songs, MAX_SB_SONGS, sb_song_row_fields);

	bud_state_apply_array(
	        json, "opts", g_sb_options, sizeof(sb_song_option_t),
	        &sb_app_state.n_song_options, MAX_SB_OPTS,
	        sb_song_option_fields);
}

/* ── Zoom slider event handler ──────────────────────────── */

static void fetch_sb_transpose(int song_index, int semitones)
{
	if (!bud_host_fetch_fn)
		return;
	char url[1024];
	snprintf(
	        url, sizeof(url),
	        "/api/gig/%s/transpose?n=%d&t=%d%s%s&z=%d",
	        sb_app_state.sb_id, song_index, semitones,
	        sb_app_state.latin ? "&l=1" : "",
	        sb_app_state.show_media ? "&m=1" : "", sb_app_state.zoom);
	bud_host_fetch_fn(url, strlen(url), 1);
}

extern void wasm_mark_dirty(void);
extern void wasm_flush(void);

void wasm_fetch_callback(int request_id, const char *data, int data_len)
{
	(void)request_id;
	(void)data_len;
	char chord_html[65536];
	bud_json_str(data, "chord_html", chord_html, sizeof(chord_html));
	if (!chord_html[0])
		return;

	/* Extract song index from JSON */
	const char *k = strstr(data, "\"index\":");
	int idx = k ? atoi(k + 8) : 0;
	if (idx < 0 || idx >= g_sb_n_chord_nodes || !g_sb_chord_nodes[idx])
		return;

	extern void bud_patch_innerhtml(unsigned int node_id, const char *html);
	bud_patch_innerhtml(bud_node_id(g_sb_chord_nodes[idx]), chord_html);
}

static int on_sb_transpose_change(bud_event *event)
{
	const char *value = (const char *)event->user;
	if (!value)
		return 0;
	int semitones = atoi(value);

	const char *n_str = bud_get_attr(event->target, "data-n");
	int song_index = n_str ? atoi(n_str) : 0;

	fetch_sb_transpose(song_index, semitones);
	return 0;
}

/* ── Option checkbox change handler (latin, flats, media) ── */

extern void (*bud_host_set_location_fn)(const char *url, size_t len);

static void sb_toggle_media(int show)
{
	if (!bud_host_set_location_fn)
		return;
	for (int i = 0; i < sb_app_state.n_songs && i < g_sb_n_chord_nodes; i++)
	{
		if (!g_sb_media_nodes[i])
			continue;
		if (show) {
			char html[8192];
			sb_song_row_data_t *s = &g_sb_songs[i];
			site_ui_build_media_html(
			        s->yt, s->audio, s->pdf, html, sizeof(html));
			extern void bud_patch_innerhtml(
			        unsigned int node_id, const char *html);
			bud_patch_innerhtml(
			        bud_node_id(g_sb_media_nodes[i]),
			        html[0] ? html : "");
		} else {
			extern void bud_patch_innerhtml(
			        unsigned int node_id, const char *html);
			bud_patch_innerhtml(
			        bud_node_id(g_sb_media_nodes[i]), "");
		}
	}
}

static int on_sb_option_change(bud_event *event)
{
	const char *value = (const char *)event->user;
	const char *name = bud_get_attr(event->target, "name");

#ifdef __wasm__
	/* Always log on entry */
	bud_host_log("ENTRY", 5);
	bud_host_log("evtype=", 7);
	if (event->type)
		bud_host_log(event->type, strlen(event->type));
	bud_host_log("name=", 5);
	if (name)
		bud_host_log(name, strlen(name));
	bud_host_log("value=", 6);
	if (value)
		bud_host_log(value, strlen(value));
	bud_host_log("g_sb_main=", 10);
	if (g_sb_main)
		bud_host_log("ok", 2);
	else
		bud_host_log("NULL", 4);
#endif

	if (!name || !value)
		return 0;

	if (strcmp(name, "l") == 0)
		sb_app_state.latin = atoi(value);
	else if (strcmp(name, "m") == 0) {
		sb_app_state.show_media = atoi(value);
		sb_toggle_media(sb_app_state.show_media);
	} else if (strcmp(name, "z") == 0) {
		int z = atoi(value);
		if (z < VIEWER_ZOOM_MIN)
			z = VIEWER_ZOOM_MIN;
		if (z > VIEWER_ZOOM_MAX)
			z = VIEWER_ZOOM_MAX;
		sb_app_state.zoom = z;
		ui_apply_zoom(g_sb_main, NULL, z);
	}

	/* For latin/flats, re-fetch all songs with new flags */
	if (strcmp(name, "b") == 0 || strcmp(name, "l") == 0) {
		for (int i = 0; i < sb_app_state.n_songs; i++)
			fetch_sb_transpose(i, g_sb_songs[i].transpose);
	}

	/* Update URL */
	if (bud_host_set_location_fn) {
		char url[1024];
		snprintf(
		        url, sizeof(url), "%s?l=%d&m=%d&z=%d",
		        sb_app_state.path, sb_app_state.latin ? 1 : 0,
		        sb_app_state.show_media ? 1 : 0, sb_app_state.zoom);
		bud_host_set_location_fn(url, strlen(url));
	}

	/* Persist viewer settings to the shared song prefs (zoom,
	 * latin, video) so both modules stay in sync. */
	if (bud_host_fetch_fn) {
		char url[512];
		snprintf(
		        url, sizeof(url), "/api/song/prefs?l=%d&m=%d&z=%d",
		        sb_app_state.latin ? 1 : 0,
		        sb_app_state.show_media ? 1 : 0, sb_app_state.zoom);
		bud_host_fetch_fn(url, strlen(url), 1);
	}
	return 0;
}

/* ── Body builder (defined in detail.c, forward-declared here) ── */
static bud_node *sb_build_body_content(void);

/* ── WASM app entry point (isomorphic: server .so + WASM .wasm) ── */

bud_node *bud_app_render(void)
{
	g_sb_root = NULL;
	g_sb_main = NULL;
	g_sb_n_chord_nodes = 0;
	memset(g_sb_media_nodes, 0, sizeof(g_sb_media_nodes));

	char zoom_str[16];
	char zoom_style[64];

	snprintf(zoom_str, sizeof(zoom_str), "%d", sb_app_state.zoom);
	snprintf(
	        zoom_style, sizeof(zoom_style),
	        "width:100%%;max-width:100%%;--chord-zoom:%d",
	        sb_app_state.zoom);

	/* Option checkboxes + zoom slider inside a form */
	bud_node *opts =
	        lx_el("form", lx_attr("class", "viewer-controls"),
	              lx_attr("method", "GET"),
	              lx_attr("action", sb_app_state.path),
	              lx_attr("data-viewer-opts", "gig"),
	              lx_node(site_ui_checkbox(
	                      "l", "Latin", sb_app_state.latin,
	                      on_sb_option_change)),
	              lx_node(site_ui_checkbox(
	                      "m", "Video", sb_app_state.show_media,
	                      on_sb_option_change)),
	              lx_el("label", lx_text("Zoom"),
	                    lx_el("input", lx_attr("type", "range"),
	                          lx_attr("name", "z"),
	                          lx_attr("min", "%d", VIEWER_ZOOM_MIN),
	                          lx_attr("max", "%d", VIEWER_ZOOM_MAX),
	                          lx_attr("step", "10"),
	                          lx_attr("value", zoom_str),
	                          lx_attr("data-detail-viewer-zoom", "1"),
	                          lx_bind("input", 0, on_sb_option_change),
	                          lx_bind("change", 0, on_sb_option_change))),
	              lx_el("button", lx_attr("type", "submit"),
	                    lx_attr("class", "btn"),
	                    lx_attr("data-wasm-hide", "1"), lx_text("Apply")))
	                .data.node;

	/* Item menu (edit/delete, owner only) */
	bud_node *item_menu = site_ui_item_menu(
	        "gig", sb_app_state.sb_id, sb_app_state.is_owner);

	/* Menu items fragment */
	bud_node *menu_items =
	        lx_frag(lx_node(opts),
	                item_menu ? lx_node(item_menu) : lx_none())
	                .data.node;

	/* Build body content from state (proper Bud nodes for hydration) */
	g_sb_n_chord_nodes = 0;
	bud_node *body_content = sb_build_body_content();

	/* Page layout with proper Bud nodes */
	bud_node *layout = site_ui_layout(
	        sb_app_state.title, sb_app_state.path,
	        site_ui_module_icon("gig"),
	        sb_app_state.user, menu_items, body_content);

	/* Main wrapper with zoom CSS custom property */
	g_sb_main = lx_el("div", lx_attr("id", "sb-main"),
	                  lx_attr("data-zoom", zoom_str),
	                  lx_attr("style", zoom_style), lx_node(layout))
	                    .data.node;

	/* Root div */
	g_sb_root = lx_el("div", lx_attr("id", "bud-root"), lx_node(g_sb_main))
	                    .data.node;

	return g_sb_root;
}
static bud_node *sb_render_empty_list(void)
{
	return lx_el("p", lx_attr("class", "text-muted"),
	             lx_text("No songs yet."))
	        .data.node;
}

static bud_node *sb_render_add_song_form(
        const char *add_action, const char *csrf_token, bud_node *song_options)
{
	return lx_el("form", lx_attr("method", "POST"),
	             lx_attr("action", add_action),
	             lx_attr("class", "flex gap-2 items-center "
	                              "mt-2 mb-4 p-2 "
	                              "bg-surface rounded"),
	             lx_el("input", lx_attr("type", "hidden"),
	                   lx_attr("name", "csrf_token"),
	                   lx_attr("value", csrf_token)),
	             lx_el("input", lx_attr("type", "text"),
	                   lx_attr("name", "song_id"),
	                   lx_attr("list", "sb-song-datalist"),
	                   lx_attr("placeholder", "Search songs..."),
	                   lx_attr("autocomplete", "off"),
	                   lx_attr("class", "border rounded p-1")),
	             lx_el("datalist", lx_attr("id", "sb-song-datalist"),
	                   song_options ? lx_node(song_options) : lx_none()),
	             lx_el("button", lx_attr("type", "submit"),
	                   lx_attr("class", "btn text-sm py-1 px-2"),
	                   lx_text("Add Song")))
	        .data.node;
}

/* ── Song row helpers ──────────────────────────────── */

static bud_node *sb_render_key_options(int t, int orig_key, int flags)
{
	int latin = (flags & TRANSP_LATIN) ? 1 : 0;
	bud_node *key_opts = NULL;
	for (int i = -11; i <= 11; i++) {
		char v[16];
		snprintf(v, sizeof(v), "%d", i);
		bud_node *o =
		        lx_el("option", lx_attr("value", v),
		              i == t ? lx_attr("selected", "") : lx_none(),
		              lx_text(key_name(i, orig_key, latin)))
		                .data.node;
		if (!key_opts)
			key_opts = bud_fragment();
		bud_append(key_opts, o);
	}
	return key_opts;
}

static bud_node *sb_render_song_row(
        const char *s_title, const char *s_type, const char *song_href,
        const char *tgt_key, int is_owner, const char *csrf_token,
        const char *rem_action, const char *tpose_action,
        const char *rand_action, const char *t_str, const char *chord_html,
        int orig_key, int flags, const char *n_buf, const char *yt,
        const char *audio, const char *pdf, bud_node **out_pre,
        bud_node **out_media)
{
	int t = t_str ? atoi(t_str) : 0;
	bud_node *key_opts = sb_render_key_options(t, orig_key, flags);

	/* Always create the <pre> node and its raw child (even with empty
	 * content on WASM) so the hydrated tree structure matches the
	 * server-rendered DOM and node IDs stay aligned. */
	bud_node *chord_pre =
	        lx_el("pre", lx_attr("data-gig-chord-data", n_buf),
	              lx_attr("class", "whitespace-pre-wrap "
	                               "font-mono text-xs "
	                               "mt-1 p-2 rounded "
	                               "chord-data"),
	              lx_node(bud_raw(chord_html ? chord_html : "")))
	                .data.node;
	if (out_pre)
		*out_pre = chord_pre;

	/* Media container (always rendered, patched by WASM) */
	bud_node *media_node =
	        lx_el("div", lx_attr("data-gig-media", n_buf),
	              lx_attr("class", "gig-media mt-1"),
	              lx_node(bud_raw("")))
	                .data.node;

	/* Pre-populate media content on server when show_media is active */
	if (sb_app_state.show_media && (yt[0] || audio[0] || pdf[0])) {
		bud_node *slot = site_ui_render_media_slot(yt, audio, pdf);
		if (slot)
			media_node =
			        lx_el("div",
			              lx_attr("data-gig-media", n_buf),
			              lx_attr("class", "gig-media mt-1"),
			              lx_node(slot))
			                .data.node;
	}

	if (out_media)
		*out_media = media_node;

	/* Title/type column */
	bud_node *title_col =
	        lx_el("div", lx_attr("class", "flex gap-16 justify-between"),
	              lx_el("div", lx_attr("class", "flex flex-col"),
	                    s_type && s_type[0]
	                            ? lx_el("span",
	                                    lx_attr("class", "text-xs italic "
	                                                     "text-muted"),
	                                    lx_text(s_type))
	                            : lx_none(),
	                    lx_el("a", lx_attr("class", "font-bold"),
	                          song_href ? lx_attr("href", song_href)
	                                    : lx_none(),
	                          lx_text(s_title))),
	              sb_app_state.user[0]
	                      ? lx_none()
	                      : lx_el("span",
	                              lx_attr("data-gig-target-key", ""),
	                              lx_attr("class", "text-xs text-muted"),
	                              lx_text(tgt_key)))
	                .data.node;

	/* Owner action controls */
	bud_node *owner_ctrl = NULL;
	if (is_owner) {
		owner_ctrl =
		        lx_el("div", lx_attr("class", "flex gap-2"),
		              lx_el("form", lx_attr("method", "POST"),
		                    lx_attr("action", tpose_action),
		                    lx_attr("class", "flex gap-1 items-center"),
		                    lx_el("input", lx_attr("type", "hidden"),
		                          lx_attr("name", "csrf_token"),
		                          lx_attr("value", csrf_token)),
		                    lx_el("input", lx_attr("type", "hidden"),
		                          lx_attr("name", "n"),
		                          lx_attr("value", n_buf)),
		                    lx_el("select", lx_attr("name", "t"),
		                          lx_attr("data-n", n_buf),
		                          lx_attr("class", "border rounded p-1 "
		                                           "text-xs"),
		                          lx_bind("change", 0,
		                                  on_sb_transpose_change),
		                          lx_node(key_opts)),
		                    lx_el("button", lx_attr("type", "submit"),
		                          lx_attr("data-wasm-hide", ""),
		                          lx_attr("class",
		                                  "btn text-xs py-1 px-2"),
		                          lx_text("Set Key"))),
		              lx_el("form", lx_attr("method", "POST"),
		                    lx_attr("action", rand_action),
		                    lx_attr("enctype", "multipart/form-data"),
		                    lx_attr("style", "display:inline"),
		                    lx_el("input", lx_attr("type", "hidden"),
		                          lx_attr("name", "csrf_token"),
		                          lx_attr("value", csrf_token)),
		                    lx_el("input", lx_attr("type", "hidden"),
		                          lx_attr("name", "n"),
		                          lx_attr("value", n_buf)),
		                    lx_el("button", lx_attr("type", "submit"),
		                          lx_attr("class",
		                                  "btn text-xs py-1 px-2"),
		                          lx_text("🎲"))),
		              lx_el("form", lx_attr("method", "POST"),
		                    lx_attr("action", rem_action),
		                    lx_el("input", lx_attr("type", "hidden"),
		                          lx_attr("name", "csrf_token"),
		                          lx_attr("value", csrf_token)),
		                    lx_el("button", lx_attr("type", "submit"),
		                          lx_attr("data-testid",
		                                  "remove-song-btn"),
		                          lx_attr("class", "btn btn-danger "
		                                           "text-xs py-1 px-2"),
		                          lx_text("🗑"))))
		                .data.node;
	}

	bud_node *header =
	        lx_el("div",
	              lx_attr("class", "flex justify-between items-center"),
	              lx_node(title_col),
	              owner_ctrl ? lx_node(owner_ctrl) : lx_none())
	                .data.node;

	return lx_el("div", lx_attr("data-gig-item", ""),
	             lx_attr("class",
	                     "flex flex-col gap-1 p-2 bg-surface rounded"),
	             lx_node(header), lx_node(chord_pre), lx_node(media_node))
	        .data.node;
}

static bud_node *sb_render_header(const char *grp_href, const char *owner)
{
	bud_node *frag = bud_fragment();
	if (!frag)
		return NULL;

	if (owner && owner[0]) {
		bud_append(
		        frag, lx_el("p", lx_attr("class", "text-sm text-muted"),
		                    lx_frag(lx_text("by "), lx_text(owner)))
		                      .data.node);
	}

	if (grp_href && grp_href[0]) {
		bud_append(
		        frag, lx_el("a", lx_attr("href", grp_href),
		                    lx_attr("class", "text-sm text-link"),
		                    lx_text("\xe2\x86\xa9 back to group"))
		                      .data.node);
	}

	return frag;
}

static bud_node *sb_render_song_option(const char *value, const char *text)
{
	return lx_el("option", lx_attr("value", value), lx_text(text))
	        .data.node;
}

/* ── Body content builder (called from bud_app_render in shared.c) ── */

static bud_node *sb_build_body_content(void)
{
	bud_node *frag = bud_fragment();
	char grp_href[256] = { 0 };

	/* Header */
	if (sb_app_state.grp_id[0])
		snprintf(
		        grp_href, sizeof(grp_href), "/grp/%s",
		        sb_app_state.grp_id);
	{
		bud_node *hdr =
		        sb_render_header(grp_href, sb_app_state.owner);
		if (hdr)
			bud_append(frag, hdr);
	}

	/* Add-song form (shown when logged in + grp + options available) */
	if (sb_app_state.grp_id[0] && sb_app_state.user[0] &&
	    sb_app_state.n_song_options > 0)
	{
		char add_action[256];
		snprintf(
		        add_action, sizeof(add_action),
		        "/api/gig/%s/songs", sb_app_state.sb_id);

		bud_node *song_options = NULL;
		for (int j = 0; j < sb_app_state.n_song_options; j++) {
			bud_node *opt = sb_render_song_option(
			        g_sb_options[j].id, g_sb_options[j].title);
			if (!song_options)
				song_options = lx_frag(lx_node(opt)).data.node;
			else
				bud_append(song_options, opt);
		}

		bud_append(
		        frag, sb_render_add_song_form(
		                      add_action, sb_app_state.csrf_token,
		                      song_options));
	}

	/* Song list */
	if (sb_app_state.n_songs == 0) {
		bud_append(frag, sb_render_empty_list());
	} else {
		for (int i = 0; i < sb_app_state.n_songs; i++) {
			sb_song_row_data_t *s = &g_sb_songs[i];

			char n_buf[16], song_href[320], tgt_key[32];
			char rem_action[256], tpose_action[256],
			        rand_action[256], t_str[16];

			snprintf(n_buf, sizeof(n_buf), "%d", i);

			if (s->song_id[0])
				snprintf(
				        song_href, sizeof(song_href),
				        "/song/%s", s->song_id);
			else
				song_href[0] = '\0';

			snprintf(
			        tgt_key, sizeof(tgt_key), "%s",
			        target_key_name(
			                s->orig_key, s->transpose,
			                (s->flags & TRANSP_LATIN) ? 1 : 0));

			snprintf(
			        rem_action, sizeof(rem_action),
			        "/api/gig/%s/song/%d/remove",
			        sb_app_state.sb_id, i);
			snprintf(
			        tpose_action, sizeof(tpose_action),
			        "/api/gig/%s/song/%d/transpose",
			        sb_app_state.sb_id, i);
			snprintf(
			        rand_action, sizeof(rand_action),
			        "/gig/%s/randomize", sb_app_state.sb_id);

			snprintf(t_str, sizeof(t_str), "%d", s->transpose);

			bud_node *pre_ptr = NULL, *media_ptr = NULL;
			bud_node *row = sb_render_song_row(
			        s->title, s->type,
			        song_href[0] ? song_href : NULL, tgt_key,
			        sb_app_state.is_owner, sb_app_state.csrf_token,
			        rem_action, tpose_action, rand_action, t_str,
			        s->chord_html, s->orig_key, s->flags, n_buf,
			        s->yt, s->audio, s->pdf, &pre_ptr, &media_ptr);

			if (pre_ptr && g_sb_n_chord_nodes < MAX_SB_SONGS)
				g_sb_chord_nodes[g_sb_n_chord_nodes++] =
				        pre_ptr;

			if (media_ptr && g_sb_n_chord_nodes <= MAX_SB_SONGS)
				g_sb_media_nodes[g_sb_n_chord_nodes - 1] =
				        media_ptr;

			bud_append(frag, row);
		}
	}

	return frag;
}
