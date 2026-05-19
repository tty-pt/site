use indexmap::IndexMap;
use dioxus::prelude::*;
use ndc_dioxus_shared::{
    current_user, html_response, html_response_with_status, item_menu, layout,
    render_hyle_edit, render_hyle_list_with_source, split_path,
    RequestContext, ResponsePayload,
};
use serde::Deserialize;

use super::{load_dataset_item_json, load_dataset_source};

pub fn route(ctx: &RequestContext<'_>) -> Option<ResponsePayload> {
    let parts = split_path(ctx.path);
    match (ctx.method, parts.as_slice()) {
        ("GET", ["poem"]) | ("POST", ["poem"]) => {
            let source = load_dataset_source("poem.items")?;
            Some(render_hyle_list_with_source(
                ctx,
                "poem",
                Some("📝"),
                source,
                &["title", "owner"],
                20,
            ))
        }
        ("GET", ["poem", id]) if *id != "add" => Some(render_detail(ctx, id)),
        ("GET", ["poem", id, "edit"]) => Some(render_edit(ctx, id)),
        ("GET", ["poem", id, "delete"]) if *id != "add" => {
            let json = load_dataset_item_json(ctx.fd, "poem.items", id)?;
            let item: PoemRow = serde_json::from_str(&json).ok()?;
            Some(ndc_dioxus_shared::render_delete_confirm("poem", id, &item.title, ctx))
        }
        _ => ndc_dioxus_shared::default_crud_routes(ctx, "poem", Some("📝"), None, None),
    }
}

pub fn render_detail(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
    let json = match load_dataset_item_json(ctx.fd, "poem.items", id) {
        Some(j) => j,
        None => {
            return html_response_with_status(404, "404", rsx! { "Poem not found" })
        }
    };

    let item: PoemRow = match serde_json::from_str(&json) {
        Ok(p) => p,
        Err(e) => return html_response("500", rsx! { "Parse error: {e}" }),
    };

    let title = &item.title;
    let owner = &item.owner;
    let body_content = &item.body_content;
    let is_owner = current_user(ctx) == Some(owner);
    let path = format!("/poem/{id}");
    let page_title = format!("poem: {title}");

    html_response(
        &page_title,
        layout(
            current_user(ctx),
            &page_title,
            &path,
            Some("📝"),
            Some(item_menu("poem", id, is_owner)),
            rsx! {
                div { class: "flex flex-col gap-4",
                    div { class: "poem-body bg-surface p-6 rounded shadow-sm whitespace-pre-wrap font-serif leading-relaxed",
                        "{body_content}"
                    }
                    if !owner.is_empty() {
                        div { class: "text-sm text-muted text-right", "By {owner}" }
                    }
                }
            },
        ),
    )
}

pub fn render_edit(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
    let json = match load_dataset_item_json(ctx.fd, "poem.items", id) {
        Some(j) => j,
        None => {
            return html_response_with_status(404, "404", rsx! { "Poem not found" })
        }
    };

    let item: PoemRow = match serde_json::from_str(&json) {
        Ok(p) => p,
        Err(e) => return html_response("500", rsx! { "Parse error: {e}" }),
    };

    let is_owner = current_user(ctx) == Some(&item.owner);
    if !is_owner {
        return html_response_with_status(403, "403", rsx! { "Forbidden" });
    }

    let mut fields = IndexMap::new();
    fields.insert("title".to_owned(), item.title.clone());
    fields.insert("owner".to_owned(), item.owner.clone());
    // body_content is a file field, we don't pass its content to the form initial value
    // as we want an empty file input for uploading.

    render_hyle_edit(
        ctx,
        "poem",
        Some("📝"),
        id,
        fields,
        &["title", "body_content"],
        "multipart/form-data",
    )
}

#[derive(Debug, Deserialize)]
struct PoemRow {
    #[allow(dead_code)]
    id: String,
    #[serde(default)]
    title: String,
    #[serde(default)]
    owner: String,
    #[serde(default)]
    body_content: String,
}
