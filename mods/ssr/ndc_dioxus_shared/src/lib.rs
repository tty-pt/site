pub mod blueprint;
pub mod hyle_ssr;

pub use blueprint::get_blueprint;
pub use hyle_ssr::{items_to_source, item_to_source, render_hyle_edit, render_hyle_list};

use dioxus::prelude::*;
use dioxus_ssr::render_element;

use url::form_urlencoded;

pub struct ModuleRef<'a> {
    pub id:    &'a str,
    pub title: &'a str,
    pub flags: u32,
}

impl<'a> ModuleRef<'a> {
    pub fn enabled(&self) -> bool {
        self.flags != 0
    }
}

pub struct RequestContext<'a> {
    pub method:      &'a str,
    pub path:        &'a str,
    pub query:       &'a str,
    pub body:        &'a [u8],
    pub remote_user: Option<&'a str>,
    pub modules:     &'a [ModuleRef<'a>],
    pub csrf_token:  &'a str,
}

pub struct ResponsePayload {
    pub status: u16,
    pub content_type: String,
    pub location: Option<String>,
    pub body: String,
}

/// Safe borrowed view of song item data passed from C via FFI.
pub struct SongItem<'a> {
    pub title:        &'a str,
    pub data:         &'a str,
    pub yt:           &'a str,
    pub audio:        &'a str,
    pub pdf:          &'a str,
    pub categories:   &'a str,
    pub author:       &'a str,
    pub original_key: i32,
    pub viewer_zoom:  i32,
    pub show_media:   bool,
    pub viewer_bemol: bool,
    pub viewer_latin: bool,
    pub owner:        bool,
}

/// Safe borrowed view of poem item data passed from C via FFI.
pub struct PoemItem<'a> {
    pub title:        &'a str,
    pub head_content: &'a str,
    pub body_content: &'a str,
    pub owner:        bool,
}

pub struct SongbookSong<'a> {
    pub chord_id:    &'a str,
    pub format:      &'a str,
    pub chord_title: &'a str,
    pub chord_data:  &'a str,
    pub transpose:   i32,
    pub original_key: i32,
}

/// Safe borrowed view of songbook item data passed from C via FFI.
pub struct SongbookItem<'a> {
    pub title:      &'a str,
    pub owner:      &'a str,
    pub choir:      &'a str,
    pub viewer_zoom: i32,
    pub songs:      Vec<SongbookSong<'a>>,
}

pub struct ChoirSong<'a> {
    pub id:            &'a str,
    pub title:         &'a str,
    pub format:        &'a str,
    pub preferred_key: i32,
    pub original_key:  i32,
}

pub struct ChoirEntry<'a> {
    pub id:    &'a str,
    pub title: &'a str,
}

/// Safe borrowed view of choir item data passed from C via FFI.
pub struct ChoirItem<'a> {
    pub title:      &'a str,
    pub owner_name: &'a str,
    pub formats:    &'a str,
    pub songs:      Vec<ChoirSong<'a>>,
    pub all_songs:  Vec<ChoirEntry<'a>>,
    pub songbooks:  Vec<ChoirEntry<'a>>,
}

/// Interpret a raw byte body as UTF-8 text. Zero allocation; returns "" on invalid UTF-8.
pub fn body_str(b: &[u8]) -> &str {
    std::str::from_utf8(b).unwrap_or("")
}

pub fn split_path(path: &str) -> Vec<&str> {
    path.trim_matches('/')
        .split('/')
        .filter(|part| !part.is_empty())
        .collect()
}

pub fn parse_pairs(text: &str) -> Vec<(String, String)> {
    form_urlencoded::parse(text.as_bytes())
        .into_owned()
        .collect()
}

pub fn get_pair<'a>(pairs: &'a [(String, String)], key: &str) -> Option<&'a str> {
    pairs.iter().find(|(k, _)| k == key).map(|(_, v)| v.as_str())
}

pub fn current_user<'a>(ctx: &RequestContext<'a>) -> Option<&'a str> {
    ctx.remote_user
}

pub fn login_href(ret: &str) -> String {
    let mut out = form_urlencoded::Serializer::new(String::new());
    out.append_pair("ret", ret);
    format!("/auth/login?{}", out.finish())
}

