bud_node *poem_form_content(
        int is_edit,
        const char *id,
        const poem_cache_t *meta,
        const char *csrf_token)
{
	char action[256];
	char cancel_href[256];
	if (is_edit) {
		snprintf(action, sizeof(action), "/poem/%s/edit", id);
		snprintf(cancel_href, sizeof(cancel_href), "/poem/%s", id);
	} else {
		snprintf(action, sizeof(action), "/poem/add");
		snprintf(cancel_href, sizeof(cancel_href), "/poem/");
	}

	const char *title_val = NULL;
	if (meta) {
		title_val = (const char *)meta + offsetof(poem_cache_t, title);
	}

	static const form_field_t ff[] = {
		{ "title", "title", 0 },
		{ "body_content", "body_content", 2 },
		{ NULL, NULL, 0 }
	};
	const char *vals[2] = { title_val, NULL };

	bud_node *fields = site_ui_form_fields(ff, vals, csrf_token);
	bud_append(fields, site_ui_form_actions(cancel_href, "Save", NULL));

	return lx_el("form",
	             lx_attr("action", action),
	             lx_attr("method", "POST"),
	             lx_attr("enctype", "multipart/form-data"),
	             lx_attr("class", "flex flex-col gap-4"),
	             lx_node(fields))
	        .data.node;
}
