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

/* Shared omnisearch song picker (same module, allowed by boundary checker) */
#include "song_picker.c"

typedef struct {
	int orig_key;
	char repo_id[256];
	char title[256];
	char transpose[16];
	char format[32];
} sb_edit_row_t;

typedef struct {
	const char *id;
	const char *title;
} sb_grp_opt_t;

/* Edit page picker state (filled by gig.c; this page is SSR-only) */
static list_state_t g_edit_pick_state;
static int g_edit_replace_index = -1;
static char g_edit_replace_title[256];

static void sb_edit_set_replace(int idx, const char *title)
{
	g_edit_replace_index = idx;
	snprintf(g_edit_replace_title, sizeof(g_edit_replace_title), "%s",
	         title ? title : "");
}

/* Omnisearch picker: add mode by default, replace mode when
 * ?replace=N was requested (set via sb_edit_set_replace). Both modes
 * post straight to the API and come back to this page via back=. */
static bud_node *sb_render_edit_picker(const char *sb_id, const char *csrf_token)
{
	char action[256], post_action[256], back[256];
	sb_picker_spec_t spec;

	snprintf(action, sizeof(action), "/gig/%s/edit", sb_id);
	snprintf(back, sizeof(back), "/gig/%s", sb_id);
	if (g_edit_replace_index >= 0)
		snprintf(post_action, sizeof(post_action),
		         "/api/gig/%s/song/%d/replace", sb_id, g_edit_replace_index);
	else
		snprintf(post_action, sizeof(post_action), "/api/gig/%s/songs", sb_id);

	memset(&spec, 0, sizeof(spec));
	spec.get_action = action;
	spec.post_action = post_action;
	spec.form_id = "edit-pick-post";
	spec.csrf = csrf_token;
	spec.aria_base = g_edit_replace_index >= 0 ? "Replace" : "Add";
	spec.hint = g_edit_replace_index >= 0
	                    ? "Click a song to swap it in \xe2\x80\x94 its key and format are kept."
	                    : "Click a song to add it.";
	spec.back = back;
	spec.replace_index = g_edit_replace_index;
	spec.replace_title = g_edit_replace_title;
	spec.pref_names = NULL;
	spec.pref_vals = NULL;
	spec.n_prefs = 0;

	return sb_picker_render(&spec, &g_edit_pick_state);
}

