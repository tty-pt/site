#include "bud/bud.h"
#include "bud/bud_jsx.h"
#include "bud/bud_app.h"
#include <hyle-bud/hyle-bud.h>
#include "../fields.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../song/ux/music.c"

#include "../../common/ux/site_ui.c"

/* List machinery for the song picker (site_ui.c must come first). */
#include "../../index/ux/list.c"

typedef struct {
	int orig_key;
	char repo_id[256];
	char title[256];
	char transpose[16];
	char format[32];
} sb_edit_row_t;

/* grp is a pinned single-reference picker over the shared dataset
 * (spec §2); descriptor shared by the edit form and its sibling GET
 * draft form. */
static const form_field_t sb_grp_ff[] = { { "grp", "Group:", 0, FF_REF_SINGLE,
	                                    "grp.items", 0 },
	                                  { NULL, NULL, 0, 0, NULL, 0 } };

/* Edit page picker state (filled by gig.c; this page is SSR-only) */
static pick_view_t g_edit_pv;

/* Omnisearch picker: add mode to append a new song to the gig */
static bud_node *sb_render_edit_picker(
        const char *sb_id, const char *csrf_token, const pick_view_t *pv)
{
	char action[256], post_action[256];

	snprintf(action, sizeof(action), "/gig/%s/edit", sb_id);
	snprintf(post_action, sizeof(post_action), "/api/gig/%s/songs", sb_id);

	site_ui_action_picker_spec_t spec = {
		.key = "song_id",
		.label = "Song:",
		.target = "song.items",
		.get_action = action,
		.post_action = post_action,
		.form_id = "edit-pick-post",
		.csrf_token = csrf_token,
		.submit_label = "Add",
		.hint = "Click a song to add it.",
		.auto_submit = 1,
	};

	return site_ui_action_picker(&spec, pv);
}

static bud_node *sb_render_single_song_picker(
        const char *key, const char *cur_id, const char *cur_title,
        const char *get_form_id, const pick_view_t *pv, int is_active,
        const char *search_param, const char *page_param)
{
	const pick_entry_t *e =
	        (is_active && pv && pv->n > 0) ? &pv->entries[0] : NULL;
	hyle_bud_option_t sel_opt = { .id = cur_id,
		                      .label = (cur_title && cur_title[0])
		                                       ? cur_title
		                                       : cur_id };

	hyle_bud_picker_desc_t d = { .key = key ? key : "song_id",
		                     .label = "song",
		                     .source = "song.items",
		                     .multi = 0,
		                     .get_form_id = get_form_id,
		                     .page_opts = e ? e->page_opts : NULL,
		                     .npage = e ? e->npage : 0,
		                     .sel = &sel_opt,
		                     .nsel = (cur_id && cur_id[0]) ? 1 : 0,
		                     .q = (e && e->q) ? e->q : "",
		                     .page = e ? e->page : 0,
		                     .per_page = e ? e->per_page : 15,
		                     .total = e ? e->total : 0,
		                     .search_param = search_param,
		                     .page_param = page_param };

	bud_node *picker = hyle_bud_picker_field(&d);
	if (picker)
		bud_add_class(picker, "gig-song-title-picker");
	return picker;
}

