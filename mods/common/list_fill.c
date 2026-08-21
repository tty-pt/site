/* Native companion to mods/index/ux/list.c: schema-driven column
 * collection, filter-option resolution, query whitelisting and the
 * source_query fill for any list-grade surface (index lists, grp/gig
 * song pickers). Textual include; compiled into each module .so.
 * NEVER include from a WASM TU (axil/qmap/source calls). */

#ifndef __wasm__

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── moved verbatim from mods/index/index.c ────────────────── */

static int idx_resolve_filter_options(
        const char *target_source, unsigned target_hd, list_opt_t *pool,
        int pool_avail)
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
	while (qmap_next(&key, &val, cur) && nopts < pool_avail) {
		const char *row_id = (const char *)key;
		const char *name = NULL;
		if (display_field[0]) {
			char name_key[320];
			snprintf(
			        name_key, sizeof(name_key), "%s:%s", row_id,
			        display_field);
			name = (const char *)qmap_get(target_hd, name_key);
		}
		strncpy(pool[nopts].id, row_id, sizeof(pool[nopts].id) - 1);
		pool[nopts].id[sizeof(pool[nopts].id) - 1] = '\0';
		strncpy(pool[nopts].label, name ? name : row_id,
		        sizeof(pool[nopts].label) - 1);
		pool[nopts].label[sizeof(pool[nopts].label) - 1] = '\0';
		nopts++;
	}
	qmap_fin(cur);

	return nopts;
}

static int idx_schema_collect(
        const char *dataset_id, const char *select_csv, col_t *cols,
        int max_cols)
{
	unsigned schema_hd;
	int n = 0;
	uint32_t cur;
	const void *key;
	const void *val;

	schema_hd = source_get_schema_hd(dataset_id);
	if (!schema_hd)
		return 0;

	if (select_csv && select_csv[0]) {
		char copy[256];
		char *tok;
		char *rest;

		strncpy(copy, select_csv, sizeof(copy) - 1);
		copy[sizeof(copy) - 1] = '\0';
		rest = copy;
		while ((tok = strtok_r(rest, ",", &rest)) && n < max_cols) {
			strncpy(cols[n].key, tok, sizeof(cols[n].key) - 1);
			cols[n].key[sizeof(cols[n].key) - 1] = '\0';
			col_tok_label(
			        cols[n].label, sizeof(cols[n].label), tok);
			if (cols[n].label[0] >= 'a')
				cols[n].label[0] -= 32;
			val = qmap_get(schema_hd, tok);
			cols[n].type = 0;
			cols[n].target_source[0] = '\0';
			cols[n].target_hd = 0;
			cols[n].filter[0] = '\0';
			if (val && ((const char *)val)[0] == '{') {
				int t;
				char ts[64] = "";
				char fs[16] = "";
				int m =
				        sscanf((const char *)val,
				               "{\"t\":%d,\"s\":\"%63[^\"]\","
				               "\"f\":\"%15[^\"]\"",
				               &t, ts, fs);
				if (m >= 1)
					cols[n].type = t;
				if (m >= 2 && ts[0]) {
					strncpy(cols[n].target_source, ts,
					        sizeof(cols[n].target_source) -
					                1);
				}
				if (m >= 3 && fs[0]) {
					strncpy(cols[n].filter, fs,
					        sizeof(cols[n].filter) - 1);
				}
			}
			n++;
		}
	} else {
		cur = qmap_iter(schema_hd, NULL, 0);
		while (n < max_cols && qmap_next(&key, &val, cur)) {
			strncpy(cols[n].key, (const char *)key,
			        sizeof(cols[n].key) - 1);
			cols[n].key[sizeof(cols[n].key) - 1] = '\0';
			col_tok_label(
			        cols[n].label, sizeof(cols[n].label),
			        (const char *)key);
			if (cols[n].label[0] >= 'a')
				cols[n].label[0] -= 32;
			cols[n].type = 0;
			n++;
		}
		qmap_fin(cur);
	}
	return n;
}

