use indexmap::IndexMap;
use dioxus::prelude::*;
use ndc_dioxus_shared::{
    body_str, current_user, get_pair, html_response, html_response_with_status, item_menu, layout,
    parse_dataset_items, parse_pairs, render_hyle_edit, render_hyle_list, split_path,
    RequestContext, ResponsePayload,
};
use crate::load_dataset_json;
use serde::Deserialize;

pub fn route(ctx: &RequestContext<'_>) -> Option<ResponsePayload> {
    let parts = split_path(ctx.path);
    match (ctx.method, parts.as_slice()) {
        ("POST", ["poem"]) => Some(render_poem_list(ctx)),
        (_, ["poem", id]) if *id != "add" => Some(render_poem_detail(ctx, id)),
        ("GET", ["poem", id, "edit"]) => Some(render_poem_edit(ctx, id)),
        _ => ndc_dioxus_shared::default_crud_routes(
            ctx,
            "poem",
            Some("📝"),
            None,
            None,
        ),
    }
}

fn render_poem_list(ctx: &RequestContext<'_>) -> ResponsePayload {
    let body_str = std::str::from_utf8(ctx.body).unwrap_or("");
    let items = parse_dataset_items(body_str, &["title"]);
    render_hyle_list(ctx, "poem", Some("📝"), items, &["title"], 10)
}

fn render_poem_detail(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
    let json = match load_dataset_json(ctx.fd, "poem.items") {
        Some(j) => j,
        None => return html_response("500", rsx!{ "Dataset error" }),
    };

    let parsed: DatasetJson = match serde_json::from_str(&json) {
        Ok(p) => p,
        Err(_) => return html_response("500", rsx!{ "Parse error" }),
    };

    let item = match parsed.rows.iter().find(|p| p.id == id) {
        Some(i) => i,
        None => return html_response_with_status(404, "404", rsx!{ "Poem not found" }),
    };

    let title = item.title.as_str();
    let content = item.body_content.as_str();
    let is_owner = current_user(ctx).map(|u| u == item.owner).unwrap_or(false);

    html_response(
        title,
        layout(
            current_user(ctx),
            title,
            ctx.path,
            Some("📝"),
            Some(item_menu("poem", id, is_owner)),
            rsx! {
                div {
                    style: "padding: 20px; max-width: 800px; margin: 0 auto;",
                    div {
                        dangerous_inner_html: "{content}"
                    }
                }
            },
        ),
    )
}

fn render_poem_edit(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
    let json = match load_dataset_json(ctx.fd, "poem.items") {
        Some(j) => j,
        None => return html_response("500", rsx!{ "Dataset error" }),
    };

    let parsed: DatasetJson = match serde_json::from_str(&json) {
        Ok(p) => p,
        Err(_) => return html_response("500", rsx!{ "Parse error" }),
    };

    let item = match parsed.rows.iter().find(|p| p.id == id) {
        Some(i) => i,
        None => return html_response_with_status(404, "404", rsx!{ "Poem not found" }),
    };

    let is_owner = current_user(ctx).map(|u| u == item.owner).unwrap_or(false);
    if !is_owner {
        return html_response_with_status(403, "403", rsx!{ "Forbidden" });
    }

    let item = parsed.rows.into_iter().find(|p| p.id == id);
    let fields = IndexMap::from([
        ("title".to_owned(), item.as_ref().map(|p| p.title.clone()).unwrap_or_default()),
        ("body_content".to_owned(), item.map(|p| p.body_content.clone()).unwrap_or_default()),
    ]);

    render_hyle_edit(
        ctx,
        "poem",
        Some("📝"),
        id,
        fields,
        &["title", "body_content"],
        "multipart/form-data",
        vec![],
    )
}

#[derive(Debug, Deserialize)]
struct DatasetJson {
    rows: Vec<PoemRow>,
}

#[derive(Debug, Deserialize)]
struct PoemRow {
    id: String,
    #[serde(default)]
    title: String,
    #[serde(default)]
    owner: String,
    #[serde(default)]
    body_content: String,
}