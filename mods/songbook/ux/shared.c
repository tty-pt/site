/* Transpose flags (matching transp.h) */
#ifndef TRANSP_HTML
#define TRANSP_HTML 0x04
#endif
#ifndef TRANSP_BEMOL
#define TRANSP_BEMOL 0x08
#endif
#ifndef TRANSP_LATIN
#define TRANSP_LATIN 0x80
#endif

/* ── WASM app state (songbook WASM runtime) ────────────── */

#define MAX_SB_SONGS 128
#define MAX_SB_OPTS 512

typedef struct {
	char title[256];
	char song_id[256];
	int orig_key;
	int transpose;
	int flags;
	char yt[512];
	char audio[512];
	char pdf[512];
	char *chord_html; /* server-side only; free after render */
} sb_song_row_data_t;

typedef struct {
	char id[128];
	char title[256];
} sb_song_option_t;

typedef struct {
	char sb_id[128];
	int zoom;
	int bemol;
	int latin;
	int show_media;
	int is_owner;
	char title[256];
	char user[64];
	char csrf_token[33];
	char choir_id[128];
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
static bud_node *g_sb_zoom_label = NULL;

/* ── WASM state init (called by JS bridge before bud_app_mount) ── */

/* ── JSON array parsers for songs + opts ────────────── */

static void parse_songs_array(const char *json)
{
	/* Format: "songs": [ [title,song_id,key,tr,flags,yt,audio,pdf],...] */
	const char *p = strstr(json, "\"songs\"");
	if (!p)
		return;
	p += 7; /* skip "songs" */
	while (*p == ' ' || *p == '\t' || *p == '\n')
		p++;
	if (*p != ':')
		return;
	p++;
	while (*p == ' ' || *p == '\t' || *p == '\n')
		p++;
	if (*p != '[')
		return;
	p++;
	sb_app_state.n_songs = 0;
	while (*p && *p != ']' && sb_app_state.n_songs < MAX_SB_SONGS) {
		while (*p == ' ' || *p == '\t' || *p == '\n')
			p++;
		if (*p != '[')
			break;
		p++;
		sb_song_row_data_t *s = &g_sb_songs[sb_app_state.n_songs];
		memset(s, 0, sizeof(*s));

		/* title */
		if (*p == '"') {
			p++;
			int i = 0;
			while (*p && *p != '"' && i < 255) {
				if (*p == '\\' && *(p + 1) == '"') {
					s->title[i++] = '"';
					p += 2;
				} else {
					s->title[i++] = *p++;
				}
			}
			s->title[i] = '\0';
			if (*p == '"')
				p++;
		}
		if (*p == ',')
			p++;

		/* song_id */
		while (*p == ' ' || *p == '\t' || *p == '\n')
			p++;
		if (*p == '"') {
			p++;
			int i = 0;
			while (*p && *p != '"' && i < 127)
				s->song_id[i++] = *p++;
			s->song_id[i] = '\0';
			if (*p == '"')
				p++;
		}
		if (*p == ',')
			p++;

		/* orig_key, transpose, flags */
		while (*p == ' ' || *p == '\t' || *p == '\n')
			p++;
		s->orig_key = atoi(p);
		while (*p && *p != ',' && *p != ']')
			p++;
		if (*p == ',')
			p++;
		while (*p == ' ' || *p == '\t' || *p == '\n')
			p++;
		s->transpose = atoi(p);
		while (*p && *p != ',' && *p != ']')
			p++;
		if (*p == ',')
			p++;
		while (*p == ' ' || *p == '\t' || *p == '\n')
			p++;
		s->flags = atoi(p);
		while (*p && *p != ',' && *p != ']')
			p++;
		if (*p == ',')
			p++;

		/* yt, audio, pdf strings */
		{
			const char *str_fields[] = { s->yt, s->audio, s->pdf };
			int str_sizes[] = { 512, 512, 512 };
			for (int fi = 0; fi < 3; fi++) {
				while (*p == ' ' || *p == '\t' || *p == '\n')
					p++;
				if (*p == '"') {
					p++;
					int i = 0;
					while (*p && *p != '"' &&
					       i < str_sizes[fi] - 1)
					{
						if (*p == '\\' &&
						    *(p + 1) == '"')
						{
							((char *)str_fields[fi])
							        [i++] = '"';
							p += 2;
						} else {
							((char *)str_fields[fi])
							        [i++] = *p++;
						}
					}
					((char *)str_fields[fi])[i] = '\0';
					if (*p == '"')
						p++;
				}
				if (*p == ',') {
					p++;
				} else if (*p != ']' && *p != '"' && fi < 2) {
					break;
				}
			}
		}

		while (*p == ' ' || *p == '\t' || *p == '\n')
			p++;
		if (*p == ']')
			p++;

		s->chord_html = NULL;
		sb_app_state.n_songs++;

		while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ',') {
			if (*p == ',') {
				p++;
				break;
			}
			p++;
		}
	}
}

