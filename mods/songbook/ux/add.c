static bud_node *
sb_render_add_form(const char *csrf_token, const char *choir_val)
{
	static const form_field_t ff[] = { { "title", "Title:", 0 },
		                           { NULL, NULL, 0 } };

	bud_node *fields = site_ui_form_fields(ff, NULL, csrf_token);
	if (choir_val[0]) {
		bud_append(
		        fields,
		        lx_el("input",
		              lx_attr("type", "hidden"),
		              lx_attr("name", "choir"),
		              lx_attr("value", choir_val))
		                .data.node);
	}
	bud_append(fields, site_ui_form_actions("/songbook/", "Add", NULL));

	return lx_el("form",
	             lx_attr("action", "/songbook/add"),
	             lx_attr("method", "POST"),
	             lx_attr("enctype", "multipart/form-data"),
	             lx_attr("class", "flex flex-col gap-4"),
	             lx_node(fields))
	        .data.node;
}
