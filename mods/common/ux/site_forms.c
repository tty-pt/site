#ifndef SITE_FORMS_C
#define SITE_FORMS_C

#include "site_ui.h"
#include "bud/bud_app.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bud/bud_jsx.h"
bud_node *site_ui_form_actions(
        const char *cancel_href, const char *submit_label, bud_node *extra)
{
	return lx_el("div", lx_attr("class", "flex gap-2"),
	             lx_el("button", lx_attr("type", "submit"),
	                   lx_attr("class", "btn btn-primary"),
	                   lx_text(submit_label)),
	             extra ? lx_node(extra) : lx_none(),
	             lx_el("a", lx_attr("href", cancel_href),
	                   lx_attr("class", "btn btn-secondary"),
	                   lx_text("Cancel")))
	        .data.node;
}

bud_node *site_ui_delete_confirm(
        const char *module, const char *id, const char *title,
        const char *csrf_token)
{
	char action_path[PATH_MAX];
	char cancel_path[PATH_MAX];
	item_action_path(
	        module, id, "delete", action_path, sizeof(action_path));
	site_ui_item_path(module, id, cancel_path, sizeof(cancel_path));

	return lx_el("div", lx_attr("class", "center"),
	             lx_el("p", lx_text("Are you sure you want to delete "),
	                   lx_el("strong",
	                         lx_text((title && title[0]) ? title : id)),
	                   lx_text("?")),
	             lx_el("form", lx_attr("method", "POST"),
	                   lx_attr("action", action_path),
	                   lx_attr("enctype", "multipart/form-data"),
	                   lx_el("input", lx_attr("type", "hidden"),
	                         lx_attr("name", "csrf_token"),
	                         lx_attr("value", csrf_token)),
	                   lx_node(site_ui_form_actions(
	                           cancel_path, "Delete", NULL))))
	        .data.node;
}

bud_node *site_ui_add_form(
        const char *module, const char *csrf_token, int has_error,
        const char *error_msg)
{
	char action[PATH_MAX];
	char cancel_href[PATH_MAX];
	snprintf(action, sizeof(action), "/%s/add", module);
	snprintf(cancel_href, sizeof(cancel_href), "/%s/", module);

	return lx_frag((has_error && error_msg)
	                       ? lx_el("p", lx_attr("class", "text-error"),
	                               lx_text(error_msg))
	                       : lx_none(),
	               lx_el("form", lx_attr("action", action),
	                     lx_attr("method", "POST"),
	                     lx_attr("enctype", "multipart/form-data"),
	                     lx_attr("class", "flex flex-col gap-4"),
	                     lx_el("input", lx_attr("type", "hidden"),
	                           lx_attr("name", "csrf_token"),
	                           lx_attr("value", csrf_token)),
	                     lx_el("label", lx_text("Title:"),
	                           lx_el("input", lx_attr("name", "title"))),
	                     lx_node(site_ui_form_actions(
	                             cancel_href, "Add", NULL))))
	        .data.node;
}

static bud_node *site_ui_textarea_value(const char *value)
{
	const char *src = value ? value : "";
	size_t len = strlen(src);
	char *escaped;
	char *dst;

	if (len > (SIZE_MAX - 1) / 6)
		return bud_raw("");
	escaped = malloc(len * 6 + 1);
	if (!escaped)
		return bud_raw("");
	dst = escaped;
	while (*src) {
		const char *entity = NULL;
		size_t entity_len = 0;

		switch (*src) {
		case '&':
			entity = "&amp;";
			entity_len = 5;
			break;
		case '<':
			entity = "&lt;";
			entity_len = 4;
			break;
		case '>':
			entity = "&gt;";
			entity_len = 4;
			break;
		default:
			*dst++ = *src++;
			continue;
		}
		memcpy(dst, entity, entity_len);
		dst += entity_len;
		src++;
	}
	*dst = '\0';

	bud_node *node = bud_raw(escaped);
	free(escaped);
	return node;
}

