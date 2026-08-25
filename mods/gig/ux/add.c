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
	static const form_field_t ff[] = {
	        { "title", "Title:", 0, 0, NULL, 0 },
	        { "grp", "Group:", 0, FF_REF_SINGLE, "grp.items", 0 },
	        { NULL, NULL, 0, 0, NULL, 0 }
	};

	/* grp renders as a pinned picker (?grp=<slug> preselect arrives
	 * as a draft overlay via pick_view_collect); title stays plain.
	 * The ?grp= passthrough hidden input is gone — the pinned row
	 * submits natively through this POST form (spec §2). */
	bud_node *fields = site_ui_form_fields_ex(ff, vals, csrf_token, pv);
	bud_append(fields, site_ui_form_actions("/gig/", "Add", NULL));

	bud_node *form = lx_el("form", lx_attr("action", "/gig/add"),
	                       lx_attr("method", "POST"),
	                       lx_attr("enctype", "multipart/form-data"),
	                       lx_attr("class", "flex flex-col gap-4"),
	                       lx_node(fields))
	                          .data.node;

	{
		bud_node *sib = site_ui_sibling_get_form("/gig/add", ff,
		        vals, pv);
		bud_node *both = bud_fragment();

		bud_append(both, form);
		if (sib)
			bud_append(both, sib);
		return both;
	}
}
