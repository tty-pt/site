bud_node *auth_render_register(const char *user)
{
	const char *user_lbl = ui_t("Username:");
	const char *pass_lbl = ui_t("Password:");
	const char *confirm_lbl = ui_t("Confirm password:");
	const char *submit_lbl = ui_t("Register");
	const char *reg_title = ui_t("Register");

	bud_node *frag =
	        bud_tpl("<form action='/auth/register' method='POST' "
	                "class='flex flex-col gap-4'>"
	                "  <label>%s"
	                "    <input required name='username'/>"
	                "  </label>"
	                "  <label>%s"
	                "    <input required name='password' type='password'/>"
	                "  </label>"
	                "  <label>%s"
	                "    <input required name='password2' type='password'/>"
	                "  </label>"
	                "  <label>Email:"
	                "    <input required name='email' type='email'/>"
	                "  </label>"
	                "  <button type='submit'>%s</button>"
	                "</form>",
	                user_lbl, pass_lbl, confirm_lbl, submit_lbl);

	return site_ui_form_page(
	        user, reg_title, "/auth/register", "\xf0\x9f\x93\x9d", NULL, NULL, frag);
}
