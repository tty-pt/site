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

static const pick_entry_t *pick_entry_of(const pick_view_t *pv, const char *key)
{
	int i;

	if (!pv)
		return NULL;
	for (i = 0; i < pv->n; i++) {
		if (pv->entries[i].key && strcmp(pv->entries[i].key, key) == 0)
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
	static char tmpl[512];

	snprintf(
	        tmpl, sizeof(tmpl),
	        "/pick/%s/options?key=%s&multi=%d&label=&sel={sel}"
	        "&pick_q_%s={q}&pick_page_%s={page}",
	        e->target ? e->target : "", e->key, e->multi, e->key, e->key);
	return tmpl;
}

static int ascii_strcasecmp(const char *s1, const char *s2)
{
	if (!s1 || !s2)
		return s1 == s2 ? 0 : (s1 ? 1 : -1);
	while (*s1 && *s2) {
		unsigned char c1 = (unsigned char)*s1;
		unsigned char c2 = (unsigned char)*s2;
		if (c1 >= 'A' && c1 <= 'Z')
			c1 += ('a' - 'A');
		if (c2 >= 'A' && c2 <= 'Z')
			c2 += ('a' - 'A');
		if (c1 != c2)
			return c1 - c2;
		s1++;
		s2++;
	}
	return (unsigned char)*s1 - (unsigned char)*s2;
}

static int ascii_strncasecmp(const char *s1, const char *s2, size_t n)
{
	if (!s1 || !s2 || n == 0)
		return 0;
	while (n && *s1 && *s2) {
		unsigned char c1 = (unsigned char)*s1;
		unsigned char c2 = (unsigned char)*s2;
		if (c1 >= 'A' && c1 <= 'Z')
			c1 += ('a' - 'A');
		if (c2 >= 'A' && c2 <= 'Z')
			c2 += ('a' - 'A');
		if (c1 != c2)
			return c1 - c2;
		s1++;
		s2++;
		n--;
	}
	if (n == 0)
		return 0;
	return (unsigned char)*s1 - (unsigned char)*s2;
}

static int pick_val_has_token(const char *vals, const char *token)
{
	size_t tlen;

	if (!vals || !token || !token[0])
		return 0;
	tlen = strlen(token);
	while (*vals) {
		size_t seg;
		const char *end = strpbrk(vals, ",\n\r");

		seg = end ? (size_t)(end - vals) : strlen(vals);
		while (seg && (vals[0] == ' ' || vals[0] == '\r')) {
			vals++;
			seg--;
		}
		while (seg && (vals[seg - 1] == ' ' || vals[seg - 1] == '\r'))
			seg--;
		if (seg == tlen && (strncmp(vals, token, seg) == 0 ||
		                    ascii_strncasecmp(vals, token, seg) == 0))
			return 1;
		if (!end)
			break;
		vals = end + 1;
		while (*vals && (*vals == ',' || *vals == '\n' ||
		                 *vals == '\r' || *vals == ' '))
			vals++;
	}
	return 0;
}

static int pick_is_selected_opt(
        const pick_entry_t *e, const char *id, const char *label,
        const char *val)
{
	int j;

	if (e && e->nsel > 0) {
		for (j = 0; j < e->nsel; j++) {
			if (e->sel[j].id && id &&
			    (strcmp(e->sel[j].id, id) == 0 ||
			     ascii_strcasecmp(e->sel[j].id, id) == 0))
				return 1;
			if (e->sel[j].label && label &&
			    (strcmp(e->sel[j].label, label) == 0 ||
			     ascii_strcasecmp(e->sel[j].label, label) == 0))
				return 1;
			if (e->sel[j].id && label &&
			    (strcmp(e->sel[j].id, label) == 0 ||
			     ascii_strcasecmp(e->sel[j].id, label) == 0))
				return 1;
		}
	}
	if (val && val[0]) {
		if (id && pick_val_has_token(val, id))
			return 1;
		if (label && pick_val_has_token(val, label))
			return 1;
	}
	return 0;
}

static bud_node *pick_inline_single(const pick_entry_t *e, const char *val)
{
	bud_node *opts = bud_fragment();
	int i;
	int matched = 0;

	bud_append(
	        opts,
	        lx_el("option", lx_attr("value", ""),
	              (!val || !val[0]) ? lx_attr("selected", "") : lx_none(),
	              lx_text("None"))
	                .data.node);
	for (i = 0; i < e->npage; i++) {
		int sel = pick_is_selected_opt(
		        e, e->page_opts[i].id, e->page_opts[i].label, val);
		if (sel)
			matched = 1;
		bud_append(
		        opts,
		        lx_el("option", lx_attr("value", e->page_opts[i].id),
		              sel ? lx_attr("selected", "") : lx_none(),
		              lx_text(e->page_opts[i].label))
		                .data.node);
	}
	/* Current value missing from the list: keep it visible. */
	if (val && val[0] && !matched)
		bud_append(
		        opts, lx_el("option", lx_attr("value", val),
		                    lx_attr("selected", ""), lx_text(val))
		                      .data.node);
	return lx_el("select", lx_attr("name", e->key), lx_node(opts))
	        .data.node;
}

static bud_node *pick_inline_multi(const pick_entry_t *e, const char *val)
{
	bud_node *grid = bud_fragment();
	int i;

	for (i = 0; i < e->npage; i++)
		bud_append(
		        grid, lx_el("label",
		                    lx_attr("class",
		                            "picker-inline hyle-picker-option"),
		                    lx_el("input", lx_attr("type", "checkbox"),
		                          lx_attr("name", e->key),
		                          lx_attr("value", e->page_opts[i].id),
		                          lx_attr("data-filter",
		                                  e->page_opts[i].label),
		                          pick_is_selected_opt(
		                                  e, e->page_opts[i].id,
		                                  e->page_opts[i].label, val)
		                                  ? lx_attr("checked", "")
		                                  : lx_none()),
		                    lx_text(e->page_opts[i].label))
		                      .data.node);
	return grid;
}

static bud_node *pick_ref_node(
        const form_field_t *f, const char *val, const pick_view_t *pv,
        const char *form_id)
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

	if (eff > 0 && e->total <= eff)
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
        const form_field_t *fields, const char **values, const char *csrf_token,
        const pick_view_t *pv)
{
	bud_node *frag = bud_fragment();
	char pick_form_id_buf[192];
	const char *first_ref = site_ui_pick_form_id(fields);

	if (!frag)
		return NULL;
	pick_form_id_buf[0] = '\0';
	if (first_ref)
		snprintf(
		        pick_form_id_buf, sizeof(pick_form_id_buf), "pickq-%s",
		        first_ref);

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
			ctl = pick_ref_node(f, val, pv, pick_form_id_buf);
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

		bud_append(
		        frag, lx_el("label", lx_text(f->label), lx_node(ctl))
		                      .data.node);
	}
	return frag;
}

