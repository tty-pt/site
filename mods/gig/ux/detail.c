#include "bud/bud.h"
#include "bud/bud_jsx.h"
#include "bud/bud_app.h"
#include <hyle-bud/hyle-bud.h>
#include "../fields.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../common/viewer_zoom.h"

#include <transp/music.h>
#include <transp/transp_flags.h>

#include "../../common/ux/site_ui.c"
#include "../../common/state_macros.h"

/* List machinery for the song picker (site_ui.c must come first). */
#include "../../index/ux/list.c"

#ifdef __wasm__
__attribute__((import_module("env"), import_name("bud_host_log"))) void
bud_host_log(const char *msg, size_t len);
#endif

#define MAX_SB_SONGS 128

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

typedef struct {
	char sb_id[128];
	char path[256];
	int zoom;
	int latin;
	int show_media;
	int t_pref;
	int bemol;
	int is_owner;
	char title[256];
	char user[64];
	char csrf_token[33];
	char grp_id[128];
	char owner[64];
	int n_songs;
	int active_row_pick;
	char pick_q[256];
	int pick_page;
	int active_fmt_pick;
	char pick_fmt_q[256];
	int pick_fmt_page;
} sb_app_state_t;

static sb_app_state_t sb_app_state = { 0 };
static sb_song_row_data_t g_sb_songs[MAX_SB_SONGS];
static site_ui_picker_buffer_t g_sb_pick_buf;
static pick_view_t g_sb_pick_state;
static site_ui_picker_buffer_t g_sb_fmt_pick_buf;
static pick_view_t g_sb_fmt_pick_state;
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
	OVERLAY_INT(t, sb_app_state_t, t_pref),
	OVERLAY_INT(b, sb_app_state_t, bemol),
	OVERLAY_INT(owner, sb_app_state_t, is_owner),
	OVERLAY_STR(title, sb_app_state_t, title, 256),
	OVERLAY_STR(user, sb_app_state_t, user, 64),
	OVERLAY_STR(csrf, sb_app_state_t, csrf_token, 33),
	OVERLAY_STR(grp, sb_app_state_t, grp_id, 128),
	OVERLAY_STR(owner_name, sb_app_state_t, owner, 64),
	OVERLAY_INT(active_row, sb_app_state_t, active_row_pick),
	OVERLAY_STR(pick_q, sb_app_state_t, pick_q, 256),
	OVERLAY_INT(pick_page, sb_app_state_t, pick_page),
	OVERLAY_INT(active_fmt, sb_app_state_t, active_fmt_pick),
	OVERLAY_STR(pick_fmt_q, sb_app_state_t, pick_fmt_q, 256),
	OVERLAY_INT(pick_fmt_page, sb_app_state_t, pick_fmt_page),
	FIELD_END
};

BUD_STATE_FIELDS(sb_song_row_data_t, sb_song_row_fields, SB_SONG_ROW_SCHEMA)

/* ── WASM state init (called by JS bridge before bud_app_mount) ── */

void wasm_init(const char *json, int len)
{
	size_t jlen = len >= 0 ? (size_t)len : 0;

	memset(&sb_app_state, 0, sizeof(sb_app_state));

	bud_state_apply_len(&sb_app_state, gig_app_fields, json, jlen);

	if (sb_app_state.zoom < VIEWER_ZOOM_MIN ||
	    sb_app_state.zoom > VIEWER_ZOOM_MAX)
		sb_app_state.zoom = VIEWER_ZOOM_DEFAULT;

	bud_state_apply_array_len(
	        json, jlen, "songs", g_sb_songs, sizeof(sb_song_row_data_t),
	        &sb_app_state.n_songs, MAX_SB_SONGS, sb_song_row_fields);

	site_ui_picker_state_from_json(
	        json, jlen, "song_id", "song.items", 0, sb_app_state.pick_q,
	        sb_app_state.pick_page, &g_sb_pick_buf, &g_sb_pick_state);

	site_ui_picker_state_from_json(
	        json, jlen, "format", "song.types", 0, sb_app_state.pick_fmt_q,
	        sb_app_state.pick_fmt_page, &g_sb_fmt_pick_buf,
	        &g_sb_fmt_pick_state);
}

/* ── Zoom slider event handler ──────────────────────────── */

