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
	        "    %node"
	        "    %node"
	        "  </form>"
	        "</div>",
	        prompt,
	        (title && title[0]) ? title : (id ? id : ""), action_path,
	        csrf_token ? bud_hidden_input("csrf_token", csrf_token) : NULL,
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
	        "  %node"
	        "  <label>%s"
	        "    <input name='title'/>"
	        "  </label>"
	        "  %node"
	        "</form>",
	        (has_error && error_msg)
	                ? bud_tpl("<p class='text-error'>%s</p>", error_msg)
	                : NULL,
	        action,
	        csrf_token ? bud_hidden_input("csrf_token", csrf_token) : NULL,
	        title_lbl,
	        site_ui_form_actions(cancel_href, "Add", NULL));
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

	hyle_bud_action_picker_spec_t spec = {
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
	return hyle_bud_action_picker(&spec, pv);
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
	bud_append(extra, bud_hidden_input_int("n", row_idx));
	if (back_href && back_href[0]) {
		bud_append(extra, bud_hidden_input("back", back_href));
	}

	snprintf(
	        header_buf, sizeof(header_buf),
	        "Replace #%d  \xe2\x80\x94  Replacing: %s", row_idx + 1,
	        (cur_title && cur_title[0]) ? cur_title
	                                    : (cur_id ? cur_id : ""));

	hyle_bud_action_picker_spec_t spec = {
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

	return hyle_bud_action_picker(&spec, pv);
}

/* ── Reusable Action Form & Item Row Primitives ──────────────────── */

bud_node *site_ui_action_form(
        const char *action, const char *csrf_token, const char *method,
        bud_node *inputs, const char *btn_label, const char *btn_class)
{
	bud_node *form = bud_element("form");
	if (!form)
		return NULL;
	bud_set_attr(form, "action", action ? action : "");
	bud_set_attr(form, "method", (method && method[0]) ? method : "POST");
	bud_set_attr(form, "class", "flex gap-1 items-center");
	if (csrf_token)
		bud_append(form, bud_hidden_input("csrf_token", csrf_token));
	if (inputs)
		bud_append(form, inputs);
	if (btn_label && btn_label[0])
		bud_append(
		        form,
		        bud_submit_btn(
		                btn_label,
		                btn_class ? btn_class : "btn text-xs py-1 px-2"));
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
	bud_node *bar = lx_n("form",
	                     lx_attr("class", "hyle-filter-bar flex flex-wrap "
	                                      "gap-2 items-center"),
	                     lx_attr("method", "GET"),
	                     lx_attr("action", action ? action : ""));

	for (int i = 0; i < n_specs; i++) {
		const site_ui_filter_spec_t *s = &specs[i];
		if (!s->field || !s->field[0])
			continue;

		if (s->kind == FILTER_SEARCH) {
			bud_append(
			        bar,
			        lx_n("input", lx_attr("type", "search"),
			             lx_attr("name", s->field),
			             lx_attr("placeholder",
			                     s->label ? ui_t(s->label)
			                              : ui_t("Search…")),
			             lx_attr("class", "border rounded px-2 "
			                              "py-1 text-sm"),
			             (current_q && current_q[0])
			                     ? lx_attr("value", current_q)
			                     : lx_none()));
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
	        lx_n("button", lx_attr("type", "submit"),
	             lx_attr("class", "btn btn-primary text-sm py-1 px-3"),
	             lx_textf("%s", ui_t("Filter"))));

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

SITE_UI_WEAK void site_ui_picker_state_to_json(
        const pick_view_t *pv, struct json_object *j_root)
{
	hyle_bud_picker_state_to_json(
	        (const hyle_bud_picker_view_t *)pv, j_root);
}

#include "../site_sanitize.h"

#endif
