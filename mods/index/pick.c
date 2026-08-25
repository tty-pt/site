/* Native companion for the /pick/:id/options fragment route and the
 * per-form pick view. Included after list_fill.c — shares the
 * idx_query_param/idx_url_decode TU statics. NEVER include from a
 * WASM TU (axil/qmap/source/hyle-bud calls). */

#ifndef __wasm__

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <hyle-bud/hyle-bud.h>
#include "ux/site_ui.h"

#define PICK_MAX_OPTS 128
#define PICK_MAX_SEL 256
#define PICK_DEFAULT_PER_PAGE 15
#define PICK_MAX_SCROLL_PAGES 10

/* Defined here AND in index.h (index.c cannot include its own header:
 * XY_DECL and XY_IMPL may not share a TU). Keep identical. */
#ifndef PICK_CTX_T
#define PICK_CTX_T
typedef struct {
	char q[256];
	int page; /* 0-based window index */
	int per_page;
} pick_ctx_t;
#endif

/* Thread-local pools backing the borrowed hyle_bud_option_t strings
 * returned by pick_options_fill/pick_selected_fill (route-handler
 * use: one field per request). Multi-field callers (phase 4's
 * pick_view_collect) use the pick_fill/pick_resolve internals with
 * their own per-entry buffers instead. */
static __thread char pick_opt_ids[PICK_MAX_OPTS][64];
static __thread char pick_opt_labels[PICK_MAX_OPTS][256];
static __thread char pick_sel_ids[PICK_MAX_SEL][64];
static __thread char pick_sel_labels[PICK_MAX_SEL][256];

/* Fragment HTML work buffers. */
#define PICK_PANEL_SZ 65536
#define PICK_ROWS_SZ 32768
#define PICK_VALUES_SZ 8192
static __thread char pick_panel_buf[PICK_PANEL_SZ];
static __thread char pick_values_buf[PICK_VALUES_SZ];
static __thread char pick_rows_buf[PICK_ROWS_SZ];

/* Current-request context stashed by pick_ctx_load so
 * pick_options_fill can shape the source_query string without widening
 * its declared signature. */
static __thread const char *pick_req_qs;
static __thread const char *pick_req_key;

/* First non-id schema key = display label convention (same as
 * idx_resolve_filter_options). */
static void pick_display_field(
        const char *dataset_id, char *out, size_t sz)
{
	unsigned schema_hd;
	uint32_t cur;
	const void *key;
	const void *val;

	out[0] = '\0';
	schema_hd = source_get_schema_hd(dataset_id);
	if (!schema_hd)
		return;
	cur = qmap_iter(schema_hd, NULL, 0);
	while (qmap_next(&key, &val, cur)) {
		if (strcmp((const char *)key, "id") == 0)
			continue;
		snprintf(out, sz, "%s", (const char *)key);
		break;
	}
	qmap_fin(cur);
}

static const char *pick_label_of(unsigned fields_hd, const char *row_id,
        const char *display_field)
{
	char name_key[320];
	const char *name;

	if (!display_field[0])
		return row_id;
	snprintf(name_key, sizeof(name_key), "%s:%s", row_id,
	        display_field);
	name = (const char *)qmap_get(fields_hd, name_key);
	return name ? name : row_id;
}

/* Copy the still-url-encoded `pick_q_<key>` segment out of the raw
 * query string, renamed to `q=`, plus page/per_page — byte-preserving
 * splice so accents survive without a decode/re-encode round trip. */
static void pick_qs_shape(const char *raw_qs, const char *key,
        int page0, int per_page, char *out, size_t out_sz)
{
	char pname[192];
	size_t plen;
	const char *p;

	snprintf(pname, sizeof(pname), "pick_q_%s", key ? key : "");
	plen = strlen(pname);

	if (raw_qs) {
		p = raw_qs;
		while (p) {
			p = strstr(p, pname);
			if (!p)
				break;
			if ((p == raw_qs || p[-1] == '&') &&
			    p[plen] == '=' && p[plen + 1])
			{
				const char *end = strchr(p + plen + 1, '&');
				size_t vlen = end ? (size_t)(end -
				                          p - plen - 1)
				                  : strlen(p + plen + 1);
				int off;
				if (vlen > out_sz - 64)
					vlen = out_sz - 64;
				memcpy(out, "q=", 2);
				memcpy(out + 2, p + plen + 1, vlen);
				out[vlen + 2] = '\0';
				off = (int)(vlen + 2);
				snprintf(out + off, out_sz - (size_t)off,
				        "&page=%d&per_page=%d", page0 + 1,
				        per_page);
				return;
			}
			p++;
		}
	}
	snprintf(out, out_sz, "page=%d&per_page=%d", page0 + 1,
	        per_page);
}

