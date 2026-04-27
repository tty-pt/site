use dioxus::prelude::*;
use serde_json::Value;

use crate::{
    ModuleEntry, RequestContext, ResponsePayload, collection_path, current_user, empty_state,
    html_response, item_path, layout, login_redirect, parse_json_body,
};

pub(crate) fn route(ctx: &RequestContext) -> Option<ResponsePayload> {
	let parts = crate::split_path(&ctx.path);
	match (ctx.method.as_str(), parts.as_slice()) {
		("GET", []) => Some(render_home(ctx)),
		_ => None,
	}
}

fn parse_index_items(body: &str) -> Vec<(String, String)> {
    body.lines()
        .filter_map(|line| {
            let trimmed = line.trim();
            if trimmed.is_empty() {
                return None;
            }
            let idx = trimmed.find(' ')?;
            Some((trimmed[..idx].to_string(), trimmed[idx + 1..].to_string()))
        })
        .collect()
}

pub(crate) fn render_home(ctx: &RequestContext) -> ResponsePayload {
    let buttons: Vec<(String, String)> = ctx
        .modules
        .iter()
        .filter(|m: &&ModuleEntry| m.enabled())
        .map(|m| {
            let label = if m.title.is_empty() {
                m.id.clone()
            } else {
                m.title.clone()
            };
            (m.id.clone(), label)
        })
        .collect();
    html_response(
        "tty.pt",
        layout(
            current_user(ctx),
            "tty.pt",
            &ctx.path,
            None,
            None,
            rsx! {
                div { class: "center",
                    for (module_id, label) in buttons {
                        a {
                            href: "/{module_id}/",
                            class: "btn",
                            "{label}"
                        }
                    }
                }
            },
        ),
    )
}

pub(crate) fn render_list(ctx: &RequestContext, module: &str) -> ResponsePayload {
    let items = parse_index_items(&ctx.body);
    let display_items: Vec<(String, String)> = items
        .into_iter()
        .map(|(id, title)| {
            let label = if title.is_empty() { id.clone() } else { title };
            (id, label)
        })
        .collect();
    let add_href = format!("/{module}/add");
    let menu_items = current_user(ctx).map(|_| {
        rsx! {
            a { href: "{add_href}", class: "btn",
                span { "➕" }
                label { "add" }
            }
        }
    });
    html_response(
        module,
        layout(
            current_user(ctx),
            module,
            &format!("/{module}"),
            Some("🏠"),
            menu_items,
            rsx! {
                div { class: "center",
                    if display_items.is_empty() {
                        { empty_state("No items yet.") }
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

pub(crate) fn render_add_form(
    ctx: &RequestContext,
    module: &str,
    extra_fields: Vec<(&str, String)>,
) -> ResponsePayload {
    if current_user(ctx).is_none() {
        return login_redirect(ctx);
    }
    let path = format!("/{module}/add");
    html_response(
        "Add Item",
        layout(
            current_user(ctx),
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
                        a { href: "{collection_path(module)}", class: "btn btn-secondary", "Cancel" }
                    }
                }
            },
        ),
    )
}

pub(crate) fn render_delete_confirm(
    ctx: &RequestContext,
    module: &str,
    id: &str,
) -> ResponsePayload {
    let result = parse_json_body::<serde_json::Value>(&ctx.body);
    let title = result
        .ok()
        .and_then(|v: Value| v.get("title").and_then(Value::as_str).map(str::to_string))
        .unwrap_or_default();
    let display_title = if title.is_empty() {
        id.to_string()
    } else {
        title.clone()
    };
    let path = format!("/{module}/{id}/delete");
    html_response(
        &format!("Delete {display_title}"),
        layout(
            current_user(ctx),
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
                            a { href: "{item_path(module, id)}", class: "btn btn-secondary", "Cancel" }
                        }
                    }
                }
            },
        ),
    )
}
