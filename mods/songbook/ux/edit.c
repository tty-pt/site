typedef struct {
	const char *repo_id;
	const char *title;
	const char *transpose;
	const char *format;
} sb_edit_row_t;

typedef struct {
	const char *id;
	const char *title;
} sb_repo_opt_t;

#define SB_EDIT_EMPTY_ROWS 3

static bud_node *sb_render_edit_form(
        const char *action,
        const char *csrf_token,
        const char *title,
        const char *cancel_href,
        int n_songs,
        const sb_edit_row_t *songs,
        int n_options,
        const sb_repo_opt_t *options)
{
	int total = n_songs + SB_EDIT_EMPTY_ROWS;
	bud_node *rows = bud_fragment();
	char amount_str[16];
	snprintf(amount_str, sizeof(amount_str), "%d", total);

	if (n_options == 0) {
		bud_append(
		        rows,
		        lx_el("p",
		              lx_attr("class", "text-muted"),
		              lx_text("No songs available. Add songs to the "
		                      "choir repertoire first."))
		                .data.node);
	} else {
		for (int i = 0; i < total; i++) {
			char song_f[32], key_f[32], fmt_f[32];
			snprintf(song_f, sizeof(song_f), "song_%d", i);
			snprintf(key_f, sizeof(key_f), "key_%d", i);
			snprintf(fmt_f, sizeof(fmt_f), "fmt_%d", i);

			const char *cur =
			        (i < n_songs) ? songs[i].repo_id : NULL;
			const char *k = (i < n_songs && songs[i].transpose)
			                        ? songs[i].transpose
			                        : "";
			const char *f = (i < n_songs && songs[i].format)
			                        ? songs[i].format
			                        : "";

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

			bud_node *select =
			        lx_el("select",
			              lx_attr("name", song_f),
			              lx_attr("class", "border rounded p-1"),
			              opts ? lx_node(opts) : lx_none())
			                .data.node;

			bud_append(
			        rows,
			        lx_el("div",
			              lx_attr("class",
			                      "flex gap-2 items-center"),
			              lx_node(select),
			              lx_el("input",
			                    lx_attr("type", "text"),
			                    lx_attr("name", key_f),
			                    lx_attr("class",
			                            "border rounded p-1 w-16"),
			                    lx_attr("placeholder", "Key"),
			                    k[0] ? lx_attr("value", k)
			                         : lx_none()),
			              lx_el("input",
			                    lx_attr("type", "text"),
			                    lx_attr("name", fmt_f),
			                    lx_attr("class",
			                            "border rounded p-1 w-24"),
			                    lx_attr("placeholder", "Format"),
			                    (f[0] && strcmp(f, "any") != 0)
			                            ? lx_attr("value", f)
			                            : lx_none()))
			                .data.node);
		}
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
	             lx_el("h3", lx_attr("class", "mt-2"), lx_text("Songs")),
	             lx_node(rows),
	             lx_node(site_ui_form_actions(
	                     cancel_href, "Save Changes", NULL)))
	        .data.node;
}
