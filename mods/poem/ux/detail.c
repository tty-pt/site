static bud_node *poem_render_detail_body(const char *content, const char *owner)
{
	char owner_text[128] = { 0 };
	if (owner && owner[0])
		snprintf(owner_text, sizeof(owner_text), "By %s", owner);

	return lx_el("div",
	             lx_attr("class", "flex flex-col gap-4"),
	             lx_el("div",
	                   lx_attr("class",
	                           "poem-body bg-surface p-6 rounded "
	                           "shadow-sm font-serif leading-relaxed"),
	                   lx_node(bud_raw(content))),
	             (owner && owner[0])
	                     ? lx_el("div",
	                             lx_attr("class",
	                                     "text-sm text-muted text-right"),
	                             lx_text(owner_text))
	                     : lx_none())
	        .data.node;
}
