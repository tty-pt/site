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
	const char *sub = submit_label ? ui_t(submit_label) : ui_t("Submit");
	const char *canc = ui_t("Cancel");
	return bud_tpl(
	        "<div class='flex gap-2'>"
	        "  <button type='submit' class='btn btn-primary'>%s</button>"
	        "  %node"
	        "  <a href='%s' class='btn btn-secondary'>%s</a>"
	        "</div>",
	        sub, extra,
	        cancel_href ? cancel_href : "", canc);
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

	const char *prompt = ui_t("Are you sure you want to delete");

	return bud_tpl(
	        "<div class='center'>"
	        "  <p>%s <strong>%s</strong>?</p>"
	        "  <form method='POST' action='%s' "
	        "enctype='multipart/form-data'>"
	        "    <input type='hidden' name='csrf_token' value='%s'/>"
	        "    %node"
	        "  </form>"
	        "</div>",
	        prompt,
	        (title && title[0]) ? title : (id ? id : ""), action_path,
	        csrf_token ? csrf_token : "",
	        site_ui_form_actions(cancel_path, "Delete", NULL));
}

bud_node *site_ui_add_form(
        const char *module, const char *csrf_token, int has_error,
        const char *error_msg)
{
	char action[PATH_MAX];
	char cancel_href[PATH_MAX];
	snprintf(action, sizeof(action), "/%s/add", module);
	snprintf(cancel_href, sizeof(cancel_href), "/%s/", module);

	const char *title_lbl = ui_t("Title:");

	return bud_tpl(
	        "%node"
	        "<form action='%s' method='POST' enctype='multipart/form-data' "
	        "class='flex flex-col gap-4'>"
	        "  <input type='hidden' name='csrf_token' value='%s'/>"
	        "  <label>%s"
	        "    <input name='title'/>"
	        "  </label>"
	        "  %node"
	        "</form>",
	        (has_error && error_msg)
	                ? bud_tpl("<p class='text-error'>%s</p>", error_msg)
	                : NULL,
	        action, csrf_token ? csrf_token : "",
	        title_lbl,
	        site_ui_form_actions(cancel_href, "Add", NULL));
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

/* ── Declarative Schema-Driven Form Builder ──────────────────────── */

bud_node *site_ui_form_from_desc(
        const char *action, const char *cancel_href, const char *submit_label,
        const hyle_schema_desc_t *desc, const void *struct_ptr,
        const char *csrf_token, const pick_view_t *pv, const char *vstr_val)
{
	return hyle_bud_form(
	        desc, struct_ptr, action, cancel_href, submit_label, csrf_token,
	        pv, vstr_val);
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

	int allow_add = spec->allow_add ? (e ? e->allow_add : 1) : 0;
	char url_tmpl_buf[512];
	snprintf(
	        url_tmpl_buf, sizeof(url_tmpl_buf),
	        "/pick/%s/"
	        "options?key=%s&multi=0&add=%d&label=&sel={sel}&pick_q_%s={q}&pick_"
	        "page_%s={page}",
	        (e && e->target) ? e->target : spec->target, spec->key,
	        allow_add ? 1 : 0,
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
		.page_param = pp,
		.allow_add = allow_add
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
	bud_node *form = bud_tpl(
	        "<form action='%s' method='%s' class='flex gap-1 items-center'>"
	        "  %node"
	        "  %node"
	        "  %node"
	        "</form>",
	        action ? action : "", (method && method[0]) ? method : "POST",
	        csrf_token ? bud_tpl("<input type='hidden' name='csrf_token' "
	                             "value='%s'/>",
	                             csrf_token)
	                   : NULL,
	        inputs,
	        (btn_label && btn_label[0])
	                ? bud_tpl("<button type='submit' "
	                          "class='%s'>%s</button>",
	                          btn_class ? btn_class
	                                    : "btn text-xs py-1 px-2",
	                          btn_label)
	                : NULL);
	return form;
}

bud_node *site_ui_item_row(
        const char *title, const char *href, const char *subtitle,
        bud_node *action_controls)
{
	return bud_tpl(
	        "<div class='flex justify-between items-center p-2 bg-surface "
	        "rounded'>"
	        "  <div class='flex flex-col'>"
	        "    %node"
	        "    %node"
	        "  </div>"
	        "  %node"
	        "</div>",
	        (href && href[0])
	                ? bud_tpl("<a class='font-bold' href='%s'>%s</a>", href,
	                          title ? title : "")
	                : bud_tpl("<a class='font-bold'>%s</a>",
	                          title ? title : ""),
	        (subtitle && subtitle[0])
	                ? bud_tpl("<span class='text-xs text-muted'>%s</span>",
	                          subtitle)
	                : NULL,
	        action_controls);
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
	        NULL, key ? key : target, row_idx, cur_id, cur_title,
	        get_action, pv, is_active, extra_class, sibling_out);
}

/* ── Generic Customizable Filter Bar ──────────────────────────────── */

bud_node *site_ui_filter_bar(
        const site_ui_filter_spec_t *specs, int n_specs, const char *action,
        const char *current_q, const pick_view_t *pv)
{
	bud_node *bar = lx_el("form",
	                      lx_attr("class", "hyle-filter-bar flex flex-wrap "
	                                       "gap-2 items-center"),
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
			        lx_el("input", lx_attr("type", "search"),
			              lx_attr("name", s->field),
			              lx_attr("placeholder",
			                      s->label ? ui_t(s->label)
			                               : ui_t("Search…")),
			              lx_attr("class", "border rounded px-2 "
			                               "py-1 text-sm"),
			              (current_q && current_q[0])
			                      ? lx_attr("value", current_q)
			                      : lx_none())
			                .data.node);
		} else if (
		        s->kind == FILTER_SINGLE_DROPDOWN ||
		        s->kind == FILTER_MULTISELECT)
		{
			const pick_entry_t *e = NULL;
			if (pv) {
				for (int pi = 0; pi < pv->n; pi++) {
					if (pv->entries[pi].key &&
					    strcmp(pv->entries[pi].key,
					           s->field) == 0)
					{
						e = &pv->entries[pi];
						break;
					}
				}
			}
			bud_node *f_node = hyle_bud_filter_field(
			        s->field, s->label ? ui_t(s->label) : s->field,
			        (s->kind == FILTER_MULTISELECT)
			                ? HYLE_FIELD_MULTI_REFERENCE
			                : HYLE_FIELD_REFERENCE,
			        s->current_val ? s->current_val : "",
			        e ? e->page_opts : NULL, e ? e->npage : 0,
			        s->filter_style ? s->filter_style : "dropdown");
			if (f_node)
				bud_append(bar, f_node);
		}
	}

	bud_append(
	        bar,
	        lx_el("button", lx_attr("type", "submit"),
	              lx_attr("class", "btn btn-primary text-sm py-1 px-3"),
	              lx_text(ui_t("Filter")))
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