static const char *idx_resolve_refs(const col_t *col, const char *raw)
{
	static char buf[4096];
	static char last_target[64] = "";
	static char display_field[64] = "";
	const char *df;

	if (!raw || !raw[0] || !col->target_hd)
		return raw;

	if (strcmp(last_target, col->target_source) != 0) {
		unsigned shd = source_get_schema_hd(col->target_source);
		display_field[0] = '\0';
		if (shd) {
			uint32_t ccur;
			const void *ckey;
			const void *cval;
			ccur = qmap_iter(shd, NULL, 0);
			while (qmap_next(&ckey, &cval, ccur)) {
				const char *fn = (const char *)ckey;
				if (strcmp(fn, "id") == 0)
					continue;
				strncpy(display_field, fn,
				        sizeof(display_field) - 1);
				break;
			}
			qmap_fin(ccur);
		}
		strncpy(last_target, col->target_source,
		        sizeof(last_target) - 1);
	}

	df = display_field[0] ? display_field : NULL;
	if (!df)
		return raw;

	buf[0] = '\0';
	{
		const char *p = raw;
		while (*p) {
			const char *nl = strchr(p, '\n');
			size_t llen = nl ? (size_t)(nl - p) : strlen(p);
			if (llen > 0) {
				char num[32];
				size_t cplen = llen < sizeof(num) - 1
				                       ? llen
				                       : sizeof(num) - 1;
				memcpy(num, p, cplen);
				num[cplen] = '\0';
				const char *slug = NULL;
				const char *name = NULL;
				/* Try position lookup first */
				if (num[0] >= '0' && num[0] <= '9') {
					uint32_t pos = (uint32_t)atoi(num);
					slug = qmap_get_key(
					        col->target_hd, pos);
				}
				/* If not a position or not found, treat as
				 * raw slug */
				if (!slug)
					slug = num;
				if (slug) {
					char name_key[320];
					snprintf(
					        name_key, sizeof(name_key),
					        "%s:%s", slug, df);
					name = (const char *)qmap_get(
					        col->target_hd, name_key);
					if (buf[0])
						strncat(buf, ", ",
						        sizeof(buf) -
						                strlen(buf) -
						                1);
					strncat(buf, name ? name : slug,
					        sizeof(buf) - strlen(buf) - 1);
				}
			}
			if (!nl)
				break;
			p = nl + 1;
		}
	}

	if (!buf[0])
		return raw;

	return buf;
}

/* ── query-string shaping ────────────────────────────────────────────
 * Two modes:
 *  - Picker (allow_fields=0): strict WHITELIST — only control keys
 *    survive. Detail pages carry viewer prefs (t/b/f/l/m/z) which must
 *    never become bogus field filters.
 *  - Index list (allow_fields=1): BLACKLIST — arbitrary schema fields
 *    (title=, author=, ...) ARE legitimate filters; strip only the
 *    reserved single-letter pref keys. */

static const char *const LF_PREF_KEYS[] = { "t", "b", "f", "l", "m", "z",
	                                        NULL };

static int lf_key_is(const char *k, size_t n, const char *name)
{
	size_t l = strlen(name);

	return n == l && memcmp(k, name, l) == 0;
}

/* Rewrite the per_page value inside an already-cleaned query string so
 * the source engine never materializes more rows than list_state_t can
 * hold (LIST_MAX_ROWS). */
static void lf_qs_rewrite_per_page(char *qs, size_t cap, unsigned v)
{
	char val[16];
	char *p, *e;
	size_t vl, rest;

	snprintf(val, sizeof(val), "%u", v);
	vl = strlen(val);
	p = strstr(qs, "per_page=");
	if (!p)
		return;
	p += 9;
	e = p;
	while (*e && *e != '&')
		e++;
	rest = strlen(e);
	if ((size_t)(p - qs) + vl + rest >= cap)
		return;
	memmove(p + vl, e, rest + 1);
	memcpy(p, val, vl);
}

void list_fill_qs_clean(const char *raw, char *out, size_t outlen)
{
	const char *p;
	size_t pos = 0;
	int first = 1;

	out[0] = '\0';
	if (!raw)
		return;
	p = raw;
	while (*p) {
		const char *amp = strchr(p, '&');
		size_t plen = amp ? (size_t)(amp - p) : strlen(p);
		const char *eq = memchr(p, '=', plen);
		size_t klen = eq ? (size_t)(eq - p) : 0;
		int keep = 0;

		if (eq) {
			keep = lf_key_is(p, klen, "q") ||
			       lf_key_is(p, klen, "custom") ||
			       lf_key_is(p, klen, "sort") ||
			       lf_key_is(p, klen, "page") ||
			       lf_key_is(p, klen, "per_page") ||
			       lf_key_is(p, klen, "data") ||
			       lf_key_is(p, klen, "type");
		}
		if (keep && plen > 0) {
			size_t n;
			if (!first && pos + 1 < outlen)
				out[pos++] = '&';
			n = plen;
			if (pos + n > outlen - 1)
				n = outlen - 1 - pos;
			memcpy(out + pos, p, n);
			pos += n;
			out[pos] = '\0';
			first = 0;
		}
		p += plen;
		if (*p)
			p++;
	}
}

