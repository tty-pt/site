use dioxus::prelude::*;
use ndc_dioxus_shared::{
    RequestContext, ResponsePayload, PoemItem, body_str, current_user,
    html_response_with_head, item_menu, item_path, parse_index_items_rich,
    render_hyle_edit, render_hyle_list, split_path,
};

pub fn route(ctx: &RequestContext<'_>) -> Option<ResponsePayload> {
    let parts = split_path(ctx.path);
    match (ctx.method, parts.as_slice()) {
        ("POST", ["poem"]) => {
            let items = parse_index_items_rich(body_str(ctx.body), &[]);
            Some(render_hyle_list(ctx, "poem", Some("📜"), items, &["title"], 20))
        }
        _ => ndc_dioxus_shared::default_crud_routes(
            ctx, "poem", Some("📜"),
            None::<ndc_dioxus_shared::CrudHandler>,
            None::<ndc_dioxus_shared::CrudHandler>,
        ),
    }
}

pub fn render_detail(payload: &PoemItem<'_>, id: &str, ctx: &RequestContext<'_>) -> ResponsePayload {
    let title        = payload.title;
    let body_content = payload.body_content;
    let head_content = payload.head_content;
    let owner        = payload.owner;
    let page_title = if title.is_empty() {
        format!("poem: {id}")
    } else {
        title.to_string()
    };
    html_response_with_head(
        &page_title,
        head_content,
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

pub fn render_edit_typed(payload: &PoemItem<'_>, id: &str, ctx: &RequestContext<'_>) -> ResponsePayload {
    let fields = [("title", payload.title)]
        .iter()
        .map(|&(k, v)| (k.to_owned(), v.to_owned()))
        .collect();
    render_hyle_edit(
        ctx,
        "poem",
        Some("📜"),
        id,
        fields,
        &["title", "file"],
        "multipart/form-data",
        vec![],
    )
}
