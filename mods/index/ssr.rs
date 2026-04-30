use dioxus::prelude::*;

use crate::{ModuleEntry, RequestContext, ResponsePayload, current_user, html_response, layout, split_path};

pub(crate) fn route(ctx: &RequestContext) -> Option<ResponsePayload> {
	let parts = split_path(&ctx.path);
	match (ctx.method.as_str(), parts.as_slice()) {
		("GET", []) => Some(render_home(ctx)),
		_ => None,
	}
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