/* Fill opts[] from one source_query window; strings land in caller
 * buffers so multi-field callers avoid pool collisions. */
static int pick_fill(const char *dataset_id, const char *req_qs,
        const char *key, const pick_ctx_t *ctx,
        char (*id_buf)[64], char (*label_buf)[256],
        hyle_bud_option_t *opts, int max, int *total)
{
	char qs[1024];
	char display_field[64];
	unsigned result_hd, fields_hd;
	const char *total_str;
	uint32_t cur;
	const void *rkey;
	const void *rval;
	int n = 0;

	*total = 0;
	if (!dataset_id || !dataset_id[0] || max <= 0)
		return 0;

	pick_qs_shape(req_qs, key, ctx->page, ctx->per_page, qs,
	        sizeof(qs));

	result_hd = source_query(dataset_id, qs);
	if (!result_hd)
		return 0;

	total_str = (const char *)qmap_get(result_hd, "__total__");
	*total = total_str ? atoi(total_str) : 0;

	fields_hd = source_get_fields_hd(dataset_id);
	pick_display_field(dataset_id, display_field,
	        sizeof(display_field));

	cur = qmap_iter(result_hd, NULL, 0);
	while (n < max && qmap_next(&rkey, &rval, cur)) {
		const char *row_id;
		const char *name;

		if (strcmp((const char *)rkey, "__total__") == 0)
			continue;
		row_id = (const char *)rkey;
		snprintf(id_buf[n], sizeof(id_buf[n]), "%s", row_id);
		name = pick_label_of(fields_hd, row_id, display_field);
		snprintf(label_buf[n], sizeof(label_buf[n]), "%s", name);
		opts[n].id = id_buf[n];
		opts[n].label = label_buf[n];
		n++;
	}
	qmap_fin(cur);
	qmap_close(result_hd);
	return n;
}

XY_IMPL(int, pick_options_fill,
	const char *, dataset_id,
	pick_ctx_t *, ctx,
	hyle_bud_option_t *, opts,
	int, max,
	int *, total)
{
	if (max > PICK_MAX_OPTS)
		max = PICK_MAX_OPTS;
	return pick_fill(dataset_id, pick_req_qs, pick_req_key, ctx,
	        pick_opt_ids, pick_opt_labels, opts, max, total);
}

/* Split comma slugs and resolve each to {id,label}: stored tokens may
 * be positions OR raw slugs (idx_resolve_refs precedent — position-
 * first lookup via qmap_get_key, slug fallback). */
static int pick_resolve(const char *dataset_id, unsigned fields_hd,
        const char *comma_slugs, char (*id_buf)[64],
        char (*label_buf)[256], hyle_bud_option_t *out, int max)
{
	char display_field[64];
	const char *p = comma_slugs;
	int n = 0;

	if (!fields_hd || !p || !p[0] || max <= 0)
		return 0;
	pick_display_field(dataset_id, display_field,
	        sizeof(display_field));

	while (*p && n < max) {
		const char *comma = strchr(p, ',');
		size_t len = comma ? (size_t)(comma - p) : strlen(p);
		char token[128];
		const char *slug = NULL;

		if (len >= sizeof(token))
			len = sizeof(token) - 1;
		memcpy(token, p, len);
		token[len] = '\0';
		if (token[0]) {
			if (strspn(token, "0123456789") == strlen(token))
			{
				slug = qmap_get_key(fields_hd,
				        (uint32_t)atoi(token));
				if (slug && !slug[0])
					slug = NULL;
			}
			if (!slug)
				slug = token;

			snprintf(id_buf[n], sizeof(id_buf[n]), "%.60s",
			        slug);
			{
				char name_key[320];
				const char *name = NULL;

				if (display_field[0]) {
					snprintf(name_key,
					        sizeof(name_key), "%s:%s",
					        slug, display_field);
					name = (const char *)qmap_get(
					        fields_hd, name_key);
				}
				snprintf(label_buf[n],
				        sizeof(label_buf[n]), "%s",
				        name ? name : slug);
			}
			out[n].id = id_buf[n];
			out[n].label = label_buf[n];
			n++;
		}
		if (!comma)
			break;
		p = comma + 1;
	}
	return n;
}

