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
    int auto_submit;
    const char *search_param;   /* scoped pick_q_ override, e.g. "pick_q_song_id__2" */
    const char *page_param;     /* scoped pick_page_ override */
    const char **pref_names;
    int *pref_vals;
    int n_prefs;
} sb_picker_spec_t;

static const form_field_t sb_pick_song_ff[] = {
	{ "song_id", "Song", 0, FF_REF_SINGLE, "song.items", 0 },
	FIELD_END
};

/* Top-level standalone / action song picker (for adding a song) */
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
		.auto_submit = spec->auto_submit,
		.extra_post_inputs = extra_post,
		.search_param = spec->search_param,
		.page_param = spec->page_param
	};

	return site_ui_action_picker(&aspec, pv);
}

/* ── Standard single-select song picker with default value ── */

static bud_node *sb_render_single_song_picker(
        const char *key, const char *cur_id, const char *cur_title,
        const char *get_form_id, const pick_view_t *pv, int is_active,
        const char *search_param, const char *page_param)
{
	const pick_entry_t *e =
	        (is_active && pv && pv->n > 0) ? &pv->entries[0] : NULL;
	hyle_bud_option_t sel_opt = {
		.id = cur_id,
		.label = (cur_title && cur_title[0]) ? cur_title : cur_id
	};
	char url_tmpl[512];
	snprintf(url_tmpl, sizeof(url_tmpl),
	         "/pick/song.items/options?key=%s&multi=0&label=&sel={sel}&pick_q_%s={q}&pick_page_%s={page}",
	         key ? key : "song_id", key ? key : "song_id",
	         key ? key : "song_id");

	hyle_bud_picker_desc_t d = {
		.key = key ? key : "song_id",
		.label = "song",
		.source = "song.items",
		.multi = 0,
		.get_form_id = get_form_id,
		.url_tmpl = url_tmpl,
		.page_opts = e ? e->page_opts : NULL,
		.npage = e ? e->npage : 0,
		.sel = &sel_opt,
		.nsel = (cur_id && cur_id[0]) ? 1 : 0,
		.q = (e && e->q) ? e->q : "",
		.page = e ? e->page : 0,
		.per_page = e ? e->per_page : 15,
		.total = e ? e->total : 0,
		.search_param = search_param,
		.page_param = page_param
	};

	bud_node *picker = hyle_bud_picker_field(&d);
	if (picker)
		bud_add_class(picker, "gig-song-title-picker");
	return picker;
}

#endif
