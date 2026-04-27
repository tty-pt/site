use dioxus::prelude::*;
use serde::Deserialize;
use serde_json::Value;

use crate::{
    RequestContext, ResponsePayload, current_user, error_page, form_actions, form_page,
    html_response, html_response_with_status, item_menu, item_path, parse_json_body,
};

pub(crate) fn route(ctx: &RequestContext) -> Option<ResponsePayload> {
	let parts = crate::split_path(&ctx.path);
	match (ctx.method.as_str(), parts.as_slice()) {
		("GET", ["poem", "add"]) => Some(crate::index::render_add_form(ctx, "poem", Vec::new())),
		("POST", ["poem"]) => Some(crate::index::render_list(ctx, "poem")),
		("POST", ["poem", id, "delete"]) => Some(crate::index::render_delete_confirm(ctx, "poem", id)),
		("POST", ["poem", id]) => Some(render_detail(ctx, id)),
		("POST", ["poem", id, "edit"]) => Some(render_edit(ctx, id)),
		_ => None,
	}
}

#[derive(Clone, Debug, Deserialize)]
struct PoemPayload {
    title: Option<String>,
    lang: Option<String>,
    owner: Option<bool>,
}

pub(crate) fn render_detail(ctx: &RequestContext, id: &str) -> ResponsePayload {
    match parse_json_body::<PoemPayload>(&ctx.body) {
        Ok(payload) => {
            let title = payload.title.unwrap_or_default();
            let lang = payload.lang.unwrap_or_else(|| "pt_PT".to_string());
            let owner = payload.owner.unwrap_or(false);
            let page_title = if title.is_empty() {
                format!("poem: {id}")
            } else {
                title.clone()
            };
            html_response(
                &page_title,
                crate::layout(
                    current_user(ctx),
                    &page_title,
                    &item_path("poem", id),
                    Some("📜"),
                    Some(item_menu("poem", id, owner)),
                    if lang.is_empty() {
                        crate::empty_state("No content yet.")
                    } else {
                        rsx! {
                            iframe {
                                src: "/poem/{id}/{lang}.html",
                                class: "w-full",
                                style: "height: 80vh; border: none;"
                            }
                        }
                    },
                ),
            )
        }
        Err(err) => html_response_with_status(
            err.status,
            &err.status.to_string(),
            error_page(current_user(ctx), &item_path("poem", id), err.status, &err.message),
        ),
    }
}

pub(crate) fn render_edit(ctx: &RequestContext, id: &str) -> ResponsePayload {
    let title = parse_json_body::<Value>(&ctx.body)
        .ok()
        .and_then(|v| v.get("title").and_then(Value::as_str).map(str::to_string))
        .unwrap_or_default();
    let path = format!("/poem/{id}/edit");
    html_response(
        &format!("Edit {}", if title.is_empty() { id } else { title.as_str() }),
        form_page(
            current_user(ctx),
            &format!("Edit {}", if title.is_empty() { id } else { title.as_str() }),
            &path,
            Some("📜"),
            Some("Edit Poem"),
            rsx! {
                form { method: "POST", action: "{path}", enctype: "multipart/form-data", class: "flex flex-col gap-4",
                    label { "Title:"
                        input { r#type: "text", name: "title", value: "{title}" }
                    }
                    label { "File:"
                        input { r#type: "file", name: "file", accept: ".html,.htm,.txt" }
                    }
                    { form_actions(&item_path("poem", id), "Save", None) }
                }
            },
        ),
    )
}
