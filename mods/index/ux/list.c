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

/* Collect every occurrence of a repeated query param and join the
 * url-decoded values with commas (display-only for multi-select). */
static const char *
idx_query_params_join(const char *qs, const char *name, char *buf, size_t len)
{
	const char *p;
	size_t nlen;
	size_t pos = 0;

	if (!qs || !name || !buf || len == 0)
		return NULL;
	buf[0] = '\0';
	nlen = strlen(name);
	p = qs;
	while (p) {
		p = strstr(p, name);
		if (!p)
			break;
		if ((p == qs || p[-1] == '&') && p[nlen] == '=') {
			const char *end;
			size_t n;
			char tmp[512];

			p += nlen + 1;
			end = strchr(p, '&');
			n = end ? (size_t)(end - p) : strlen(p);
			if (n >= sizeof(tmp))
				n = sizeof(tmp) - 1;
			memcpy(tmp, p, n);
			tmp[n] = '\0';
			idx_url_decode(tmp);
			if (pos > 0 && pos + 1 < len) {
				buf[pos++] = ',';
				buf[pos] = '\0';
			}
			n = strlen(tmp);
			if (pos + n + 1 > len)
				n = len - pos - 1;
			memcpy(buf + pos, tmp, n);
			pos += n;
			buf[pos] = '\0';
		}
		p++;
	}
	return pos > 0 ? buf : NULL;
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
	char filter[16];
} col_t;

/* ── List state (shared native <-> WASM) ──────────────────── */

#define LIST_MAX_COLS 32
#define LIST_MAX_ROWS 256
#define LIST_MAX_OPTS 256

typedef struct {
	char id[64];
	char label[128];
} list_opt_t;

typedef struct {
	char key[64];
	char label[64];
	int type;
	char target_source[64];
	char filter[16];   /* "dropdown" or "" */
	char current[512]; /* comma-joined checked ids (display/checked-state)
	                    */
	int opt_start;     /* index into state->opts pool */
	int opt_count;
} list_col_t;

typedef struct {
	char module[64];
	char username[64];
	char query[512]; /* raw QUERY_STRING (form action, pagination links) */
	char sort_field[64];
	int sort_asc;
	int page, per_page, total, has_page;
	int ncols;
	list_col_t cols[LIST_MAX_COLS];
	int nopts;
	list_opt_t opts[LIST_MAX_OPTS];
	int nids;
	const char *ids[LIST_MAX_ROWS];                    /* row slugs */
	const char *values[LIST_MAX_ROWS * LIST_MAX_COLS]; /* display strings */
} list_state_t;

/* ── Rendering functions ──────────────────────────────────── */

static bud_node *idx_content_lookup(const char *qs)
{
	char cur[512] = "";

	idx_query_param(qs, "data", cur, sizeof(cur));

	return lx_el("label", lx_attr("class", "filter-field filter-lookup"),
	             lx_text("Content"), lx_text(":"),
	             lx_el("input", lx_attr("type", "text"),
	                   lx_attr("name", "data"),
	                   lx_attr("class", "filter-lookup"),
	                   lx_attr("placeholder", "lyrics / chords\xe2\x80\xa6 "
	                                          " e.g. \"a quiet place\""),
	                   lx_attr("value", cur)))
	        .data.node;
}

static bud_node *idx_filter_bar(const list_state_t *state)
{
	bud_node *bar = bud_fragment();
	int i;

	if (!bar)
		return NULL;

	hyle_bud_ms_reset();

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

	if (!strcmp(state->module, "song"))
		bud_append(bar, idx_content_lookup(state->query));

	return bar;
}