void list_fill_qs_strip_prefs(const char *raw, char *out, size_t outlen)
{
	const char *p;
	size_t pos = 0;
	int first = 1;
	int i;

	out[0] = '\0';
	if (!raw)
		return;
	p = raw;
	while (*p) {
		const char *amp = strchr(p, '&');
		size_t plen = amp ? (size_t)(amp - p) : strlen(p);
		const char *eq = memchr(p, '=', plen);
		size_t klen = eq ? (size_t)(eq - p) : 0;
		int drop = 0;

		if (eq) {
			for (i = 0; LF_PREF_KEYS[i]; i++) {
				if (lf_key_is(p, klen, LF_PREF_KEYS[i])) {
					drop = 1;
					break;
				}
			}
		}
		if (!drop && plen > 0) {
			size_t n;
			if (!first && pos + 1 < outlen)
				out[pos++] = '&';
			n = plen;
			if (pos + n > outlen - 1)
				n = outlen - 1 - pos;
			memcpy(out + pos, p, n);
			pos += n;
			out[pos] = '\0';
			first = 0;
		}
		p += plen;
		if (*p)
			p++;
	}
}

/* Fill `state` (caller zeroes it and sets module[/username] first) from
 * `raw_qs`: parse params, collect schema columns + currents + options,
 * run source_query on the shaped qs (see query-string shaping note
 * above; allow_fields=1 for index lists, 0 for detail-page pickers),
 * slice locally like the list page does, strdup display window into
 * state.ids/values. state->query mirrors the qs handed to source_query.
 * Returns 0 ok; -1 when source_query failed (cols/q/custom remain
 * valid so the filter chrome still renders; nids=total=0).
 * Free with list_fill_free() after render. */
