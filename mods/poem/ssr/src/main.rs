use dioxus::prelude::*;
use serde::Deserialize;
use serde_json::Value;

use ndc_dioxus_shared::{
    RequestContext, ResponsePayload, current_user, edit_form_page, form_actions,
    html_response_with_head, item_menu, item_path, parse_json_body,
};

pub fn route(ctx: &RequestContext) -> Option<ResponsePayload> {
	ndc_dioxus_shared::default_crud_routes(ctx, "poem", render_detail, render_edit)
}

#[derive(Clone, Debug, Deserialize)]
struct PoemPayload {
    title: Option<String>,
    owner: Option<bool>,
    head_content: Option<String>,
    body_content: Option<String>,
}

pub fn render_detail(ctx: &RequestContext, id: &str) -> ResponsePayload {
    match parse_json_body::<PoemPayload>(&ctx.body) {
        Ok(payload) => {
            let title = payload.title.unwrap_or_default();
            let body_content = payload.body_content.unwrap_or_default();
            let head_content = payload.head_content.unwrap_or_default();
            let owner = payload.owner.unwrap_or(false);
            let page_title = if title.is_empty() {
                format!("poem: {id}")
            } else {
                title.clone()
            };
            html_response_with_head(
                &page_title,
                &head_content,
                ndc_dioxus_shared::layout(
                    current_user(ctx),
                    &page_title,
                    &item_path("poem", id),
                    Some("📜"),
                    Some(item_menu("poem", id, owner)),
                    if body_content.is_empty() {
                        ndc_dioxus_shared::empty_state("No content yet.")
                    } else {
                        rsx! {
                            div {
                                class: "w-full poem-content",
                                dangerous_inner_html: "{body_content}"
                            }
                        }
                    }
                )
            )
        }
        Err(err) => ndc_dioxus_shared::render_item_error(ctx, &item_path("poem", id), &err),
    }
}

pub fn render_edit(ctx: &RequestContext, id: &str) -> ResponsePayload {
    let title = parse_json_body::<Value>(&ctx.body)
        .ok()
        .and_then(|v| v.get("title").and_then(Value::as_str).map(str::to_string))
        .unwrap_or_default();
    let path = format!("/poem/{id}/edit");
    let heading = format!("Edit {}", if title.is_empty() { id } else { title.as_str() });
    edit_form_page(
        current_user(ctx),
        &heading,
        &path,
        Some("📜"),
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
    )
}
