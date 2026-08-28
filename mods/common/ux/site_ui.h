#ifndef SITE_UI_H
#define SITE_UI_H

#include "bud/bud.h"
#include "site_chrome.h"
#include <stddef.h>
#include <hyle-bud/hyle-bud.h>

/* Progressive-enhancement transport for pickers (always-on,
 * self-gating; see docs/SSR-CONTRACT.md). Versioned via the generated
 * asset header when present (phase 5 adds SITE_FRAGMENTS_V there). */
#if __has_include("version.gen.h")
#include "version.gen.h"
#endif
#ifndef SITE_FRAGMENTS_V
#define SITE_FRAGMENTS_V "?v=1"
#endif
#define SITE_UI_FRAGMENTS_SCRIPT                                               \
	"<script defer src=\"/hyle-fragments.js" SITE_FRAGMENTS_V "\"></"      \
	"script>"

char *site_ui_page(
        const char *title, const char *path, const char *icon, const char *user,
        const char *extra_head, const char *module, bud_node *body);
bud_node *site_ui_layout(
        const char *title, const char *path, const char *icon, const char *user,
        bud_node *menu_items, bud_node *children);
const char *site_ui_module_icon(const char *module);
const char *site_ui_module_display(const char *module);
bud_node *site_ui_menu(const char *user, const char *path);
bud_node *site_ui_item_menu(const char *module, const char *id, int is_owner);
bud_node *site_ui_form_actions(
        const char *cancel_href, const char *submit_label, bud_node *extra);
bud_node *site_ui_form_page(
        const char *user, const char *title, const char *path, const char *icon,
        const char *heading, const char *extra_head, bud_node *children);
bud_node *site_ui_empty_state(const char *message);
bud_node *site_ui_viewer_controls(
        const char *module, int zoom, const char *save_url,
        bud_event_handler_fn on_zoom_change, bud_node **zoom_text_out);
bud_node *site_ui_delete_confirm(
        const char *module, const char *id, const char *title,
        const char *csrf_token);
bud_node *site_ui_add_form(
        const char *module, const char *csrf_token, int has_error,
        const char *error_msg);
void parent_path(const char *path, char *buf, size_t len);
void site_ui_item_path(
        const char *module, const char *id, char *buf, size_t len);
void item_action_path(
        const char *module, const char *id, const char *action, char *buf,
        size_t len);
void site_ui_collection_path(const char *module, char *buf, size_t len);
void auth_path(const char *action, char *buf, size_t len);
void login_href(const char *ret, char *buf, size_t len);

/* ── Form field descriptor for generic form builder ── */
#ifndef BUD_RECORD
#define BUD_RECORD 0
#define BUD_EXCLUDE 1
#define BUD_REF_DISPLAY 2
#define BUD_OVERLAY_INT 3
#define BUD_OVERLAY_STR 4
#define BUD_INVERSE 5
#endif

/* ── Omnisearch picker view (filled native-side by
 * hyle_bud_picker_view_collect_schema; WASM callers pass NULL and ref
 * fields degrade to plain text inputs) ── */
#define FF_PICKER_MAX_FIELDS HYLE_BUD_PICKER_MAX_FIELDS

typedef hyle_bud_picker_entry_t pick_entry_t;
typedef hyle_bud_picker_view_t pick_view_t;

/* ── Zoom helpers ─────────────────────────────────── */
void ui_apply_zoom(bud_node *main_node, bud_node *zoom_label, int zoom);
int ui_on_zoom_change(
        bud_event *event, int *zoom_out, bud_node *main_node,
        bud_node *zoom_label);

/* ── String-First Action & Replace Picker Components ──────── */
bud_node *site_ui_picker(
        const char *target, const char *post_action, const char *get_action,
        const char *csrf_token, const pick_view_t *pv, int auto_submit);

bud_node *site_ui_row_replace_picker(
        const char *target, int row_idx, const char *cur_id,
        const char *cur_title, const char *post_action, const char *back_href,
        const char *csrf_token, const pick_view_t *pv);

/* ── Reusable Action Form & Item Row Primitives ─────────── */
bud_node *site_ui_action_form(
        const char *action, const char *csrf_token, const char *method,
        bud_node *inputs, const char *btn_label, const char *btn_class);

bud_node *site_ui_item_row(
        const char *title, const char *href, const char *subtitle,
        bud_node *action_controls);

/* ── Declarative Schema-Driven Form Builder ─────────── */
/* Builds a complete POST form (and sibling GET form if ref fields exist)
 * directly from hyle_schema_desc_t schema table and struct data. */
