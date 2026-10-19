#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <ttypt/axil.h>
#include <ttypt/auth.h>
#include <hyle-source/hyle_source.h>
#include <hyle-bud/hyle-bud.h>
#include <hyle/url.h>

#include "../common/common.h"

#define PICK_MAX_OPTS 128
#define PICK_MAX_SEL 256
#define PICK_DEFAULT_PER_PAGE 15
#define PICK_MAX_SCROLL_PAGES 10
#define PICK_PANEL_SZ 65536
#define PICK_ROWS_SZ 32768
#define PICK_VALUES_SZ 8192

static __thread char pick_panel_buf[PICK_PANEL_SZ];
static __thread char pick_values_buf[PICK_VALUES_SZ];
static __thread char pick_rows_buf[PICK_ROWS_SZ];
static __thread char pick_opt_ids[PICK_MAX_OPTS][64];
static __thread char pick_opt_labels[PICK_MAX_OPTS][256];
static __thread char pick_sel_ids[PICK_MAX_SEL][64];
static __thread char pick_sel_labels[PICK_MAX_SEL][256];

/* JSON error envelope ({"error":...}) — the picker fragment contract always
 * returns JSON, so we must not route errors through mods/common's HTML/plain
 * respond_error path. */
static int pick_json_error(int fd, int status, const char *msg)
{
	char buf[512];
	snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg ? msg : "Error");
	return respond_json(fd, status, buf);
}

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
		return pick_json_error(fd, 500, "Envelope failed");
	json = malloc((size_t)needed + 1);
	if (!json)
		return pick_json_error(fd, 500, "Out of memory");
	va_start(ap, fmt);
	vsnprintf(json, (size_t)needed + 1, fmt, ap);
	va_end(ap);
	respond_json(fd, 200, json);
	free(json);
	return 1;
}

/* ── Picker Fragment Route: GET /pick/:id/options ────────────────── */

static int pick_options_handler(int fd, char *body)
{
	(void)body;
	char dataset[192];
	char qs[2048];
	char key[192];
	char label[256];
	char sel_raw[2048];
	char buf[16];
	hyle_option_t opts[PICK_MAX_OPTS];
	hyle_option_t sel[PICK_MAX_SEL];
	int nopts = 0, nsel = 0, total = 0;
	int multi = 0;
	int is_append = 0;
	int page = 0;
	int per_page = PICK_DEFAULT_PER_PAGE;
	char q_buf[256] = { 0 };

	axil_env_get(fd, dataset, sizeof(dataset), "PATTERN_PARAM_ID");
	if (!dataset[0])
		return pick_json_error(fd, 400, "Missing dataset");

	const char *user = get_request_user(fd);
	if (!user || !user[0])
		return pick_json_error(fd, 401, "Unauthorized");

	axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING");

	key[0] = '\0';
	hyle_qs_param(qs, "key", key, sizeof(key));
	if (!key[0] || strlen(key) > 96)
		return pick_json_error(fd, 400, "Missing key");

	label[0] = '\0';
	hyle_qs_param(qs, "label", label, sizeof(label));
	sel_raw[0] = '\0';
	hyle_qs_param(qs, "sel", sel_raw, sizeof(sel_raw));
	buf[0] = '\0';
	hyle_qs_param(qs, "multi", buf, sizeof(buf));
	multi = (buf[0] == '1');

	char pname[256];
	snprintf(pname, sizeof(pname), "pick_q_%s", key);
	hyle_qs_param(qs, pname, q_buf, sizeof(q_buf));

	buf[0] = '\0';
	snprintf(pname, sizeof(pname), "pick_page_%s", key);
	hyle_qs_param(qs, pname, buf, sizeof(buf));
	if (buf[0]) {
		page = atoi(buf);
		if (page < 0)
			page = 0;
		if (page > 10000)
			page = 10000;
	}

	buf[0] = '\0';
	hyle_qs_param(qs, "more", buf, sizeof(buf));
	is_append = (buf[0] == '1');
	if (is_append) {
		buf[0] = '\0';
		hyle_qs_param(qs, "page", buf, sizeof(buf));
		if (buf[0]) {
			page = atoi(buf);
			if (page < 0)
				page = 0;
			if (page > 10000)
				page = 10000;
		}
	}

	if (!hyle_source_find(dataset))
		return pick_json_error(fd, 404, "Unknown dataset");

	nsel = hyle_source_resolve_tokens(
	        dataset, sel_raw, sel, PICK_MAX_SEL, pick_sel_ids,
	        pick_sel_labels);

	nopts = hyle_source_resolve_options(
	        dataset, q_buf, page, per_page, opts, PICK_MAX_OPTS, &total,
	        pick_opt_ids, pick_opt_labels);

	if (is_append) {
		hyle_bud_picker_desc_t d;
		char *rows_esc;
		int eof;

		if (page >= PICK_MAX_SCROLL_PAGES &&
		    (page + 1) * per_page < total)
		{
			return pick_respond_jsonf(
			        fd, "{\"rows\":\"<div class=\\\"hyle-picker-"
			            "refine\\\">Too many results \xe2\x80\x94 "
			            "refine your search.</div>\",\"eof\":1}");
		}

		memset(&d, 0, sizeof(d));
		d.key = key;
		d.label = label;
		d.source = dataset;
		d.multi = multi;
		d.q = q_buf;
		d.page = page;
		d.per_page = per_page;
		d.total = total;
		d.page_opts = opts;
		d.npage = nopts;
		d.sel = sel;
		d.nsel = nsel;

		hyle_bud_picker_rows(&d, pick_rows_buf, sizeof(pick_rows_buf));
		eof = (page + 1) * per_page >= total;

		rows_esc = pick_json_escape(pick_rows_buf);
		if (!rows_esc)
			return pick_json_error(fd, 500, "Out of memory");
		int rc = pick_respond_jsonf(
		        fd, "{\"rows\":\"%s\",\"eof\":%d}", rows_esc,
		        eof ? 1 : 0);
		free(rows_esc);
		return rc;
	}

	{
		hyle_bud_picker_desc_t d;
		char *panel_esc;
		char *values_esc;

		memset(&d, 0, sizeof(d));
		d.key = key;
		d.label = label;
		d.source = dataset;
		d.multi = multi;
		d.q = q_buf;
		d.page = page;
		d.per_page = per_page;
		d.total = total;
		d.page_opts = opts;
		d.npage = nopts;
		d.sel = sel;
		d.nsel = nsel;
		int allow_add = 0;
		buf[0] = '\0';
		hyle_qs_param(qs, "add", buf, sizeof(buf));
		if (!buf[0])
			hyle_qs_param(qs, "allow_add", buf, sizeof(buf));
		if (buf[0] == '1' && hyle_source_is_creatable(dataset))
			allow_add = 1;
		d.allow_add = allow_add;

		hyle_bud_picker_slots(
		        &d, pick_panel_buf, sizeof(pick_panel_buf),
		        pick_values_buf, sizeof(pick_values_buf));

		panel_esc = pick_json_escape(pick_panel_buf);
		values_esc = pick_json_escape(pick_values_buf);
		if (!panel_esc || !values_esc) {
			free(panel_esc);
			free(values_esc);
			return pick_json_error(fd, 500, "Out of memory");
		}
		int rc = pick_respond_jsonf(
		        fd, "{\"slots\":{\"panel\":\"%s\",\"values\":\"%s\"}}",
		        panel_esc, values_esc);
		free(panel_esc);
		free(values_esc);
		return rc;
	}
}

void source_install_pick_routes(void)
{
	axil_register_handler("GET:/pick/:id/options", pick_options_handler);
}