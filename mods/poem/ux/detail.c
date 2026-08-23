static bud_node *poem_render_detail_body(
        const char *content, const char *owner, const char *id)
{
	char owner_text[128] = { 0 };
	char body_src[192];

	if (owner && owner[0])
		snprintf(owner_text, sizeof(owner_text), "By %s", owner);
	snprintf(body_src, sizeof(body_src), "/poem/%s/pt_PT.html", id);

	return lx_el("div",
	             lx_attr("class", "flex flex-col gap-4"),
	             (content && content[0])
	                     ? lx_el("iframe",
	                             lx_attr("src", body_src),
	                             lx_attr("class",
	                                     "poem-body w-full border rounded"),
	                             lx_attr("style",
	                                     "width:100%;height:85vh;border:"
	                                     "0"),
	                             lx_attr("title", "poem body"))
	                     : lx_el("p", lx_attr("class", "text-muted"),
	                             lx_text("No content uploaded yet.")),
	             (owner && owner[0])
	                     ? lx_el("div",
	                             lx_attr("class",
	                                     "text-sm text-muted text-right"),
	                             lx_text(owner_text))
	                     : lx_none())
	        .data.node;
}
