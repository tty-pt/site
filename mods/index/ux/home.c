static bud_node *idx_home_layout(
        const char *user,
        const char **mod_names,
        const char **mod_titles,
        size_t mod_count)
{
	bud_node *frag =
	        lx_el("div", lx_attr("class", "center flex flex-col gap-2"))
	                .data.node;
	size_t i;

	for (i = 0; i < mod_count; i++) {
		char href[256];
		site_ui_collection_path(mod_names[i], href, sizeof(href));
		bud_append(
		        frag,
		        lx_el("a",
		              lx_attr("href", href),
		              lx_attr("class", "btn"),
		              lx_text(mod_titles[i]))
		                .data.node);
	}

	return site_ui_form_page(
	        user, "tty.pt", "/", "\xf0\x9f\x8f\xa0", NULL, frag);
}