static void parse_opts_array(const char *json)
{
	/* Format: "opts": [ [id,title],...] */
	const char *p = strstr(json, "\"opts\"");
	if (!p)
		return;
	p += 6; /* skip "opts" (6 chars) */
	while (*p == ' ' || *p == '\t' || *p == '\n')
		p++;
	if (*p != ':')
		return;
	p++;
	while (*p == ' ' || *p == '\t' || *p == '\n')
		p++;
	if (*p != '[')
		return;
	p++;
	sb_app_state.n_song_options = 0;
	while (*p && *p != ']' && sb_app_state.n_song_options < MAX_SB_OPTS) {
		while (*p == ' ' || *p == '\t' || *p == '\n')
			p++;
		if (*p != '[')
			break;
		p++;
		sb_song_option_t *o =
		        &g_sb_options[sb_app_state.n_song_options];
		memset(o, 0, sizeof(*o));

		/* id */
		if (*p == '"') {
			p++;
			int i = 0;
			while (*p && *p != '"' && i < 127)
				o->id[i++] = *p++;
			o->id[i] = '\0';
			if (*p == '"')
				p++;
		}
		if (*p == ',')
			p++;

		/* title */
		while (*p == ' ' || *p == '\t' || *p == '\n')
			p++;
		if (*p == '"') {
			p++;
			int i = 0;
			while (*p && *p != '"' && i < 255) {
				if (*p == '\\' && *(p + 1) == '"') {
					o->title[i++] = '"';
					p += 2;
				} else {
					o->title[i++] = *p++;
				}
			}
			o->title[i] = '\0';
			if (*p == '"')
				p++;
		}

		while (*p && *p != ']')
			p++;
		if (*p == ']')
			p++;
		sb_app_state.n_song_options++;

		while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ',') {
			if (*p == ',') {
				p++;
				break;
			}
			p++;
		}
	}
}

/* ── Field descriptor table for sb_app_state_t (simple fields) ── */

static const bud_field_desc_t songbook_app_fields[] = {
	OVERLAY_STR(id, sb_app_state_t, sb_id, 128),
	OVERLAY_INT(zoom, sb_app_state_t, zoom),
	OVERLAY_INT(b, sb_app_state_t, bemol),
	OVERLAY_INT(l, sb_app_state_t, latin),
	OVERLAY_INT(m, sb_app_state_t, show_media),
	OVERLAY_INT(owner, sb_app_state_t, is_owner),
	OVERLAY_STR(title, sb_app_state_t, title, 256),
	OVERLAY_STR(user, sb_app_state_t, user, 64),
	OVERLAY_STR(csrf, sb_app_state_t, csrf_token, 33),
	OVERLAY_STR(choir, sb_app_state_t, choir_id, 128),
	OVERLAY_STR(owner_name, sb_app_state_t, owner, 64),
	FIELD_END
};

/* ── Diagnostics ── */

#define BUD_LOG(msg)                                                           \
	do {                                                                   \
		const char *_m = (msg);                                        \
		if (bud_host_log_fn)                                           \
			bud_host_log_fn(_m, strlen(_m));                       \
	} while (0)

/* ── WASM state init (called by JS bridge before bud_app_mount) ── */

void wasm_init(const char *json, int len)
{
	(void)len;
	memset(&sb_app_state, 0, sizeof(sb_app_state));

	bud_state_apply(&sb_app_state, songbook_app_fields, json);

	if (sb_app_state.zoom < 70 || sb_app_state.zoom > 170)
		sb_app_state.zoom = 100;

	parse_songs_array(json);

	parse_opts_array(json);
}

/* ── Zoom slider event handler ──────────────────────────── */

static int on_sb_zoom_change(bud_event *event)
{
	return ui_on_zoom_change(
	        event, &sb_app_state.zoom, g_sb_main, g_sb_zoom_label);
}

