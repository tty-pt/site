#ifndef SITE_FORMS_C
#define SITE_FORMS_C

#include "site_ui.h"

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

#endif
