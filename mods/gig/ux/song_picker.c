#ifndef SONG_PICKER_C
#define SONG_PICKER_C

#include "bud/bud.h"
#include "bud/bud_jsx.h"
#include <hyle-bud/hyle-bud.h>
#include "../fields.h"
#include <stdio.h>
#include <string.h>

/* List machinery for the song picker (site_ui.c must come first). */
#include "../../index/ux/list.c"

typedef struct {
    const char *get_action;
    const char *post_action;
    const char *form_id;
    const char *csrf;
    const char *aria_base;
    const char *hint;
    const char *back;
    int replace_index;
    const char *replace_title;
    const char **pref_names;
    int *pref_vals;
    int n_prefs;
} sb_picker_spec_t;

static const form_field_t sb_pick_song_ff[] = {
	{ "song_id", "Song", 0, FF_REF_SINGLE, "song.items", 0 },
	FIELD_END
};

static bud_node *sb_picker_render(const sb_picker_spec_t *spec, pick_view_t *pv)
{
	char header_buf[512] = { 0 };
	bud_node *extra_post = bud_fragment();

	if (spec->replace_index >= 0) {
		snprintf(header_buf, sizeof(header_buf),
		        "Replace Song #%d  \xe2\x80\x94  Replacing: %s",
		        spec->replace_index + 1,
		        spec->replace_title ? spec->replace_title : "");
		char n_str[16];
		snprintf(n_str, sizeof(n_str), "%d", spec->replace_index);
		bud_append(extra_post,
		           lx_el("input", lx_attr("type", "hidden"),
		                 lx_attr("name", "n"), lx_attr("value", n_str))
		                   .data.node);
	}

	if (spec->back && spec->back[0]) {
		bud_append(extra_post,
		           lx_el("input", lx_attr("type", "hidden"),
		                 lx_attr("name", "back"),
		                 lx_attr("value", spec->back))
		                   .data.node);
	}

	site_ui_action_picker_spec_t aspec = {
		.key = "song_id",
		.label = "Song:",
		.target = "song.items",
		.get_action = spec->get_action,
		.post_action = spec->post_action,
		.form_id = spec->form_id ? spec->form_id : "sb-pick-post",
		.csrf_token = spec->csrf,
		.submit_label = spec->aria_base ? spec->aria_base : "Add",
		.hint = spec->hint ? spec->hint : "Select a song to add it.",
		.cancel_href = (spec->replace_index >= 0) ? (spec->back && spec->back[0] ? spec->back : "/") : NULL,
		.cancel_label = "Cancel",
		.header_text = (spec->replace_index >= 0) ? header_buf : NULL,
		.pref_names = spec->pref_names,
		.pref_vals = spec->pref_vals,
		.n_prefs = spec->n_prefs,
		.extra_post_inputs = extra_post
	};

	return site_ui_action_picker(&aspec, pv);
}

#endif