bud_node *site_ui_sibling_get_form(
        const char *action, const form_field_t *fields, const char **values,
        const pick_view_t *pv)
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
		bud_append(
		        hiddens,
		        lx_el("input", lx_attr("type", "hidden"),
		              lx_attr("name", f->name), lx_attr("value", val))
		                .data.node);
	}
	bud_append(
	        hiddens,
	        lx_el("input", lx_attr("type", "hidden"),
	              lx_attr("name", "per_page"), lx_attr("value", "50"))
	                .data.node);

	return lx_el("form", lx_attr("id", form_id),
	             lx_attr("action", action ? action : ""),
	             lx_attr("method", "GET"),
	             lx_attr("class", "pick-sibling-form"), lx_node(hiddens))
	        .data.node;
}

/* ── Declarative Schema-Driven Form Builder ──────────────────────── */

static const char *site_ui_default_field_label(const char *key)
{
	if (!key || !key[0])
		return "";
	if (strcmp(key, "title") == 0)
		return "Title:";
	if (strcmp(key, "type") == 0)
		return "Type:";
	if (strcmp(key, "author") == 0)
		return "Author:";
	if (strcmp(key, "yt") == 0)
		return "Youtube ID:";
	if (strcmp(key, "audio") == 0)
		return "Audio URL:";
	if (strcmp(key, "pdf") == 0)
		return "PDF URL:";
	if (strcmp(key, "data") == 0)
		return "Chords/Lyrics:";
	if (strcmp(key, "format") == 0)
		return "Format (one per line):";
	if (strcmp(key, "group") == 0 ||
	    (key[0] == 'g' && key[1] == 'r' && key[2] == 'p' && !key[3]))
		return "Group:";
	if (strcmp(key, "body_content") == 0)
		return "Content:";
	return key;
}

