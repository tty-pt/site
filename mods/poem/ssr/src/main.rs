use dioxus::prelude::*;
use ndc_dioxus_shared::{
    RequestContext, ResponsePayload, PoemItem, current_user, display_or_id, edit_form_page,
    edit_path, form_actions, html_response_with_head, item_menu, item_path,
};

pub fn route(ctx: &RequestContext<'_>) -> Option<ResponsePayload> {
	ndc_dioxus_shared::default_crud_routes(ctx, "poem", None::<fn(_, _) -> _>, None::<fn(_, _) -> _>)
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
    let title = payload.title;
    let path = edit_path("poem", id);
    let heading = format!("Edit {}", display_or_id(title, id));
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
