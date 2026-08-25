#include "bud/bud.h"
#include "bud/bud_jsx.h"
#include "bud/bud_app.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../common/ux/site_ui.c"
#include "../fields.h"

/* type is a multi-reference picker over the shared song.types dataset
 * (schema already MULTI_REFERENCE); stored values are newline-joined
 * tokens (positions or slugs) which pick_view_collect normalizes. */
static const form_field_t song_ff[] = {
        { "title", "Title:", 0, 0, NULL, 0 },
        { "type", "Type:", 0, FF_REF_MULTI, "song.types", 0 },
        { "author", "Author:", 0, 0, NULL, 0 },
        { "yt", "Youtube ID:", 0, 0, NULL, 0 },
        { "audio", "Audio URL:", 0, 0, NULL, 0 },
        { "pdf", "PDF URL:", 0, 0, NULL, 0 },
        { "data", "Chords/Lyrics:", 1, 0, NULL, 0 },
        { NULL, NULL, 0, 0, NULL, 0 }
};

/* Fill vals[] parallel to song_ff from the meta cache (+ data.txt
 * body); empty strings when meta is NULL (add page). */
void song_form_values(
        const song_cache_t *meta, const char *data_val, const char **vals)
{
	int i;

	for (i = 0; song_ff[i].name; i++) {
		if (strcmp(song_ff[i].name, "data") == 0) {
			vals[i] = data_val;
		} else if (meta) {
			for (const bud_field_desc_t *f = song_fields; f->key;
			     f++)
			{
				if (strcmp(f->key, song_ff[i].name) == 0) {
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
}

bud_node *song_form_content(
        int is_edit, const char *id, const char **vals,
        const char *csrf_token, const pick_view_t *pv)
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

	bud_node *fields = site_ui_form_fields_ex(
	        song_ff, vals, csrf_token, pv);
	bud_append(fields, site_ui_form_actions(cancel_href, "Save", NULL));

	bud_node *form = lx_el("form", lx_attr("action", action),
	                       lx_attr("method", "POST"),
	                       lx_attr("enctype", "multipart/form-data"),
	                       lx_attr("class", "flex flex-col gap-4"),
	                       lx_node(fields))
	                          .data.node;
	{
		bud_node *sib = site_ui_sibling_get_form(
		        action, song_ff, vals, pv);
		bud_node *both = bud_fragment();

		bud_append(both, form);
		if (sib)
			bud_append(both, sib);
		return both;
	}
}