XY_IMPL(int, pick_selected_fill,
	const char *, dataset_id,
	const char *, comma_slugs,
	hyle_bud_option_t *, out,
	int, max)
{
	unsigned fields_hd;

	if (!dataset_id || !dataset_id[0] || max <= 0)
		return 0;
	if (max > PICK_MAX_SEL)
		max = PICK_MAX_SEL;
	fields_hd = source_get_fields_hd(dataset_id);
	return pick_resolve(dataset_id, fields_hd, comma_slugs,
	        pick_sel_ids, pick_sel_labels, out, max);
}

/* Read pick_q_<key> / pick_page_<key> / per_page from the request
 * query string (page is 0-based here; the source engine gets page+1,
 * matching the 1-based list convention). Also stashes qs/key for
 * pick_options_fill's query shaping. */
XY_IMPL(int, pick_ctx_load, char *, qs, const char *, key, pick_ctx_t *,
        out)
{
	char buf[64];
	char pname[192];

	memset(out, 0, sizeof(*out));
	out->per_page = PICK_DEFAULT_PER_PAGE;
	pick_req_qs = qs;
	pick_req_key = key;
	if (!qs || !key || !key[0])
		return 0;

	snprintf(pname, sizeof(pname), "pick_q_%s", key);
	idx_query_param(qs, pname, out->q, sizeof(out->q));

	snprintf(pname, sizeof(pname), "pick_page_%s", key);
	buf[0] = '\0';
	idx_query_param(qs, pname, buf, sizeof(buf));
	if (buf[0]) {
		out->page = atoi(buf);
		if (out->page < 0)
			out->page = 0;
		if (out->page > 10000)
			out->page = 10000;
	}

	buf[0] = '\0';
	idx_query_param(qs, "per_page", buf, sizeof(buf));
	if (buf[0]) {
		int pp = atoi(buf);
		if (pp > 0)
			out->per_page = pp;
	}
	if (out->per_page > PICK_MAX_OPTS)
		out->per_page = PICK_MAX_OPTS;
	return 0;
}

/* ── fragment route: GET /pick/:id/options ──────────────────────── */

static char *pick_json_escape(const char *s)
{
	size_t n = s ? strlen(s) : 0;
	char *esc = malloc(n * 6 + 8);

	if (!esc)
		return NULL;
	axil_json_escape(s ? s : "", esc, n * 6 + 8);
	return esc;
}

static int pick_respond_jsonf(int fd, const char *fmt, ...)
{
	va_list ap;
	int needed;
	char *json;

	va_start(ap, fmt);
	needed = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (needed < 0)
		return server_error(fd, "Envelope failed");
	json = malloc((size_t)needed + 1);
	if (!json)
		return server_error(fd, "Out of memory");
	va_start(ap, fmt);
	vsnprintf(json, (size_t)needed + 1, fmt, ap);
	va_end(ap);
	respond_json(fd, 200, json);
	free(json);
	return 1;
}

