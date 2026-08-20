#include "bud/bud.h"
#include "bud/bud_jsx.h"
#include "bud/bud_app.h"
#include "../fields.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../common/ux/site_ui.c"

static bud_node *
sb_render_add_form(const char *csrf_token, const char *grp_val)
{
	static const form_field_t ff[] = { { "title", "Title:", 0 },
		                           { NULL, NULL, 0 } };

	bud_node *fields = site_ui_form_fields(ff, NULL, csrf_token);
	if (grp_val[0]) {
		bud_append(
		        fields, lx_el("input", lx_attr("type", "hidden"),
		                      lx_attr("name", "grp"),
		                      lx_attr("value", grp_val))
		                        .data.node);
	}
	bud_append(fields, site_ui_form_actions("/gig/", "Add", NULL));

	return lx_el("form", lx_attr("action", "/gig/add"),
	             lx_attr("method", "POST"),
	             lx_attr("enctype", "multipart/form-data"),
	             lx_attr("class", "flex flex-col gap-4"), lx_node(fields))
	        .data.node;
}
