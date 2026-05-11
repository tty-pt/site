bud_node *song_form_content(
        int is_edit,
        const char *id,
        const song_cache_t *meta,
        const char *data_val,
        const char *csrf_token)
{
	char action[256];
	char cancel_href[256];
	if (is_edit) {
		snprintf(action, sizeof(action), "/song/%s/edit", id);
		snprintf(cancel_href, sizeof(cancel_href), "/song/%s", id);
	} else {
		snprintf(action, sizeof(action), "/song/add");
		snprintf(cancel_href, sizeof(cancel_href), "/song/");
	}

	static const form_field_t ff[] = {
		{ "title", "Title:", 0 },        { "type", "Type:", 1 },
		{ "author", "Author:", 0 },      { "yt", "Youtube ID:", 0 },
		{ "audio", "Audio URL:", 0 },    { "pdf", "PDF URL:", 0 },
		{ "data", "Chords/Lyrics:", 1 }, { NULL, NULL, 0 }
	};

	const char *vals[7];
	for (int i = 0; ff[i].name; i++) {
		if (strcmp(ff[i].name, "data") == 0) {
			vals[i] = data_val;
		} else if (meta) {
			for (const bud_field_desc_t *f = song_fields; f->key;
			     f++)
			{
				if (strcmp(f->key, ff[i].name) == 0) {
					vals[i] =
					        (const char *)meta + f->offset;
					break;
				}
			}
			if (!vals[i])
				vals[i] = "";
		} else {
			vals[i] = "";
		}
	}

	bud_node *fields = site_ui_form_fields(ff, vals, csrf_token);
	bud_append(fields, site_ui_form_actions(cancel_href, "Save", NULL));

	return lx_el("form",
	             lx_attr("action", action),
	             lx_attr("method", "POST"),
	             lx_attr("enctype", "multipart/form-data"),
	             lx_attr("class", "flex flex-col gap-4"),
	             lx_node(fields))
	        .data.node;
}