static int pick_options_handler(int fd, char *body)
{
	(void)body;
	char dataset[192];
	char qs[2048];
	char key[192];
	char label[256];
	char sel_raw[2048];
	char buf[16];
	pick_ctx_t ctx;
	hyle_bud_option_t opts[PICK_MAX_OPTS];
	hyle_bud_option_t sel[PICK_MAX_SEL];
	int nopts, nsel, total;
	int multi = 0;
	int is_append = 0;

	axil_env_get(fd, dataset, sizeof(dataset), "PATTERN_PARAM_ID");
	if (!dataset[0])
		return bad_request(fd, "Missing dataset");
	if (!require_user(fd))
		return 1;

	axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");

	key[0] = '\0';
	idx_query_param(qs, "key", key, sizeof(key));
	if (!key[0] || strlen(key) > 96)
		return bad_request(fd, "Missing key");

	label[0] = '\0';
	idx_query_param(qs, "label", label, sizeof(label));
	sel_raw[0] = '\0';
	idx_query_param(qs, "sel", sel_raw, sizeof(sel_raw));
	buf[0] = '\0';
	idx_query_param(qs, "multi", buf, sizeof(buf));
	multi = buf[0] == '1';

	pick_ctx_load(qs, key, &ctx);

	buf[0] = '\0';
	idx_query_param(qs, "more", buf, sizeof(buf));
	is_append = buf[0] == '1';
	if (is_append) {
		buf[0] = '\0';
		idx_query_param(qs, "page", buf, sizeof(buf));
		if (buf[0]) {
			ctx.page = atoi(buf);
			if (ctx.page < 0)
				ctx.page = 0;
			if (ctx.page > 10000)
				ctx.page = 10000;
		}
	}

	if (!source_find(dataset))
		return not_found(fd, "Unknown dataset");

	nsel = pick_selected_fill(dataset, sel_raw, sel, PICK_MAX_SEL);
	nopts = pick_options_fill(dataset, &ctx, opts, PICK_MAX_OPTS,
	        &total);

	if (is_append) {
		hyle_bud_picker_desc_t d;
		char *rows_esc;
		int eof;

		/* Infinite-scroll cap (~10 pages at default per_page):
		 * stop feeding rows and hand back a refinement hint so
		 * DOM size stays bounded (spec §3.1/R6). */
		if (ctx.page >= PICK_MAX_SCROLL_PAGES &&
		        (ctx.page + 1) * ctx.per_page < total)
		{
			return pick_respond_jsonf(fd,
			        "{\"rows\":\"<div class=\\\"hyle-picker-"
			        "refine\\\">Too many results \xe2\x80\x94 "
			        "refine your search.</div>\",\"eof\":1}");
		}

		memset(&d, 0, sizeof(d));
		d.key = key;
		d.label = label;
		d.source = dataset;
		d.multi = multi;
		d.q = ctx.q;
		d.page = ctx.page;
		d.per_page = ctx.per_page;
		d.total = total;
		d.page_opts = opts;
		d.npage = nopts;
		d.sel = sel;
		d.nsel = nsel;

		hyle_bud_picker_rows(&d, pick_rows_buf,
		        sizeof(pick_rows_buf));
		eof = (ctx.page + 1) * ctx.per_page >= total;

		rows_esc = pick_json_escape(pick_rows_buf);
		if (!rows_esc)
			return server_error(fd, "Out of memory");
		{
			int rc = pick_respond_jsonf(fd,
			        "{\"rows\":\"%s\",\"eof\":%d}", rows_esc,
			        eof ? 1 : 0);
			free(rows_esc);
			return rc;
		}
	}

	{
		hyle_bud_picker_desc_t d;
		char *panel_esc;
		char *values_esc;
		int rc;

		memset(&d, 0, sizeof(d));
		d.key = key;
		d.label = label;
		d.source = dataset;
		d.multi = multi;
		d.q = ctx.q;
		d.page = ctx.page;
		d.per_page = ctx.per_page;
		d.total = total;
		d.page_opts = opts;
		d.npage = nopts;
		d.sel = sel;
		d.nsel = nsel;

		hyle_bud_picker_slots(&d, pick_panel_buf,
		        sizeof(pick_panel_buf), pick_values_buf,
		        sizeof(pick_values_buf));

		panel_esc = pick_json_escape(pick_panel_buf);
		values_esc = pick_json_escape(pick_values_buf);
		if (!panel_esc || !values_esc) {
			free(panel_esc);
			free(values_esc);
			return server_error(fd, "Out of memory");
		}
		rc = pick_respond_jsonf(fd,
		        "{\"slots\":{\"panel\":\"%s\",\"values\":\"%s\"}}",
		        panel_esc, values_esc);
		free(panel_esc);
		free(values_esc);
		return rc;
	}
}

/* ── per-form pick view (native only) ───────────────────────────── */

#define PICK_VIEW_MAX_OPTS 128
#define PICK_VIEW_MAX_SEL 64
#define PICK_OVERLAY_ARENA 16384

static __thread hyle_bud_option_t pick_v_opts[FF_PICKER_MAX_FIELDS]
        [PICK_VIEW_MAX_OPTS];