static bud_node *sb_render_single_format_picker(
        const char *key, const char *cur_id, const char *cur_title,
        const char *get_form_id, const pick_view_t *pv, int is_active,
        const char *search_param, const char *page_param)
{
	const pick_entry_t *e =
	        (is_active && pv && pv->n > 0) ? &pv->entries[0] : NULL;
	hyle_bud_option_t sel_opt = { .id = cur_id,
		                      .label = (cur_title && cur_title[0])
		                                       ? cur_title
		                                       : cur_id };

	hyle_bud_picker_desc_t d = { .key = key ? key : "fmt",
		                     .label = "format",
		                     .source = "song.types",
		                     .multi = 0,
		                     .get_form_id = get_form_id,
		                     .page_opts = e ? e->page_opts : NULL,
		                     .npage = e ? e->npage : 0,
		                     .sel = &sel_opt,
		                     .nsel = (cur_id && cur_id[0]) ? 1 : 0,
		                     .q = (e && e->q) ? e->q : "",
		                     .page = e ? e->page : 0,
		                     .per_page = e ? e->per_page : 15,
		                     .total = e ? e->total : 0,
		                     .search_param = search_param,
		                     .page_param = page_param };

	bud_node *picker = hyle_bud_picker_field(&d);
	if (picker)
		bud_add_class(picker, "gig-format-picker");
	return picker;
}

