/* ── Pure helpers (WASM-compilable) ───────────────────────── */

static const char *idx_select_fields_for(const char *module)
{
	if (!strcmp(module, "song"))
		return "title,type,author";
	if (!strcmp(module, "poem"))
		return "title,owner";
	if (!strcmp(module, "songbook"))
		return "title,choir";
	if (!strcmp(module, "choir"))
		return "title";
	return "title";
}

static void idx_url_decode(char *s)
{
	char *d = s;

	if (!s)
		return;
	while (*s) {
		if (*s == '%' && s[1] && s[2]) {
			unsigned int v;
			sscanf(s + 1, "%2x", &v);
			*d++ = (char)v;
			s += 3;
		} else if (*s == '+') {
			*d++ = ' ';
			s++;
		} else {
			*d++ = *s++;
		}
	}
	*d = '\0';
}

static const char *
idx_query_param(const char *qs, const char *name, char *buf, size_t len)
{
	const char *p;
	size_t nlen;

	if (!qs || !name || !buf || len == 0)
		return NULL;
	nlen = strlen(name);
	p = qs;
	while (p) {
		p = strstr(p, name);
		if (!p)
			return NULL;
		if ((p == qs || p[-1] == '&') && p[nlen] == '=') {
			const char *end;
			size_t n;

			p += nlen + 1;
			end = strchr(p, '&');
			n = end ? (size_t)(end - p) : strlen(p);
			if (n >= len)
				n = len - 1;
			memcpy(buf, p, n);
			buf[n] = '\0';
			idx_url_decode(buf);
			return buf;
		}
		p++;
	}
	return NULL;
}

static void idx_parse_sort(const char *qs, char *field, size_t flen, int *asc)
{
	char sort_val[128];
	const char *sv;
	const char *colon;

	*asc = 1;
	field[0] = '\0';
	sv = idx_query_param(qs, "sort", sort_val, sizeof(sort_val));
	if (!sv)
		return;
	colon = strchr(sv, ':');
	if (colon) {
		size_t n = (size_t)(colon - sv);
		if (n >= flen)
			n = flen - 1;
		memcpy(field, sv, n);
		field[n] = '\0';
		*asc = (strcmp(colon + 1, "desc") != 0);
	} else {
		strncpy(field, sv, flen - 1);
		field[flen - 1] = '\0';
		*asc = 1;
	}
}

static void col_tok_label(char *out, size_t len, const char *key)
{
	size_t i;

	for (i = 0; key[i] && i < len - 1; i++)
		out[i] = key[i] == '_' ? ' ' : key[i];
	out[i] = '\0';
}

/* ── Column definition ────────────────────────────────────── */

typedef struct {
	char key[64];
	char label[64];
	int type;
	char target_source[64];
	unsigned target_hd;
} col_t;

/* ── Filter option resolution ─────────────────────────────── */

static int idx_resolve_filter_options(
        const char *target_source,
        unsigned target_hd,
        hyle_bud_option_t *opts,
        int max_opts)
{
	unsigned row_hd;
	unsigned schema_hd;
	char display_field[64] = "";
	int nopts = 0;
	uint32_t cur;
	const void *key;
	const void *val;

	if (!target_source || !target_source[0] || !target_hd)
		return 0;

	row_hd = source_get_data_hd(target_source);
	if (!row_hd)
		return 0;

	schema_hd = source_get_schema_hd(target_source);
	if (schema_hd) {
		cur = qmap_iter(schema_hd, NULL, 0);
		while (qmap_next(&key, &val, cur)) {
			const char *fn = (const char *)key;
			if (strcmp(fn, "id") == 0)
				continue;
			strncpy(display_field, fn, sizeof(display_field) - 1);
			break;
		}
		qmap_fin(cur);
	}

	cur = qmap_iter(row_hd, NULL, 0);
	while (qmap_next(&key, &val, cur) && nopts < max_opts) {
		const char *row_id = (const char *)key;
		opts[nopts].id = row_id;
		if (display_field[0]) {
			char name_key[320];
			snprintf(
			        name_key,
			        sizeof(name_key),
			        "%s:%s",
			        row_id,
			        display_field);
			const char *name =
			        (const char *)qmap_get(target_hd, name_key);
			opts[nopts].label = name ? name : row_id;
		} else {
			opts[nopts].label = row_id;
		}
		nopts++;
	}
	qmap_fin(cur);

	return nopts;
}

/* ── Rendering functions ──────────────────────────────────── */

