bud_node *auth_render_register(const char *user)
{
	bud_node *frag =
	        bud_tpl("<form action='/auth/register' method='POST' "
	                "class='flex flex-col gap-4'>"
	                "  <label>Username:"
	                "    <input required name='username'/>"
	                "  </label>"
	                "  <label>Password:"
	                "    <input required name='password' type='password'/>"
	                "  </label>"
	                "  <label>Confirm:"
	                "    <input required name='password2' type='password'/>"
	                "  </label>"
	                "  <label>Email:"
	                "    <input required name='email' type='email'/>"
	                "  </label>"
	                "  <button type='submit'>Register</button>"
	                "</form>");

	return site_ui_form_page(
	        user, "Register", "/auth/register", "🔑", NULL, NULL, frag);
}
