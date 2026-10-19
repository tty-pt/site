#include "bud/bud.h"
#include "bud/bud_jsx.h"
#include "bud/bud_app.h"
#include "../fields.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../common/ux/site_ui.c"

static bud_node *sb_render_add_form(
        const char *csrf_token, const char **vals, const pick_view_t *pv)
{
	gig_cache_t meta;
	memset(&meta, 0, sizeof(meta));
	if (vals && vals[0] && vals[0][0])
		snprintf(meta.title, sizeof(meta.title), "%s", vals[0]);
	if (vals && vals[1] && vals[1][0])
		snprintf(meta.grp, sizeof(meta.grp), "%s", vals[1]);

	return site_ui_form_from_desc(
	        "/gig/add", "/gig/", "Add", gig_fields, vals ? &meta : NULL,
	        csrf_token, pv, NULL);
}
