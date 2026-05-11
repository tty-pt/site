static bud_node *sb_render_empty_list(void)
{
	return lx_el("p",
	             lx_attr("class", "text-muted"),
	             lx_text("No songs yet."))
	        .data.node;
}

static bud_node *sb_render_add_song_form(
        const char *add_action, const char *csrf_token, bud_node *song_options)
{
	return lx_el("form",
	             lx_attr("method", "POST"),
	             lx_attr("action", add_action),
	             lx_attr("class",
	                     "flex gap-2 items-center "
	                     "mt-2 mb-4 p-2 "
	                     "bg-surface rounded"),
	             lx_el("input",
	                   lx_attr("type", "hidden"),
	                   lx_attr("name", "csrf_token"),
	                   lx_attr("value", csrf_token)),
	             lx_el("input",
	                   lx_attr("type", "text"),
	                   lx_attr("name", "song_id"),
	                   lx_attr("list", "sb-song-datalist"),
	                   lx_attr("placeholder", "Search songs..."),
	                   lx_attr("autocomplete", "off"),
	                   lx_attr("class", "border rounded p-1")),
	             lx_el("datalist",
	                   lx_attr("id", "sb-song-datalist"),
	                   song_options ? lx_node(song_options) : lx_none()),
	             lx_el("button",
	                   lx_attr("type", "submit"),
	                   lx_attr("class", "btn text-sm py-1 px-2"),
	                   lx_text("Add Song")))
	        .data.node;
}