static bud_node *idx_list_layout(const list_state_t *state)
{
	char path[256];
	char title[128];
	const char *col_keys[LIST_MAX_COLS];
	const char *col_labels[LIST_MAX_COLS];
	bud_node *filter_bar;
	bud_node *table;
	bud_node *pagination;
	bud_node *filter_wrap;
	bud_node *actions_wrap;
	bud_node *form;
	bud_node *add_btn;
	char href_buf[256];
	int i;

	for (i = 0; i < state->ncols && i < LIST_MAX_COLS; i++) {
		col_keys[i] = state->cols[i].key;
		col_labels[i] = state->cols[i].label;
	}

	filter_bar = idx_filter_bar(state);
	table = hyle_bud_table(
	        col_keys, col_labels, state->ncols, (const char **)state->ids,
	        state->nids, (const char **)state->values, state->module,
	        state->sort_field, state->sort_asc, state->query);
	pagination = hyle_bud_pagination(
	        state->page, state->per_page, state->total, state->nids,
	        state->query);

	filter_wrap = NULL;
	actions_wrap = NULL;
	if (filter_bar) {
		filter_wrap = lx_el("div", lx_attr("class", "hyle-filter-bar"),
		                    lx_node(filter_bar))
		                      .data.node;
		actions_wrap =
		        lx_el("div", lx_attr("class", "hyle-filter-actions"),
		              lx_el("button", lx_attr("type", "reset"),
		                    lx_text("Clear")),
		              lx_el("button", lx_attr("type", "submit"),
		                    lx_text("Apply")))
		                .data.node;
		bud_append(filter_wrap, actions_wrap);
	}

	form = lx_el("form", lx_attr("method", "get"), lx_attr("action", ""),
	             lx_attr("class", "list-form"),
	             filter_wrap ? lx_node(filter_wrap) : lx_none(),
	             table ? lx_node(table) : lx_none(),
	             pagination ? lx_node(pagination) : lx_none())
	               .data.node;

	snprintf(title, sizeof(title), "%ss", state->module);
	if (title[0] >= 'a')
		title[0] -= 32;
	site_ui_collection_path(state->module, path, sizeof(path));

	snprintf(href_buf, sizeof(href_buf), "/%s/add", state->module);
	add_btn = (state->username[0])
	                  ? lx_el("a", lx_attr("href", href_buf),
	                          lx_attr("class", "btn"), lx_text("+ add"))
	                            .data.node
	                  : NULL;

	return site_ui_layout(
	        title, path, "\xf0\x9f\x93\x8b", state->username, add_btn,
	        lx_el("div", lx_attr("class", "center"), lx_node(form))
	                .data.node);
}

static bud_node *idx_list_empty_layout(const list_state_t *state)
{
	char path[256];
	char href_buf[256];
	bud_node *add_btn;
	bud_node *filter_bar;
	bud_node *filter_wrap;
	bud_node *actions_wrap;
	bud_node *form;
	char title[128];

	site_ui_collection_path(state->module, path, sizeof(path));

	snprintf(href_buf, sizeof(href_buf), "/%s/add", state->module);
	add_btn = (state->username[0])
	                  ? lx_el("a", lx_attr("href", href_buf),
	                          lx_attr("class", "btn"), lx_text("+ add"))
	                            .data.node
	                  : NULL;

	filter_bar = idx_filter_bar(state);
	filter_wrap = NULL;
	actions_wrap = NULL;
	if (filter_bar) {
		filter_wrap = lx_el("div", lx_attr("class", "hyle-filter-bar"),
		                    lx_node(filter_bar))
		                      .data.node;
		actions_wrap =
		        lx_el("div", lx_attr("class", "hyle-filter-actions"),
		              lx_el("button", lx_attr("type", "reset"),
		                    lx_text("Clear")),
		              lx_el("button", lx_attr("type", "submit"),
		                    lx_text("Apply")))
		                .data.node;
		bud_append(filter_wrap, actions_wrap);
	}

	form = lx_el("form", lx_attr("method", "get"), lx_attr("action", ""),
	             lx_attr("class", "list-form"),
	             filter_wrap ? lx_node(filter_wrap) : lx_none(),
	             lx_node(site_ui_empty_state("No items")))
	               .data.node;

	snprintf(title, sizeof(title), "%ss", state->module);
	if (title[0] >= 'a')
		title[0] -= 32;

	return site_ui_layout(
	        title, path, "\xf0\x9f\x93\x8b", state->username,
	        add_btn,
	        lx_el("div", lx_attr("class", "center"), lx_node(form))
	                .data.node);
}

/* ── JSON state writer (native -> WASM) ───────────────────── */

typedef struct {
	char *buf;
	size_t size;
	size_t pos;
	int overflow;
} list_json_w;