static bud_node *idx_filter_bar(col_t *cols, int ncols, const char *qs)
{
	bud_node *bar = bud_fragment();
	int i;

	if (!bar)
		return NULL;

	for (i = 0; i < ncols; i++) {
		char cur[512] = "";
		hyle_bud_option_t opts[256];
		int nopts = 0;
		bud_node *field;

		idx_query_param(qs, cols[i].key, cur, sizeof(cur));

		if (cols[i].target_hd &&
		    (cols[i].type == SOURCE_FIELD_REFERENCE ||
		     cols[i].type == SOURCE_FIELD_MULTI_REFERENCE))
		{
			nopts = idx_resolve_filter_options(
			        cols[i].target_source,
			        cols[i].target_hd,
			        opts,
			        256);
		}

		field = hyle_bud_filter_field(
		        cols[i].key,
		        cols[i].label,
		        cols[i].type,
		        cur,
		        nopts > 0 ? opts : NULL,
		        nopts);

		if (field)
			bud_append(bar, field);
	}
	return bar;
}

static bud_node *idx_list_layout(
        const char *module,
        const char *query_str,
        const char *username,
        int page,
        int per_page,
        int total,
        col_t *cols,
        int ncols,
        const char **ids,
        int nids,
        const char **values,
        const char *sort_field,
        int sort_asc,
        int has_page)
{
	char path[256];
	char title[128];
	const char *col_keys[32];
	const char *col_labels[32];
	int i;

	(void)has_page;

	for (i = 0; i < ncols && i < 32; i++) {
		col_keys[i] = cols[i].key;
		col_labels[i] = cols[i].label;
	}

	bud_node *filter_bar = idx_filter_bar(cols, ncols, query_str);
	bud_node *table = hyle_bud_table(
	        col_keys,
	        col_labels,
	        ncols,
	        ids,
	        nids,
	        values,
	        module,
	        sort_field,
	        sort_asc,
	        query_str);
	bud_node *pagination =
	        hyle_bud_pagination(page, per_page, total, nids, query_str);

	bud_node *filter_wrap = NULL;
	bud_node *actions_wrap = NULL;
	if (filter_bar) {
		filter_wrap = lx_el("div",
		                    lx_attr("class", "hyle-filter-bar"),
		                    lx_node(filter_bar))
		                      .data.node;
		actions_wrap = lx_el("div",
		                     lx_attr("class", "hyle-filter-actions"),
		                     lx_el("button",
		                           lx_attr("type", "reset"),
		                           lx_text("Clear")),
		                     lx_el("button",
		                           lx_attr("type", "submit"),
		                           lx_text("Apply")))
		                       .data.node;
		bud_append(filter_wrap, actions_wrap);
	}

	bud_node *form = lx_el("form",
	                       lx_attr("method", "get"),
	                       lx_attr("action", ""),
	                       lx_attr("class", "list-form"),
	                       filter_wrap ? lx_node(filter_wrap) : lx_none(),
	                       table ? lx_node(table) : lx_none(),
	                       pagination ? lx_node(pagination) : lx_none())
	                         .data.node;

	snprintf(title, sizeof(title), "%ss", module);
	if (title[0] >= 'a')
		title[0] -= 32;
	site_ui_collection_path(module, path, sizeof(path));

	char href_buf[256];
	snprintf(href_buf, sizeof(href_buf), "/%s/add", module);
	bud_node *add_btn = (username && username[0])
	                            ? lx_el("a",
	                                    lx_attr("href", href_buf),
	                                    lx_attr("class", "btn"),
	                                    lx_text("+ add"))
	                                      .data.node
	                            : NULL;

	return site_ui_layout(
	        title,
	        path,
	        "\xf0\x9f\x93\x8b",
	        username,
	        add_btn,
	        lx_el("div", lx_attr("class", "center"), lx_node(form))
	                .data.node);
}

static bud_node *idx_list_empty_layout(const char *module, const char *username)
{
	char path[256];
	site_ui_collection_path(module, path, sizeof(path));

	char href_buf[256];
	snprintf(href_buf, sizeof(href_buf), "/%s/add", module);
	bud_node *add_btn = (username && username[0])
	                            ? lx_el("a",
	                                    lx_attr("href", href_buf),
	                                    lx_attr("class", "btn"),
	                                    lx_text("+ add"))
	                                      .data.node
	                            : NULL;

	return site_ui_layout(
	        module,
	        path,
	        "\xf0\x9f\x93\x8b",
	        username,
	        add_btn,
	        lx_el("div",
	              lx_attr("class", "center"),
	              lx_node(site_ui_empty_state("No items")))
	                .data.node);
}
