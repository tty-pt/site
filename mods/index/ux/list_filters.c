#ifndef INDEX_UX_LIST_FILTERS_C
#define INDEX_UX_LIST_FILTERS_C

static bud_node *idx_content_lookup(
        const char *qs, const char *field, const char *label,
        const char *placeholder)
{
	char cur[512] = "";

	idx_query_param(qs, field, cur, sizeof(cur));

	return lx_el("label", lx_attr("class", "filter-field filter-lookup"),
	             lx_text(label), lx_text(":"),
	             lx_el("input", lx_attr("type", "text"),
	                   lx_attr("name", field),
	                   lx_attr("class", "filter-lookup"),
	                   lx_attr("placeholder", placeholder),
	                   lx_attr("value", cur)))
	        .data.node;
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
	bud_node *input;

	input = lx_el("input", lx_attr("type", "search"), lx_attr("name", "q"),
	              lx_attr("placeholder", "Search\xe2\x80\xa6"),
	              lx_attr("aria-label", "Search everything"),
	              (q && q[0]) ? lx_attr("value", q) : lx_none())
	                .data.node;
	return lx_el("label", lx_attr("class", "hyle-omnisearch"),
	             lx_attr("data-hyle-omnisearch", "1"), lx_node(input))
	        .data.node;
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
		icon = "\xe2\x9a\x99";
		aria = "Advanced filters";
	} else {
		other = "omni";
		icon = "\xe2\x8c\x95";
		aria = "Search everything";
	}
	idx_mode_href(href, sizeof(href), state, omni ? 1 : 0);
	toggle = lx_el("a", lx_attr("class", "hyle-mode-toggle"),
	               lx_attr("data-hyle-mode-toggle", other),
	               lx_attr("href", href), lx_attr("aria-label", aria),
	               lx_text(icon))
	                 .data.node;
	if (toggle)
		wrap = lx_el("div", lx_attr("class", "hyle-filter-bar"),
		             lx_attr("data-hyle-mode",
		                     omni ? "omni" : "custom"),
		             lx_node(toggle), lx_node(bar))
		               .data.node;
	else
		wrap = lx_el("div", lx_attr("class", "hyle-filter-bar"),
		             lx_attr("data-hyle-mode",
		                     omni ? "omni" : "custom"),
		             lx_node(bar))
		               .data.node;
	if (!wrap)
		return bar;
	if (state->custom) {
		hidden = lx_el("input", lx_attr("type", "hidden"),
		               lx_attr("name", "custom"), lx_attr("value", "1"))
		                 .data.node;
		if (hidden)
			bud_append(wrap, hidden);
	}
	actions = lx_el("div", lx_attr("class", "hyle-filter-actions"),
	                omni ? lx_none()
	                     : lx_el("button", lx_attr("type", "reset"),
	                             lx_text("Clear")),
	                lx_el("button", lx_attr("type", "submit"),
	                      lx_text("Apply")))
	                  .data.node;
	if (actions)
		bud_append(wrap, actions);
	return wrap;
}

#endif