static void ljw_reset(list_json_w *w, char *buf, size_t size)
{
	w->buf = buf;
	w->size = size;
	w->pos = 0;
	w->overflow = 0;
	buf[0] = '\0';
}

static void ljw_put(list_json_w *w, const char *s)
{
	size_t n;

	if (w->overflow)
		return;
	n = strlen(s);
	if (w->pos + n + 1 > w->size) {
		w->overflow = 1;
		return;
	}
	memcpy(w->buf + w->pos, s, n);
	w->pos += n;
	w->buf[w->pos] = '\0';
}

static void ljw_str(list_json_w *w, const char *s)
{
	const char *p;

	ljw_put(w, "\"");
	for (p = s ? s : ""; *p; p++) {
		unsigned char c = (unsigned char)*p;
		if (c == '"')
			ljw_put(w, "\\\"");
		else if (c == '\\')
			ljw_put(w, "\\\\");
		else if (c == '\n')
			ljw_put(w, "\\n");
		else if (c == '\r')
			ljw_put(w, "\\r");
		else if (c == '\t')
			ljw_put(w, "\\t");
		else if (c < 0x20) {
			char esc[8];
			snprintf(esc, sizeof(esc), "\\u%04x", c);
			ljw_put(w, esc);
		} else {
			char tmp[2] = { (char)c, '\0' };
			ljw_put(w, tmp);
		}
	}
	ljw_put(w, "\"");
}

static void ljw_key(list_json_w *w, const char *key)
{
	ljw_str(w, key);
	ljw_put(w, ":");
}

static void ljw_int(list_json_w *w, const char *key, int val)
{
	char tmp[32];

	snprintf(tmp, sizeof(tmp), "\"%s\":%d", key, val);
	ljw_put(w, tmp);
}

static void ljw_str_key(list_json_w *w, const char *key, const char *val)
{
	ljw_key(w, key);
	ljw_str(w, val);
}

/* Serialize the list state for the WASM enhancement bundle.
 * Returns 0 on success, -1 when the buffer was too small (caller should
 * fall back to pure SSR — the enhancement is additive). */
int list_state_to_json(const list_state_t *state, char *out, size_t out_sz)
{
	list_json_w w;
	int i;

	if (!out || out_sz == 0)
		return -1;
	if (!state)
		return -1;

	ljw_reset(&w, out, out_sz);
	ljw_put(&w, "{");
	ljw_str_key(&w, "module", state->module);
	ljw_put(&w, ",");
	ljw_str_key(&w, "username", state->username);
	ljw_put(&w, ",");
	ljw_str_key(&w, "query", state->query);
	ljw_put(&w, ",");
	ljw_str_key(&w, "sort_field", state->sort_field);
	ljw_put(&w, ",");
	ljw_int(&w, "sort_asc", state->sort_asc);
	ljw_put(&w, ",");
	ljw_int(&w, "page", state->page);
	ljw_put(&w, ",");
	ljw_int(&w, "per_page", state->per_page);
	ljw_put(&w, ",");
	ljw_int(&w, "total", state->total);
	ljw_put(&w, ",");
	ljw_int(&w, "has_page", state->has_page);
	ljw_put(&w, ",");
	ljw_int(&w, "ncols", state->ncols);
	ljw_put(&w, ",\"cols\":[");
	for (i = 0; i < state->ncols; i++) {
		const list_col_t *col = &state->cols[i];
		if (i > 0)
			ljw_put(&w, ",");
		ljw_put(&w, "{");
		ljw_str_key(&w, "key", col->key);
		ljw_put(&w, ",");
		ljw_str_key(&w, "label", col->label);
		ljw_put(&w, ",");
		ljw_int(&w, "type", col->type);
		ljw_put(&w, ",");
		ljw_str_key(&w, "target_source", col->target_source);
		ljw_put(&w, ",");
		ljw_str_key(&w, "filter", col->filter);
		ljw_put(&w, ",");
		ljw_str_key(&w, "current", col->current);
		if (col->opt_count > 0) {
			int k;
			ljw_put(&w, ",");
			ljw_int(&w, "noptions", col->opt_count);
			ljw_put(&w, ",\"opts\":[");
			for (k = 0; k < col->opt_count; k++) {
				const list_opt_t *o =
				        &state->opts[col->opt_start + k];
				if (k > 0)
					ljw_put(&w, ",");
				ljw_put(&w, "{");
				ljw_str_key(&w, "id", o->id);
				ljw_put(&w, ",");
				ljw_str_key(&w, "label", o->label);
				ljw_put(&w, "}");
			}
			ljw_put(&w, "]");
		}
		ljw_put(&w, "}");
	}
	ljw_put(&w, "]");
	ljw_put(&w, ",");
	ljw_int(&w, "nids", state->nids);
	ljw_put(&w, ",\"ids\":[");
	for (i = 0; i < state->nids; i++) {
		if (i > 0)
			ljw_put(&w, ",");
		ljw_str(&w, state->ids[i]);
	}
	ljw_put(&w, "],\"rows\":[");
	for (i = 0; i < state->nids; i++) {
		int j;
		if (i > 0)
			ljw_put(&w, ",");
		ljw_put(&w, "[");
		for (j = 0; j < state->ncols; j++) {
			if (j > 0)
				ljw_put(&w, ",");
			ljw_str(&w, state->values[i * state->ncols + j]);
		}
		ljw_put(&w, "]");
	}
	ljw_put(&w, "]}");

	return w.overflow ? -1 : 0;
}

