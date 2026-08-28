#include "bud/bud.h"
#include "bud/bud_app.h"
#include <hyle-bud/hyle-bud.h>
#include "../fields.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <transp/music.h>
#include "../../common/ux/site_ui.c"
#include "../../index/ux/list.c"

typedef struct {
	int orig_key;
	char repo_id[256];
	char title[256];
	char transpose[16];
	char format[32];
} sb_edit_row_t;

static bud_node *render_key_options(int cur_key, int orig_key)
{
	bud_node *opts = bud_fragment();
	int norm_key = ((cur_key % 12) + 12) % 12;
	for (int si = 0; si < 12; si++) {
		bud_append(
		        opts, bud_tpl("<option value='%d' %b>%s</option>", si,
		                      si == norm_key ? "selected" : NULL,
		                      key_name(si, orig_key, 0)));
	}
	return opts;
}

static bud_node *render_song_row(
        int i, const sb_edit_row_t *song, const char *action,
        int active_edit_row, const pick_view_t *row_pv, int active_edit_fmt_row,
        const pick_view_t *fmt_row_pv, bud_node **row_sibs)
{
	char key_f[32], remove_f[32], song_href[320] = { 0 };
	snprintf(key_f, sizeof(key_f), "key_%d", i);
	snprintf(remove_f, sizeof(remove_f), "remove_%d", i);
	if (song->repo_id[0])
		snprintf(
		        song_href, sizeof(song_href), "/song/%s",
		        song->repo_id);

	int is_active = (i == active_edit_row);
	bud_node *picker_node = hyle_bud_filter_scoped(
	        gig_song_fields, "song", i, song->repo_id, song->title, action,
	        is_active ? row_pv : NULL, is_active, "gig-song-title-picker",
	        row_sibs);

	bud_node *view_link =
	        song_href[0] ? bud_tpl("<a href='%s' class='text-xs text-muted "
	                               "ml-1' title='View song'>↗</a>",
	                               song_href)
	                     : NULL;

	int is_fmt_active = (i == active_edit_fmt_row);
	const char *f_val = song->format[0] ? song->format : "";
	bud_node *fmt_picker = hyle_bud_filter_scoped(
	        gig_song_fields, "fmt", i, f_val, f_val, action,
	        is_fmt_active ? fmt_row_pv : NULL, is_fmt_active,
	        "gig-format-picker", row_sibs);

	return bud_tpl(
	        "<div class='flex gap-2 items-center w-full'>"
	        "  <span class='flex items-center gap-1 flex-1 min-w-0'>%node "
	        "%node</span>"
	        "  <select name='%s' class='border rounded p-1 "
	        "w-24'>%node</select>"
	        "  <span class='flex items-center gap-1 flex-1 "
	        "min-w-0'>%node</span>"
	        "  <label class='text-sm cursor-pointer flex-shrink-0'>"
	        "    <input type='checkbox' name='%s'/> Remove"
	        "  </label>"
	        "</div>",
	        picker_node, view_link, key_f,
	        render_key_options(atoi(song->transpose), song->orig_key),
	        fmt_picker, remove_f);
}

static bud_node *sb_render_edit_form(
        const char *action, const char *csrf_token, const char *title,
        const char *sb_id, const char *grp_id, const pick_view_t *pv,
        const char *cancel_href, int n_songs, const sb_edit_row_t *songs,
        int n_format_opts, const char **format_opts, const char *song_source,
        int active_edit_row, const pick_view_t *row_pv, int active_edit_fmt_row,
        const pick_view_t *fmt_row_pv, const pick_view_t *add_pv)
{
	(void)n_format_opts;
	(void)format_opts;
	bud_node *rows = bud_fragment();
	bud_node *row_sibs = bud_fragment();
	char amount_str[16];
	snprintf(amount_str, sizeof(amount_str), "%d", n_songs);

	/* grp picker via canonical hyle_bud_filter */
	bud_node *grp_field_node =
	        hyle_bud_filter(gig_fields, "grp", grp_id ? grp_id : "", pv);
	bud_node *grp_fields =
	        bud_tpl("<label>Group:"
	                "  %node"
	                "</label>",
	                grp_field_node);

	/* Omnisearch picker for adding songs rendered via canonical
	 * hyle_bud_filter */
	bud_node *picker = NULL;
	if (active_edit_row < 0 && active_edit_fmt_row < 0 && add_pv) {
		picker = hyle_bud_filter(gig_song_fields, "song", "", add_pv);
		if (picker) {
			char post_action[256];
			snprintf(
			        post_action, sizeof(post_action),
			        "/api/gig/%s/songs", sb_id);
			picker =
			        bud_tpl("<form id='edit-pick-post' "
			                "method='post' action='%s' class='flex "
			                "gap-2 items-center mb-4'>"
			                "  <input type='hidden' "
			                "name='csrf_token' value='%s'/>"
			                "  %node"
			                "  <button type='submit' class='btn "
			                "btn-primary hyle-picker-submit'>Add "
			                "Song</button>"
			                "</form>",
			                post_action,
			                csrf_token ? csrf_token : "", picker);
		}
	}

	for (int i = 0; i < n_songs; i++) {
		bud_node *row = render_song_row(
		        i, &songs[i], action, active_edit_row, row_pv,
		        active_edit_fmt_row, fmt_row_pv, &row_sibs);
		if (row)
			bud_append(rows, row);
	}

	bud_node *form = bud_tpl(
	        "<form action='%s' method='POST' enctype='multipart/form-data' "
	        "class='flex flex-col gap-4'>"
	        "  <input type='hidden' name='csrf_token' value='%s'/>"
	        "  <input type='hidden' name='amount' value='%s'/>"
	        "  <label>Title:"
	        "    <input type='text' name='title' value='%s'/>"
	        "  </label>"
	        "  %node"
	        "  %node"
	        "  <h3 class='mt-2'>Songs</h3>"
	        "  %node"
	        "  <div class='flex gap-2'>"
	        "    <button type='submit' class='btn btn-primary' "
	        "id='edit-form-submit'>Save Changes</button>"
	        "    <a href='%s' class='btn btn-secondary'>Cancel</a>"
	        "  </div>"
	        "</form>",
	        action, csrf_token ? csrf_token : "", amount_str,
	        title ? title : "", grp_fields,
	        (grp_id && grp_id[0])
	                ? bud_tpl("<label>Song source:"
	                          "  <select name='song_source' class='border "
	                          "rounded p-1 w-48'>"
	                          "    <option value='all' %b>All "
	                          "songs</option>"
	                          "    <option value='repertoire' "
	                          "%b>Repertoire only</option>"
	                          "  </select>"
	                          "</label>",
	                          (!song_source ||
	                           strcmp(song_source, "all") == 0)
	                                  ? "selected"
	                                  : NULL,
	                          (song_source &&
	                           strcmp(song_source, "repertoire") == 0)
	                                  ? "selected"
	                                  : NULL)
	                : NULL,
	        rows, cancel_href ? cancel_href : "");

	/* Sibling GET draft form */
	bud_node *grp_sib =
	        bud_tpl("<form id='pickq-grp' action='%s' method='GET' "
	                "class='pick-sibling-form'>"
	                "  <input type='hidden' name='title' value='%s'/>"
	                "</form>",
	                action, title ? title : "");

	return bud_tpl(
	        "%node"
	        "%node"
	        "%node"
	        "%node",
	        picker, form, grp_sib, row_sibs);
}