pub fn login_redirect(ctx: &RequestContext<'_>) -> ResponsePayload {
    let ret: String = if ctx.query.is_empty() {
        ctx.path.to_string()
    } else {
        format!("{}?{}", ctx.path, ctx.query)
    };
    ResponsePayload {
        status: 303,
        content_type: "text/html; charset=utf-8".to_string(),
        location: Some(login_href(&ret)),
        body: String::new(),
    }
}

fn render_document_with_head(title: &str, body: Element, extra_head: &str) -> String {
    let body_html = render_element(body);
    format!(
        "<!DOCTYPE html><html lang=\"pt\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>{}</title><link rel=\"stylesheet\" href=\"/styles.css\"><link rel=\"stylesheet\" href=\"/hyle.css\">{}</head><body style=\"margin: 0\">{}<script type=\"module\" src=\"/wasm.js\"></script></body></html>",
        escape_html(title),
        extra_head,
        body_html
    )
}

fn render_document(title: &str, body: Element) -> String {
    render_document_with_head(title, body, "")
}

pub fn html_response(title: &str, body: Element) -> ResponsePayload {
    html_response_with_head(title, "", body)
}

pub fn html_response_with_head(
    title: &str,
    extra_head: &str,
    body: Element,
) -> ResponsePayload {
    ResponsePayload {
        status: 200,
        content_type: "text/html; charset=utf-8".to_string(),
        location: None,
        body: render_document_with_head(title, body, extra_head),
    }
}

pub fn html_response_with_status(
    status: u16,
    title: &str,
    body: Element,
) -> ResponsePayload {
    ResponsePayload {
        status,
        content_type: "text/html; charset=utf-8".to_string(),
        location: None,
        body: render_document(title, body),
    }
}

pub fn parent_path(path: &str) -> String {
    let mut parts: Vec<&str> = split_path(path);
    parts.pop();
    if parts.is_empty() {
        "/".to_string()
    } else {
        format!("/{}", parts.join("/"))
    }
}

pub fn collection_path(module: &str) -> String {
    format!("/{module}/")
}

pub fn item_path(module: &str, id: &str) -> String {
    format!("/{module}/{id}")
}

pub fn item_action_path(module: &str, id: &str, action: &str) -> String {
    format!("/{module}/{id}/{action}")
}

pub fn auth_path(action: &str) -> String {
    format!("/auth/{action}")
}

fn escape_html(input: &str) -> String {
    input
        .replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
}

pub fn key_names(use_bemol: bool, use_latin: bool) -> &'static [&'static str] {
    match (use_bemol, use_latin) {
        (false, false) => &["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"],
        (true, false) => &["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"],
        (false, true) => &["Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"],
        (true, true) => &["Do", "Reb", "Re", "Mib", "Mi", "Fa", "Solb", "Sol", "Lab", "La", "Sib", "Si"],
    }
}

pub fn key_transpose_options(original_key: i32, use_bemol: bool, use_latin: bool) -> Vec<(i32, String)> {    key_names(use_bemol, use_latin)
        .iter()
        .enumerate()
        .map(|(i, key)| {
            let semitones = ((i as i32 - original_key) % 12 + 12) % 12;
            let suffix = if semitones == 0 { " (Original)" } else { "" };
            (semitones, format!("{key}{suffix}"))
        })
        .collect()
}

pub fn display_or_id<'a>(title: &'a str, id: &'a str) -> &'a str {
    if title.is_empty() { id } else { title }
}

pub fn parse_json_array<T: for<'de> serde::Deserialize<'de>>(raw: &str) -> Vec<T> {
    serde_json::from_str(raw).unwrap_or_default()
}

pub fn edit_path(module: &str, id: &str) -> String {
    format!("/{module}/{id}/edit")
}

pub fn item_menu(module: &str, id: &str, is_owner: bool) -> Element {
    if !is_owner {
        return rsx! {};
    }
    let edit_href = item_action_path(module, id, "edit");
    let delete_href = item_action_path(module, id, "delete");
    rsx! {
        a { href: "{edit_href}", class: "btn",
            span { "✏️" }
            span { "edit" }
        }
        a { href: "{delete_href}", class: "btn",
            span { "🗑️" }
            span { "delete" }
        }
    }
}