static __thread char pick_v_ids[FF_PICKER_MAX_FIELDS]
        [PICK_VIEW_MAX_OPTS][64];
static __thread char pick_v_labels[FF_PICKER_MAX_FIELDS]
        [PICK_VIEW_MAX_OPTS][256];
static __thread hyle_bud_option_t
        pick_v_sel[FF_PICKER_MAX_FIELDS][PICK_VIEW_MAX_SEL];
static __thread char pick_v_sel_ids[FF_PICKER_MAX_FIELDS]
        [PICK_VIEW_MAX_SEL][64];
static __thread char pick_v_sel_labels[FF_PICKER_MAX_FIELDS]
        [PICK_VIEW_MAX_SEL][256];
static __thread char pick_v_raw[FF_PICKER_MAX_FIELDS][1024];
static __thread char pick_v_slugs[FF_PICKER_MAX_FIELDS][1024];
static __thread char pick_overlay[PICK_OVERLAY_ARENA];

/* Normalize a stored multi-ref value (newline/comma tokens that may be
 * positions OR slugs) into a comma-joined slug list for
 * pick_selected_fill/pick_resolve. */
static void pick_tokens_to_slugs(const char *dataset, unsigned fields_hd, const char *raw,
        char *out, size_t out_sz)
{
	char display_field[64];
	const char *p = raw;
	size_t off = 0;

	out[0] = '\0';
	if (!raw)
		return;
	
	pick_display_field(dataset, display_field, sizeof(display_field));

	while (*p && off + 2 < out_sz) {
		const char *end = strpbrk(p, "\r\n,");
		size_t len = end ? (size_t)(end - p) : strlen(p);
		char token[128];
		const char *slug = NULL;

		if (len >= sizeof(token))
			len = sizeof(token) - 1;
		memcpy(token, p, len);
		token[len] = '\0';
		if (token[0]) {
			if (strspn(token, "0123456789") == strlen(token)) {
				slug = qmap_get_key(fields_hd,
				        (uint32_t)atoi(token));
				if (slug && !slug[0])
					slug = NULL;
			}
			/* Fallback reverse-lookup by display name */
			if (!slug && display_field[0]) {
				unsigned result_hd;
				char qs[512];
				snprintf(qs, sizeof(qs), "%s=%s", display_field, token);
				result_hd = source_query(dataset, qs);
				if (result_hd) {
					uint32_t cur = qmap_iter(result_hd, NULL, 0);
					const void *rk;
					const void *rv;
					while (qmap_next(&rk, &rv, cur)) {
						if (strcmp((const char *)rk, "__total__") != 0) {
							slug = (const char *)rk;
							break;
						}
					}
					qmap_fin(cur);
					qmap_close(result_hd);
				}
			}
			if (!slug)
				slug = token;
			if (off)
				out[off++] = ',';
			snprintf(out + off, out_sz - (size_t)off, "%.60s",
			        slug);
			off = strlen(out);
		}
		if (!end)
			break;
		p = end + 1;
	}
}

/* Shared implementation; when `scope` is non-empty this collector's
 * pick_q_/pick_page_ query params are namespaced as `<key>__<scope>`
 * so multiple independent picker instances can coexist on one page.
 * Entry value/radio names stay unscoped (e->key = field name). */