static void fetch_sb_transpose(int song_index, int semitones)
{
	if (!bud_host_fetch_fn)
		return;
	char url[1024];
	snprintf(
	        url,
	        sizeof(url),
	        "/api/songbook/%s/transpose?n=%d&t=%d%s%s%s",
	        sb_app_state.sb_id,
	        song_index,
	        semitones,
	        sb_app_state.bemol ? "&b=1" : "",
	        sb_app_state.latin ? "&l=1" : "",
	        sb_app_state.show_media ? "&m=1" : "");
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

static void sb_build_media_html(int song_index, char *out, size_t out_sz)
{
	sb_song_row_data_t *s = &g_sb_songs[song_index];
	if (!s->yt[0] && !s->audio[0] && !s->pdf[0]) {
		out[0] = '\0';
		return;
	}
	char buf[8192];
	int pos = 0;
#define APPEND(...)                                                            \
	do {                                                                   \
		int r = snprintf(                                              \
		        buf + pos, sizeof(buf) - (size_t)pos, __VA_ARGS__);    \
		if (r > 0)                                                     \
			pos += r;                                              \
		if ((size_t)pos >= sizeof(buf))                                \
			goto done;                                             \
	} while (0)

	if (s->yt[0]) {
		char src[1024];
		snprintf(
		        src,
		        sizeof(src),
		        "https://www.youtube.com/embed/%s",
		        s->yt);
		APPEND("<div class=\"flex flex-col gap-4 w-full\">"
		       "<iframe src=\"%s\" class=\"w-full aspect-video "
		       "border-none\" allowfullscreen></iframe></div>",
		       src);
	}
	if (s->audio[0]) {
		APPEND("<div class=\"flex flex-col gap-4 w-full\">"
		       "<audio controls class=\"w-full\">"
		       "<source src=\"%s\" type=\"audio/mpeg\">"
		       "</audio></div>",
		       s->audio);
	}
	if (s->pdf[0]) {
		APPEND("<div class=\"flex flex-col gap-4 w-full\">"
		       "<a href=\"%s\" target=\"_blank\" rel=\"noopener\" "
		       "class=\"text-blue-600\">View PDF</a></div>",
		       s->pdf);
	}
#undef APPEND
done:
	snprintf(out, out_sz, "%s", buf);
}

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
			sb_build_media_html(i, html, sizeof(html));
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
	if (!name || !value)
		return 0;

	if (strcmp(name, "b") == 0)
		sb_app_state.bemol = atoi(value);
	else if (strcmp(name, "l") == 0)
		sb_app_state.latin = atoi(value);
	else if (strcmp(name, "m") == 0) {
		sb_app_state.show_media = atoi(value);
		sb_toggle_media(sb_app_state.show_media);
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
		        url,
		        sizeof(url),
		        "/songbook/%s?b=%d&l=%d&m=%d",
		        sb_app_state.sb_id,
		        sb_app_state.bemol ? 1 : 0,
		        sb_app_state.latin ? 1 : 0,
		        sb_app_state.show_media ? 1 : 0);
		bud_host_set_location_fn(url, strlen(url));
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
	g_sb_zoom_label = NULL;
	g_sb_n_chord_nodes = 0;
	memset(g_sb_media_nodes, 0, sizeof(g_sb_media_nodes));

	char zoom_str[16];
	char zoom_style[64];

	snprintf(zoom_str, sizeof(zoom_str), "%d", sb_app_state.zoom);
	snprintf(
	        zoom_style,
	        sizeof(zoom_style),
	        "width:100%%;max-width:100%%;--chord-zoom:%d",
	        sb_app_state.zoom);

	/* Option checkboxes (latin, flats, media) */
	bud_node *opts = lx_el("div",
	                       lx_attr("class", "viewer-controls"),
	                       lx_attr("data-viewer-opts", "songbook"),
	                       lx_node(site_ui_checkbox(
	                               "l",
	                               "Latin",
	                               sb_app_state.latin,
	                               on_sb_option_change)),
	                       lx_node(site_ui_checkbox(
	                               "b",
	                               "Flats (\xe2\x99\xad)",
	                               sb_app_state.bemol,
	                               on_sb_option_change)),
	                       lx_node(site_ui_checkbox(
	                               "m",
	                               "Video",
	                               sb_app_state.show_media,
	                               on_sb_option_change)))
	                         .data.node;

	/* Zoom controls with WASM event handler */
	bud_node *zoom_ctrl = site_ui_viewer_controls(
	        "songbook",
	        sb_app_state.zoom,
	        "/api/song/prefs",
	        on_sb_zoom_change,
	        &g_sb_zoom_label);

	/* Item menu (edit/delete, owner only) */
	bud_node *item_menu = site_ui_item_menu(
	        "songbook", sb_app_state.sb_id, sb_app_state.is_owner);

	/* Menu items fragment */
	bud_node *menu_items =
	        lx_frag(lx_node(opts),
	                zoom_ctrl ? lx_node(zoom_ctrl) : lx_none(),
	                item_menu ? lx_node(item_menu) : lx_none())
	                .data.node;

	/* Build body content from state (proper Bud nodes for hydration) */
	g_sb_n_chord_nodes = 0;
	bud_node *body_content = sb_build_body_content();

	/* Page layout with proper Bud nodes */
	bud_node *layout = site_ui_layout(
	        sb_app_state.title,
	        sb_app_state.title,
	        "\xf0\x9f\x93\x95",
	        sb_app_state.user,
	        menu_items,
	        body_content);

	/* Main wrapper with zoom CSS custom property */
	g_sb_main = lx_el("div",
	                  lx_attr("id", "sb-main"),
	                  lx_attr("data-zoom", zoom_str),
	                  lx_attr("style", zoom_style),
	                  lx_node(layout))
	                    .data.node;

	/* Root div */
	g_sb_root = lx_el("div",
	                  lx_attr("id", "bud-root"),
	                  lx_node(g_sb_main),
#ifdef __wasm__
	                  lx_el("div",
	                        lx_attr("data-test", "wasm-only"),
	                        lx_text("WASM-only test")),
#endif
	                  )
	                    .data.node;

	return g_sb_root;
}