static bud_node *sb_render_song_row(
        const char *s_title,
        const char *song_href,
        const char *tgt_key,
        int is_owner,
        const char *csrf_token,
        const char *rem_action,
        const char *tpose_action,
        const char *rand_action,
        const char *t_str,
        const char *chord_html,
        int orig_key,
        int flags,
        const char *n_buf,
        bud_node **out_pre)
{
	int t = t_str ? atoi(t_str) : 0;
	int bemol = (flags & TRANSP_BEMOL) ? 1 : 0;
	int latin = (flags & TRANSP_LATIN) ? 1 : 0;
	bud_node *key_opts = NULL;
	for (int i = -11; i <= 11; i++) {
		char v[16];
		snprintf(v, sizeof(v), "%d", i);
		bud_node *o =
		        lx_el("option",
		              lx_attr("value", v),
		              i == t ? lx_attr("selected", "") : lx_none(),
		              lx_text(key_name(i, orig_key, bemol, latin)))
		                .data.node;
		if (!key_opts)
			key_opts = bud_fragment();
		bud_append(key_opts, o);
	}

	/* Always create the <pre> node and its raw child (even with empty
	 * content on WASM) so the hydrated tree structure matches the
	 * server-rendered DOM and node IDs stay aligned. */
	bud_node *chord_pre =
	        lx_el("pre",
	              lx_attr("data-songbook-chord-data", n_buf),
	              lx_attr("class",
	                      "whitespace-pre-wrap "
	                      "font-mono text-xs "
	                      "mt-1 p-2 rounded "
	                      "chord-data"),
	              lx_node(bud_raw(chord_html ? chord_html : "")))
	                .data.node;
	if (out_pre)
		*out_pre = chord_pre;

	return lx_el("div",
	             lx_attr("data-songbook-item", ""),
	             lx_attr("class",
	                     "flex flex-col gap-1 p-2 bg-surface rounded"),
	             lx_el("div",
	                   lx_attr("class",
	                           "flex justify-between items-center"),
	                   lx_el("div",
	                         lx_attr("class", "flex flex-col"),
	                         lx_el("a",
	                               lx_attr("class", "font-bold"),
	                               song_href ? lx_attr("href", song_href)
	                                         : lx_none(),
	                               lx_text(s_title)),
	                         lx_el("span",
	                               lx_attr("data-songbook-target-key", ""),
	                               lx_attr("class", "text-xs text-muted"),
	                               lx_text(tgt_key))),
	                   is_owner
	                           ? lx_el("div",
	                                   lx_attr("class", "flex gap-2"),
	                                   lx_el("form",
	                                         lx_attr("method", "POST"),
	                                         lx_attr("action",
	                                                 tpose_action),
	                                         lx_attr("class",
	                                                 "flex "
	                                                 "gap-1 "
	                                                 "items-"
	                                                 "center"),
	                                         lx_el("input",
	                                               lx_attr("type",
	                                                       "hidden"),
	                                               lx_attr("name",
	                                                       "csrf_"
	                                                       "token"),
	                                               lx_attr("value",
	                                                       csrf_token)),
	                                         lx_el("input",
	                                               lx_attr("type",
	                                                       "hidden"),
	                                               lx_attr("name", "n"),
	                                               lx_attr("value", n_buf)),
	                                         lx_el("select",
	                                               lx_attr("name", "t"),
	                                               lx_attr("data-n", n_buf),
	                                               lx_attr("class",
	                                                       "border "
	                                                       "rounded "
	                                                       "p-1 "
	                                                       "text-xs"),
	                                               lx_bind("change",
	                                                       0,
	                                                       on_sb_transpose_change),
	                                               lx_node(key_opts)),
	                                         lx_el("button",
	                                               lx_attr("type",
	                                                       "submit"),
	                                               lx_attr("data-wasm-hide",
	                                                       ""),
	                                               lx_attr("class",
	                                                       "btn "
	                                                       "text-xs "
	                                                       "py-1 "
	                                                       "px-2"),
	                                               lx_text("Set "
	                                                       "Key"))),
	                                   lx_el("form",
	                                         lx_attr("method", "POST"),
	                                         lx_attr("action", rand_action),
	                                         lx_attr("enctype",
	                                                 "multipart/"
	                                                 "form-data"),
	                                         lx_attr("style",
	                                                 "display:"
	                                                 "inline"),
	                                         lx_el("input",
	                                               lx_attr("type",
	                                                       "hidden"),
	                                               lx_attr("name",
	                                                       "csrf_"
	                                                       "token"),
	                                               lx_attr("value",
	                                                       csrf_token)),
	                                         lx_el("input",
	                                               lx_attr("type",
	                                                       "hidden"),
	                                               lx_attr("name", "n"),
	                                               lx_attr("value", n_buf)),
	                                         lx_el("button",
	                                               lx_attr("type",
	                                                       "submit"),
	                                               lx_attr("class",
	                                                       "btn "
	                                                       "text-xs "
	                                                       "py-1 "
	                                                       "px-2"),
	                                               lx_text("🎲"))),
	                                   lx_el("form",
	                                         lx_attr("method", "POST"),
	                                         lx_attr("action", rem_action),
	                                         lx_el("input",
	                                               lx_attr("type",
	                                                       "hidden"),
	                                               lx_attr("name",
	                                                       "csrf_"
	                                                       "token"),
	                                               lx_attr("value",
	                                                       csrf_token)),
	                                         lx_el("button",
	                                               lx_attr("type",
	                                                       "submit"),
	                                               lx_attr("data-testid",
	                                                       "remove-song-"
	                                                       "btn"),
	                                               lx_attr("class",
	                                                       "btn "
	                                                       "btn-"
	                                                       "danger "
	                                                       "text-xs "
	                                                       "py-1 "
	                                                       "px-2"),
	                                               lx_text("🗑"))))
	                           : lx_none()),
	             lx_node(chord_pre))
	        .data.node;
}

static bud_node *
sb_render_header(const char *title, const char *choir_href, const char *owner)
{
	bud_node *frag = bud_fragment();
	if (!frag)
		return NULL;

	bud_append(frag, lx_el("h1", lx_text(title)).data.node);

	if (owner && owner[0]) {
		bud_append(
		        frag,
		        lx_el("p",
		              lx_attr("class", "text-sm text-muted"),
		              lx_frag(lx_text("by "), lx_text(owner)))
		                .data.node);
	}

	if (choir_href && choir_href[0]) {
		bud_append(
		        frag,
		        lx_el("a",
		              lx_attr("href", choir_href),
		              lx_attr("class", "text-sm text-link"),
		              lx_text("\xe2\x86\xa9 back to choir"))
		                .data.node);
	}

	return frag;
}