bud_node *site_ui_form_fields(
        const form_field_t *fields, const char **values, const char *csrf_token)
{
	bud_node *frag = bud_fragment();
	if (!frag)
		return NULL;

	bud_append(
	        frag, lx_el("input", lx_attr("type", "hidden"),
	                    lx_attr("name", "csrf_token"),
	                    lx_attr("value", csrf_token))
	                      .data.node);

	for (const form_field_t *f = fields; f->name; f++) {
		const char *val = values ? values[f - fields] : NULL;
		if (f->type == 2) {
			bud_append(
			        frag,
			        lx_el("label", lx_text(f->label),
			              lx_el("input", lx_attr("type", "file"),
			                    lx_attr("name", f->name)))
			                .data.node);
		} else if (f->type == 1) {
			bud_append(
			        frag,
			        lx_el("label", lx_text(f->label),
			              lx_el("textarea",
			                    lx_attr("name", f->name),
			                    lx_attr("class",
			                            "font-mono w-full"),
			                    lx_node(site_ui_textarea_value(
			                            val))))
			                .data.node);
		} else {
			bud_append(
			        frag,
			        lx_el("label", lx_text(f->label),
			              lx_el("input", lx_attr("type", "text"),
			                    lx_attr("name", f->name),
			                    (val && val[0])
			                            ? lx_attr("value", val)
			                            : lx_none()))
			                .data.node);
		}
	}
	return frag;
}

/* ── Omnisearch picker support ───────────────────────────────────── */

#define PICK_QS_BUDGET 2048

static const pick_entry_t *pick_entry_of(
        const pick_view_t *pv, const char *key)
{
	int i;

	if (!pv)
		return NULL;
	for (i = 0; i < pv->n; i++) {
		if (pv->entries[i].key && strcmp(pv->entries[i].key, key)
		                == 0)
			return &pv->entries[i];
	}
	return NULL;
}

/* First ref-field key names the one sibling GET form on the page. */
const char *site_ui_pick_form_id(const form_field_t *fields)
{
	const form_field_t *f;

	if (!fields)
		return NULL;
	for (f = fields; f->name; f++) {
		if (f->ref != FF_REF_NONE)
			return f->name;
	}
	return NULL;
}

/* Fragment URL template; {q} {page} {sel} are JS substitution slots
 * and the trailing ctx params carry the server-side defaults. */
static const char *pick_url_tmpl(const pick_entry_t *e)
{
#ifndef __wasm__
	static __thread char tmpl[512];
#else
	static char tmpl[512];
#endif

	snprintf(tmpl, sizeof(tmpl),
	        "/pick/%s/options?key=%s&multi=%d&label=&sel={sel}"
	        "&pick_q_%s={q}&pick_page_%s={page}",
	        e->target ? e->target : "", e->key, e->multi, e->key,
	        e->key);
	return tmpl;
}

static int pick_val_has_token(const char *vals, const char *token)
{
	size_t tlen;

	if (!vals || !token || !token[0])
		return 0;
	tlen = strlen(token);
	while (*vals) {
		size_t seg;
		const char *end = strpbrk(vals, ",\n");

		seg = end ? (size_t)(end - vals) : strlen(vals);
		while (seg && (vals[0] == ' ' || vals[0] == '\r')) {
			vals++;
			seg--;
		}
		while (seg && (vals[seg - 1] == ' ' || vals[seg - 1] == '\r'))
			seg--;
		if (seg == tlen && strncmp(vals, token, seg) == 0)
			return 1;
		if (!end)
			break;
		vals = end + 1;
	}
	return 0;
}