static bud_node *sb_render_edit_form(
        const char *action, const char *csrf_token, const char *title,
        const char *sb_id, const char *grp_id, const pick_view_t *pv,
        const char *cancel_href, int n_songs, const sb_edit_row_t *songs,
        int n_format_opts, const char **format_opts, const char *song_source,
        int active_edit_row, const pick_view_t *row_pv, int active_edit_fmt_row,
        const pick_view_t *fmt_row_pv)
{
	bud_node *rows = bud_fragment();
	bud_node *row_sibs = bud_fragment();
	char amount_str[16];
	snprintf(amount_str, sizeof(amount_str), "%d", n_songs);

	/* grp picker (threshold select or omnisearch) via the shared
	 * descriptor; pv carries options/pinned from the collector. */
	const char *grp_vals[1];
	grp_vals[0] = (grp_id && grp_id[0]) ? grp_id : "";
	bud_node *grp_fields =
	        site_ui_form_fields_ex(sb_grp_ff, grp_vals, NULL, pv);

	/* Omnisearch song picker for adding songs (rendered outside main edit
	 * form) */
	const pick_view_t *add_pv =
	        (active_edit_row < 0 && active_edit_fmt_row < 0) ? &g_edit_pv
	                                                         : NULL;
	bud_node *picker = sb_render_edit_picker(sb_id, csrf_token, add_pv);

	for (int i = 0; i < n_songs; i++) {
		char song_f[32], key_f[32], fmt_f[32], remove_f[32];
		snprintf(song_f, sizeof(song_f), "song_%d", i);
		snprintf(key_f, sizeof(key_f), "key_%d", i);
		snprintf(fmt_f, sizeof(fmt_f), "fmt_%d", i);
		snprintf(remove_f, sizeof(remove_f), "remove_%d", i);

		int cur_key = atoi(songs[i].transpose);
		const char *f_val = songs[i].format[0] ? songs[i].format : "";
		int orig_key = songs[i].orig_key;

		char song_href[320];
		song_href[0] = '\0';
		if (songs[i].repo_id[0])
			snprintf(
			        song_href, sizeof(song_href), "/song/%s",
			        songs[i].repo_id);

		/* Per-row standard song picker with default selection */
		char get_form_id[64];
		snprintf(get_form_id, sizeof(get_form_id), "pickq-song_%d", i);

		char search_param[64], page_param[64];
		snprintf(
		        search_param, sizeof(search_param), "pick_q_song_%d",
		        i);
		snprintf(
		        page_param, sizeof(page_param), "pick_page_song_%d", i);

		int is_active = (i == active_edit_row);
		const pick_view_t *cur_row_pv = is_active ? row_pv : NULL;

		bud_node *picker_node = sb_render_single_song_picker(
		        song_f, songs[i].repo_id, songs[i].title, get_form_id,
		        cur_row_pv, is_active, search_param, page_param);

		bud_node *view_link =
		        song_href[0] ? lx_el("a", lx_attr("href", song_href),
		                             lx_attr("class",
		                                     "text-xs text-muted ml-1"),
		                             lx_attr("title", "View song"),
		                             lx_text("\xe2\x86\x97"))
		                               .data.node
		                     : NULL;

		bud_node *song_ctl =
		        lx_el("span",
		              lx_attr("class",
		                      "flex items-center gap-1 flex-1 min-w-0"),
		              picker_node ? lx_node(picker_node) : lx_none(),
		              view_link ? lx_node(view_link) : lx_none())
		                .data.node;

		/* Sibling GET form for no-JS pagination/search of this row's
		 * song */
		bud_node *row_sib =
		        lx_el("form", lx_attr("id", get_form_id),
		              lx_attr("action", action),
		              lx_attr("method", "GET"),
		              lx_attr("class", "pick-sibling-form"),
		              lx_el("input", lx_attr("type", "hidden"),
		                    lx_attr("name", "title"),
		                    lx_attr("value", title ? title : "")),
		              (grp_id && grp_id[0])
		                      ? lx_el("input",
		                              lx_attr("type", "hidden"),
		                              lx_attr("name", "grp"),
		                              lx_attr("value", grp_id))
		                      : lx_none())
		                .data.node;
		if (row_sibs && row_sib)
			bud_append(row_sibs, row_sib);

		/* Key selector: -11 to +11 with key name labels */
		bud_node *key_opts = NULL;
		for (int si = -11; si <= 11; si++) {
			char v[16];
			snprintf(v, sizeof(v), "%d", si);
			bud_node *o =
			        lx_el("option", lx_attr("value", v),
			              si == cur_key ? lx_attr("selected", "")
			                            : lx_none(),
			              lx_text(key_name(si, orig_key, 0)))
			                .data.node;
			if (!key_opts)
				key_opts = bud_fragment();
			bud_append(key_opts, o);
		}
		/* Fallback if cur_key is outside -11..11 */
		if (cur_key < -11 || cur_key > 11) {
			char v[16];
			snprintf(v, sizeof(v), "%d", cur_key);
			bud_node *o = lx_el("option", lx_attr("value", v),
			                    lx_attr("selected", ""),
			                    lx_text(songs[i].transpose))
			                      .data.node;
			bud_append(key_opts, o);
		}

		bud_node *key_select =
		        lx_el("select", lx_attr("name", key_f),
		              lx_attr("class", "border rounded p-1 w-24"),
		              lx_node(key_opts))
		                .data.node;

		/* Format selector: omni-dropdown picker for song.types */
		char fmt_get_form_id[64];
		snprintf(
		        fmt_get_form_id, sizeof(fmt_get_form_id),
		        "pickq-fmt_%d", i);
		char fmt_search_param[64], fmt_page_param[64];
		snprintf(
		        fmt_search_param, sizeof(fmt_search_param),
		        "pick_q_fmt_%d", i);
		snprintf(
		        fmt_page_param, sizeof(fmt_page_param),
		        "pick_page_fmt_%d", i);

		int is_fmt_active = (i == active_edit_fmt_row);
		const pick_view_t *cur_fmt_pv =
		        is_fmt_active ? fmt_row_pv : NULL;

		bud_node *fmt_picker_node = sb_render_single_format_picker(
		        fmt_f, f_val, f_val, fmt_get_form_id, cur_fmt_pv,
		        is_fmt_active, fmt_search_param, fmt_page_param);

		bud_node *fmt_ctl =
		        lx_el("span",
		              lx_attr("class",
		                      "flex items-center gap-1 flex-1 min-w-0"),
		              fmt_picker_node ? lx_node(fmt_picker_node)
		                              : lx_none())
		                .data.node;

		/* Sibling GET form for format picker */
		bud_node *fmt_sib =
		        lx_el("form", lx_attr("id", fmt_get_form_id),
		              lx_attr("action", action),
		              lx_attr("method", "GET"),
		              lx_attr("class", "pick-sibling-form"),
		              lx_el("input", lx_attr("type", "hidden"),
		                    lx_attr("name", "title"),
		                    lx_attr("value", title ? title : "")),
		              (grp_id && grp_id[0])
		                      ? lx_el("input",
		                              lx_attr("type", "hidden"),
		                              lx_attr("name", "grp"),
		                              lx_attr("value", grp_id))
		                      : lx_none())
		                .data.node;
		if (row_sibs && fmt_sib)
			bud_append(row_sibs, fmt_sib);

		bud_node *row =
		        lx_el("div",
		              lx_attr("class",
		                      "flex gap-2 items-center w-full"),
		              lx_node(song_ctl), lx_node(key_select),
		              lx_node(fmt_ctl),
		              lx_el("label",
		                    lx_attr("class", "text-sm cursor-pointer "
		                                     "flex-shrink-0"),
		                    lx_el("input", lx_attr("type", "checkbox"),
		                          lx_attr("name", remove_f)),
		                    lx_text(" Remove")))
		                .data.node;

		bud_append(rows, row);
	}

	bud_node *form =
	        lx_el("form", lx_attr("action", action),
	              lx_attr("method", "POST"),
	              lx_attr("enctype", "multipart/form-data"),
	              lx_attr("class", "flex flex-col gap-4"),
	              lx_el("input", lx_attr("type", "hidden"),
	                    lx_attr("name", "csrf_token"),
	                    lx_attr("value", csrf_token)),
	              lx_el("input", lx_attr("type", "hidden"),
	                    lx_attr("name", "amount"),
	                    lx_attr("value", amount_str)),
	              lx_el("label", lx_text("Title:"),
	                    lx_el("input", lx_attr("type", "text"),
	                          lx_attr("name", "title"),
	                          (title && title[0]) ? lx_attr("value", title)
	                                              : lx_none())),
	              lx_node(grp_fields),
	              (grp_id && grp_id[0])
	                      ? lx_el("label", lx_text("Song source:"),
	                              lx_el("select",
	                                    lx_attr("name", "song_source"),
	                                    lx_attr("class", "border rounded "
	                                                     "p-1 w-48"),
	                                    lx_el("option",
	                                          lx_attr("value", "all"),
	                                          (!song_source ||
	                                           strcmp(song_source, "all") ==
	                                                   0)
	                                                  ? lx_attr("selected",
	                                                            "")
	                                                  : lx_none(),
	                                          lx_text("All "
	                                                  "songs")),
	                                    lx_el("option",
	                                          lx_attr("value",
	                                                  "repertoire"),
	                                          (song_source &&
	                                           strcmp(song_source,
	                                                  "repertoire") == 0)
	                                                  ? lx_attr("selected",
	                                                            "")
	                                                  : lx_none(),
	                                          lx_text("Repertoire "
	                                                  "only"))))
	                      : lx_none(),
	              lx_el("h3", lx_attr("class", "mt-2"), lx_text("Songs")),
	              lx_node(rows),
	              lx_el("div", lx_attr("class", "flex gap-2"),
	                    lx_el("button", lx_attr("type", "submit"),
	                          lx_attr("class", "btn btn-primary"),
	                          lx_attr("id", "edit-form-submit"),
	                          lx_text("Save Changes")),
	                    lx_el("a", lx_attr("href", cancel_href),
	                          lx_attr("class", "btn btn-secondary"),
	                          lx_text("Cancel"))))
	                .data.node;

	/* Sibling GET draft form (never nested in the POST form): Enter
	 * / paging inside the picker round-trip mirrors of every sibling
	 * field so no-JS edits survive (spec §1.3, §3.2). */
	{
		bud_node *sib = site_ui_sibling_get_form(
		        action, sb_grp_ff, grp_vals, pv);
		bud_node *both = bud_fragment();

		if (picker)
			bud_append(both, picker);
		bud_append(both, form);
		if (sib)
			bud_append(both, sib);
		if (row_sibs)
			bud_append(both, row_sibs);
		return both;
	}
}
