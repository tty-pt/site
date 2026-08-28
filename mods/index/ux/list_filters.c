#ifndef INDEX_UX_LIST_FILTERS_C
#define INDEX_UX_LIST_FILTERS_C

static bud_node *idx_content_lookup(
        const char *qs, const char *field, const char *label,
        const char *placeholder)
{
	char cur[512] = "";

	idx_query_param(qs, field, cur, sizeof(cur));

	return bud_tpl(
	        "<label class='filter-field filter-lookup'>%s:"
	        "  <input type='text' name='%s' class='filter-lookup' "
	        "placeholder='%s' value='%s'/>"
	        "</label>",
	        label ? label : "", field ? field : "",
	        placeholder ? placeholder : "", cur);
}

static void
idx_mode_href(char *out, size_t n, const list_state_t *s, int custom)
{
	size_t pos;
	int first;

	if (!out || n == 0)
		return;
	pos = (size_t)snprintf(out, n, "?");
	if (pos >= n)
		return;
	first = 1;
	if (custom) {
		pos += (size_t)snprintf(out + pos, n - pos, "custom=1");
		first = 0;
		if (pos >= n)
			return;
	}
	if (s && s->sort_field[0]) {
		pos += (size_t)snprintf(
		        out + pos, n - pos, "%ssort=%s:%s", first ? "" : "&",
		        s->sort_field, s->sort_asc ? "asc" : "desc");
		first = 0;
		if (pos >= n)
			return;
	}
	if (s && s->per_page > 0 && s->per_page != 10)
		snprintf(
		        out + pos, n - pos, "%sper_page=%d", first ? "" : "&",
		        s->per_page);
}

static bud_node *idx_omnisearch_field(const char *q)
{
	return bud_tpl(
	        "<label class='hyle-omnisearch' data-hyle-omnisearch='1'>"
	        "  <input type='search' name='q' placeholder='Search…' "
	        "aria-label='Search everything' value='%s'/>"
	        "</label>",
	        q ? q : "");
}

static bud_node *idx_filter_bar(const list_state_t *state)
{
	bud_node *bar;
	int i;

	hyle_bud_ms_reset();
	if (!state->custom)
		return idx_omnisearch_field(state->q);

	bar = bud_fragment();
	if (!bar)
		return NULL;

	for (i = 0; i < state->ncols; i++) {
		const list_col_t *col = &state->cols[i];
		hyle_bud_option_t opts[LIST_MAX_OPTS];
		int nopts = 0;
		bud_node *field;
		int k;

		for (k = 0; k < col->opt_count && k < LIST_MAX_OPTS; k++) {
			const list_opt_t *o = &state->opts[col->opt_start + k];
			opts[nopts].id = o->id;
			opts[nopts].label = o->label;
			nopts++;
		}

		field = hyle_bud_filter_field(
		        col->key, col->label, col->type, col->current,
		        nopts > 0 ? opts : NULL, nopts, col->filter);

		if (field)
			bud_append(bar, field);
	}

	if (state->content_field[0])
		bud_append(
		        bar, idx_content_lookup(
		                     state->query, state->content_field,
		                     state->content_label,
		                     state->content_placeholder));

	return bar;
}

static bud_node *idx_filter_chrome(const list_state_t *state)
{
	bud_node *bar;
	bud_node *wrap;
	bud_node *actions;
	bud_node *hidden;
	bud_node *toggle;
	char href[256];
	const char *other;
	const char *icon;
	const char *aria;
	int omni;

	bar = idx_filter_bar(state);
	if (!bar)
		return NULL;
	omni = !state->custom;
	if (omni) {
		other = "custom";
		icon = "⚙";
		aria = "Advanced filters";
	} else {
		other = "omni";
		icon = "⌕";
		aria = "Search everything";
	}
	idx_mode_href(href, sizeof(href), state, omni ? 1 : 0);
	toggle = bud_tpl(
	        "<a class='hyle-mode-toggle' data-hyle-mode-toggle='%s' "
	        "href='%s' aria-label='%s'>%s</a>",
	        other, href, aria, icon);

	wrap =
	        bud_tpl("<div class='hyle-filter-bar' data-hyle-mode='%s'>"
	                "  %node"
	                "  %node"
	                "</div>",
	                omni ? "omni" : "custom", toggle, bar);
	if (!wrap)
		return bar;

	if (state->custom) {
		hidden = bud_tpl(
		        "<input type='hidden' name='custom' value='1'/>");
		if (hidden)
			bud_append(wrap, hidden);
	}
	actions = bud_tpl(
	        "<div class='hyle-filter-actions'>"
	        "  %node"
	        "  <button type='submit'>Apply</button>"
	        "</div>",
	        omni ? NULL : bud_tpl("<button type='reset'>Clear</button>"));
	if (actions)
		bud_append(wrap, actions);
	return wrap;
}

#endif
