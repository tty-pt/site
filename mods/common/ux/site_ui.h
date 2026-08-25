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
#define SITE_UI_FRAGMENTS_SCRIPT \
	"<script defer src=\"/hyle-fragments.js" SITE_FRAGMENTS_V \
	"\"></script>"

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
#define FF_REF_NONE 0
#define FF_REF_SINGLE 1
#define FF_REF_MULTI 2
/* Target total at or below this renders an inline select/checkbox grid;
 * above it, the omnisearch picker. Descriptor max_inline=0 → default. */
#define FF_PICKER_THRESHOLD 50

typedef struct {
	const char *name;
	const char *label;
	int type;                /* 0=text, 1=textarea, 2=file */
	int ref;                 /* FF_REF_NONE | SINGLE | MULTI */
	const char *target;      /* dataset id when ref != FF_REF_NONE */
	int max_inline;          /* 0 = default FF_PICKER_THRESHOLD */
} form_field_t;

/* ── Omnisearch picker view (filled native-side by index's
 * pick_view_collect; WASM callers pass NULL and ref fields degrade to
 * plain text inputs) ── */
#define FF_PICKER_MAX_FIELDS HYLE_BUD_PICKER_MAX_FIELDS

typedef hyle_bud_picker_entry_t pick_entry_t;
typedef hyle_bud_picker_view_t pick_view_t;

/* ── Zoom helpers ─────────────────────────────────── */
void ui_apply_zoom(bud_node *main_node, bud_node *zoom_label, int zoom);
int ui_on_zoom_change(
        bud_event *event, int *zoom_out, bud_node *main_node,
        bud_node *zoom_label);

/* ── Generic form field builder ───────────────────── */
bud_node *site_ui_form_fields(
        const form_field_t *fields, const char **values,
        const char *csrf_token);

/* Descriptor-driven builder with omnisearch picker support: text/
 * textarea/file exactly as site_ui_form_fields; ref fields render by
 * runtime threshold (inline select/grid below, picker above). pv may
 * be NULL (no picker data — ref fields degrade to text inputs). */
bud_node *site_ui_form_fields_ex(
        const form_field_t *fields, const char **values,
        const char *csrf_token, const pick_view_t *pv);

/* Sibling GET form for no-JS draft round-trips (search/paging controls
 * bind to it via the HTML5 form= attribute): hidden mirrors of every
 * non-file field's current value plus each ref field's slugs and the
 * per_page default. Emitted adjacent to — never inside — the main POST
 * form. Tracks the ~2KB query-string budget; mirrors beyond it are
 * dropped (degrades to saved-state behavior). */
bud_node *site_ui_sibling_get_form(
        const char *action, const form_field_t *fields,
        const char **values, const pick_view_t *pv);

/* Name key of the page's first ref field (used to derive the shared
 * sibling GET form id "pickq-<key>"); NULL when the descriptor has no
 * ref fields. */
const char *site_ui_pick_form_id(const form_field_t *fields);

/* ── Action / Standalone Picker Component ───────────── */
typedef struct {
	const char *key;           /* field name, e.g. "song_id" */
	const char *label;         /* field label, e.g. "Song:" */
	const char *target;        /* target dataset, e.g. "song.items" */
	const char *get_action;    /* action URL for sibling GET form */
	const char *post_action;   /* action URL for POST form */
	const char *form_id;       /* POST form ID, default "pick-post" */
	const char *csrf_token;    /* CSRF token for POST form */
	const char *submit_label;  /* submit button text, default "Add" */
	const char *hint;          /* optional hint text above picker */
	const char *cancel_href;   /* optional cancel link href */
	const char *cancel_label;  /* optional cancel link label */
	const char *header_text;   /* optional header text above picker */
	const char *search_param;  /* custom search input name; NULL defaults to "pick_q_<key>" */
	const char *page_param;    /* custom page input name; NULL defaults to "pick_page_<key>" */
	const char **pref_names;   /* optional extra GET input names */
	const int *pref_vals;      /* optional extra GET input integer values */
	int n_prefs;               /* number of pref entries */
	bud_node *extra_post_inputs; /* optional extra hidden inputs for POST form */
} site_ui_action_picker_spec_t;

bud_node *site_ui_action_picker(
        const site_ui_action_picker_spec_t *spec, const pick_view_t *pv);

/* ── WASM / SSR Picker State JSON Serialization ───────── */
#define SITE_UI_PICKER_MAX_OPTS HYLE_BUD_PICKER_MAX_OPTS

typedef hyle_bud_picker_buffer_t site_ui_picker_buffer_t;

void site_ui_picker_state_from_json(
        const char *json, size_t jlen,
        const char *key, const char *target, int multi,
        const char *q, int page,
        site_ui_picker_buffer_t *buf, pick_view_t *pv_out);

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

/* ── WASM state helpers (D01) ─────────────────────────────────── */
char *site_ui_state_head(const char *json);
int site_ui_respond_with_state(
        int fd, const char *title, const char *path, const char *icon,
        const char *user, const char *state_json, const char *module,
        bud_node *body);

#endif
