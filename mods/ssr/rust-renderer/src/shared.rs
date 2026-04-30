use dioxus::prelude::*;
use dioxus_ssr::render_element;
use serde::Deserialize;
use serde_json::Value;
use url::form_urlencoded;

pub struct RequestContext {
    pub method: String,
    pub path: String,
    pub query: String,
    pub body: String,
    pub remote_user: Option<String>,
    pub modules: Vec<ModuleEntry>,
}

pub struct ResponsePayload {
    pub status: u16,
    pub content_type: String,
    pub location: Option<String>,
    pub body: String,
}

#[derive(Clone, Debug, Deserialize)]
pub struct ModuleEntry {
    pub id: String,
    pub title: String,
    pub flags: Value,
}

#[derive(Clone, Debug)]
pub struct RouteError {
    pub status: u16,
    pub message: String,
}

impl ModuleEntry {
    pub fn enabled(&self) -> bool {
        match &self.flags {
            Value::String(s) => s.parse::<u32>().unwrap_or(0) != 0,
            Value::Number(n) => n.as_u64().unwrap_or(0) != 0,
            Value::Bool(b) => *b,
            _ => false,
        }
    }
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

pub fn current_user(ctx: &RequestContext) -> Option<&str> {
    ctx.remote_user.as_deref()
}

pub fn login_href(ret: &str) -> String {
    let mut out = form_urlencoded::Serializer::new(String::new());
    out.append_pair("ret", ret);
    format!("/auth/login?{}", out.finish())
}

pub fn login_redirect(ctx: &RequestContext) -> ResponsePayload {
    let ret = if ctx.query.is_empty() {
        ctx.path.clone()
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
