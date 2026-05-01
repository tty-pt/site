use dioxus::prelude::*;
use dioxus_ssr::render_element;
use serde::Deserialize;
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

/// Interpret a raw byte body as UTF-8 text. Zero allocation; returns "" on invalid UTF-8.
pub fn body_str(b: &[u8]) -> &str {
    std::str::from_utf8(b).unwrap_or("")
}

#[derive(Clone, Debug)]
pub struct RouteError {
    pub status: u16,
    pub message: String,
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

pub fn parse_error_body(text: &str) -> Option<RouteError> {
    let pairs = parse_pairs(text);
    let error = get_pair(&pairs, "error")?;
    let status = get_pair(&pairs, "status")
        .and_then(|v| v.parse::<u16>().ok())
        .unwrap_or(500);
    Some(RouteError {
        status,
        message: error.to_string(),
    })
}

pub fn parse_json_body<T: for<'de> Deserialize<'de>>(text: &str) -> Result<T, RouteError> {
    if let Some(err) = parse_error_body(text) {
        return Err(err);
    }
    serde_json::from_str(text).map_err(|_| RouteError {
        status: 500,
        message: "Failed to parse posted JSON".to_string(),
    })
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
        "<!DOCTYPE html><html lang=\"pt\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>{}</title><link rel=\"stylesheet\" href=\"/styles.css\">{}</head><body style=\"margin: 0\">{}<script type=\"module\" src=\"/wasm.js\"></script></body></html>",
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

pub fn prefs_save_url(ctx: &RequestContext<'_>) -> &'static str {
    if current_user(ctx).is_some() { "/api/song/prefs" } else { "" }
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

pub fn render_list(ctx: &RequestContext<'_>, module: &str) -> ResponsePayload {
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
            Some("🏠"),
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
    extra_fields: Vec<(&str, String)>,
) -> ResponsePayload {
    if crate::current_user(ctx).is_none() {
        return crate::login_redirect(ctx);
    }
    let path = format!("/{module}/add");
    crate::html_response(
        "Add Item",
        crate::layout(
            crate::current_user(ctx),
            "Add Item",
            &path,
            Some("🏠"),
            None,
            rsx! {
                form {
                    action: "{path}",
                    method: "POST",
                    enctype: "multipart/form-data",
                    class: "flex flex-col gap-4",
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

/// Render the standard error response for a failed item detail/edit request.
pub fn render_item_error(ctx: &RequestContext<'_>, back_path: &str, err: &RouteError) -> ResponsePayload {
    html_response_with_status(
        err.status,
        &err.status.to_string(),
        error_page(current_user(ctx), back_path, err.status, &err.message),
    )
}

/// Standard CRUD route dispatcher for modules that only have the default 5 arms.
/// Modules with extra arms can call this and fall back with `.or_else(|| ...)`.
pub fn default_crud_routes<F, G>(
    ctx: &RequestContext<'_>,
    module: &str,
    render_detail: F,
    render_edit: G,
) -> Option<ResponsePayload>
where
    F: Fn(&RequestContext<'_>, &str) -> ResponsePayload,
    G: Fn(&RequestContext<'_>, &str) -> ResponsePayload,
{
    let parts = split_path(&ctx.path);
    match (ctx.method, parts.as_slice()) {
        ("GET", [m, "add"]) if *m == module =>
            Some(render_add_form(ctx, module, Vec::new())),
        ("POST", [m]) if *m == module =>
            Some(render_list(ctx, module)),
        ("POST", [m, id, "delete"]) if *m == module =>
            Some(render_delete_confirm(module, id, "", ctx)),
        ("POST", [m, id]) if *m == module =>
            Some(render_detail(ctx, id)),
        ("POST", [m, id, "edit"]) if *m == module =>
            Some(render_edit(ctx, id)),
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
                    form { method: "POST", action: "{path}",
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
