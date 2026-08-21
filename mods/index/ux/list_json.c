#ifndef INDEX_UX_LIST_JSON_C
#define INDEX_UX_LIST_JSON_C

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
		else if (c == '<')
			ljw_put(w, "\\u003c");
		else if (c == '/')
			ljw_put(w, "\\/");
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
	ljw_str_key(&w, "display_name", state->display_name);
	ljw_put(&w, ",");
	ljw_str_key(&w, "content_field", state->content_field);
	ljw_put(&w, ",");
	ljw_str_key(&w, "content_label", state->content_label);
	ljw_put(&w, ",");
	ljw_str_key(&w, "content_placeholder", state->content_placeholder);
	ljw_put(&w, ",");
	ljw_str_key(&w, "username", state->username);
	ljw_put(&w, ",");
	ljw_str_key(&w, "query", state->query);
	ljw_put(&w, ",");
	ljw_int(&w, "custom", state->custom);
	ljw_put(&w, ",");
	ljw_str_key(&w, "q", state->q);
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
		list_fj_opts_ctx octx;

		octx.state = state;
		octx.slot = state->nopts;
		octx.max = LIST_MAX_OPTS;
		bud_json_array_for_each_key_len(
		        col_json, strlen(col_json), "opts", list_fj_opt,
		        &octx);
		col->opt_count = octx.slot - state->nopts;
		state->nopts = octx.slot;
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

void list_state_from_json_len(
        list_state_t *state, const char *json, size_t len)
{
	list_fj_col_ctx cctx;
	list_fj_ids_ctx ictx;
	list_fj_row_ctx rctx;

	if (!state || !json)
		return;
	if (len == 0)
		len = strlen(json);
	memset(state, 0, sizeof(*state));

	bud_json_str_len(
	        json, len, "module", state->module, sizeof(state->module));
	bud_json_str_len(
	        json, len, "display_name", state->display_name,
	        sizeof(state->display_name));
	bud_json_str_len(
	        json, len, "content_field", state->content_field,
	        sizeof(state->content_field));
	bud_json_str_len(
	        json, len, "content_label", state->content_label,
	        sizeof(state->content_label));
	bud_json_str_len(
	        json, len, "content_placeholder", state->content_placeholder,
	        sizeof(state->content_placeholder));
	bud_json_str_len(
	        json, len, "username", state->username,
	        sizeof(state->username));
	bud_json_str_len(json, len, "query", state->query, sizeof(state->query));
	state->custom = bud_json_int_len(json, len, "custom", 0) ? 1 : 0;
	bud_json_str_len(json, len, "q", state->q, sizeof(state->q));
	bud_json_str_len(
	        json, len, "sort_field", state->sort_field,
	        sizeof(state->sort_field));
	state->sort_asc = bud_json_int_len(json, len, "sort_asc", 1);
	state->page = bud_json_int_len(json, len, "page", 1);
	state->per_page = bud_json_int_len(json, len, "per_page", 10);
	state->total = bud_json_int_len(json, len, "total", 0);
	state->has_page = bud_json_int_len(json, len, "has_page", 0);

	cctx.state = state;
	cctx.col_index = 0;
	bud_json_array_for_each_key_len(json, len, "cols", list_fj_col, &cctx);

	state->nids = bud_json_int_len(json, len, "nids", 0);
	ictx.ids = state->ids;
	ictx.n = 0;
	ictx.max = LIST_MAX_ROWS;
	bud_json_array_for_each_key_len(json, len, "ids", list_fj_id, &ictx);
	if (ictx.n > 0 || state->nids == 0)
		state->nids = ictx.n;

	rctx.state = state;
	rctx.row = 0;
	rctx.ncols = state->ncols;
	bud_json_array_for_each_key_len(json, len, "rows", list_fj_row, &rctx);
}

void list_state_from_json(list_state_t *state, const char *json)
{
	size_t len = json ? strlen(json) : 0;
	list_state_from_json_len(state, json, len);
}

#endif