static bud_node *sb_render_edit_form(
        const char *action, const char *csrf_token, const char *title,
        const char *sb_id, const char *grp_id, int n_grps, const sb_grp_opt_t *grps,
        const char *cancel_href, int n_songs, const sb_edit_row_t *songs,
        int n_format_opts, const char **format_opts, const char *song_source)
{
	bud_node *rows = bud_fragment();
	char amount_str[16];
	snprintf(amount_str, sizeof(amount_str), "%d", n_songs);

	/* Build grp dropdown options */
	bud_node *grp_opts = bud_fragment();
	bud_append(
	        grp_opts,
	        lx_el("option", lx_attr("value", ""),
	              (!grp_id || !grp_id[0]) ? lx_attr("selected", "")
	                                      : lx_none(),
	              lx_text("None"))
	                .data.node);
	for (int ci = 0; ci < n_grps; ci++) {
		const sb_grp_opt_t *c = &grps[ci];
		if (!c->id)
			break;
		bud_append(
		        grp_opts, lx_el("option", lx_attr("value", c->id),
		                        (grp_id && strcmp(grp_id, c->id) == 0)
		                                ? lx_attr("selected", "")
		                                : lx_none(),
		                        lx_text(c->title))
		                          .data.node);
	}
	bud_node *grp_select =
	        lx_el("select", lx_attr("name", "grp"),
	              lx_attr("class", "border rounded p-1 w-60"),
	              lx_node(grp_opts))
	                .data.node;

	/* Omnisearch song picker at the top (add mode, or replace mode
	 * when ?replace=N is active) */
	bud_node *picker = sb_render_edit_picker(sb_id, csrf_token);
	if (picker)
		bud_append(rows, picker);

	for (int i = 0; i < n_songs; i++) {
		char song_f[32], key_f[32], fmt_f[32], remove_f[32], repl_f[32];
		snprintf(song_f, sizeof(song_f), "song_%d", i);
		snprintf(key_f, sizeof(key_f), "key_%d", i);
		snprintf(fmt_f, sizeof(fmt_f), "fmt_%d", i);
		snprintf(remove_f, sizeof(remove_f), "remove_%d", i);
		snprintf(repl_f, sizeof(repl_f), "?replace=%d", i);

		int cur_key = atoi(songs[i].transpose);
		const char *f_val = songs[i].format[0] ? songs[i].format : "";
		int orig_key = songs[i].orig_key;
		char change_href[320];
		snprintf(change_href, sizeof(change_href), "/gig/%s/edit%s",
		         sb_id, repl_f);

		/* Song identity: read-only title link + hidden field so the
		 * bulk Save keeps the current song unless replaced via the
		 * omnisearch picker (?replace=N). */
		char song_href[320];
		song_href[0] = '\0';
		if (songs[i].repo_id[0])
			snprintf(song_href, sizeof(song_href), "/song/%s",
			         songs[i].repo_id);

		bud_node *song_ctl =
		        lx_el("span", lx_attr("class", "flex items-center gap-2 w-60"),
		              lx_el("input", lx_attr("type", "hidden"),
		                    lx_attr("name", song_f),
		                    lx_attr("value", songs[i].repo_id)),
		              lx_el("a",
		                    song_href[0] ? lx_attr("href", song_href)
		                                 : lx_none(),
		                    lx_attr("class", "font-medium text-sm truncate"),
		                    lx_text(songs[i].title)))
		                .data.node;

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

		/* Format selector: dropdown or text input */
		bud_node *fmt_ctl = NULL;
		if (n_format_opts > 0) {
			bud_node *fmt_opts = bud_fragment();
			for (int fi = 0; fi < n_format_opts; fi++) {
				bud_append(
				        fmt_opts,
				        lx_el("option",
				              lx_attr("value", format_opts[fi]),
				              (f_val[0] &&
				               strcmp(f_val, format_opts[fi]) == 0)
				                      ? lx_attr("selected", "")
				                      : lx_none(),
				              lx_text(format_opts[fi]))
				                .data.node);
			}
			/* Fallback when current format not in list */
			if (f_val[0]) {
				int found = 0;
				for (int fi = 0; fi < n_format_opts; fi++) {
					if (strcmp(f_val, format_opts[fi]) == 0) {
						found = 1;
						break;
					}
				}
				if (!found) {
					bud_append(
					        fmt_opts,
					        lx_el("option",
					              lx_attr("value", f_val),
					              lx_attr("selected", ""),
					              lx_text(f_val))
					                .data.node);
				}
			}
			fmt_ctl = lx_el("select", lx_attr("name", fmt_f),
			                lx_attr("class", "border rounded p-1 w-32"),
			                lx_node(fmt_opts))
			                  .data.node;
		} else {
			fmt_ctl = lx_el("input", lx_attr("type", "text"),
			                lx_attr("name", fmt_f),
			                lx_attr("class", "border rounded p-1 w-24"),
			                lx_attr("placeholder", "Format"),
			                (f_val[0] && strcmp(f_val, "any") != 0)
			                        ? lx_attr("value", f_val)
			                        : lx_none())
			                  .data.node;
		}

		bud_node *row =
		        lx_el("div",
		              lx_attr("class", "flex gap-2 items-center"),
		              lx_node(song_ctl),
		              lx_el("a", lx_attr("href", change_href),
		                    lx_attr("class", "btn text-xs py-1 px-2"),
		                    lx_attr("aria-label", "Change song"),
		                    lx_text("\xf0\x9f\x94\x84")),
		              lx_node(key_select), lx_node(fmt_ctl),
		              lx_el("label",
		                    lx_attr("class", "text-sm cursor-pointer"),
		                    lx_el("input",
		                          lx_attr("type", "checkbox"),
		                          lx_attr("name", remove_f)),
		                    lx_text(" Remove")))
		                .data.node;

		bud_append(rows, row);
	}

	return lx_el("form", lx_attr("action", action),
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
	             lx_el("label", lx_text("Group:"), lx_node(grp_select)),
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
	                                         lx_attr("value", "repertoire"),
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
}