static bud_node *sb_render_song_option(const char *value, const char *text)
{
	return lx_el("option", lx_attr("value", value), lx_text(text))
	        .data.node;
}

/* ── Body content builder (called from bud_app_render in shared.c) ── */

static bud_node *sb_build_body_content(void)
{
	bud_node *frag = bud_fragment();
	char choir_href[256] = { 0 };

	/* Header */
	if (sb_app_state.choir_id[0])
		snprintf(
		        choir_href,
		        sizeof(choir_href),
		        "/choir/%s",
		        sb_app_state.choir_id);
	{
		bud_node *hdr = sb_render_header(
		        sb_app_state.title, choir_href, sb_app_state.owner);
		if (hdr)
			bud_append(frag, hdr);
	}

	/* Add-song form (shown when logged in + choir + options available) */
	if (sb_app_state.choir_id[0] && sb_app_state.user[0] &&
	    sb_app_state.n_song_options > 0)
	{
		char add_action[256];
		snprintf(
		        add_action,
		        sizeof(add_action),
		        "/api/songbook/%s/songs",
		        sb_app_state.sb_id);

		bud_node *song_options = NULL;
		for (int j = 0; j < sb_app_state.n_song_options; j++) {
			bud_node *opt = sb_render_song_option(
			        g_sb_options[j].id, g_sb_options[j].title);
			if (!song_options)
				song_options = lx_frag(lx_node(opt)).data.node;
			else
				bud_append(song_options, opt);
		}

		bud_append(
		        frag,
		        sb_render_add_song_form(
		                add_action,
		                sb_app_state.csrf_token,
		                song_options));
	}

	/* Song list */
	if (sb_app_state.n_songs == 0) {
		bud_append(frag, sb_render_empty_list());
	} else {
		for (int i = 0; i < sb_app_state.n_songs; i++) {
			sb_song_row_data_t *s = &g_sb_songs[i];

			char n_buf[16], song_href[256], tgt_key[32];
			char rem_action[256], tpose_action[256],
			        rand_action[256], t_str[16];

			snprintf(n_buf, sizeof(n_buf), "%d", i);

			if (s->song_id[0])
				snprintf(
				        song_href,
				        sizeof(song_href),
				        "/song/%s",
				        s->song_id);
			else
				song_href[0] = '\0';

			snprintf(
			        tgt_key,
			        sizeof(tgt_key),
			        "%s",
			        target_key_name(
			                s->orig_key, s->transpose, s->flags));

			snprintf(
			        rem_action,
			        sizeof(rem_action),
			        "/api/songbook/%s/song/%d/remove",
			        sb_app_state.sb_id,
			        i);
			snprintf(
			        tpose_action,
			        sizeof(tpose_action),
			        "/api/songbook/%s/song/%d/transpose",
			        sb_app_state.sb_id,
			        i);
			snprintf(
			        rand_action,
			        sizeof(rand_action),
			        "/songbook/%s/randomize",
			        sb_app_state.sb_id);

			snprintf(t_str, sizeof(t_str), "%d", s->transpose);

			bud_node *pre_ptr = NULL;
			bud_node *row = sb_render_song_row(
			        s->title,
			        song_href[0] ? song_href : NULL,
			        tgt_key,
			        sb_app_state.is_owner,
			        sb_app_state.csrf_token,
			        rem_action,
			        tpose_action,
			        rand_action,
			        t_str,
			        s->chord_html,
			        s->orig_key,
			        s->flags,
			        n_buf,
			        &pre_ptr);

			if (pre_ptr && g_sb_n_chord_nodes < MAX_SB_SONGS)
				g_sb_chord_nodes[g_sb_n_chord_nodes++] =
				        pre_ptr;

			bud_append(frag, row);
		}
	}

	return frag;
}
