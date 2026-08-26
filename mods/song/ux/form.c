#include "bud/bud.h"
#include "bud/bud_jsx.h"
#include "bud/bud_app.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../common/ux/site_ui.c"
#include "../fields.h"

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

bud_node *song_form_content(
        int is_edit, const char *id, const song_cache_t *meta,
        const char *data_val, const char *csrf_token, const pick_view_t *pv)
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

	return site_ui_form_from_desc(
	        action, cancel_href, "Save", song_fields, meta, csrf_token,
	        pv, data_val);
}
