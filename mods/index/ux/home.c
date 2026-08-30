static bud_node *idx_home_layout(
        const char *user, const char **mod_names, const char **mod_titles,
        size_t mod_count)
{
	bud_node *frag =
	        bud_tpl("<div class='center flex flex-col gap-2'></div>");
	size_t i;

	for (i = 0; i < mod_count; i++) {
		char href[256];
		site_ui_collection_path(mod_names[i], href, sizeof(href));
		bud_node *link = bud_tpl(
		        "<a href='%s' class='btn'>%s</a>", href,
		        ui_t(mod_titles[i]));
		if (link)
			bud_append(frag, link);
	}

	return site_ui_form_page(user, "tty.pt", "/", "🏠", NULL, NULL, frag);
}
