static bud_node *ch_render_add_form(const char *csrf_token)
{
	static const form_field_t ff[] = {
		{ "title", "Title:", 0 },
		{ "format", "Format (one per line):", 1 },
		{ NULL, NULL, 0 }
	};

	bud_node *fields = site_ui_form_fields(ff, NULL, csrf_token);
	bud_append(fields, site_ui_form_actions("/choir/", "Add", NULL));

	return lx_el("form",
	             lx_attr("action", "/choir/add"),
	             lx_attr("method", "POST"),
	             lx_attr("enctype", "multipart/form-data"),
	             lx_attr("class", "flex flex-col gap-4"),
	             lx_node(fields))
	        .data.node;
}
