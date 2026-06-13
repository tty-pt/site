static bud_node *ch_render_detail_header(const char *title, const char *owner)
{
	bud_node *frag = bud_fragment();
	if (!frag)
		return NULL;

	if (owner && owner[0]) {
		char owner_buf[256];
		snprintf(owner_buf, sizeof(owner_buf), "By %s", owner);
		bud_append(
		        frag,
		        lx_el("div",
		              lx_attr("class", "flex justify-end"),
		              lx_el("a",
		                    lx_attr("href", "/"),
		                    lx_attr("class", "text-xs text-muted"),
		                    lx_text(owner_buf)))
		                .data.node);
	}

	return frag;
}

static bud_node *ch_render_songbooks_header(void)
{
	return lx_el("h3", lx_text("Songbooks")).data.node;
}

static bud_node *ch_render_songbooks_empty(void)
{
	return lx_el("p",
	             lx_attr("class", "text-muted"),
	             lx_text("No songbooks yet."))
	        .data.node;
}

static bud_node *ch_render_songbook_link(const char *title, const char *href)
{
	return lx_el("a",
	             lx_attr("class", "btn"),
	             lx_attr("href", href),
	             lx_text(title))
	        .data.node;
}

static bud_node *ch_render_repertoire_header(void)
{
	return lx_el("h3", lx_text("Repertoire")).data.node;
}

static bud_node *ch_render_repertoire_empty(void)
{
	return lx_el("p",
	             lx_attr("class", "text-muted"),
	             lx_text("No songs in repertoire yet."))
	        .data.node;
}

static bud_node *ch_render_song_row(
        const char *s_title,
        const char *song_href,
        const char *key_label,
        int is_owner,
        int orig_key,
        int transpose,
        const char *csrf_token,
        const char *key_action,
        const char *rem_action)
{
	bud_node *row =
	        lx_el("div",
	              lx_attr("class",
	                      "flex justify-between items-center "
	                      "p-2 bg-surface rounded"),
	              lx_el("div",
	                    lx_attr("class", "flex flex-col"),
	                    lx_el("a",
	                          lx_attr("class", "font-bold"),
	                          lx_attr("href", song_href),
	                          lx_text(s_title)),
	                    lx_el("span",
	                          lx_attr("class", "text-xs text-muted"),
	                          lx_text(key_label))))
	                .data.node;

	if (is_owner) {
		bud_node *controls = bud_fragment();

		/* Key selector */
		static const char *CH_KEYS[] = { "C",  "C#", "D",  "D#",
			                         "E",  "F",  "F#", "G",
			                         "G#", "A",  "A#", "B" };
		bud_node *key_sel = lx_el("select",
		                          lx_attr("name", "key"),
		                          lx_attr("class", "text-xs p-1"))
		                            .data.node;
		for (int k = 0; k < 12; k++) {
			int semitones = ((k - orig_key) % 12 + 12) % 12;
			char val[8];
			snprintf(val, sizeof(val), "%d", semitones);
			char label[32];
			if (semitones == 0)
				snprintf(
				        label,
				        sizeof(label),
				        "%s (Original)",
				        CH_KEYS[k]);
			else
				snprintf(
				        label, sizeof(label), "%s", CH_KEYS[k]);
			bud_append(
			        key_sel,
			        lx_el("option",
			              lx_attr("value", val),
			              semitones == transpose
			                      ? lx_attr("selected", "selected")
			                      : lx_none(),
			              lx_text(label))
			                .data.node);
		}

		bud_append(
		        controls,
		        lx_el("form",
		              lx_attr("method", "POST"),
		              lx_attr("action", key_action),
		              lx_attr("class", "flex gap-1 items-center"),
		              lx_el("input",
		                    lx_attr("type", "hidden"),
		                    lx_attr("name", "csrf_token"),
		                    lx_attr("value", csrf_token)),
		              lx_node(key_sel),
		              lx_el("button",
		                    lx_attr("type", "submit"),
		                    lx_attr("class", "btn text-xs py-1 px-2"),
		                    lx_text("Set")))
		                .data.node);

		bud_append(
		        controls,
		        lx_el("form",
		              lx_attr("method", "POST"),
		              lx_attr("action", rem_action),
		              lx_el("input",
		                    lx_attr("type", "hidden"),
		                    lx_attr("name", "csrf_token"),
		                    lx_attr("value", csrf_token)),
		              lx_el("button",
		                    lx_attr("type", "submit"),
		                    lx_attr("class",
		                            "btn btn-danger "
		                            "text-xs py-1 px-2"),
		                    lx_text("Remove")))
		                .data.node);

		bud_append(
		        row,
		        lx_el("div",
		              lx_attr("class", "flex gap-2"),
		              lx_node(controls))
		                .data.node);
	}

	return row;
}

