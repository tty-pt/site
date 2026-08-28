bud_node *
auth_render_login(const char *user, const char *ret, const char *error)
{
	bud_node *frag = bud_tpl(
	        "%node"
	        "<form action='/auth/login' method='POST' class='flex flex-col "
	        "gap-4'>"
	        "  <label>Username:"
	        "    <input required name='username'/>"
	        "  </label>"
	        "  <label>Password:"
	        "    <input required name='password' type='password'/>"
	        "  </label>"
	        "  %node"
	        "  <button type='submit'>Login</button>"
	        "</form>",
	        (error && error[0]) ? bud_tpl("<p class='error'>%s</p>", error)
	                            : NULL,
	        (ret && ret[0]) ? bud_tpl("<input type='hidden' name='ret' "
	                                  "value='%s'/>",
	                                  ret)
	                        : NULL);

	return site_ui_form_page(
	        user, "Login", "/auth/login", "🔑", NULL, NULL, frag);
}
