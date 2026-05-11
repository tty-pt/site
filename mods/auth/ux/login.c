bud_node *
auth_render_login(const char *user, const char *ret, const char *error)
{
	bud_node *frag =
	        lx_frag((error && error[0]) ? lx_el("p",
	                                            lx_attr("class", "error"),
	                                            lx_text(error))
	                                    : lx_none(),
	                lx_el("form",
	                      lx_attr("action", "/auth/login"),
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
	                      (ret && ret[0]) ? lx_el("input",
	                                              lx_attr("type", "hidden"),
	                                              lx_attr("name", "ret"),
	                                              lx_attr("value", ret))
	                                      : lx_none(),
	                      lx_el("button",
	                            lx_attr("type", "submit"),
	                            lx_text("Login"))))
	                .data.node;

	return site_ui_form_page(
	        user, "Login", "/auth/login", "🔑", NULL, frag);
}
