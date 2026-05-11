bud_node *auth_render_register(const char *user)
{
	bud_node *frag =
	        lx_frag(lx_el("form",
	                      lx_attr("action", "/auth/register"),
	                      lx_attr("method", "POST"),
	                      lx_attr("class", "flex flex-col gap-4"),
	                      lx_el("label",
	                            lx_text("Username:"),
	                            lx_el("input",
	                                  lx_attr("required", ""),
	                                  lx_attr("name", "username"))),
	                      lx_el("label",
	                            lx_text("Password:"),
	                            lx_el("input",
	                                  lx_attr("required", ""),
	                                  lx_attr("name", "password"),
	                                  lx_attr("type", "password"))),
	                      lx_el("label",
	                            lx_text("Confirm:"),
	                            lx_el("input",
	                                  lx_attr("required", ""),
	                                  lx_attr("name", "password2"),
	                                  lx_attr("type", "password"))),
	                      lx_el("label",
	                            lx_text("Email:"),
	                            lx_el("input",
	                                  lx_attr("required", ""),
	                                  lx_attr("name", "email"),
	                                  lx_attr("type", "email"))),
	                      lx_el("button",
	                            lx_attr("type", "submit"),
	                            lx_text("Register"))))
	                .data.node;

	return site_ui_form_page(
	        user, "Register", "/auth/register", "📝", NULL, frag);
}
