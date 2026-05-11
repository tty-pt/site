static bud_node *ch_render_edit_form(
        const char *action,
        const char *csrf_token,
        const char *title,
        const char *format,
        const char *cancel_href)
{
	static const form_field_t ff[] = {
		{ "title", "Title:", 0 },
		{ "format", "Format (one per line):", 1 },
		{ NULL, NULL, 0 }
	};
	const char *vals[2] = { title, format };
	bud_node *fields = site_ui_form_fields(ff, vals, csrf_token);
	bud_append(
	        fields,
	        site_ui_form_actions(cancel_href, "Save Changes", NULL));

	return lx_el("form",
	             lx_attr("action", action),
	             lx_attr("method", "POST"),
	             lx_attr("enctype", "multipart/form-data"),
	             lx_attr("class", "flex flex-col gap-4"),
	             lx_node(fields))
	        .data.node;
}
