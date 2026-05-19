pub use ndc_types::{
    auth_path, body_str, collection_path, display_or_id,
    edit_path, get_pair, item_action_path, item_path, key_names, key_transpose_options,
    parent_path, parse_pairs, split_path,
};

use dioxus::prelude::*;
use dioxus::dioxus_core::ComponentFunction;
use dioxus_fullstack_core::HydrationContext;

pub struct ModuleRef<'a> {
    pub id: &'a str,
    pub title: &'a str,
    pub flags: u32,
}

impl<'a> ModuleRef<'a> {
    pub fn enabled(&self) -> bool {
        self.flags != 0
    }
}

pub struct RequestContext<'a> {
    pub fd: i32,
    pub method: &'a str,
    pub path: &'a str,
    pub query: &'a str,
    pub body: &'a [u8],
    pub remote_user: Option<&'a str>,
    pub modules: &'a [ModuleRef<'a>],
    pub csrf_token: &'a str,
}

pub struct ResponsePayload {
    pub status: u16,
    pub content_type: String,
    pub location: Option<String>,
    pub body: String,
}

pub fn current_user<'a>(ctx: &RequestContext<'a>) -> Option<&'a str> {
    ctx.remote_user
}

pub fn login_href(ret: &str) -> String {
    let mut out = url::form_urlencoded::Serializer::new(String::new());
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

fn hydration_bootstrap_script() -> String {
    let hydration = HydrationContext::default().serialized();
    #[cfg(debug_assertions)]
    {
        let debug_types = if hydration.debug_types.is_empty() {
            "{}"
        } else {
            &hydration.debug_types
        };
        let debug_locations = if hydration.debug_locations.is_empty() {
            "{}"
        } else {
            &hydration.debug_locations
        };
        format!(
            "<script>window.initial_dioxus_hydration_data=\"{}\";window.initial_dioxus_hydration_debug_types={};window.initial_dioxus_hydration_debug_locations={};window.hydrate_queue=[];</script>",
            hydration.data, debug_types, debug_locations
        )
    }
    #[cfg(not(debug_assertions))]
    {
        format!(
            "<script>window.initial_dioxus_hydration_data=\"{}\";window.initial_dioxus_hydration_debug_types={};window.initial_dioxus_hydration_debug_locations={};window.hydrate_queue=[];</script>",
            hydration.data, "{}", "{}"
        )
    }
}

const DEFAULT_CSS: &str =
    r#"<link rel="stylesheet" href="/styles.css"><link rel="stylesheet" href="/hyle.css">"#;

pub fn render_document_html_with_head(
    title: &str,
    body_html: &str,
    extra_head: &str,
    module: &str,
) -> String {
    let module_attr = if module.is_empty() {
        String::new()
    } else {
        format!(" data-modules=\"{}\"", module)
    };
    let module_script = if module.is_empty() {
        String::new()
    } else {
        format!("<script type=\"module\" src=\"/{}-client.js\"></script>", module)
    };
    let head_content = format!("{}{}", DEFAULT_CSS, extra_head);
    let hydration_script = hydration_bootstrap_script();
    format!(
        "<!DOCTYPE html><html lang=\"pt\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>{}</title>{}</head><body style=\"margin: 0\"{}>{}</body>{}{}</html>",
        escape_html(title),
        head_content,
        module_attr,
        body_html,
        hydration_script,
        module_script
    )
}

fn pre_render_element(element: Element) -> String {
    fn app(props: Element) -> Element {
        props
    }

    let mut dom = VirtualDom::new_with_props(app, element);
    dom.rebuild_in_place();
    dioxus_ssr::pre_render(&dom)
}

fn render_document_with_head(title: &str, body: Element, extra_head: &str, module: &str) -> String {
    let body_html = pre_render_element(body);
    render_document_html_with_head(title, &body_html, extra_head, module)
}

fn render_document(title: &str, body: Element, module: &str) -> String {
    render_document_with_head(title, body, "", module)
}

pub fn html_response(title: &str, body: Element) -> ResponsePayload {
    html_response_with_head(title, "", body, "")
}

pub fn html_response_with_module(title: &str, body: Element, module: &str) -> ResponsePayload {
    html_response_with_head(title, "", body, module)
}

pub fn html_response_with_head(
    title: &str,
    extra_head: &str,
    body: Element,
    module: &str,
) -> ResponsePayload {
    ResponsePayload {
        status: 200,
        content_type: "text/html; charset=utf-8".to_string(),
        location: None,
        body: render_document_with_head(title, body, extra_head, module),
    }
}

pub fn html_response_with_status(status: u16, title: &str, body: Element) -> ResponsePayload {
    ResponsePayload {
        status,
        content_type: "text/html; charset=utf-8".to_string(),
        location: None,
        body: render_document(title, body, ""),
    }
}

/// Render a Dioxus component's output to HTML, running inside the VirtualDom
/// runtime so that event handlers (ListenerCallback) can be created safely.
pub fn render_component<C, P>(component: C, props: P) -> String
where
    C: ComponentFunction<P>,
    P: Clone + 'static,
{
    let mut dom = VirtualDom::new_with_props(component, props);
    dom.rebuild_in_place();
    dioxus_ssr::pre_render(&dom)
}

/// Build a full ResponsePayload by rendering a Dioxus component and wrapping
/// the result in the site's HTML document shell.
pub fn html_response_with_component<C, P>(
    title: &str,
    extra_head: &str,
    module: &str,
    component: C,
    props: P,
) -> ResponsePayload
where
    C: ComponentFunction<P>,
    P: Clone + 'static,
{
    let body_html = render_component(component, props);
    ResponsePayload {
        status: 200,
        content_type: "text/html; charset=utf-8".to_string(),
        location: None,
        body: render_document_html_with_head(title, &body_html, extra_head, module),
    }
}

pub fn escape_html(input: &str) -> String {
    input
        .replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
}
