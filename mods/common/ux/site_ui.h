#ifndef SITE_UI_H
#define SITE_UI_H

#include "bud/bud.h"
#include <stddef.h>

char *site_ui_page(
        const char *title,
        const char *extra_head,
        const char *module,
        bud_node *body);
bud_node *site_ui_layout(
        const char *title,
        const char *path,
        const char *icon,
        const char *user,
        bud_node *menu_items,
        bud_node *children);
bud_node *site_ui_menu(const char *user, const char *path, const char *icon);
bud_node *site_ui_item_menu(const char *module, const char *id, int is_owner);
bud_node *site_ui_form_actions(
        const char *cancel_href, const char *submit_label, bud_node *extra);
bud_node *site_ui_form_page(
        const char *user,
        const char *title,
        const char *path,
        const char *icon,
        const char *heading,
        bud_node *children);
bud_node *site_ui_empty_state(const char *message);
bud_node *site_ui_viewer_controls(
        const char *module,
        int zoom,
        const char *save_url,
        bud_event_handler_fn on_zoom_change,
        bud_node **zoom_text_out);
bud_node *site_ui_delete_confirm(
        const char *module,
        const char *id,
        const char *title,
        const char *csrf_token);
bud_node *site_ui_add_form(
        const char *module,
        const char *csrf_token,
        int has_error,
        const char *error_msg);
void parent_path(const char *path, char *buf, size_t len);
void site_ui_item_path(
        const char *module, const char *id, char *buf, size_t len);
void item_action_path(
        const char *module,
        const char *id,
        const char *action,
        char *buf,
        size_t len);
void site_ui_collection_path(const char *module, char *buf, size_t len);
void auth_path(const char *action, char *buf, size_t len);
void login_href(const char *ret, char *buf, size_t len);

/* ── Form field descriptor for generic form builder ── */
typedef struct {
	const char *name;
	const char *label;
	int type; /* 0=text, 1=textarea, 2=file */
} form_field_t;

/* ── Key name helpers ─────────────────────────────── */
const char *key_name(int semitones, int orig_key, int bemol, int latin);
const char *target_key_name(int orig_key, int transpose, int flags);

/* ── Zoom helpers ─────────────────────────────────── */
void ui_apply_zoom(bud_node *main_node, bud_node *zoom_label, int zoom);
int ui_on_zoom_change(
        bud_event *event,
        int *zoom_out,
        bud_node *main_node,
        bud_node *zoom_label);

/* ── Generic form field builder ───────────────────── */
bud_node *site_ui_form_fields(
        const form_field_t *fields,
        const char **values,
        const char *csrf_token);

/* ── Shared checkbox builder ────────────────────── */
bud_node *site_ui_checkbox(
        const char *name,
        const char *label,
        int checked,
        bud_event_handler_fn on_change);

/* ── Shared media slot renderer ──────────────────── */
bud_node *
site_ui_render_media_slot(const char *yt, const char *audio, const char *pdf);

#endif