/* ── JSON state reader (WASM side) ────────────────────────── */

static void
list_json_unescape_elem(const char *elem, size_t len, char *out, size_t out_sz)
{
	char wrap[4096];

	if (len + 6 >= sizeof(wrap))
		len = sizeof(wrap) - 7;
	snprintf(wrap, sizeof(wrap), "{\"v\":%.*s}", (int)len, elem);
	bud_json_str(wrap, "v", out, out_sz);
}

typedef struct {
	list_state_t *state;
	int slot;
	int max;
} list_fj_opts_ctx;

typedef struct {
	list_state_t *state;
	int col_index;
} list_fj_col_ctx;

typedef struct {
	const char **ids;
	int n;
	int max;
} list_fj_ids_ctx;

typedef struct {
	list_state_t *state;
	int row;
	int ncols;
	int col;
} list_fj_cell_ctx;

typedef struct {
	list_state_t *state;
	int row;
	int ncols;
} list_fj_row_ctx;

static void list_fj_opt(const char *elem, size_t len, void *user)
{
	list_fj_opts_ctx *ctx = (list_fj_opts_ctx *)user;
	list_opt_t *o;
	char opt_json[512];
	size_t n;

	if (ctx->slot >= ctx->max)
		return;
	n = len < sizeof(opt_json) - 1 ? len : sizeof(opt_json) - 1;
	memcpy(opt_json, elem, n);
	opt_json[n] = '\0';
	o = &ctx->state->opts[ctx->slot];
	bud_json_str(opt_json, "id", o->id, sizeof(o->id));
	bud_json_str(opt_json, "label", o->label, sizeof(o->label));
	ctx->slot++;
}

static void list_fj_col(const char *elem, size_t len, void *user)
{
	list_fj_col_ctx *ctx = (list_fj_col_ctx *)user;
	list_state_t *state = ctx->state;
	list_col_t *col;
	char col_json[4096];
	const char *opts_json;
	size_t n;
	int nopts;

	if (ctx->col_index >= LIST_MAX_COLS)
		return;
	n = len < sizeof(col_json) - 1 ? len : sizeof(col_json) - 1;
	memcpy(col_json, elem, n);
	col_json[n] = '\0';

	col = &state->cols[ctx->col_index];
	bud_json_str(col_json, "key", col->key, sizeof(col->key));
	bud_json_str(col_json, "label", col->label, sizeof(col->label));
	col->type = bud_json_int(col_json, "type", 0);
	bud_json_str(
	        col_json, "target_source", col->target_source,
	        sizeof(col->target_source));
	bud_json_str(col_json, "filter", col->filter, sizeof(col->filter));
	bud_json_str(col_json, "current", col->current, sizeof(col->current));
	col->opt_start = state->nopts;
	col->opt_count = 0;

	nopts = bud_json_int(col_json, "noptions", 0);
	if (nopts > 0) {
		opts_json = strstr(col_json, "\"opts\":");
		if (opts_json) {
			list_fj_opts_ctx octx;

			octx.state = state;
			octx.slot = state->nopts;
			octx.max = LIST_MAX_OPTS;
			opts_json += 7;
			bud_json_array_for_each(opts_json, list_fj_opt, &octx);
			col->opt_count = octx.slot - state->nopts;
			state->nopts = octx.slot;
		}
	}
	state->ncols = ctx->col_index + 1;
	ctx->col_index++;
}