int list_fill_state(
        list_state_t *state, const char *dataset_id, const char *raw_qs,
        int allow_fields)
{
	col_t cols[LIST_MAX_COLS];
	const char *all_ids[1024];
	char page_buf[64] = { 0 }, per_page_buf[64] = { 0 };
	char sort_field[64] = { 0 }, custom_buf[8] = { 0 };
	uint32_t page = 1, per_page = 10;
	int sort_asc;
	int ncols, nid_all = 0;
	unsigned result_hd, fields_hd;
	const char *total_str;
	uint32_t cur;
	const void *key;
	const void *val;
	uint32_t offset, disp_count;
	int disp_nids, i, j;
	int per_page_clamped;

	idx_query_param(raw_qs, "page", page_buf, sizeof(page_buf));
	if (page_buf[0]) {
		page = (uint32_t)atoi(page_buf);
		state->has_page = 1;
	}
	idx_query_param(raw_qs, "per_page", per_page_buf, sizeof(per_page_buf));
	per_page_clamped = 0;
	if (per_page_buf[0]) {
		per_page = (uint32_t)atoi(per_page_buf);
		if (per_page > LIST_MAX_ROWS) {
			per_page = LIST_MAX_ROWS;
			per_page_clamped = 1;
		}
	}
	idx_parse_sort(raw_qs, sort_field, sizeof(sort_field), &sort_asc);
	snprintf(
	        state->sort_field, sizeof(state->sort_field), "%s", sort_field);
	state->sort_asc = sort_asc;
	state->page = (int)page;
	state->per_page = (int)per_page;
	idx_query_param(raw_qs, "custom", custom_buf, sizeof(custom_buf));
	state->custom = strcmp(custom_buf, "1") == 0;
	idx_query_param(raw_qs, "q", state->q, sizeof(state->q));

	ncols = idx_schema_collect(
	        dataset_id, idx_select_fields_for(state->module), cols,
	        LIST_MAX_COLS);
	state->ncols = ncols;

	state->nopts = 0;
	for (i = 0; i < ncols; i++) {
		char cur_buf[512] = "";

		snprintf(
		        state->cols[i].key, sizeof(state->cols[i].key), "%s",
		        cols[i].key);
		snprintf(
		        state->cols[i].label, sizeof(state->cols[i].label),
		        "%s", cols[i].label);
		state->cols[i].type = cols[i].type;
		snprintf(
		        state->cols[i].target_source,
		        sizeof(state->cols[i].target_source), "%s",
		        cols[i].target_source);
		snprintf(
		        state->cols[i].filter, sizeof(state->cols[i].filter),
		        "%s", cols[i].filter);

		if (cols[i].target_source[0] && !cols[i].target_hd)
			cols[i].target_hd =
			        source_get_fields_hd(cols[i].target_source);

		{
			int is_multi =
			        cols[i].type == SOURCE_FIELD_MULTI_REFERENCE;

			if (!is_multi &&
			    cols[i].type == SOURCE_FIELD_REFERENCE &&
			    (strcmp(cols[i].filter, "multiselect") == 0 ||
			     strcmp(cols[i].filter, "grid") == 0))
				is_multi = 1;
			if (is_multi && cols[i].target_hd) {
				idx_query_params_join(
				        raw_qs, cols[i].key,
				        state->cols[i].current,
				        sizeof(state->cols[i].current));
			} else {
				idx_query_param(
				        raw_qs, cols[i].key, cur_buf,
				        sizeof(cur_buf));
				snprintf(
				        state->cols[i].current,
				        sizeof(state->cols[i].current), "%s",
				        cur_buf);
			}
		}

		if (cols[i].target_hd &&
		    (cols[i].type == SOURCE_FIELD_REFERENCE ||
		     cols[i].type == SOURCE_FIELD_MULTI_REFERENCE))
		{
			int n = idx_resolve_filter_options(
			        cols[i].target_source, cols[i].target_hd,
			        state->opts + state->nopts,
			        LIST_MAX_OPTS - state->nopts);
			state->cols[i].opt_start = state->nopts;
			state->cols[i].opt_count = n;
			state->nopts += n;
		}
	}

	if (allow_fields)
		list_fill_qs_strip_prefs(
		        raw_qs, state->query, sizeof(state->query));
	else
		list_fill_qs_clean(raw_qs, state->query, sizeof(state->query));

	result_hd = source_query(dataset_id, state->query);
	if (!result_hd) {
		state->nids = 0;
		state->total = 0;
		return -1;
	}

	total_str = (const char *)qmap_get(result_hd, "__total__");
	state->total = total_str ? atoi(total_str) : 0;

	cur = qmap_iter(result_hd, NULL, 0);
	while (nid_all < 1024 && qmap_next(&key, &val, cur)) {
		if (strcmp((const char *)key, "__total__") == 0)
			continue;
		all_ids[nid_all++] = (const char *)key;
	}
	qmap_fin(cur);

	/* Local slicing. When the request carries an explicit ?page=, the
	 * source engine already applied the page/per_page window to the
	 * result set (nid_all IS the page) — slicing again would empty it.
	 * Without ?page=, nid_all is the full match set and we slice
	 * locally. disp_count is clamped to LIST_MAX_ROWS either way:
	 * state->ids/values are fixed-size arrays and a wider window
	 * overflows into values[] and corrupts the heap (double free in
	 * list_fill_free). */
	if (per_page_clamped)
		lf_qs_rewrite_per_page(
		        state->query, sizeof(state->query), per_page);
	offset = 0;
	disp_count = (uint32_t)nid_all;
	if (!state->has_page) {
		offset = (page - 1) * per_page;
		if (offset > (uint32_t)nid_all)
			offset = 0;
		disp_count = per_page;
		if (offset + disp_count > (uint32_t)nid_all)
			disp_count = (uint32_t)nid_all - offset;
	}
	if (disp_count > LIST_MAX_ROWS)
		disp_count = LIST_MAX_ROWS;

	disp_nids = (int)disp_count;
	fields_hd = source_get_fields_hd(dataset_id);
	for (i = 0; i < disp_nids; i++) {
		state->ids[i] = strdup(all_ids[offset + i]);
		if (!state->ids[i])
			break;
	}
	disp_nids = i;
	state->nids = disp_nids;

	for (i = 0; i < disp_nids; i++) {
		for (j = 0; j < ncols; j++) {
			char fkey[256];
			const char *fval;

			snprintf(
			        fkey, sizeof(fkey), "%s:%s", state->ids[i],
			        cols[j].key);
			fval = (const char *)qmap_get(fields_hd, fkey);
			if (!fval)
				fval = "";
			if (j > 0 &&
			    cols[j].type == SOURCE_FIELD_MULTI_REFERENCE &&
			    cols[j].target_hd)
				fval = idx_resolve_refs(&cols[j], fval);
			state->values[i * ncols + j] = strdup(fval);
		}
	}

	qmap_close(result_hd);
	return 0;
}

void list_fill_free(list_state_t *state)
{
	int i, j;

	if (!state)
		return;
	for (i = 0; i < state->nids; i++) {
		free((void *)state->ids[i]);
		for (j = 0; j < state->ncols; j++)
			free((void *)state->values[i * state->ncols + j]);
	}
	state->nids = 0;
}

#endif /* !__wasm__ */