bud_node *site_ui_form_from_desc(
        const char *action, const char *cancel_href, const char *submit_label,
        const hyle_schema_desc_t *desc, const void *struct_ptr,
        const char *csrf_token, const pick_view_t *pv, const char *vstr_val);

/* ── Action / Standalone Picker Component ───────────── */
typedef struct {
	const char *key;        /* field name, e.g. "song_id" */
	const char *label;      /* field label, e.g. "Song:" */
	const char *target;     /* target dataset, e.g. "song.items" */
	const char *default_id; /* optional pre-selected default value ID */
	const char
	        *default_label;  /* optional pre-selected default value label */
	const char *get_action;  /* action URL for sibling GET form */
	const char *post_action; /* action URL for POST form */
	const char *form_id;     /* POST form ID, default "pick-post" */
	const char *csrf_token;  /* CSRF token for POST form */
	const char *submit_label; /* submit button text, default "Add" */
	const char *hint;         /* optional hint text above picker */
	const char *cancel_href;  /* optional cancel link href */
	const char *cancel_label; /* optional cancel link label */
	const char *header_text;  /* optional header text above picker */
	const char *scope; /* scope suffix, e.g. "0" for pick_q_<key>__0 */
	int auto_submit; /* 1 = auto-submit on radio change when scripts active
	                  */
	const char *search_param; /* custom search input name; NULL defaults to
	                             "pick_q_<key>" */
	const char *page_param;   /* custom page input name; NULL defaults to
	                             "pick_page_<key>" */
	const char **pref_names;  /* optional extra GET input names */
	const int *pref_vals;     /* optional extra GET input integer values */
	int n_prefs;              /* number of pref entries */
	bud_node *extra_post_inputs; /* optional extra hidden inputs for POST
	                                form */
} site_ui_action_picker_spec_t;

bud_node *site_ui_action_picker(
        const site_ui_action_picker_spec_t *spec, const pick_view_t *pv);

/* ── Generic Row / Cell Picker Primitives ─────────────────── */
bud_node *site_ui_cell_picker(
        const char *target, const char *key, int row_idx, const char *cur_id,
        const char *cur_title, const char *get_action, const char *post_action,
        const char *csrf_token, const pick_view_t *pv, int is_active,
        const char *extra_class, bud_node **sibling_out);

/* ── Generic Customizable Filter Bar ──────────────────────── */
#define FILTER_SEARCH 0
#define FILTER_SINGLE_DROPDOWN 1
#define FILTER_MULTISELECT 2
#define FILTER_CUSTOM 3

typedef struct {
	const char *field;
	const char *label;
	const char *target;
	const char *current_val;
	const char *filter_style;
	int kind;
	int default_op;
	int collapsible;
} site_ui_filter_spec_t;

bud_node *site_ui_filter_bar(
        const site_ui_filter_spec_t *specs, int n_specs, const char *action,
        const char *current_q, const pick_view_t *pv);

/* ── WASM / SSR Picker State JSON Serialization ───────── */
#define SITE_UI_PICKER_MAX_OPTS HYLE_BUD_PICKER_MAX_OPTS

typedef hyle_bud_picker_buffer_t site_ui_picker_buffer_t;

void site_ui_picker_state_from_json(
        const char *json, size_t jlen, const char *key, const char *target,
        int multi, const char *q, int page, site_ui_picker_buffer_t *buf,
        pick_view_t *pv_out);

#ifndef __wasm__
struct json_object;
void site_ui_picker_state_to_json(
        const pick_view_t *pv, struct json_object *j_root);
#endif

/* ── Shared checkbox builder ────────────────────── */
bud_node *site_ui_checkbox(
        const char *name, const char *label, int checked,
        bud_event_handler_fn on_change);

/* ── Shared media slot renderer ──────────────────── */
bud_node *
site_ui_render_media_slot(const char *yt, const char *audio, const char *pdf);

/* ── Shared media HTML string builder (for WASM innerHTML patching) ─── */
int site_ui_build_media_html(
        const char *yt, const char *audio, const char *pdf, char *out,
        size_t out_sz);

/* ── HTML sanitization for safe user markup ──────────────────── */
char *site_ui_sanitize_html(const char *raw);

/* ── WASM state helpers (D01) ─────────────────────────────────── */
char *site_ui_state_head(const char *json);
int site_ui_respond_with_state(
        int fd, const char *title, const char *path, const char *icon,
        const char *user, const char *state_json, const char *module,
        bud_node *body);

#endif
