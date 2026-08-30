bud_node *
auth_render_login(const char *user, const char *ret, const char *error)
{
	const char *user_lbl = ui_t("Username:");
	const char *pass_lbl = ui_t("Password:");
	const char *submit_lbl = ui_t("Sign in");
	const char *login_title = ui_t("Sign in");

	bud_node *frag = bud_tpl(
	        "%node"
	        "<form action='/auth/login' method='POST' class='flex flex-col "
	        "gap-4'>"
	        "  <label>%s"
	        "    <input required name='username'/>"
	        "  </label>"
	        "  <label>%s"
	        "    <input required name='password' type='password'/>"
	        "  </label>"
	        "  %node"
	        "  <button type='submit'>%s</button>"
	        "</form>",
	        (error && error[0]) ? bud_tpl("<p class='error'>%s</p>", error)
	                            : NULL,
	        user_lbl, pass_lbl,
	        (ret && ret[0]) ? bud_tpl("<input type='hidden' name='ret' "
	                                  "value='%s'/>",
	                                  ret)
	                        : NULL,
	        submit_lbl);

	return site_ui_form_page(
	        user, login_title, "/auth/login", "\xf0\x9f\x94\x91", NULL, NULL, frag);
}