static bud_node *pick_inline_single(const pick_entry_t *e,
        const char *val)
{
	bud_node *opts = bud_fragment();
	int i;
	int matched = 0;

	bud_append(opts, lx_el("option", lx_attr("value", ""),
	                       (!val || !val[0]) ? lx_attr("selected", "")
	                                         : lx_none(),
	                       lx_text("None"))
	                         .data.node);
	for (i = 0; i < e->npage; i++) {
		int sel = val && val[0] && strcmp(val, e->page_opts[i].id)
		                                  == 0;
		if (sel)
			matched = 1;
		bud_append(opts,
		        lx_el("option", lx_attr("value", e->page_opts[i].id),
		                sel ? lx_attr("selected", "") : lx_none(),
		                lx_text(e->page_opts[i].label))
		                .data.node);
	}
	/* Current value missing from the list: keep it visible. */
	if (val && val[0] && !matched)
		bud_append(opts,
		        lx_el("option", lx_attr("value", val),
		                lx_attr("selected", ""), lx_text(val))
		                .data.node);
	return lx_el("select", lx_attr("name", e->key),
	             lx_node(opts))
	            .data.node;
}

static bud_node *pick_inline_multi(
        const pick_entry_t *e, const char *val)
{
	bud_node *grid = bud_fragment();
	int i;

	for (i = 0; i < e->npage; i++)
		bud_append(grid,
		        lx_el("label", lx_attr("class", "picker-inline"),
                lx_el("input",
                       lx_attr("type", "checkbox"),
                       lx_attr("name", e->key),
                       lx_attr("value", e->page_opts[i].id),
                       lx_attr("data-filter", e->page_opts[i].label),
                       pick_val_has_token(val,
                               e->page_opts[i].id)
                               ? lx_attr("checked", "")
                               : lx_none()),
                lx_text(e->page_opts[i].label))
                .data.node);
	return grid;
}

static bud_node *pick_ref_node(const form_field_t *f, const char *val,
        const pick_view_t *pv, const char *form_id)
{
	const pick_entry_t *e = pick_entry_of(pv, f->name);
	int eff = f->max_inline > 0 ? f->max_inline : FF_PICKER_THRESHOLD;

	/* No view data (wasm render / unreadable target): plain text
	 * input — no dead dropdowns, ever. */
	if (!e || !e->key || !e->target || !e->target[0])
		return lx_el("input", lx_attr("type", "text"),
		              lx_attr("name", f->name),
		              (val && val[0]) ? lx_attr("value", val)
		                              : lx_none())
		            .data.node;

	if (e->total <= eff)
		return e->multi ? pick_inline_multi(e, val)
		                : pick_inline_single(e, val);

	return hyle_bud_picker_field(
	        &(hyle_bud_picker_desc_t){ .key = f->name,
	                .label = f->label,
	                .source = e->target,
	                .multi = e->multi,
	                .get_form_id = form_id,
	                .url_tmpl = pick_url_tmpl(e),
	                .page_opts = e->page_opts,
	                .npage = e->npage,
	                .sel = e->sel,
	                .nsel = e->nsel,
	                .q = e->q,
	                .page = e->page,
	                .per_page = e->per_page,
	                .total = e->total });
}