pub fn menu(user: Option<&str>, path: &str, icon: Option<&str>) -> Element {
    let is_home = path == "/" || path.is_empty();
    let icon = icon.unwrap_or("🏠");
    let login_link = login_href(path);
    let register_href = auth_path("register");
    let up_href = parent_path(path);
    rsx! {
        if !is_home {
            a { class: "btn", href: "{up_href}",
                span { "{icon}" }
                span { "go up" }
            }
        }
        if let Some(user) = user {
            a { class: "btn", href: "/{user}/",
                span { "😊" }
                span { "me" }
            }
            a { class: "btn", href: "/auth/logout",
                span { "🚪" }
                span { "logout" }
            }
        } else {
            a { class: "btn", href: "{login_link}",
                span { "🔑" }
                span { "login" }
            }
            a { class: "btn", href: "{register_href}",
                span { "📝" }
                span { "register" }
            }
        }
    }
}

pub fn layout(
    user: Option<&str>,
    title: &str,
    path: &str,
    icon: Option<&str>,
    menu_items: Option<Element>,
    children: Element,
) -> Element {
    rsx! {
        main { class: "main",
            h1 { "{title}" }
            { children }
        }
        nav { class: "menu",
            input { id: "menu-functions", name: "functions", r#type: "checkbox", class: "hidden" }
            label { r#for: "menu-functions", class: "menu-overlay", aria_label: "Close Menu" }
            span { class: "functions flex-1 fixed right-0 z-50 h-full overflow-y-auto text-sm capitalize flex flex-col p-4",
                div { class: "relative z-20 flex flex-col gap-2",
                    { menu(user, path, icon) }
                    if let Some(menu_items) = menu_items {
                        div { class: "menu-separator" }
                        div { class: "module-menu", { menu_items } }
                    }
                }
                label { r#for: "menu-functions", class: "absolute inset-0 z-10 cursor-pointer", aria_label: "Close Menu" }
            }
            span { class: "fixed top-0 right-0 z-30 p-2 flex items-center gap-4",
                label { r#for: "menu-functions", class: "menu-toggle flex items-center justify-center cursor-pointer text-base btn", aria_label: "Menu", "⚙️" }
            }
        }
    }
}

pub fn form_actions(cancel_href: &str, submit_label: &str, extra: Option<Element>) -> Element {
    rsx! {
        div { class: "flex gap-2",
            button { r#type: "submit", class: "btn btn-primary", "{submit_label}" }
            if let Some(extra) = extra {
                { extra }
            }
            a { href: "{cancel_href}", class: "btn btn-secondary", "Cancel" }
        }
    }
}

pub fn form_page(
	user: Option<&str>,
	title: &str,
	path: &str,
    icon: Option<&str>,
    heading: Option<&str>,
    children: Element,
) -> Element {
    layout(
        user,
        title,
        path,
        icon,
        None,
        rsx! {
            div { class: "center",
                if let Some(heading) = heading {
                    h1 { "{heading}" }
                }
                { children }
            }
        },
	)
}

pub fn edit_form_page(
	user: Option<&str>,
	title: &str,
	path: &str,
	icon: Option<&str>,
	children: Element,
) -> ResponsePayload {
	html_response(
		title,
		form_page(user, title, path, icon, None, children),
	)
}

pub fn error_page(
	user: Option<&str>,
	path: &str,
    status: u16,
    message: &str,
) -> Element {
    layout(
        user,
        &status.to_string(),
        path,
        None,
        None,
        rsx! { p { "{message}" } },
    )
}

pub fn empty_state(message: &str) -> Element {
    rsx! { p { class: "text-muted", "{message}" } }
}

pub struct IndexItem {
    pub id: String,
    pub title: String,
    pub extra: Vec<(String, String)>, // e.g. [("type", "Comunhão")]
}

/// Parse a list body where lines are either:
///   tab-separated:   "id\ttitle\textra1\textra2\r\n"  (rich format from song_format_line)
///   space-separated: "id title\r\n"                   (generic format from index_render_list)
pub fn parse_index_items_rich(body: &str, extra_keys: &[&str]) -> Vec<IndexItem> {
    body.lines()
        .filter_map(|line| {
            let line = line.trim_end_matches('\r').trim();
            if line.is_empty() {
                return None;
            }
            if line.contains('\t') {
                let mut parts = line.splitn(2 + extra_keys.len(), '\t');
                let id = parts.next()?.to_string();
                let title = parts.next().unwrap_or("").to_string();
                let title = if title.is_empty() { id.clone() } else { title };
                let extra = extra_keys
                    .iter()
                    .zip(parts)
                    .map(|(k, v)| (k.to_string(), v.to_string()))
                    .collect();
                Some(IndexItem { id, title, extra })
            } else {
                let idx = line.find(' ')?;
                let id = line[..idx].to_string();
                let title = line[idx + 1..].to_string();
                let title = if title.is_empty() { id.clone() } else { title };
                Some(IndexItem { id, title, extra: vec![] })
            }
        })
        .collect()
}

/// Render a filterable index table for a module.
///
/// `filter_fields` is a slice of `(key, label)` pairs used to build the filter form
/// and table columns. The first element must always be `("title", ...)`.
/// For any field named `"type"`, a `<select>` dropdown is rendered (populated from
/// distinct values in `items`); all other fields get a text `<input>`.
///
/// Filtering is applied in Rust: each active query param is matched
/// case-insensitively against the corresponding field of each item.
pub fn render_index_table(
    ctx: &RequestContext<'_>,
    module: &str,
    icon: Option<&str>,
    items: Vec<IndexItem>,
    filter_fields: &[(&str, &str)],
) -> ResponsePayload {
    let query_pairs = parse_pairs(ctx.query);

    // Collect active filter values from query string
    let filters: Vec<(String, String)> = filter_fields
        .iter()
        .map(|(key, _)| {
            let val = get_pair(&query_pairs, key).unwrap_or("").to_string();
            (key.to_string(), val)
        })
        .collect();

    // Collect distinct values for "type" field (for select dropdown)
    let type_values: Vec<String> = {
        let mut vals: Vec<String> = items
            .iter()
            .flat_map(|item| {
                item.extra
                    .iter()
                    .filter(|(k, _)| k == "type")
                    .map(|(_, v)| v.clone())
            })
            .collect();
        vals.sort();
        vals.dedup();
        vals
    };

    // Apply filters
    let filtered: Vec<&IndexItem> = items
        .iter()
        .filter(|item| {
            filters.iter().all(|(key, val)| {
                if val.is_empty() {
                    return true;
                }
                let val_lower = val.to_lowercase();
                if key == "title" {
                    item.title.to_lowercase().contains(&val_lower)
                } else {
                    item.extra
                        .iter()
                        .find(|(k, _)| k == key)
                        .map(|(_, v)| {
                            // for type: exact match (it's a select)
                            if key == "type" {
                                v.to_lowercase() == val_lower
                            } else {
                                v.to_lowercase().contains(&val_lower)
                            }
                        })
                        .unwrap_or(false)
                }
            })
        })
        .collect();

    let add_href = format!("/{module}/add");
    let collection_href = format!("/{module}/");
    let menu_items = crate::current_user(ctx).map(|_| {
        rsx! {
            a { href: "{add_href}", class: "btn",
                span { "➕" }
                label { "add" }
            }
        }
    });

    // Build filter_fields and type_values as owned data for the closure
    let filter_fields_owned: Vec<(String, String)> = filter_fields
        .iter()
        .map(|(k, l)| (k.to_string(), l.to_string()))
        .collect();

    let has_extra_cols = filter_fields.len() > 1;

    crate::html_response(
        module,
        crate::layout(
            crate::current_user(ctx),
            module,
            &collection_href,
            Some(icon.unwrap_or("🏠")),
            menu_items,
            rsx! {
                div { class: "center",
                    // Filter form
                    form { method: "get", action: "{collection_href}", class: "flex flex-wrap gap-2 mb-4 items-end",
                        for (key, label) in filter_fields_owned.iter() {
                            label { class: "flex flex-col gap-1 text-sm",
                                "{label}"
                                if key == "type" {
                                    select { name: "{key}", class: "p-1 border rounded",
                                        option { value: "", "All" }
                                        for type_val in type_values.iter() {
                                            {
                                                let selected = filters.iter()
                                                    .find(|(k, _)| k == key)
                                                    .map(|(_, v)| v == type_val)
                                                    .unwrap_or(false);
                                                rsx! {
                                                    option {
                                                        value: "{type_val}",
                                                        selected,
                                                        "{type_val}"
                                                    }
                                                }
                                            }
                                        }
                                    }
                                } else {
                                    {
                                        let cur_val = filters.iter()
                                            .find(|(k, _)| k == key)
                                            .map(|(_, v)| v.as_str())
                                            .unwrap_or("");
                                        rsx! {
                                            input {
                                                r#type: "text",
                                                name: "{key}",
                                                value: "{cur_val}",
                                                class: "p-1 border rounded"
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        button { r#type: "submit", class: "btn", "Filter" }
                    }
                    // Results
                    if filtered.is_empty() {
                        { crate::empty_state("No items found.") }
                    } else {
                        table { class: "w-full max-w-2xl text-left border-collapse",
                            thead {
                                tr {
                                    for (_, label) in filter_fields_owned.iter() {
                                        th { class: "border-b p-2 text-sm font-semibold", "{label}" }
                                    }
                                }
                            }
                            tbody {
                                for item in filtered.iter() {
                                    tr { class: "border-b hover:bg-surface",
                                        td { class: "p-2",
                                            a { href: "/{module}/{item.id}/", class: "hover:underline", "{item.title}" }
                                        }
                                        if has_extra_cols {
                                            for (key, _) in filter_fields_owned.iter().skip(1) {
                                                td { class: "p-2 text-sm text-muted",
                                                    {
                                                        let v = item.extra.iter()
                                                            .find(|(k, _)| k == key)
                                                            .map(|(_, v)| v.as_str())
                                                            .unwrap_or("");
                                                        rsx! { "{v}" }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            },
        ),
    )
}

fn parse_index_items(body: &str) -> Vec<(String, String)> {
    body.lines()
        .filter_map(|line| {
            let trimmed = line.trim();
            if trimmed.is_empty() {
                return None;
            }
            let idx = trimmed.find(' ')?;
            let id = trimmed[..idx].to_string();
            let title = trimmed[idx + 1..].to_string();
            let label = if title.is_empty() { id.clone() } else { title };
            Some((id, label))
        })
        .collect()
}

pub fn render_list(ctx: &RequestContext<'_>, module: &str, icon: Option<&str>) -> ResponsePayload {
    let display_items = parse_index_items(body_str(ctx.body));
    let add_href = format!("/{module}/add");
    let menu_items = crate::current_user(ctx).map(|_| {
        rsx! {
            a { href: "{add_href}", class: "btn",
                span { "➕" }
                label { "add" }
            }
        }
    });
    crate::html_response(
        module,
        crate::layout(
            crate::current_user(ctx),
            module,
            &format!("/{module}"),
            Some(icon.unwrap_or("🏠")),
            menu_items,
            rsx! {
                div { class: "center",
                    if display_items.is_empty() {
                        { crate::empty_state("No items yet.") }
                    } else {
                        for (id, label) in display_items {
                            a { href: "/{module}/{id}/", class: "btn", "{label}" }
                        }
                    }
                }
            },
        ),
    )
}

pub fn render_add_form(
    ctx: &RequestContext<'_>,
    module: &str,
    icon: Option<&str>,
    extra_fields: Vec<(&str, String)>,
) -> ResponsePayload {
    render_add_form_with_error(ctx, module, icon, extra_fields, None)
}

pub fn render_add_form_with_error(
    ctx: &RequestContext<'_>,
    module: &str,
    icon: Option<&str>,
    extra_fields: Vec<(&str, String)>,
    error: Option<&str>,
) -> ResponsePayload {
    if crate::current_user(ctx).is_none() {
        return crate::login_redirect(ctx);
    }
    let path = format!("/{module}/add");
    let error = error.map(|s| s.to_string());
    crate::html_response(
        "Add Item",
        crate::layout(
            crate::current_user(ctx),
            "Add Item",
            &path,
            Some(icon.unwrap_or("🏠")),
            None,
            rsx! {
                if let Some(msg) = error {
                    p { class: "text-error", "{msg}" }
                }
                form {
                    action: "{path}",
                    method: "POST",
                    enctype: "multipart/form-data",
                    class: "flex flex-col gap-4",
                    input { r#type: "hidden", name: "csrf_token", value: "{ctx.csrf_token}" }
                    label { "Title:"
                        input { name: "title" }
                    }
                    for (name, value) in extra_fields {
                        input { r#type: "hidden", name: "{name}", value: "{value}" }
                    }
                    div { class: "flex gap-2",
                        button { r#type: "submit", class: "btn btn-primary", "Add" }
                        a { href: "{crate::collection_path(module)}", class: "btn btn-secondary", "Cancel" }
                    }
                }
            },
        ),
    )
}

pub type CrudHandler = fn(&RequestContext<'_>, &str) -> ResponsePayload;

/// Standard CRUD route dispatcher for modules that only have the default 5 arms.
/// Modules with extra arms can call this and fall back with `.or_else(|| ...)`.
pub fn default_crud_routes(
    ctx: &RequestContext<'_>,
    module: &str,
    icon: Option<&str>,
    render_detail: Option<CrudHandler>,
    render_edit: Option<CrudHandler>,
) -> Option<ResponsePayload>
{
    let parts = split_path(&ctx.path);
    match (ctx.method, parts.as_slice()) {
        ("GET", [m, "add"]) if *m == module =>
            Some(render_add_form(ctx, module, icon, Vec::new())),
        ("POST", [m, "add"]) if *m == module => {
            let body = body_str(ctx.body);
            let pairs = parse_pairs(body);
            let error = get_pair(&pairs, "error").unwrap_or("An error occurred");
            Some(render_add_form_with_error(ctx, module, icon, Vec::new(), Some(error)))
        }
        ("POST", [m]) if *m == module =>
            Some(render_list(ctx, module, icon)),
        ("POST", [m, id, "delete"]) if *m == module =>
            Some(render_delete_confirm(module, id, "", ctx)),
        ("POST", [m, id]) if *m == module =>
            render_detail.map(|f| f(ctx, id)),
        ("POST", [m, id, "edit"]) if *m == module =>
            render_edit.map(|f| f(ctx, id)),
        _ => None,
    }
}

/// Shared viewer-controls sidebar widget used by song and songbook detail pages.
pub fn viewer_controls(module: &str, zoom: i32, save_url: &str) -> Element {
    rsx! {
        div {
            class: "viewer-controls",
            "data-detail-viewer-controls": "{module}",
            "data-detail-viewer-save-url": "{save_url}",
            label {
                "Zoom"
                input {
                    r#type: "range",
                    min: "70",
                    max: "170",
                    step: "10",
                    value: "{zoom}",
                    "data-detail-viewer-zoom": "1"
                }
            }
            p { class: "text-xs text-muted", "data-detail-viewer-zoom-label": "1", "{zoom}%" }
            label {
                input {
                    r#type: "checkbox",
                    checked: true,
                    "data-detail-viewer-wrap": "1"
                }
                span { "Wrap lines" }
            }
        }
    }
}

pub fn form_field(
    label: &str,
    name: &str,
    value: &str,
    textarea_rows: Option<usize>,
    input_type: &str,
    extra_class: &str,
) -> Element {
    match textarea_rows {
        Some(rows) => rsx! {
            label {
                "{label}"
                textarea { name: "{name}", rows: rows as i64, class: "{extra_class}", "{value}" }
            }
        },
        None => rsx! {
            label {
                "{label}"
                input { r#type: "{input_type}", name: "{name}", value: "{value}", class: "{extra_class}" }
            }
        },
    }
}

pub fn render_delete_confirm(
    module: &str,
    id: &str,
    title: &str,
    ctx: &RequestContext<'_>,
) -> ResponsePayload {
    let display_title = if title.is_empty() { id } else { title };
    let path = format!("/{module}/{id}/delete");
    crate::html_response(
        &format!("Delete {display_title}"),
        crate::layout(
            crate::current_user(ctx),
            &format!("Delete {display_title}"),
            &path,
            Some("🏠"),
            None,
            rsx! {
                div { class: "center",
                    p {
                        "Are you sure you want to delete "
                        strong { "{display_title}" }
                        "?"
                    }
                    form { method: "POST", action: "{path}", enctype: "multipart/form-data",
                        input { r#type: "hidden", name: "csrf_token", value: "{ctx.csrf_token}" }
                        div { class: "flex gap-2",
                            button { r#type: "submit", class: "btn btn-primary", "Delete" }
                            a { href: "{crate::item_path(module, id)}", class: "btn btn-secondary", "Cancel" }
                        }
                    }
                }
            },
        ),
    )
}
