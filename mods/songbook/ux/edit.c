typedef struct {
	int orig_key;
	char repo_id[256];
	char title[256];
	char transpose[16];
	char format[32];
} sb_edit_row_t;

typedef struct {
	const char *id;
	const char *title;
} sb_repo_opt_t;

typedef struct {
	const char *id;
	const char *title;
} sb_choir_opt_t;

static bud_node *sb_render_edit_form(
        const char *action,
        const char *csrf_token,
        const char *title,
        const char *choir_id,
        int n_choirs,
        const sb_choir_opt_t *choirs,
        const char *cancel_href,
        int n_songs,
        const sb_edit_row_t *songs,
        int n_options,
        const sb_repo_opt_t *options,
        int n_format_opts,
        const char **format_opts,
        const char *song_source)
{
	int total = n_songs + 1;
	bud_node *rows = bud_fragment();
	char amount_str[16];
	snprintf(amount_str, sizeof(amount_str), "%d", total);

	/* Build choir dropdown options */
	bud_node *choir_opts = bud_fragment();
	bud_append(
	        choir_opts,
	        lx_el("option",
	              lx_attr("value", ""),
	              (!choir_id || !choir_id[0]) ? lx_attr("selected", "")
	                                          : lx_none(),
	              lx_text("None"))
	                .data.node);
	for (int ci = 0; ci < n_choirs; ci++) {
		const sb_choir_opt_t *c = &choirs[ci];
		if (!c->id)
			break;
		bud_append(
		        choir_opts,
		        lx_el("option",
		              lx_attr("value", c->id),
		              (choir_id && strcmp(choir_id, c->id) == 0)
		                      ? lx_attr("selected", "")
		                      : lx_none(),
		              lx_text(c->title))
		                .data.node);
	}
	bud_node *choir_select = lx_el("select",
	                               lx_attr("name", "choir"),
		lx_attr("class", "border rounded p-1 w-60"),
	                               lx_node(choir_opts))
	                                 .data.node;

	for (int i = 0; i < total; i++) {
			char song_f[32], key_f[32], fmt_f[32], remove_f[32];
			snprintf(song_f, sizeof(song_f), "song_%d", i);
			snprintf(key_f, sizeof(key_f), "key_%d", i);
			snprintf(fmt_f, sizeof(fmt_f), "fmt_%d", i);
			snprintf(remove_f, sizeof(remove_f), "remove_%d", i);

			int is_existing = (i < n_songs);
			int is_add_row = (i == n_songs);

			const char *cur =
			        is_existing ? songs[i].repo_id : NULL;
			int cur_key = is_existing ? atoi(songs[i].transpose) : 0;
			const char *f_val = (is_existing && songs[i].format[0])
			                        ? songs[i].format
			                        : "";
			int orig_key = is_existing ? songs[i].orig_key : 0;

		/* Message when no songs are available for selection */
		if (is_add_row && n_options == 0) {
			if (song_source && strcmp(song_source, "repertoire") == 0 &&
			    choir_id && choir_id[0]) {
				bud_append(
				        rows,
				        lx_el("p",
				              lx_attr("class",
				                      "text-muted"),
				              lx_text("This choir's "
				                      "repertoire is "
				                      "empty \u2014 add "
				                      "songs there "
				                      "first."))
				                .data.node);
			} else {
				bud_append(
				        rows,
				        lx_el("p",
				              lx_attr("class",
				                      "text-muted"),
				              lx_text("No songs in the "
				                      "system yet."))
				                .data.node);
			}
			continue;
		}

			bud_node *opts = NULL;
			for (int j = 0; j < n_options; j++) {
				char val[512];
				snprintf(
				        val,
				        sizeof(val),
				        "%s [%s]",
				        options[j].title,
				        options[j].id);
				bud_node *o =
				        lx_el("option",
				              lx_attr("value", val),
				              (cur &&
				               strcmp(options[j].id, cur) == 0)
				                      ? lx_attr("selected", "")
				                      : lx_none(),
				              lx_text(options[j].title))
				                .data.node;
				if (!opts)
					opts = bud_fragment();
				bud_append(opts, o);
			}
			if (!opts && cur && is_existing) {
				char val[512];
				snprintf(
				        val,
				        sizeof(val),
				        "%s [%s]",
				        songs[i].title,
				        cur);
				bud_node *o =
				        lx_el("option",
				              lx_attr("value", val),
				              lx_attr("selected", ""),
				              lx_text(songs[i].title))
				                .data.node;
				opts = bud_fragment();
				bud_append(opts, o);
			}

			bud_node *select =
			        lx_el("select",
			              lx_attr("name", song_f),
	lx_attr("class", "border rounded p-1 w-60"),
			              opts ? lx_node(opts) : lx_none())
			                .data.node;

			/* Key selector: -11 to +11 with key name labels */
			bud_node *key_opts = NULL;
			for (int si = -11; si <= 11; si++) {
				char v[16];
				snprintf(v, sizeof(v), "%d", si);
				bud_node *o =
				        lx_el("option",
				              lx_attr("value", v),
				              si == cur_key
				                      ? lx_attr("selected", "")
				                      : lx_none(),
				              lx_text(key_name(si, orig_key, 0, 0)))
				                .data.node;
				if (!key_opts)
					key_opts = bud_fragment();
				bud_append(key_opts, o);
			}
			/* Fallback if cur_key is outside -11..11 */
			if (cur_key < -11 || cur_key > 11) {
				char v[16];
				snprintf(v, sizeof(v), "%d", cur_key);
				bud_node *o =
				        lx_el("option",
				              lx_attr("value", v),
				              lx_attr("selected", ""),
				              lx_text(songs[i].transpose))
				                .data.node;
				bud_append(key_opts, o);
			}

			bud_node *key_select =
			        lx_el("select",
			              lx_attr("name", key_f),
			              lx_attr("class",
			                      "border rounded p-1 w-24"),
			              lx_node(key_opts))
			                .data.node;

			/* Format selector: dropdown or text input */
			bud_node *fmt_ctl = NULL;
			if (n_format_opts > 0) {
				bud_node *fmt_opts = bud_fragment();
				for (int fi = 0; fi < n_format_opts; fi++) {
					bud_append(
					        fmt_opts,
					        lx_el("option",
					              lx_attr("value",
					                      format_opts[fi]),
					              (f_val[0] &&
					               strcmp(f_val,
					                      format_opts[fi]) == 0)
					                      ? lx_attr("selected",
					                                "")
					                      : lx_none(),
					              lx_text(format_opts[fi]))
					                .data.node);
				}
				/* Fallback when current format not in list */
				if (f_val[0]) {
					int found = 0;
					for (int fi = 0; fi < n_format_opts; fi++) {
						if (strcmp(
						            f_val,
						            format_opts[fi]) == 0) {
							found = 1;
							break;
						}
					}
					if (!found) {
						bud_append(
						        fmt_opts,
						        lx_el("option",
						              lx_attr("value", f_val),
						              lx_attr("selected",
						                      ""),
						              lx_text(f_val))
						                .data.node);
					}
				}
				fmt_ctl =
				        lx_el("select",
				              lx_attr("name", fmt_f),
				              lx_attr("class",
				                      "border rounded p-1 w-32"),
				              lx_node(fmt_opts))
				                .data.node;
			} else {
				fmt_ctl =
				        lx_el("input",
				              lx_attr("type", "text"),
				              lx_attr("name", fmt_f),
				              lx_attr("class",
				                      "border rounded p-1 w-24"),
				              lx_attr("placeholder",
				                      "Format"),
				              (f_val[0] &&
				               strcmp(f_val, "any") != 0)
				                      ? lx_attr("value", f_val)
				                      : lx_none())
				                .data.node;
			}

			bud_node *row =
			        lx_el("div",
			              lx_attr("class",
			                      "flex gap-2 items-center"),
			              lx_node(select),
			              lx_node(key_select),
			              lx_node(fmt_ctl),
			              is_existing
			                      ? lx_el("label",
			                              lx_attr("class",
			                                      "text-sm "
			                                      "cursor-pointer"),
			                              lx_el("input",
			                                    lx_attr("type",
			                                            "checkbox"),
			                                    lx_attr("name",
			                                            remove_f)),
			                              lx_text(" Remove"))
			                      : lx_none())
			                .data.node;

			bud_append(rows, row);
	}

	return lx_el("form",
	             lx_attr("action", action),
	             lx_attr("method", "POST"),
	             lx_attr("enctype", "multipart/form-data"),
	             lx_attr("class", "flex flex-col gap-4"),
	             lx_el("input",
	                   lx_attr("type", "hidden"),
	                   lx_attr("name", "csrf_token"),
	                   lx_attr("value", csrf_token)),
	             lx_el("input",
	                   lx_attr("type", "hidden"),
	                   lx_attr("name", "amount"),
	                   lx_attr("value", amount_str)),
	             lx_el("label",
	                   lx_text("Title:"),
	                   lx_el("input",
	                         lx_attr("type", "text"),
	                         lx_attr("name", "title"),
	                         (title && title[0]) ? lx_attr("value", title)
	                                             : lx_none())),
	lx_el("label", lx_text("Choir:"), lx_node(choir_select)),
	             (choir_id && choir_id[0])
	                     ? lx_el("label",
	                             lx_text("Song source:"),
	                             lx_el("select",
	                                   lx_attr("name",
	                                           "song_source"),
	                                   lx_attr("class",
	                                           "border rounded "
	                                           "p-1 w-48"),
	                                   lx_el("option",
	                                         lx_attr("value",
	                                                 "all"),
	                                         (!song_source ||
	                                          strcmp(song_source,
	                                                 "all") == 0)
	                                                 ? lx_attr(
	                                                           "selected",
	                                                           "")
	                                                 : lx_none(),
	                                         lx_text("All "
	                                                 "songs")),
	                                   lx_el("option",
	                                         lx_attr("value",
	                                                 "repertoire"),
	                                         (song_source &&
	                                          strcmp(song_source,
	                                                 "repertoire") ==
	                                                  0)
	                                                 ? lx_attr(
	                                                           "selected",
	                                                           "")
	                                                 : lx_none(),
	                                         lx_text("Repertoire "
	                                                 "only"))))
	                     : lx_none(),
	             lx_el("h3", lx_attr("class", "mt-2"), lx_text("Songs")),
	             lx_node(rows),
	             lx_node(site_ui_form_actions(
	                     cancel_href, "Save Changes", NULL)))
	        .data.node;
}