static bud_node *ch_render_add_song_form(
        const char *add_action, const char *csrf_token, bud_node *song_options)
{
	return lx_el("form",
	             lx_attr("method", "POST"),
	             lx_attr("action", add_action),
	             lx_attr("class",
	                     "flex gap-2 items-center "
	                     "mt-2 mb-4 p-2 "
	                     "bg-surface rounded"),
	             lx_el("input",
	                   lx_attr("type", "hidden"),
	                   lx_attr("name", "csrf_token"),
	                   lx_attr("value", csrf_token)),
	             lx_el("input",
	                   lx_attr("type", "text"),
	                   lx_attr("name", "song_id"),
	                   lx_attr("list", "choir-song-datalist"),
	                   lx_attr("placeholder", "Search songs..."),
	                   lx_attr("autocomplete", "off"),
	                   lx_attr("class", "border rounded p-1")),
	             lx_el("datalist",
	                   lx_attr("id", "choir-song-datalist"),
	                   song_options ? lx_node(song_options) : lx_none()),
	             lx_el("button",
	                   lx_attr("type", "submit"),
	                   lx_attr("class", "btn text-sm py-1 px-2"),
	                   lx_text("Add Song")))
	        .data.node;
}

static bud_node *ch_render_add_songbook_link(const char *href)
{
	return lx_el("div",
	             lx_attr("class", "mt-4"),
	             lx_el("a",
	                   lx_attr("href", href),
	                   lx_attr("class", "btn"),
	                   lx_text("\xe2\x9e\x95 add songbook")))
	        .data.node;
}

/* ── Data-driven section builders (WASM-safe: no site headers) ── */

#define CH_MAX_SONGBOOKS 128
#define CH_MAX_REP_SONGS 256
#define CH_MAX_OPT_SONGS 512

typedef struct {
	char title[256];
	char href[256];
} ch_sb_entry_t;
typedef struct {
	char title[256];
	char song_href[256];
	char key_label[64];
	int orig_key;
	int transpose;
	char key_action[256];
	char rem_action[256];
} ch_rep_entry_t;
typedef struct {
	char id[128];
	char title[256];
} ch_opt_entry_t;

static ch_sb_entry_t g_ch_songbooks[CH_MAX_SONGBOOKS];
static int g_ch_n_songbooks;
static ch_rep_entry_t g_ch_repertoire[CH_MAX_REP_SONGS];
static int g_ch_n_repertoire;
static ch_opt_entry_t g_ch_options[CH_MAX_OPT_SONGS];
static int g_ch_n_options;

static bud_node *ch_render_songbooks_section(void)
{
	bud_node *frag = bud_fragment();
	if (!frag)
		return NULL;
	bud_append(frag, ch_render_songbooks_header());
	if (g_ch_n_songbooks == 0) {
		bud_append(frag, ch_render_songbooks_empty());
	} else {
		for (int i = 0; i < g_ch_n_songbooks; i++)
			bud_append(
			        frag,
			        ch_render_songbook_link(
			                g_ch_songbooks[i].title,
			                g_ch_songbooks[i].href));
	}
	return frag;
}

static bud_node *
ch_render_repertoire_section(int is_owner, const char *csrf_token)
{
	bud_node *frag = bud_fragment();
	if (!frag)
		return NULL;
	bud_append(frag, ch_render_repertoire_header());
	if (g_ch_n_repertoire == 0) {
		bud_append(frag, ch_render_repertoire_empty());
	} else {
		for (int i = 0; i < g_ch_n_repertoire; i++) {
			ch_rep_entry_t *e = &g_ch_repertoire[i];
			bud_append(
			        frag,
			        ch_render_song_row(
			                e->title,
			                e->song_href,
			                e->key_label,
			                is_owner,
			                e->orig_key,
			                e->transpose,
			                csrf_token,
			                e->key_action,
			                e->rem_action));
		}
	}
	return frag;
}

static bud_node *
ch_render_add_song_section(const char *choir_id, const char *csrf_token)
{
	bud_node *frag = bud_fragment();
	if (!frag)
		return NULL;
	if (g_ch_n_options > 0) {
		bud_node *opts = NULL;
		for (int i = 0; i < g_ch_n_options; i++) {
			bud_node *o =
			        lx_el("option",
			              lx_attr("value", g_ch_options[i].id),
			              lx_text(g_ch_options[i].title))
			                .data.node;
			if (!opts)
				opts = lx_frag(lx_node(o)).data.node;
			else
				bud_append(opts, o);
		}
		char aa[256];
		snprintf(aa, sizeof(aa), "/api/choir/%s/songs", choir_id);
		bud_append(frag, ch_render_add_song_form(aa, csrf_token, opts));
	}
	char sbh[256];
	snprintf(sbh, sizeof(sbh), "/songbook/add?choir=%s", choir_id);
	bud_append(frag, ch_render_add_songbook_link(sbh));
	return frag;
}
