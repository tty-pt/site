#ifndef INDEX_UX_LIST_LAYOUT_C
#define INDEX_UX_LIST_LAYOUT_C

static bud_node *idx_list_layout(const list_state_t *state)
{
	char path[256];
	char title[128];
	const char *col_keys[LIST_MAX_COLS];
	const char *col_labels[LIST_MAX_COLS];
	bud_node *filter_wrap;
	bud_node *table;
	bud_node *pagination;
	bud_node *form;
	bud_node *add_btn;
	char href_buf[256];
	hyle_bud_row_action_t act;
	int i;

	for (i = 0; i < state->ncols && i < LIST_MAX_COLS; i++) {
		col_keys[i] = state->cols[i].key;
		col_labels[i] = state->cols[i].label;
	}

	filter_wrap = idx_filter_chrome(state);
	act.kind = HYLE_ROW_ACTION_LINK;
	act.css_class = NULL;
	act.label = NULL;
	act.aria_base = "Open";
	act.href_base = NULL;
	act.form_id = NULL;
	act.field_name = NULL;
	table = hyle_bud_table_actions(
	        col_keys, col_labels, state->ncols, (const char **)state->ids,
	        state->nids, (const char **)state->values, state->module,
	        state->sort_field, state->sort_asc, state->query, &act);
	pagination = hyle_bud_pagination(
	        state->page, state->per_page, state->total, state->nids,
	        state->query);

	form =
	        bud_tpl("<form method='get' action='' class='list-form'>"
	                "  %node"
	                "  %node"
	                "  %node"
	                "</form>",
	                filter_wrap, table, pagination);

	snprintf(title, sizeof(title), "%ss", state->display_name);
	if (title[0] >= 'a')
		title[0] -= 32;
	site_ui_collection_path(state->module, path, sizeof(path));

	snprintf(href_buf, sizeof(href_buf), "/%s/add", state->module);
	add_btn = (state->username[0])
	                  ? bud_tpl("<a href='%s' class='btn'>+ add</a>",
	                            href_buf)
	                  : NULL;

	return site_ui_layout(
	        title, path, site_ui_module_icon(state->module),
	        state->username, add_btn,
	        bud_tpl("<div class='center'>%node</div>", form));
}

#endif

static bud_node *idx_list_empty_layout(const list_state_t *state)
{
	char path[256];
	char href_buf[256];
	bud_node *add_btn;
	bud_node *filter_wrap;
	bud_node *form;
	char title[128];

	site_ui_collection_path(state->module, path, sizeof(path));

	snprintf(href_buf, sizeof(href_buf), "/%s/add", state->module);
	add_btn = (state->username[0])
	                  ? bud_tpl("<a href='%s' class='btn'>+ add</a>",
	                            href_buf)
	                  : NULL;

	filter_wrap = idx_filter_chrome(state);

	form =
	        bud_tpl("<form method='get' action='' class='list-form'>"
	                "  %node"
	                "  %node"
	                "</form>",
	                filter_wrap, site_ui_empty_state("No items"));

	snprintf(title, sizeof(title), "%ss", state->display_name);
	if (title[0] >= 'a')
		title[0] -= 32;

	return site_ui_layout(
	        title, path, site_ui_module_icon(state->module),
	        state->username, add_btn,
	        bud_tpl("<div class='center'>%node</div>", form));
}