bud_node *site_ui_form_from_desc(
        const char *action, const char *cancel_href, const char *submit_label,
        const hyle_schema_desc_t *desc, const void *struct_ptr,
        const char *csrf_token, const pick_view_t *pv, const char *vstr_val)
{
	form_field_t ff[32];
	const char *vals[32];
	char labels[32][64];
	int n = 0;
	int has_ref = 0;

	if (!desc)
		return NULL;

	for (const hyle_schema_desc_t *d = desc; d->key && n < 31; d++) {
		if (strcmp(d->key, "id") == 0 || strcmp(d->key, "owner") == 0 ||
		    strcmp(d->key, "song_source") == 0)
			continue;
		if (!d->writable)
			continue;
		if (d->kind >= BUD_OVERLAY_INT || d->kind == BUD_INVERSE)
			continue;

		ff[n].name = d->key;
		const char *lbl = site_ui_default_field_label(d->key);
		if (lbl == d->key) {
			/* Auto-capitalize */
			snprintf(
			        labels[n], sizeof(labels[n]), "%c%s:",
			        (d->key[0] >= 'a' && d->key[0] <= 'z')
			                ? d->key[0] - 32
			                : d->key[0],
			        d->key + 1);
			ff[n].label = labels[n];
		} else {
			ff[n].label = lbl;
		}

		/* Determine input type */
		if (d->qm_type == BUD_QM_VSTR ||
		    strcmp(d->key, "format") == 0)
		{
			ff[n].type = 1; /* textarea */
		} else if (
		        d->file && !strstr(d->file, ".txt") &&
		        !strstr(d->file, ".html"))
		{
			ff[n].type = 2; /* file */
		} else {
			ff[n].type = 0; /* text */
		}

		/* Reference / Picker handling */
		if (d->source_type == HYLE_BUD_REFERENCE) {
			ff[n].ref = FF_REF_SINGLE;
			ff[n].target = d->ref_source;
			has_ref = 1;
		} else if (d->source_type == HYLE_BUD_MULTI_REFERENCE) {
			ff[n].ref = FF_REF_MULTI;
			ff[n].target = d->ref_source;
			has_ref = 1;
		} else {
			ff[n].ref = FF_REF_NONE;
			ff[n].target = NULL;
		}
		ff[n].max_inline = 0;

		/* Value extraction */
		if (ff[n].type == 2) {
			vals[n] = NULL;
		} else if (d->qm_type == BUD_QM_VSTR && d->offset == 0) {
			vals[n] = vstr_val ? vstr_val : "";
		} else if (struct_ptr && d->source_type != HYLE_BUD_DERIVED) {
			vals[n] = (const char *)struct_ptr + d->offset;
		} else {
			vals[n] = "";
		}

		n++;
	}

	ff[n].name = NULL;
	ff[n].label = NULL;
	ff[n].type = 0;
	ff[n].ref = FF_REF_NONE;
	ff[n].target = NULL;
	ff[n].max_inline = 0;
	vals[n] = NULL;

	bud_node *fields = site_ui_form_fields_ex(ff, vals, csrf_token, pv);
	bud_append(
	        fields, site_ui_form_actions(
	                        cancel_href,
	                        submit_label ? submit_label : "Save", NULL));

	bud_node *form =
	        lx_el("form", lx_attr("action", action ? action : ""),
	              lx_attr("method", "POST"),
	              lx_attr("enctype", "multipart/form-data"),
	              lx_attr("class", "flex flex-col gap-4"), lx_node(fields))
	                .data.node;

	if (has_ref) {
		bud_node *sib = site_ui_sibling_get_form(action, ff, vals, pv);
		if (sib) {
			bud_node *both = bud_fragment();
			bud_append(both, form);
			bud_append(both, sib);
			return both;
		}
	}
	return form;
}

/* ── Action / Standalone Picker Component ────────────────────────── */

