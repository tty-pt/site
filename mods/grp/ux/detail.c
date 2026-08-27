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
		        lx_el("div", lx_attr("class", "flex justify-end"),
		              lx_el("a", lx_attr("href", "/"),
		                    lx_attr("class", "text-xs text-muted"),
		                    lx_text(owner_buf)))
		                .data.node);
	}

	return frag;
}

static bud_node *ch_render_gigs_header(void)
{
	return lx_el("h3", lx_text("Gigs")).data.node;
}

static bud_node *ch_render_gigs_empty(void)
{
	return lx_el("p", lx_attr("class", "text-muted"),
	             lx_text("No gigs yet."))
	        .data.node;
}

static bud_node *ch_render_gig_link(const char *title, const char *href)
{
	return lx_el("a", lx_attr("class", "btn"), lx_attr("href", href),
	             lx_text(title))
	        .data.node;
}

static bud_node *ch_render_repertoire_header(void)
{
	return lx_el("h3", lx_text("Repertoire")).data.node;
}

static bud_node *ch_render_repertoire_empty(void)
{
	return lx_el("p", lx_attr("class", "text-muted"),
	             lx_text("No songs in repertoire yet."))
	        .data.node;
}

static bud_node *ch_render_key_selector(int orig_key, int transpose)
{
	static const char *CH_KEYS[] = { "C",  "C#", "D",  "D#", "E",  "F",
		                         "F#", "G",  "G#", "A",  "A#", "B" };
	bud_node *sel = lx_el("select", lx_attr("name", "key"),
	                      lx_attr("class", "text-xs p-1"))
	                        .data.node;
	for (int k = 0; k < 12; k++) {
		int semitones = ((k - orig_key) % 12 + 12) % 12;
		char val[8], label[32];
		snprintf(val, sizeof(val), "%d", semitones);
		if (semitones == 0)
			snprintf(
			        label, sizeof(label), "%s (Original)",
			        CH_KEYS[k]);
		else
			snprintf(label, sizeof(label), "%s", CH_KEYS[k]);
		bud_append(
		        sel, lx_el("option", lx_attr("value", val),
		                   semitones == transpose
		                           ? lx_attr("selected", "selected")
		                           : lx_none(),
		                   lx_text(label))
		                     .data.node);
	}
	return sel;
}

static bud_node *ch_render_song_row(
        const char *s_title, const char *song_href, const char *key_label,
        int is_owner, int orig_key, int transpose, const char *csrf_token,
        const char *key_action, const char *rem_action, int pinned)
{
	bud_node *controls = NULL;

	if (is_owner) {
		bud_node *key_sel = ch_render_key_selector(orig_key, transpose);
		bud_node *set_form = site_ui_action_form(
		        key_action, csrf_token, "POST", key_sel, "Set",
		        "btn text-xs py-1 px-2");
		bud_node *rem_form = pinned
		        ? site_ui_action_form(
		                  rem_action, csrf_token, "POST", NULL,
		                  "Remove", "btn btn-danger text-xs py-1 px-2")
		        : NULL;

		controls = lx_el("div", lx_attr("class", "flex gap-2"),
		                 lx_node(set_form),
		                 rem_form ? lx_node(rem_form) : lx_none())
		                   .data.node;
	}

	return site_ui_item_row(s_title, song_href, key_label, controls);
}

static bud_node *ch_render_add_gig_link(const char *href)
{
	return lx_el("div", lx_attr("class", "mt-4"),
	             lx_el("a", lx_attr("href", href), lx_attr("class", "btn"),
	                   lx_text("\xe2\x9e\x95 add gig")))
	        .data.node;
}

/* ── Data-driven section builders (WASM-safe: no site headers) ── */

#define CH_MAX_GIGS 128
#define CH_MAX_REP_SONGS 256

/* Shared list state for the song picker, filled natively by grp.c via
 * list_fill_state() before rendering. */
static list_state_t g_ch_pick_state;

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
	int pinned;
	char key_action[256];
	char rem_action[256];
} ch_rep_entry_t;

static bud_node *ch_render_gigs_section(ch_sb_entry_t *gigs, int n_gigs)
{
	bud_node *frag = bud_fragment();
	if (!frag)
		return NULL;
	bud_append(frag, ch_render_gigs_header());
	if (n_gigs == 0) {
		bud_append(frag, ch_render_gigs_empty());
	} else {
		for (int i = 0; i < n_gigs; i++)
			bud_append(
			        frag, ch_render_gig_link(
			                      gigs[i].title, gigs[i].href));
	}
	return frag;
}