static int pick_view_collect_impl(char *body, const form_field_t *fields,
        const char **vals_in, const char **vals_out, pick_view_t *pv,
        const char *scope)
{
	int arena_off = 0;
	int ri = 0;
	int i;

	memset(pv, 0, sizeof(*pv));
	pick_overlay[0] = '\0';

	for (i = 0; fields && fields[i].name; i++) {
		const form_field_t *f = &fields[i];
		const char *val = vals_in ? vals_in[i] : NULL;

		if (i >= 64)
			break; /* vals_out caller contract */

		/* Overlay: query param (draft mirror) wins over stored. */
		if (body && body[0] && f->type != 2 &&
		    arena_off < PICK_OVERLAY_ARENA - 4)
		{
			char tmp[2048];

			tmp[0] = '\0';
			idx_query_param(body, f->name, tmp, sizeof(tmp));
			if (tmp[0]) {
				vals_out[i] = pick_overlay + arena_off;
				arena_off += snprintf(
				        pick_overlay + arena_off,
				        (size_t)(PICK_OVERLAY_ARENA -
				                arena_off),
				        "%s", tmp);
				continue;
			}
		}
		vals_out[i] = val;
	}

	for (i = 0; fields && fields[i].name && ri < FF_PICKER_MAX_FIELDS;
	        i++) {
		const form_field_t *f = &fields[i];
		pick_entry_t *e = &pv->entries[ri];
		unsigned fields_hd;
		char qs[1024];
		char skey[192];
		int total = 0, nopts = 0;
		int eff = f->max_inline > 0 ? f->max_inline
		                            : FF_PICKER_THRESHOLD;

		if (f->ref == FF_REF_NONE || !f->target || !f->target[0])
			continue;
		if (body && strlen(body) > 2048)
			continue;
		if (!source_find(f->target))
			continue;
		fields_hd = source_get_fields_hd(f->target);
		if (!fields_hd)
			continue;

		memset(e, 0, sizeof(*e));
		e->key = f->name;
		if (scope && scope[0])
			snprintf(skey, sizeof(skey), "%s__%s", f->name,
			        scope);
		else
			snprintf(skey, sizeof(skey), "%s", f->name);
		e->multi = f->ref == FF_REF_MULTI;
		e->target = f->target;
		e->per_page = PICK_DEFAULT_PER_PAGE;
		pick_v_raw[ri][0] = '\0';
		if (vals_out[i] && vals_out[i][0])
			snprintf(pick_v_raw[ri], sizeof(pick_v_raw[ri]),
			        "%s", vals_out[i]);

		{
			static __thread pick_ctx_t ctx;

			pick_ctx_load(body, skey, &ctx);
			e->q = ctx.q;
			e->page = ctx.page;
			e->per_page = ctx.per_page;

			nopts = pick_fill(f->target, body, skey, &ctx,
			        pick_v_ids[ri], pick_v_labels[ri],
			        pick_v_opts[ri], PICK_VIEW_MAX_OPTS,
			        &total);
			e->total = total;
			/* At or below threshold the inline widget needs
			 * the FULL option list: re-fetch page 1 sized to
			 * total when the ctx window truncated it. */
			if (total > 0 && total <= eff &&
			    (ctx.page != 0 || nopts < total)) {
				pick_ctx_t full = { .per_page = total };

				nopts = pick_fill(f->target, body, skey,
				        &full, pick_v_ids[ri],
				        pick_v_labels[ri], pick_v_opts[ri],
				        PICK_VIEW_MAX_OPTS, &total);
				e->page = 0;
			}
		}
		e->page_opts = pick_v_opts[ri];
		e->npage = nopts;

		pick_tokens_to_slugs(f->target, fields_hd, pick_v_raw[ri],
		        pick_v_slugs[ri], sizeof(pick_v_slugs[ri]));
		e->nsel = pick_resolve(f->target, fields_hd,
		        pick_v_slugs[ri], pick_v_sel_ids[ri],
		        pick_v_sel_labels[ri], pick_v_sel[ri],
		        PICK_VIEW_MAX_SEL);
		e->sel = pick_v_sel[ri];
		ri++;
	}
	pv->n = ri;
	return ri;
}

XY_IMPL(int, pick_view_collect,
	char *, body,
	const form_field_t *, fields,
	const char **, vals_in,
	const char **, vals_out,
	pick_view_t *, pv)
{
	return pick_view_collect_impl(body, fields, vals_in, vals_out, pv,
	        NULL);
}

XY_IMPL(int, pick_view_collect_scoped,
	char *, body,
	const form_field_t *, fields,
	const char **, vals_in,
	const char **, vals_out,
	pick_view_t *, pv,
	const char *, scope)
{
	return pick_view_collect_impl(body, fields, vals_in, vals_out, pv,
	        scope);
}

int axil_env_get(int fd, char *target, size_t dest_len, char *key);
int axil_query_parse(char *qs);

XY_IMPL(int, pick_view_collect_fd,
	int, fd,
	const form_field_t *, fields,
	const char **, vals_in,
	const char **, vals_out,
	pick_view_t *, pv)
{
	char qs[16384] = { 0 };
	if (fd > 0)
		axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");
	return pick_view_collect(qs, fields, vals_in, vals_out, pv);
}

#endif /* !__wasm__ */