static void fetch_sb_transpose(int song_index, int semitones)
{
	if (!bud_host_fetch_fn)
		return;
	char url[1024];
	snprintf(
	        url, sizeof(url), "/api/gig/%s/transpose?n=%d&t=%d%s%s&z=%d",
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
	char chord_html[65536];
	size_t dlen = data_len >= 0 ? (size_t)data_len : 0;
	bud_json_str_len(
	        data, dlen, "chord_html", chord_html, sizeof(chord_html));
	if (!chord_html[0])
		return;

	/* Extract song index from JSON */
	int idx = bud_json_int_len(data, dlen, "index", 0);
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
	(void)show;
	if (!bud_host_set_location_fn)
		return;
	for (int i = 0; i < sb_app_state.n_songs && i < g_sb_n_chord_nodes; i++)
	{
		if (!g_sb_media_nodes[i])
			continue;
		char html[8192];
		sb_song_row_data_t *s = &g_sb_songs[i];
		site_ui_build_media_html(
		        s->yt, s->audio, s->pdf, html, sizeof(html));
		extern void bud_patch_innerhtml(
		        unsigned int node_id, const char *html);
		bud_patch_innerhtml(
		        bud_node_id(g_sb_media_nodes[i]), html[0] ? html : "");
	}
}

static int on_sb_option_change(bud_event *event)
{
	const char *value = (const char *)event->user;
	const char *name = bud_get_attr(event->target, "name");

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

/* ── Body/picker builders (defined below, forward-declared here) ── */
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
	bud_node *opts = bud_tpl(
	        "<form class='viewer-controls' method='GET' action='%s' "
	        "data-viewer-opts='gig'>"
	        "  %node"
	        "  <label>Zoom"
	        "    <input type='range' name='z' min='%d' max='%d' step='10' "
	        "value='%s' data-detail-viewer-zoom='1' %bind %bind/>"
	        "  </label>"
	        "  <button type='submit' class='btn' "
	        "data-wasm-hide='1'>Apply</button>"
	        "</form>",
	        sb_app_state.path,
	        site_ui_checkbox(
	                "l", "Latin", sb_app_state.latin, on_sb_option_change),
	        VIEWER_ZOOM_MIN, VIEWER_ZOOM_MAX, zoom_str, "input",
	        on_sb_option_change, "change", on_sb_option_change);

	/* Item menu (edit/delete, owner only) */
	bud_node *item_menu = site_ui_item_menu(
	        "gig", sb_app_state.sb_id, sb_app_state.is_owner);

	/* Menu items fragment */
	bud_node *menu_items =
	        bud_tpl("%node"
	                "%node",
	                opts, item_menu);

	/* Build body content from state (proper Bud nodes for hydration) */
	g_sb_n_chord_nodes = 0;
	bud_node *body_content = sb_build_body_content();

	/* Page layout with proper Bud nodes */
	bud_node *layout = site_ui_layout(
	        sb_app_state.title, sb_app_state.path,
	        site_ui_module_icon("gig"), sb_app_state.user, menu_items,
	        body_content);

	/* Main wrapper with zoom CSS custom property */
	g_sb_main = bud_tpl(
	        "<div id='sb-main' data-zoom='%s' style='%s'>%node</div>",
	        zoom_str, zoom_style, layout);

	/* Root div */
	g_sb_root = bud_tpl("<div id='bud-root'>%node</div>", g_sb_main);

	return g_sb_root;
}

static bud_node *sb_render_empty_list(void)
{
	return bud_tpl("<p class='text-muted'>No songs yet.</p>");
}

/* Song picker: shared omnisearch construct (song_picker.c), used for
 * adding a song to the gig. Gated on state so the WASM tree stays
 * node-id aligned with SSR. */
static bud_node *sb_render_song_picker(const pick_view_t *pv)
{
	char post_action[256];
	const char *pref_names[5] = { "t", "b", "l", "m", "z" };
	int pref_vals[5];

	snprintf(
	        post_action, sizeof(post_action), "/api/gig/%s/songs",
	        sb_app_state.sb_id);

	pref_vals[0] = sb_app_state.t_pref;
	pref_vals[1] = sb_app_state.bemol;
	pref_vals[2] = sb_app_state.latin;
	pref_vals[3] = sb_app_state.show_media;
	pref_vals[4] = sb_app_state.zoom;

	site_ui_action_picker_spec_t spec = {
		.key = "song_id",
		.label = "Song:",
		.target = "song.items",
		.get_action = sb_app_state.path,
		.post_action = post_action,
		.form_id = "sb-pick-post",
		.csrf_token = sb_app_state.csrf_token,
		.submit_label = "Add",
		.hint = "Click a song to add it.",
		.auto_submit = 1,
		.pref_names = pref_names,
		.pref_vals = pref_vals,
		.n_prefs = 5,
	};

	return site_ui_action_picker(&spec, pv);
}

/* ── Per-row song picker with default value ─────────── */

static bud_node *sb_render_song_title_picker(
        int row_idx, const sb_song_row_data_t *s, const pick_view_t *pv,
        int is_active)
{
	char post_action[256];
	char n_str[16];
	char form_id[64];
	const char *pref_names[5] = { "t", "b", "l", "m", "z" };
	int pref_vals[5] = { sb_app_state.t_pref, sb_app_state.bemol,
		             sb_app_state.latin, sb_app_state.show_media,
		             sb_app_state.zoom };

	snprintf(
	        post_action, sizeof(post_action), "/api/gig/%s/song/%d/replace",
	        sb_app_state.sb_id, row_idx);
	snprintf(n_str, sizeof(n_str), "%d", row_idx);
	snprintf(form_id, sizeof(form_id), "sb-pick-post-%d", row_idx);

	bud_node *extra = bud_fragment();
	bud_append(
	        extra, lx_el("input", lx_attr("type", "hidden"),
	                     lx_attr("name", "n"), lx_attr("value", n_str))
	                       .data.node);

	site_ui_action_picker_spec_t spec = {
		.key = "song_id",
		.label = "song",
		.target = "song.items",
		.default_id = s->song_id,
		.default_label = s->title,
		.get_action = sb_app_state.path,
		.post_action = post_action,
		.form_id = form_id,
		.csrf_token = sb_app_state.csrf_token,
		.submit_label = "Replace",
		.scope = n_str,
		.auto_submit = 1,
		.pref_names = pref_names,
		.pref_vals = pref_vals,
		.n_prefs = 5,
		.extra_post_inputs = extra,
	};

	return site_ui_action_picker(&spec, is_active ? pv : NULL);
}

static bud_node *sb_render_format_picker(
        int row_idx, const sb_song_row_data_t *s, const pick_view_t *pv,
        int is_active)
{
	char post_action[256];
	char n_str[16];
	char form_id[64];
	const char *pref_names[5] = { "t", "b", "l", "m", "z" };
	int pref_vals[5] = { sb_app_state.t_pref, sb_app_state.bemol,
		             sb_app_state.latin, sb_app_state.show_media,
		             sb_app_state.zoom };

	snprintf(
	        post_action, sizeof(post_action), "/api/gig/%s/song/%d/replace",
	        sb_app_state.sb_id, row_idx);
	snprintf(n_str, sizeof(n_str), "%d", row_idx);
	snprintf(form_id, sizeof(form_id), "sb-fmt-pick-post-%d", row_idx);

	bud_node *extra = bud_fragment();
	bud_append(
	        extra, lx_el("input", lx_attr("type", "hidden"),
	                     lx_attr("name", "n"), lx_attr("value", n_str))
	                       .data.node);
	if (s->song_id[0]) {
		bud_append(
		        extra, lx_el("input", lx_attr("type", "hidden"),
		                     lx_attr("name", "song_id"),
		                     lx_attr("value", s->song_id))
		                       .data.node);
	}

	site_ui_action_picker_spec_t spec = {
		.key = "format",
		.label = "format",
		.target = "song.types",
		.default_id = s->type,
		.default_label = s->type[0] ? s->type : "any",
		.get_action = sb_app_state.path,
		.post_action = post_action,
		.form_id = form_id,
		.csrf_token = sb_app_state.csrf_token,
		.submit_label = "Set Format",
		.scope = n_str,
		.auto_submit = 1,
		.pref_names = pref_names,
		.pref_vals = pref_vals,
		.n_prefs = 5,
		.extra_post_inputs = extra,
	};

	bud_node *p = site_ui_action_picker(&spec, is_active ? pv : NULL);
	if (p)
		bud_add_class(p, "gig-format-picker-wrap text-xs");
	return p;
}

/* ── Song row helpers ──────────────────────────────── */

static bud_node *sb_render_key_options(int t, int orig_key, int flags)
{
	int latin = (flags & TRANSP_LATIN) ? 1 : 0;
	bud_node *key_opts = bud_fragment();
	int cur_t = ((t % 12) + 12) % 12;
	for (int i = 0; i < 12; i++) {
		bud_node *o =
		        bud_tpl("<option value='%d' %b>%s</option>", i,
		                i == cur_t ? "selected" : NULL,
		                key_name(i, orig_key, latin));
		if (o)
			bud_append(key_opts, o);
	}
	return key_opts;
}

static bud_node *sb_render_song_row(
        int row_idx, const sb_song_row_data_t *s, const char *tgt_key,
        int is_owner, const char *csrf_token, const char *rem_action,
        const char *tpose_action, const char *rand_action, const char *t_str,
        const char *chord_html, const char *n_buf, bud_node **out_pre,
        bud_node **out_media, const pick_view_t *row_pv, int is_active,
        const pick_view_t *fmt_row_pv, int is_fmt_active)
{
	int t = t_str ? atoi(t_str) : 0;
	bud_node *key_opts = sb_render_key_options(t, s->orig_key, s->flags);

	/* Always create the <pre> node and its raw child (even with empty
	 * content on WASM) so the hydrated tree structure matches the
	 * server-rendered DOM and node IDs stay aligned. */
	bud_node *chord_pre = bud_tpl(
	        "<pre data-gig-chord-data='%s' class='whitespace-pre-wrap "
	        "font-mono text-xs mt-1 p-2 rounded chord-data'>%raw</pre>",
	        n_buf, chord_html ? chord_html : "");
	if (out_pre)
		*out_pre = chord_pre;

	/* Media container (always rendered, patched by WASM) */
	char media_html[8192] = { 0 };
	if (s->yt[0] || s->audio[0] || s->pdf[0])
		site_ui_build_media_html(
		        s->yt, s->audio, s->pdf, media_html,
		        sizeof(media_html));

	bud_node *media_node = bud_tpl(
	        "<div data-gig-media='%s' class='gig-media flex justify-end "
	        "items-center flex-shrink-0 ml-auto'>%raw</div>",
	        n_buf, media_html);

	if (out_media)
		*out_media = media_node;

	/* Title/type column */
	bud_node *title_elem = NULL;
	char song_href[320];
	if (s->song_id[0])
		snprintf(song_href, sizeof(song_href), "/song/%s", s->song_id);
	else
		song_href[0] = '\0';

	if (is_owner) {
		bud_node *picker = sb_render_song_title_picker(
		        row_idx, s, row_pv, is_active);
		bud_node *view_link =
		        song_href[0] ? bud_tpl("<a href='%s' class='text-xs "
		                               "text-muted ml-1 flex-shrink-0' "
		                               "title='View song'>↗</a>",
		                               song_href)
		                     : NULL;

		title_elem = bud_tpl(
		        "<div class='flex items-center gap-2 flex-1 min-w-0'>"
		        "  %node"
		        "  %node"
		        "</div>",
		        picker, view_link);
	} else {
		title_elem = song_href[0]
		                     ? bud_tpl("<a class='font-bold' "
		                               "href='%s'>%s</a>",
		                               song_href, s->title)
		                     : bud_tpl("<a class='font-bold'>%s</a>",
		                               s->title);
	}

	bud_node *format_badge = NULL;
	if (is_owner) {
		format_badge = sb_render_format_picker(
		        row_idx, s, fmt_row_pv, is_fmt_active);
	} else if (s->type[0]) {
		format_badge = bud_tpl(
		        "<span class='text-xs italic text-muted'>%s</span>",
		        s->type);
	}

	bud_node *title_col = bud_tpl(
	        "<div class='flex gap-4 justify-between items-center flex-1 "
	        "min-w-0'>"
	        "  <div class='flex flex-col flex-1 min-w-0'>"
	        "    %node"
	        "    %node"
	        "  </div>"
	        "  %node"
	        "</div>",
	        format_badge, title_elem,
	        sb_app_state.user[0]
	                ? NULL
	                : bud_tpl("<span data-gig-target-key='' class='text-xs "
	                          "text-muted flex-shrink-0'>%s</span>",
	                          tgt_key ? tgt_key : ""));

	/* Owner action controls */
	bud_node *owner_ctrl = NULL;
	if (is_owner) {
		owner_ctrl = bud_tpl(
		        "<div class='flex gap-2'>"
		        "  <form method='POST' action='%s' class='flex gap-1 "
		        "items-center'>"
		        "    <input type='hidden' name='csrf_token' "
		        "value='%s'/>"
		        "    <input type='hidden' name='n' value='%s'/>"
		        "    <select name='t' data-n='%s' class='border "
		        "rounded p-1 text-xs' %bind>"
		        "      %node"
		        "    </select>"
		        "    <button type='submit' data-wasm-hide='' "
		        "class='btn text-xs py-1 px-2'>Set Key</button>"
		        "  </form>"
		        "  <form method='POST' action='%s' "
		        "enctype='multipart/form-data' style='display:inline'>"
		        "    <input type='hidden' name='csrf_token' "
		        "value='%s'/>"
		        "    <input type='hidden' name='n' value='%s'/>"
		        "    <button type='submit' class='btn text-xs py-1 "
		        "px-2'>🎲</button>"
		        "  </form>"
		        "  <form method='POST' action='%s'>"
		        "    <input type='hidden' name='csrf_token' "
		        "value='%s'/>"
		        "    <button type='submit' "
		        "data-testid='remove-song-btn' class='btn btn-danger "
		        "text-xs py-1 px-2'>🗑️</button>"
		        "  </form>"
		        "</div>",
		        tpose_action, csrf_token ? csrf_token : "", n_buf,
		        n_buf, "change", on_sb_transpose_change, key_opts,
		        rand_action, csrf_token ? csrf_token : "", n_buf,
		        rem_action, csrf_token ? csrf_token : "");
	}

	bud_node *header =
	        bud_tpl("<div class='flex justify-between items-center gap-2'>"
	                "  %node"
	                "  %node"
	                "  %node"
	                "</div>",
	                title_col, media_node, owner_ctrl);

	return bud_tpl(
	        "<div data-gig-item='' class='flex flex-col gap-1 p-2 "
	        "bg-surface rounded'>"
	        "  %node"
	        "  %node"
	        "</div>",
	        header, chord_pre);
}

static bud_node *sb_render_header(const char *grp_href, const char *owner)
{
	return bud_tpl(
	        "%node"
	        "%node",
	        (owner && owner[0])
	                ? bud_tpl("<p class='text-sm text-muted'>by %s</p>",
	                          owner)
	                : NULL,
	        (grp_href && grp_href[0])
	                ? bud_tpl("<a href='%s' class='text-sm text-link'>↩ "
	                          "back to group</a>",
	                          grp_href)
	                : NULL);
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
		bud_node *hdr = sb_render_header(grp_href, sb_app_state.owner);
		if (hdr)
			bud_append(frag, hdr);
	}

	/* Top Add Song picker (owner only) */
	if (sb_app_state.is_owner) {
		const pick_view_t *add_pv = (sb_app_state.active_row_pick < 0)
		                                    ? &g_sb_pick_state
		                                    : NULL;
		bud_node *picker = sb_render_song_picker(add_pv);

		if (picker)
			bud_append(frag, picker);
	}

	/* Song list */
	if (sb_app_state.n_songs == 0) {
		bud_append(frag, sb_render_empty_list());
	} else {
		for (int i = 0; i < sb_app_state.n_songs; i++) {
			sb_song_row_data_t *s = &g_sb_songs[i];

			char n_buf[16], tgt_key[32];
			char rem_action[256], tpose_action[256],
			        rand_action[256], t_str[16];

			snprintf(n_buf, sizeof(n_buf), "%d", i);

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
			int is_active = (i == sb_app_state.active_row_pick);
			const pick_view_t *row_pv =
			        is_active ? &g_sb_pick_state : NULL;
			int is_fmt_active = (i == sb_app_state.active_fmt_pick);
			const pick_view_t *fmt_pv =
			        is_fmt_active ? &g_sb_fmt_pick_state : NULL;

			bud_node *row = sb_render_song_row(
			        i, s, tgt_key, sb_app_state.is_owner,
			        sb_app_state.csrf_token, rem_action,
			        tpose_action, rand_action, t_str, s->chord_html,
			        n_buf, &pre_ptr, &media_ptr, row_pv, is_active,
			        fmt_pv, is_fmt_active);

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