static bud_node *ch_render_repertoire_section(
        ch_rep_entry_t *repertoire, int n_repertoire, int is_owner,
        const char *csrf_token)
{
	bud_node *frag = bud_fragment();
	if (!frag)
		return NULL;
	bud_append(frag, ch_render_repertoire_header());
	if (is_owner)
		bud_append(
		        frag,
		        lx_el("p", lx_attr("class", "text-xs text-muted"),
		              lx_text("Built from your gigs. Set a key to "
		                      "pin a song; pinned songs stay."))
		                .data.node);
	if (n_repertoire == 0) {
		bud_append(frag, ch_render_repertoire_empty());
	} else {
		for (int i = 0; i < n_repertoire; i++) {
			ch_rep_entry_t *e = &repertoire[i];
			bud_append(
			        frag,
			        ch_render_song_row(
			                e->title, e->song_href, e->key_label,
			                is_owner, e->orig_key, e->transpose,
			                csrf_token, e->key_action,
			                e->rem_action, e->pinned));
		}
	}
	return frag;
}

/* List-grade song picker: omni ⇄ custom chrome, results table whose
 * whole rows submit POST /api/grp/:id/songs (HTML5 form= attribute),
 * pagination. State-driven; SSR-only module (no wasm). */
static bud_node *
ch_render_add_song_section(const char *grp_id, const char *csrf_token)
{
	char action[256], add_action[256], sbh[256];
	const char *col_keys[LIST_MAX_COLS];
	const char *col_labels[LIST_MAX_COLS];
	hyle_bud_row_action_t act;
	bud_node *frag, *hint, *form, *chrome, *table, *pag, *post;
	int i;

	frag = bud_fragment();
	if (!frag)
		return NULL;
	snprintf(action, sizeof(action), "/grp/%s", grp_id);
	snprintf(add_action, sizeof(add_action), "/api/grp/%s/songs", grp_id);

	for (i = 0; i < g_ch_pick_state.ncols && i < LIST_MAX_COLS; i++) {
		col_keys[i] = g_ch_pick_state.cols[i].key;
		col_labels[i] = g_ch_pick_state.cols[i].label;
	}

	if (list_has_query(&g_ch_pick_state)) {
		hint = lx_el("div", lx_attr("class", "text-xs text-muted"),
		             lx_text("Click a song to add it."))
		               .data.node;
		if (hint)
			bud_append(frag, hint);
	}

	form = lx_el("form", lx_attr("method", "get"),
	             lx_attr("action", action), lx_attr("class", "list-form"))
	               .data.node;
	chrome = idx_filter_chrome(&g_ch_pick_state);
	if (form && chrome)
		bud_append(form, chrome);

	if (form && list_has_query(&g_ch_pick_state)) {
		act.kind = HYLE_ROW_ACTION_SUBMIT;
		act.css_class = NULL;
		act.label = NULL;
		act.aria_base = "Add";
		act.href_base = NULL;
		act.form_id = "ch-pick-post";
		act.field_name = "song_id";
		table = hyle_bud_table_actions(
		        col_keys, col_labels, g_ch_pick_state.ncols,
		        (const char **)g_ch_pick_state.ids,
		        g_ch_pick_state.nids,
		        (const char **)g_ch_pick_state.values, "song",
		        g_ch_pick_state.sort_field, g_ch_pick_state.sort_asc,
		        g_ch_pick_state.query, &act);
		pag = hyle_bud_pagination(
		        g_ch_pick_state.page, g_ch_pick_state.per_page,
		        g_ch_pick_state.total, g_ch_pick_state.nids, "");
		if (table)
			bud_append(form, table);
		if (pag)
			bud_append(form, pag);
	}
	if (form)
		bud_append(frag, form);
	post = lx_el("form", lx_attr("id", "ch-pick-post"),
	             lx_attr("method", "post"),
	             lx_attr("action", add_action),
	             lx_el("input", lx_attr("type", "hidden"),
	                   lx_attr("name", "csrf_token"),
	                   lx_attr("value", csrf_token)))
	               .data.node;
	if (post)
		bud_append(frag, post);

	snprintf(sbh, sizeof(sbh), "/gig/add?grp=%s", grp_id);
	bud_append(frag, ch_render_add_gig_link(sbh));
	return frag;
}
