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

static bud_node *sb_render_edit_form(
        const char *action, const char *csrf_token, const char *title,
        const char *sb_id, const char *grp_id, const pick_view_t *pv,
        const char *cancel_href, int n_songs, const sb_edit_row_t *songs,
        int n_format_opts, const char **format_opts, const char *song_source,
        int active_edit_row, const pick_view_t *row_pv, int active_edit_fmt_row,
        const pick_view_t *fmt_row_pv, const pick_view_t *add_pv)
{
	bud_node *rows = bud_fragment();
	bud_node *row_sibs = bud_fragment();
	char amount_str[16];
	snprintf(amount_str, sizeof(amount_str), "%d", n_songs);

	/* grp picker (threshold select or omnisearch) via canonical hyle_bud_filter */
	bud_node *grp_field_node = hyle_bud_filter(
	        gig_fields, "grp", grp_id ? grp_id : "", pv);
	bud_node *grp_fields = lx_el("label", lx_text("Group:"), grp_field_node ? lx_node(grp_field_node) : lx_none()).data.node;

	/* Omnisearch picker for adding songs rendered via canonical hyle_bud_filter */
	bud_node *picker = NULL;
	if (active_edit_row < 0 && active_edit_fmt_row < 0 && add_pv) {
		picker = hyle_bud_filter(
		        gig_song_fields, "song", "", add_pv);
		if (picker) {
			char post_action[256];
			snprintf(post_action, sizeof(post_action), "/api/gig/%s/songs", sb_id);
			bud_node *post_form = lx_el(
			        "form",
			        lx_attr("id", "edit-pick-post"),
			        lx_attr("method", "post"),
			        lx_attr("action", post_action),
			        lx_attr("class", "flex gap-2 items-center mb-4"),
			        lx_el("input", lx_attr("type", "hidden"),
			              lx_attr("name", "csrf_token"),
			              lx_attr("value", csrf_token ? csrf_token : "")),
			        lx_node(picker),
			        lx_el("button", lx_attr("type", "submit"),
			              lx_attr("class", "btn btn-primary hyle-picker-submit"),
			              lx_text("Add Song")))
			        .data.node;
			picker = post_form;
		}
	}

	for (int i = 0; i < n_songs; i++) {
		char key_f[32], remove_f[32];
		snprintf(key_f, sizeof(key_f), "key_%d", i);
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

		/* Per-row standard song picker via canonical hyle_bud_filter_scoped */
		int is_active = (i == active_edit_row);
		const pick_view_t *cur_row_pv = is_active ? row_pv : NULL;

		bud_node *picker_node = hyle_bud_filter_scoped(
		        gig_song_fields, "song", i, songs[i].repo_id, songs[i].title,
		        action, cur_row_pv, is_active,
		        "gig-song-title-picker", &row_sibs);

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

		/* Key selector: 0 to 11 with key name labels */
		bud_node *key_opts = NULL;
		int norm_key = ((cur_key % 12) + 12) % 12;
		for (int si = 0; si < 12; si++) {
			char v[16];
			snprintf(v, sizeof(v), "%d", si);
			bud_node *o =
			        lx_el("option", lx_attr("value", v),
			              si == norm_key ? lx_attr("selected", "")
			                             : lx_none(),
			              lx_text(key_name(si, orig_key, 0)))
			                .data.node;
			if (!key_opts)
				key_opts = bud_fragment();
			bud_append(key_opts, o);
		}

		bud_node *key_select =
		        lx_el("select", lx_attr("name", key_f),
		              lx_attr("class", "border rounded p-1 w-24"),
		              lx_node(key_opts))
		                .data.node;

		/* Format selector via canonical hyle_bud_filter_scoped */
		int is_fmt_active = (i == active_edit_fmt_row);
		const pick_view_t *cur_fmt_pv =
		        is_fmt_active ? fmt_row_pv : NULL;

		bud_node *fmt_picker_node = hyle_bud_filter_scoped(
		        gig_song_fields, "fmt", i, f_val, f_val, action,
		        cur_fmt_pv, is_fmt_active,
		        "gig-format-picker", &row_sibs);

		bud_node *fmt_ctl =
		        lx_el("span",
		              lx_attr("class",
		                      "flex items-center gap-1 flex-1 min-w-0"),
		              fmt_picker_node ? lx_node(fmt_picker_node)
		                              : lx_none())
		                .data.node;

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

	/* Sibling GET draft form */
	{
		bud_node *grp_sib = lx_el("form",
		                          lx_attr("id", "pickq-grp"),
		                          lx_attr("action", action),
		                          lx_attr("method", "GET"),
		                          lx_attr("class", "pick-sibling-form"),
		                          lx_el("input", lx_attr("type", "hidden"),
		                                lx_attr("name", "title"),
		                                lx_attr("value", title ? title : "")))
		                            .data.node;
		bud_node *both = bud_fragment();

		if (picker)
			bud_append(both, picker);
		bud_append(both, form);
		if (grp_sib)
			bud_append(both, grp_sib);
		if (row_sibs)
			bud_append(both, row_sibs);
		return both;
	}
}