bud_node *site_ui_form_fields_ex(
        const form_field_t *fields, const char **values,
        const char *csrf_token, const pick_view_t *pv)
{
	bud_node *frag = bud_fragment();
	char pick_form_id_buf[192];
	const char *first_ref = site_ui_pick_form_id(fields);

	if (!frag)
		return NULL;
	pick_form_id_buf[0] = '\0';
	if (first_ref)
		snprintf(pick_form_id_buf, sizeof(pick_form_id_buf),
		        "pickq-%s", first_ref);

	/* Pages that manage their own CSRF embed it themselves; NULL
	 * skips the hidden input here. */
	if (csrf_token)
		bud_append(
		        frag, lx_el("input", lx_attr("type", "hidden"),
		                    lx_attr("name", "csrf_token"),
		                    lx_attr("value", csrf_token))
		                      .data.node);

	for (const form_field_t *f = fields; f->name; f++) {
		const char *val = values ? values[f - fields] : NULL;
		bud_node *ctl;

		if (f->ref != FF_REF_NONE && pv)
			ctl = pick_ref_node(f, val, pv,
			        pick_form_id_buf);
		else if (f->type == 2)
			ctl = lx_el("input", lx_attr("type", "file"),
			              lx_attr("name", f->name))
			            .data.node;
		else if (f->type == 1)
			ctl = lx_el("textarea", lx_attr("name", f->name),
			              lx_attr("class", "font-mono w-full"),
			              lx_node(site_ui_textarea_value(val)))
			            .data.node;
		else
			ctl = lx_el("input", lx_attr("type", "text"),
			              lx_attr("name", f->name),
			              (val && val[0]) ? lx_attr("value", val)
			                              : lx_none())
			            .data.node;

		bud_append(frag, lx_el("label", lx_text(f->label),
		                              lx_node(ctl))
		                        .data.node);
	}
	return frag;
}

bud_node *site_ui_sibling_get_form(
        const char *action, const form_field_t *fields,
        const char **values, const pick_view_t *pv)
{
	bud_node *hiddens = bud_fragment();
	size_t budget = PICK_QS_BUDGET;
	const char *form_key = site_ui_pick_form_id(fields);
	char form_id[192];
	const form_field_t *f;

	if (!form_key)
		return NULL;
	snprintf(form_id, sizeof(form_id), "pickq-%s", form_key);

	(void)pv;
	for (f = fields; f->name; f++) {
		const char *val = values ? values[f - fields] : NULL;

		if (f->type == 2 || !val)
			continue;
		budget -= strlen(f->name) + strlen(val) + 16;
		if (budget < 0)
			break;
		bud_append(hiddens,
		        lx_el("input", lx_attr("type", "hidden"),
		                lx_attr("name", f->name),
		                lx_attr("value", val))
		                .data.node);
	}
	bud_append(hiddens,
	        lx_el("input", lx_attr("type", "hidden"),
	              lx_attr("name", "per_page"), lx_attr("value", "50"))
	                .data.node);

	return lx_el("form", lx_attr("id", form_id),
	              lx_attr("action", action ? action : ""),
	              lx_attr("method", "GET"),
	              lx_attr("class", "pick-sibling-form"),
	              lx_node(hiddens))
	            .data.node;
}

/* ── Action / Standalone Picker Component ────────────────────────── */

