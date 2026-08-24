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

static bud_node *sb_picker_render(const sb_picker_spec_t *spec, list_state_t *st)
{
    char vb[8];
    const char *col_keys[LIST_MAX_COLS];
    const char *col_labels[LIST_MAX_COLS];
    hyle_bud_row_action_t act;
    bud_node *frag, *hint, *form, *chrome, *table, *pag, *post;
    int i;

    frag = bud_fragment();
    if (!frag)
        return NULL;

    for (i = 0; i < st->ncols && i < LIST_MAX_COLS; i++) {
        col_keys[i] = st->cols[i].key;
        col_labels[i] = st->cols[i].label;
    }

    if (spec->replace_index >= 0) {
        char idx_str[16];
        const char *cancel_path =
                spec->back && spec->back[0] ? spec->back : "/";
        snprintf(idx_str, sizeof(idx_str), "%d", spec->replace_index + 1);

        bud_node *head = lx_el(
                "div", lx_attr("class", "mb-2 font-medium"),
                lx_frag(lx_text("Replace Song #"), lx_text(idx_str),
                        lx_text("  \xe2\x80\x94  Replacing: "),
                        lx_text(spec->replace_title ? spec->replace_title : "")))
                                 .data.node;
        if (head)
            bud_append(frag, head);

        bud_node *cancel = lx_el(
                "a", lx_attr("href", cancel_path),
                lx_attr("class",
                        "btn btn-secondary text-xs mb-3 inline-block"),
                lx_text("Cancel"))
                                  .data.node;
        if (cancel)
            bud_append(frag, cancel);
    }

    if (list_has_query(st)) {
        hint = lx_el("div", lx_attr("class", "text-xs text-muted"),
                     lx_text(spec->hint ? spec->hint : "Click a song to add it."))
                 .data.node;
        if (hint)
            bud_append(frag, hint);
    }

    form = lx_el("form", lx_attr("method", "get"),
                 lx_attr("action", spec->get_action), lx_attr("class", "list-form"))
               .data.node;
    chrome = idx_filter_chrome(st);
    if (form && chrome)
        bud_append(form, chrome);

    if (form && spec->n_prefs > 0) {
        for (int k = 0; k < spec->n_prefs; k++) {
            bud_node *hid;
            snprintf(vb, sizeof(vb), "%d", spec->pref_vals[k]);
            hid = lx_el("input", lx_attr("type", "hidden"),
                        lx_attr("name", spec->pref_names[k]),
                        lx_attr("value", vb))
                      .data.node;
            if (hid)
                bud_append(form, hid);
        }
    }

    if (form && list_has_query(st)) {
        act.kind = HYLE_ROW_ACTION_SUBMIT;
        act.css_class = NULL;
        act.label = NULL;
        act.aria_base = spec->aria_base ? spec->aria_base : "Add";
        act.href_base = NULL;
        act.form_id = spec->form_id ? spec->form_id : "sb-pick-post";
        act.field_name = "song_id";
        table = hyle_bud_table_actions(
                col_keys, col_labels, st->ncols,
                (const char **)st->ids,
                st->nids,
                (const char **)st->values, "song",
                st->sort_field, st->sort_asc,
                st->query, &act);
        pag = hyle_bud_pagination(
                st->page, st->per_page,
                st->total, st->nids, "");
        if (table)
            bud_append(form, table);
        if (pag)
            bud_append(form, pag);
    }
    if (form)
        bud_append(frag, form);

    post = lx_el("form", lx_attr("id", spec->form_id ? spec->form_id : "sb-pick-post"),
                 lx_attr("method", "post"), lx_attr("action", spec->post_action),
                 lx_el("input", lx_attr("type", "hidden"),
                       lx_attr("name", "csrf_token"),
                       lx_attr("value", spec->csrf ? spec->csrf : "")))
               .data.node;

    if (spec->replace_index >= 0) {
        char n_str[16];
        snprintf(n_str, sizeof(n_str), "%d", spec->replace_index);
        bud_append(post,
                   lx_el("input", lx_attr("type", "hidden"),
                         lx_attr("name", "n"),
                         lx_attr("value", n_str))
                 .data.node);
    }

    if (spec->back && spec->back[0]) {
        bud_append(post,
                   lx_el("input", lx_attr("type", "hidden"),
                         lx_attr("name", "back"),
                         lx_attr("value", spec->back))
                 .data.node);
    }

    if (post)
        bud_append(frag, post);

    return frag;
}

#endif