static void list_fj_id(const char *elem, size_t len, void *user)
{
	list_fj_ids_ctx *ctx = (list_fj_ids_ctx *)user;
	char tmp[256];

	if (ctx->n >= ctx->max)
		return;
	list_json_unescape_elem(elem, len, tmp, sizeof(tmp));
	ctx->ids[ctx->n] = strdup(tmp);
	if (ctx->ids[ctx->n])
		ctx->n++;
}

static void list_fj_cell(const char *elem, size_t len, void *user)
{
	list_fj_cell_ctx *ctx = (list_fj_cell_ctx *)user;
	char tmp[2048];

	if (ctx->col >= ctx->ncols || ctx->row >= LIST_MAX_ROWS)
		return;
	list_json_unescape_elem(elem, len, tmp, sizeof(tmp));
	ctx->state->values[ctx->row * ctx->ncols + ctx->col] = strdup(tmp);
	ctx->col++;
}

static void list_fj_row(const char *elem, size_t len, void *user)
{
	list_fj_row_ctx *ctx = (list_fj_row_ctx *)user;
	char row_json[8192];
	list_fj_cell_ctx cctx;
	size_t n;

	if (ctx->row >= LIST_MAX_ROWS)
		return;
	n = len < sizeof(row_json) - 1 ? len : sizeof(row_json) - 1;
	memcpy(row_json, elem, n);
	row_json[n] = '\0';

	cctx.state = ctx->state;
	cctx.row = ctx->row;
	cctx.ncols = ctx->ncols;
	cctx.col = 0;
	bud_json_array_for_each(row_json, list_fj_cell, &cctx);
	ctx->row++;
}

void list_state_from_json(list_state_t *state, const char *json)
{
	list_fj_col_ctx cctx;
	list_fj_ids_ctx ictx;
	list_fj_row_ctx rctx;
	const char *p;

	if (!state || !json)
		return;

	memset(state, 0, sizeof(*state));

	bud_json_str(json, "module", state->module, sizeof(state->module));
	bud_json_str(
	        json, "username", state->username, sizeof(state->username));
	bud_json_str(json, "query", state->query, sizeof(state->query));
	bud_json_str(
	        json, "sort_field", state->sort_field,
	        sizeof(state->sort_field));
	state->sort_asc = bud_json_int(json, "sort_asc", 1);
	state->page = bud_json_int(json, "page", 1);
	state->per_page = bud_json_int(json, "per_page", 10);
	state->total = bud_json_int(json, "total", 0);
	state->has_page = bud_json_int(json, "has_page", 0);

	cctx.state = state;
	cctx.col_index = 0;
	p = strstr(json, "\"cols\":");
	if (p) {
		p += 7;
		bud_json_array_for_each(p, list_fj_col, &cctx);
	}

	state->nids = bud_json_int(json, "nids", 0);
	ictx.ids = state->ids;
	ictx.n = 0;
	ictx.max = LIST_MAX_ROWS;
	p = strstr(json, "\"ids\":");
	if (p) {
		p += 6;
		bud_json_array_for_each(p, list_fj_id, &ictx);
		state->nids = ictx.n;
	}

	rctx.state = state;
	rctx.row = 0;
	rctx.ncols = state->ncols;
	p = strstr(json, "\"rows\":");
	if (p) {
		p += 7;
		bud_json_array_for_each(p, list_fj_row, &rctx);
	}
}

/* ── Shared entry point ───────────────────────────────────── */

bud_node *list_render(const list_state_t *state)
{
	bud_node *inner;

	if (!state)
		return NULL;
	inner = state->nids == 0 ? idx_list_empty_layout(state)
	                         : idx_list_layout(state);
	if (!inner)
		return NULL;
	return lx_el("div", lx_attr("id", "bud-root"), lx_node(inner))
	        .data.node;
}