bud_node *site_ui_action_picker(
        const site_ui_action_picker_spec_t *spec, const pick_view_t *pv)
{
	bud_node *frag = bud_fragment();
	bud_node *head = NULL, *cancel = NULL, *hint = NULL, *picker = NULL, *post = NULL;
	const pick_entry_t *e = NULL;

	if (!frag || !spec || !spec->key || !spec->target)
		return NULL;

	if (spec->header_text && spec->header_text[0]) {
		head = lx_el("div", lx_attr("class", "mb-2 font-medium"),
		             lx_text(spec->header_text))
		               .data.node;
	}

	if (spec->cancel_href && spec->cancel_href[0]) {
		cancel = lx_el("a", lx_attr("href", spec->cancel_href),
		               lx_attr("class",
		                       "btn btn-secondary text-xs mb-3 inline-block"),
		               lx_text(spec->cancel_label ? spec->cancel_label : "Cancel"))
		                 .data.node;
	}

	if (spec->hint && spec->hint[0]) {
		hint = lx_el("div", lx_attr("class", "text-xs text-muted mb-2"),
		             lx_text(spec->hint))
		               .data.node;
	}

	form_field_t ff[2] = {
		{ spec->key, spec->label ? spec->label : spec->key, 0, FF_REF_SINGLE, spec->target, 0 },
		{ NULL, NULL, 0, 0, NULL, 0 }
	};
	const char *vals[1] = { NULL };
	bud_node *sibling = site_ui_sibling_get_form(
	        spec->get_action, ff, vals, pv);

	if (sibling && spec->n_prefs > 0 && spec->pref_names && spec->pref_vals) {
		for (int k = 0; k < spec->n_prefs; k++) {
			char vb[16];
			snprintf(vb, sizeof(vb), "%d", spec->pref_vals[k]);
			bud_append(
			        sibling,
			        lx_el("input", lx_attr("type", "hidden"),
			              lx_attr("name", spec->pref_names[k]),
			              lx_attr("value", vb))
			                .data.node);
		}
	}

	if (pv) {
		for (int i = 0; i < pv->n; i++) {
			if (pv->entries[i].key &&
			    strcmp(pv->entries[i].key, spec->key) == 0) {
				e = &pv->entries[i];
				break;
			}
		}
		if (!e && pv->n > 0)
			e = &pv->entries[0];
	}

	char form_id_buf[192];
	snprintf(form_id_buf, sizeof(form_id_buf), "pickq-%s", spec->key);

	if (e) {
		picker = hyle_bud_picker_field(
		        &(hyle_bud_picker_desc_t){ .key = spec->key,
		                .label = spec->label ? spec->label : spec->key,
		                .source = e->target ? e->target : spec->target,
		                .multi = e->multi,
		                .get_form_id = form_id_buf,
		                .url_tmpl = pick_url_tmpl(e),
		                .page_opts = e->page_opts,
		                .npage = e->npage,
		                .sel = e->sel,
		                .nsel = e->nsel,
		                .q = e->q,
		                .page = e->page,
		                .per_page = e->per_page,
		                .total = e->total,
		                .search_param = spec->search_param,
		                .page_param = spec->page_param });
	}

	post = lx_el("form",
	             lx_attr("id", spec->form_id ? spec->form_id : "pick-post"),
	             lx_attr("method", "post"),
	             lx_attr("action", spec->post_action ? spec->post_action : ""))
	               .data.node;

	if (spec->csrf_token) {
		bud_append(post,
		           lx_el("input", lx_attr("type", "hidden"),
		                 lx_attr("name", "csrf_token"),
		                 lx_attr("value", spec->csrf_token))
		                   .data.node);
	}

	if (spec->extra_post_inputs) {
		bud_append(post, spec->extra_post_inputs);
	}

	bud_append(post,
	           lx_el("div", lx_attr("class", "flex gap-2 items-start"),
	                 picker ? lx_node(picker) : lx_none(),
	                 lx_el("button", lx_attr("type", "submit"),
	                       lx_attr("class", "btn btn-primary hyle-picker-submit mt-6"),
	                       lx_text(spec->submit_label ? spec->submit_label : "Add")))
	                   .data.node);

	if (head)
		bud_append(frag, head);
	if (cancel)
		bud_append(frag, cancel);
	if (hint)
		bud_append(frag, hint);
	if (sibling)
		bud_append(frag, sibling);
	if (post)
		bud_append(frag, post);

	return frag;
}

/* ── WASM / SSR Picker State JSON Serialization ──────────────────── */

void site_ui_picker_state_from_json(
        const char *json, size_t jlen,
        const char *key, const char *target, int multi,
        const char *q, int page,
        site_ui_picker_buffer_t *buf, pick_view_t *pv_out)
{
	hyle_bud_picker_state_from_json(
	        json, jlen, key, target, multi, q, page,
	        (hyle_bud_picker_buffer_t *)buf,
	        (hyle_bud_picker_view_t *)pv_out);
}

#ifndef __wasm__
#if __has_include(<json-c/json.h>)
#include <json-c/json.h>
void site_ui_picker_state_to_json(
        const pick_view_t *pv, struct json_object *j_root)
{
	hyle_bud_picker_state_to_json(
	        (const hyle_bud_picker_view_t *)pv, j_root);
}
#endif
#endif

#endif
