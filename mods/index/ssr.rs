use dioxus::prelude::*;

use crate::{RequestContext, ResponsePayload, current_user, html_response, layout, split_path};

pub(crate) fn route(ctx: &RequestContext<'_>) -> Option<ResponsePayload> {
	let parts = split_path(&ctx.path);
	match (ctx.method, parts.as_slice()) {
		("GET", []) => Some(render_home(ctx)),
		_ => None,
	}
}

pub(crate) fn render_home(ctx: &RequestContext<'_>) -> ResponsePayload {
    let buttons: Vec<(&str, &str)> = ctx
        .modules
        .iter()
        .filter(|m| m.enabled())
        .map(|m| (m.id, if m.title.is_empty() { m.id } else { m.title }))
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
                    for (module_id, label) in &buttons {
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