bud_node *site_ui_action_picker(
        const site_ui_action_picker_spec_t *spec, const pick_view_t *pv)
{
	bud_node *frag = bud_fragment();
	bud_node *head = NULL, *cancel = NULL, *hint = NULL, *picker = NULL,
	         *post = NULL;
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
		               lx_attr("class", "btn btn-secondary text-xs "
		                                "mb-3 inline-block"),
		               lx_text(spec->cancel_label ? spec->cancel_label
		                                          : "Cancel"))
		                 .data.node;
	}

	if (spec->hint && spec->hint[0]) {
		hint = lx_el("div", lx_attr("class", "text-xs text-muted mb-2"),
		             lx_text(spec->hint))
		               .data.node;
	}

	char form_id_buf[192];
	char search_param_buf[192];
	char page_param_buf[192];
	const char *sp = spec->search_param;
	const char *pp = spec->page_param;

	if (spec->scope && spec->scope[0]) {
		snprintf(
		        form_id_buf, sizeof(form_id_buf), "pickq-%s__%s",
		        spec->key, spec->scope);
		if (!sp) {
			snprintf(
			        search_param_buf, sizeof(search_param_buf),
			        "pick_q_%s__%s", spec->key, spec->scope);
			sp = search_param_buf;
		}
		if (!pp) {
			snprintf(
			        page_param_buf, sizeof(page_param_buf),
			        "pick_page_%s__%s", spec->key, spec->scope);
			pp = page_param_buf;
		}
	} else {
		snprintf(
		        form_id_buf, sizeof(form_id_buf), "pickq-%s",
		        spec->key);
	}

	bud_node *hiddens = bud_fragment();
	if (spec->n_prefs > 0 && spec->pref_names && spec->pref_vals) {
		for (int k = 0; k < spec->n_prefs; k++) {
			char vb[16];
			snprintf(vb, sizeof(vb), "%d", spec->pref_vals[k]);
			bud_append(
			        hiddens,
			        lx_el("input", lx_attr("type", "hidden"),
			              lx_attr("name", spec->pref_names[k]),
			              lx_attr("value", vb))
			                .data.node);
		}
	}
	bud_node *sibling =
	        lx_el("form", lx_attr("id", form_id_buf),
	              lx_attr("action",
	                      spec->get_action ? spec->get_action : ""),
	              lx_attr("method", "GET"),
	              lx_attr("class", "pick-sibling-form"), lx_node(hiddens))
	                .data.node;

	if (pv) {
		for (int i = 0; i < pv->n; i++) {
			if (pv->entries[i].key &&
			    strcmp(pv->entries[i].key, spec->key) == 0)
			{
				e = &pv->entries[i];
				break;
			}
		}
		if (!e && pv->n > 0)
			e = &pv->entries[0];
	}

	hyle_bud_option_t default_sel;
	const hyle_bud_option_t *sel = NULL;
	int nsel = 0;

	if (spec->default_id && spec->default_id[0]) {
		default_sel.id = spec->default_id;
		default_sel.label =
		        (spec->default_label && spec->default_label[0])
		                ? spec->default_label
		                : spec->default_id;
		sel = &default_sel;
		nsel = 1;
	} else if (e) {
		sel = e->sel;
		nsel = e->nsel;
	}

	char url_tmpl_buf[512];
	snprintf(
	        url_tmpl_buf, sizeof(url_tmpl_buf),
	        "/pick/%s/"
	        "options?key=%s&multi=0&label=&sel={sel}&pick_q_%s={q}&pick_"
	        "page_%s={page}",
	        (e && e->target) ? e->target : spec->target, spec->key,
	        spec->key, spec->key);

	hyle_bud_picker_desc_t d = {
		.key = spec->key,
		.label = spec->label ? spec->label : spec->key,
		.source = (e && e->target) ? e->target : spec->target,
		.multi = 0,
		.get_form_id = form_id_buf,
		.url_tmpl = url_tmpl_buf,
		.page_opts = e ? e->page_opts : NULL,
		.npage = e ? e->npage : 0,
		.sel = sel,
		.nsel = nsel,
		.q = (e && e->q) ? e->q : "",
		.page = e ? e->page : 0,
		.per_page = (e && e->per_page > 0) ? e->per_page : 15,
		.total = e ? e->total : 0,
		.search_param = sp,
		.page_param = pp
	};

	picker = hyle_bud_picker_field(&d);
	if (picker && spec->auto_submit) {
		bud_set_attr(picker, "data-hyle-auto-submit", "1");
	}

	post = lx_el("form",
	             lx_attr("id", spec->form_id ? spec->form_id : "pick-post"),
	             lx_attr("method", "post"),
	             lx_attr("class", "flex-1 min-w-0"),
	             lx_attr("action",
	                     spec->post_action ? spec->post_action : ""))
	               .data.node;

	if (spec->csrf_token) {
		bud_append(
		        post, lx_el("input", lx_attr("type", "hidden"),
		                    lx_attr("name", "csrf_token"),
		                    lx_attr("value", spec->csrf_token))
		                      .data.node);
	}

	if (spec->extra_post_inputs) {
		bud_append(post, spec->extra_post_inputs);
	}

	bud_append(
	        post,
	        lx_el("div",
	              lx_attr("class",
	                      "flex gap-2 items-center flex-1 min-w-0"),
	              picker ? lx_node(picker) : lx_none(),
	              lx_el("button", lx_attr("type", "submit"),
	                    lx_attr("class",
	                            "btn btn-primary hyle-picker-submit"),
	                    lx_text(spec->submit_label ? spec->submit_label
	                                               : "Add")))
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

/* ── String-First Action & Replace Picker Components ─────────────── */

static void derive_target_key_label(
        const char *target, char *key_out, size_t key_sz, char *lbl_out,
        size_t lbl_sz)
{
	char stem[64];
	const char *dot = strchr(target, '.');
	size_t len = dot ? (size_t)(dot - target) : strlen(target);
	if (len >= sizeof(stem))
		len = sizeof(stem) - 1;
	memcpy(stem, target, len);
	stem[len] = '\0';

	if (strcmp(stem, "types") == 0) {
		snprintf(key_out, key_sz, "type");
		snprintf(lbl_out, lbl_sz, "Type:");
	} else if (
	        stem[0] == 'g' && stem[1] == 'r' && stem[2] == 'p' && !stem[3])
	{
		snprintf(key_out, key_sz, "%s", stem);
		snprintf(lbl_out, lbl_sz, "Group:");
	} else {
		snprintf(key_out, key_sz, "%s_id", stem);
		snprintf(
		        lbl_out, lbl_sz, "%c%s:",
		        (stem[0] >= 'a' && stem[0] <= 'z') ? stem[0] - 32
		                                           : stem[0],
		        stem + 1);
	}
}

bud_node *site_ui_picker(
        const char *target, const char *post_action, const char *get_action,
        const char *csrf_token, const pick_view_t *pv, int auto_submit)
{
	char key[64], label[64];

	if (!target || !target[0])
		return NULL;

	derive_target_key_label(target, key, sizeof(key), label, sizeof(label));

	site_ui_action_picker_spec_t spec = {
		.key = key,
		.label = label,
		.target = target,
		.get_action = get_action,
		.post_action = post_action,
		.form_id = "pick-post",
		.csrf_token = csrf_token,
		.submit_label = "Add",
		.hint = "Click an item to select it.",
		.auto_submit = auto_submit,
	};
	return site_ui_action_picker(&spec, pv);
}

bud_node *site_ui_row_replace_picker(
        const char *target, int row_idx, const char *cur_id,
        const char *cur_title, const char *post_action, const char *back_href,
        const char *csrf_token, const pick_view_t *pv)
{
	char n_str[16];
	char form_id[64];
	char header_buf[512];
	char key[64], label[64];
	static const char *pref_names[5] = { "t", "b", "l", "m", "z" };

	if (!target || !target[0] || row_idx < 0)
		return NULL;

	derive_target_key_label(target, key, sizeof(key), label, sizeof(label));

	snprintf(n_str, sizeof(n_str), "%d", row_idx);
	snprintf(form_id, sizeof(form_id), "pick-replace-post-%d", row_idx);

	bud_node *extra = bud_fragment();
	bud_append(
	        extra, lx_el("input", lx_attr("type", "hidden"),
	                     lx_attr("name", "n"), lx_attr("value", n_str))
	                       .data.node);
	if (back_href && back_href[0]) {
		bud_append(
		        extra, lx_el("input", lx_attr("type", "hidden"),
		                     lx_attr("name", "back"),
		                     lx_attr("value", back_href))
		                       .data.node);
	}

	snprintf(
	        header_buf, sizeof(header_buf),
	        "Replace #%d  \xe2\x80\x94  Replacing: %s", row_idx + 1,
	        (cur_title && cur_title[0]) ? cur_title
	                                    : (cur_id ? cur_id : ""));

	site_ui_action_picker_spec_t spec = {
		.key = key,
		.label = label,
		.target = target,
		.default_id = cur_id,
		.default_label =
		        (cur_title && cur_title[0]) ? cur_title : cur_id,
		.get_action = back_href,
		.post_action = post_action,
		.form_id = form_id,
		.csrf_token = csrf_token,
		.submit_label = "Replace",
		.cancel_href = back_href,
		.cancel_label = "Cancel",
		.header_text = header_buf,
		.scope = n_str,
		.auto_submit = 1,
		.extra_post_inputs = extra,
	};

	return site_ui_action_picker(&spec, pv);
}

/* ── Reusable Action Form & Item Row Primitives ──────────────────── */

bud_node *site_ui_action_form(
        const char *action, const char *csrf_token, const char *method,
        bud_node *inputs, const char *btn_label, const char *btn_class)
{
	bud_node *form = lx_el("form", lx_attr("action", action ? action : ""),
	                       lx_attr("method",
	                               (method && method[0]) ? method : "POST"),
	                       lx_attr("class", "flex gap-1 items-center"))
	                         .data.node;
	if (csrf_token) {
		bud_append(
		        form, lx_el("input", lx_attr("type", "hidden"),
		                    lx_attr("name", "csrf_token"),
		                    lx_attr("value", csrf_token))
		                      .data.node);
	}
	if (inputs) {
		bud_append(form, inputs);
	}
	if (btn_label && btn_label[0]) {
		bud_append(
		        form,
		        lx_el("button", lx_attr("type", "submit"),
		              lx_attr("class",
		                      btn_class ? btn_class
		                                : "btn text-xs py-1 px-2"),
		              lx_text(btn_label))
		                .data.node);
	}
	return form;
}

bud_node *site_ui_item_row(
        const char *title, const char *href, const char *subtitle,
        bud_node *action_controls)
{
	bud_node *title_link =
	        lx_el("a", lx_attr("class", "font-bold"),
	              (href && href[0]) ? lx_attr("href", href) : lx_none(),
	              lx_text(title ? title : ""))
	                .data.node;

	bud_node *left_col =
	        lx_el("div", lx_attr("class", "flex flex-col"),
	              lx_node(title_link),
	              (subtitle && subtitle[0])
	                      ? lx_el("span",
	                              lx_attr("class", "text-xs text-muted"),
	                              lx_text(subtitle))
	                      : lx_none())
	                .data.node;

	return lx_el("div",
	             lx_attr("class", "flex justify-between items-center p-2 "
	                              "bg-surface rounded"),
	             lx_node(left_col),
	             action_controls ? lx_node(action_controls) : lx_none())
	        .data.node;
}

/* ── Generic Row / Cell Picker Primitives (Entity Pattern) ───────── */

bud_node *site_ui_cell_picker(
        const char *target, const char *key, int row_idx, const char *cur_id,
        const char *cur_title, const char *get_action, const char *post_action,
        const char *csrf_token, const pick_view_t *pv, int is_active,
        const char *extra_class, bud_node **sibling_out)
{
	(void)post_action;
	(void)csrf_token;
	/* Delegate directly to canonical hyle-bud filter */
	return hyle_bud_filter_scoped(
	        NULL, key ? key : target, row_idx, cur_id, cur_title, get_action,
	        pv, is_active, extra_class, sibling_out);
}

/* ── Generic Customizable Filter Bar ──────────────────────────────── */

bud_node *site_ui_filter_bar(
        const site_ui_filter_spec_t *specs, int n_specs, const char *action,
        const char *current_q, const pick_view_t *pv)
{
	bud_node *bar = lx_el("form",
	                      lx_attr("class", "hyle-filter-bar flex flex-wrap gap-2 items-center"),
	                      lx_attr("method", "GET"),
	                      lx_attr("action", action ? action : ""))
	                        .data.node;

	for (int i = 0; i < n_specs; i++) {
		const site_ui_filter_spec_t *s = &specs[i];
		if (!s->field || !s->field[0])
			continue;

		if (s->kind == FILTER_SEARCH) {
			bud_append(
			        bar,
			        lx_el("input",
			              lx_attr("type", "search"),
			              lx_attr("name", s->field),
			              lx_attr("placeholder", s->label ? s->label : "Search\xe2\x80\xa6"),
			              lx_attr("class", "border rounded px-2 py-1 text-sm"),
			              (current_q && current_q[0])
			                      ? lx_attr("value", current_q)
			                      : lx_none())
			                .data.node);
		} else if (s->kind == FILTER_SINGLE_DROPDOWN || s->kind == FILTER_MULTISELECT) {
			const pick_entry_t *e = NULL;
			if (pv) {
				for (int pi = 0; pi < pv->n; pi++) {
					if (pv->entries[pi].key && strcmp(pv->entries[pi].key, s->field) == 0) {
						e = &pv->entries[pi];
						break;
					}
				}
			}
			bud_node *f_node = hyle_bud_filter_field(
			        s->field, s->label ? s->label : s->field,
			        (s->kind == FILTER_MULTISELECT) ? HYLE_BUD_MULTI_REFERENCE : HYLE_BUD_REFERENCE,
			        s->current_val ? s->current_val : "",
			        e ? e->page_opts : NULL, e ? e->npage : 0,
			        s->filter_style ? s->filter_style : "dropdown");
			if (f_node)
				bud_append(bar, f_node);
		}
	}

	bud_append(
	        bar,
	        lx_el("button",
	              lx_attr("type", "submit"),
	              lx_attr("class", "btn btn-primary text-sm py-1 px-3"),
	              lx_text("Filter"))
	                .data.node);

	return bar;
}

/* ── WASM / SSR Picker State JSON Serialization ──────────────────── */

void site_ui_picker_state_from_json(
        const char *json, size_t jlen, const char *key, const char *target,
        int multi, const char *q, int page, site_ui_picker_buffer_t *buf,
        pick_view_t *pv_out)
{
	hyle_bud_picker_state_from_json(
	        json, jlen, key, target, multi, q, page,
	        (hyle_bud_picker_buffer_t *)buf,
	        (hyle_bud_picker_view_t *)pv_out);
}

void site_ui_picker_state_to_json(
        const pick_view_t *pv, struct json_object *j_root)
{
	hyle_bud_picker_state_to_json(
	        (const hyle_bud_picker_view_t *)pv, j_root);
}

/* ── Generic HTML sanitizer (for user-supplied markup) ───────────── */

static const char *find_ci(const char *hay, const char *needle)
{
	size_t nlen = strlen(needle);
	for (; *hay; hay++)
		if (strncasecmp(hay, needle, nlen) == 0)
			return hay;
	return NULL;
}

static void strip_style_blocks(char *buf)
{
	char *s = buf;
	while ((s = (char *)find_ci(s, "<style")) != NULL) {
		const char *close = find_ci(s + 6, "</style>");
		if (!close) {
			*s = '\0';
			break;
		}
		memmove(s, close + 8, strlen(close + 8) + 1);
	}
}

static char *extract_body(const char *raw)
{
	const char *b = find_ci(raw, "<body");
	const char *gt;
	const char *end;
	size_t len;
	char *out;

	if (!b)
		return NULL;
	gt = strchr(b, '>');
	if (!gt)
		return NULL;
	b = gt + 1;
	end = find_ci(b, "</body");
	len = end ? (size_t)(end - b) : strlen(b);
	out = malloc(len + 1);
	if (!out)
		return NULL;
	memcpy(out, b, len);
	out[len] = '\0';
	return out;
}

static int is_dangerous_tag(const char *tag)
{
	static const char *const bad[] = {
		"script", "iframe", "svg",      "object",  "embed",
		"form",   "input",  "button",   "select",  "textarea",
		"link",   "meta",   "style",    "base",    "head",
		"applet", "frame",  "frameset", "noframes"
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		if (strcasecmp(tag, bad[i]) == 0)
			return 1;
	return 0;
}

static int is_safe_attr(const char *name, const char *val)
{
	if (strncasecmp(name, "on", 2) == 0)
		return 0;
	if (strcasecmp(name, "href") == 0 || strcasecmp(name, "src") == 0) {
		if (val && strncasecmp(val, "javascript:", 11) == 0)
			return 0;
		if (val && strncasecmp(val, "data:", 5) == 0)
			return 0;
		if (val && strncasecmp(val, "vbscript:", 9) == 0)
			return 0;
	}
	return 1;
}

static void append_sanitized_str(
        char **buf, size_t *cap, size_t *len, const char *s, size_t slen)
{
	if (*len + slen + 1 >= *cap) {
		*cap = *cap ? *cap * 2 : 1024;
		while (*len + slen + 1 >= *cap)
			*cap *= 2;
		char *nb = realloc(*buf, *cap);
		if (!nb)
			return;
		*buf = nb;
	}
	memcpy(*buf + *len, s, slen);
	*len += slen;
	(*buf)[*len] = '\0';
}

static char *sanitize_html_tags(const char *html)
{
	char *out = NULL;
	size_t cap = 0, len = 0;
	const char *p = html;
	const char *tag_end;
	char tagname[64];

	while (*p) {
		if (*p == '<') {
			tag_end = strchr(p, '>');
			if (!tag_end) {
				append_sanitized_str(
				        &out, &cap, &len, p, strlen(p));
				break;
			}
			int closing = (p[1] == '/');
			const char *name_start = p + (closing ? 2 : 1);
			const char *name_end = name_start;
			while (name_end < tag_end && *name_end != ' ' &&
			       *name_end != '/' && *name_end != '>')
				name_end++;
			size_t name_len = name_end - name_start;
			if (name_len < sizeof(tagname)) {
				memcpy(tagname, name_start, name_len);
				tagname[name_len] = '\0';
			} else {
				tagname[0] = '\0';
			}

			if (is_dangerous_tag(tagname)) {
				if (!closing) {
					const char *close_tag =
					        find_ci(tag_end + 1, "</");
					if (close_tag &&
					    strncasecmp(
					            close_tag + 2, tagname,
					            name_len) == 0)
					{
						const char *gt2 =
						        strchr(close_tag, '>');
						if (gt2)
							p = gt2 + 1;
						else
							p = tag_end + 1;
					} else {
						p = tag_end + 1;
					}
				} else {
					p = tag_end + 1;
				}
				continue;
			}

			char *attr_out = NULL;
			size_t attr_cap = 0, attr_len = 0;
			const char *attr_p = name_end;

			while (attr_p < tag_end) {
				while (attr_p < tag_end &&
				       (*attr_p == ' ' || *attr_p == '\t' ||
				        *attr_p == '\n' || *attr_p == '\r'))
					attr_p++;
				if (attr_p >= tag_end || *attr_p == '/' ||
				    *attr_p == '>')
					break;
				const char *aname = attr_p;
				while (attr_p < tag_end && *attr_p != '=' &&
				       *attr_p != ' ' && *attr_p != '\t' &&
				       *attr_p != '\n' && *attr_p != '\r' &&
				       *attr_p != '/' && *attr_p != '>')
					attr_p++;
				size_t alen = attr_p - aname;
				while (attr_p < tag_end &&
				       (*attr_p == ' ' || *attr_p == '\t' ||
				        *attr_p == '\n' || *attr_p == '\r'))
					attr_p++;
				char *aval = NULL;
				size_t vlen = 0;
				if (attr_p < tag_end && *attr_p == '=') {
					attr_p++;
					while (attr_p < tag_end &&
					       (*attr_p == ' ' ||
					        *attr_p == '\t' ||
					        *attr_p == '\n' ||
					        *attr_p == '\r'))
						attr_p++;
					if (attr_p < tag_end &&
					    (*attr_p == '"' || *attr_p == '\''))
					{
						char quote = *attr_p++;
						const char *vstart = attr_p;
						while (attr_p < tag_end &&
						       *attr_p != quote)
							attr_p++;
						vlen = attr_p - vstart;
						aval = malloc(vlen + 1);
						if (aval) {
							memcpy(aval, vstart,
							       vlen);
							aval[vlen] = '\0';
						}
						if (attr_p < tag_end)
							attr_p++;
					} else {
						const char *vstart = attr_p;
						while (attr_p < tag_end &&
						       *attr_p != ' ' &&
						       *attr_p != '\t' &&
						       *attr_p != '\n' &&
						       *attr_p != '\r' &&
						       *attr_p != '/' &&
						       *attr_p != '>')
							attr_p++;
						vlen = attr_p - vstart;
						aval = malloc(vlen + 1);
						if (aval) {
							memcpy(aval, vstart,
							       vlen);
							aval[vlen] = '\0';
						}
					}
				}

				if (is_safe_attr(aname, aval ? aval : "")) {
					append_sanitized_str(
					        &attr_out, &attr_cap, &attr_len,
					        " ", 1);
					append_sanitized_str(
					        &attr_out, &attr_cap, &attr_len,
					        aname, alen);
					if (aval) {
						append_sanitized_str(
						        &attr_out, &attr_cap,
						        &attr_len, "=\"", 2);
						append_sanitized_str(
						        &attr_out, &attr_cap,
						        &attr_len, aval, vlen);
						append_sanitized_str(
						        &attr_out, &attr_cap,
						        &attr_len, "\"", 1);
					}
				}
				free(aval);
			}

			if (closing) {
				append_sanitized_str(&out, &cap, &len, "</", 2);
				append_sanitized_str(
				        &out, &cap, &len, tagname, name_len);
				append_sanitized_str(&out, &cap, &len, ">", 1);
			} else {
				append_sanitized_str(&out, &cap, &len, "<", 1);
				append_sanitized_str(
				        &out, &cap, &len, tagname, name_len);
				if (attr_out && attr_len > 0) {
					append_sanitized_str(
					        &out, &cap, &len, attr_out,
					        attr_len);
				}
				if (tag_end[-1] == '/')
					append_sanitized_str(
					        &out, &cap, &len, " /", 2);
				append_sanitized_str(&out, &cap, &len, ">", 1);
			}
			free(attr_out);
			p = tag_end + 1;
		} else {
			const char *next_lt = strchr(p, '<');
			size_t txt_len =
			        next_lt ? (size_t)(next_lt - p) : strlen(p);
			append_sanitized_str(&out, &cap, &len, p, txt_len);
			p += txt_len;
		}
	}
	return out;
}

char *site_ui_sanitize_html(const char *raw)
{
	if (!raw)
		return strdup("");
	char *body = extract_body(raw);
	char *sanitized =
	        body ? sanitize_html_tags(body) : sanitize_html_tags(raw);
	if (body)
		free(body);
	if (!sanitized)
		return strdup("");
	strip_style_blocks(sanitized);
	return sanitized;
}

#endif
