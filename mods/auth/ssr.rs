use dioxus::prelude::*;

use crate::{
    RequestContext, ResponsePayload, auth_path, body_str, current_user, form_page, get_pair,
    html_response, html_response_with_status, parse_pairs,
};

pub(crate) fn route(ctx: &RequestContext<'_>) -> Option<ResponsePayload> {
	let parts = crate::split_path(&ctx.path);
	match (ctx.method, parts.as_slice()) {
		("GET", ["auth", "login"]) => Some(render_login(ctx)),
		("POST", ["auth", "login"]) => Some(render_login(ctx)),
		("GET", ["auth", "register"]) => Some(render_register(ctx)),
		_ => None,
	}
}

pub(crate) fn render_login(ctx: &RequestContext<'_>) -> ResponsePayload {
    let (ret, error, status) = if ctx.method == "POST" {
        let pairs = parse_pairs(body_str(ctx.body));
        let ret = get_pair(&pairs, "ret").unwrap_or("/").to_string();
        let error = get_pair(&pairs, "error").map(str::to_string);
        let status = get_pair(&pairs, "status")
            .and_then(|v| v.parse::<u16>().ok())
            .unwrap_or(401);
        (ret, error, status)
    } else {
        let pairs = parse_pairs(&ctx.query);
        let ret = get_pair(&pairs, "ret").unwrap_or("/").to_string();
        (ret, None, 200u16)
    };
    html_response_with_status(
        status,
        "Login",
        form_page(
            current_user(ctx),
            "Login",
            &auth_path("login"),
            Some("🔑"),
            None,
            rsx! {
                if let Some(error) = error {
                    p { class: "error", "{error}" }
                }
                form { action: "/auth/login", method: "POST", class: "flex flex-col gap-4",
                    label { "Username:"
                        input { required: true, name: "username" }
                    }
                    label { "Password:"
                        input { required: true, r#type: "password", name: "password" }
                    }
                    input { r#type: "hidden", name: "ret", value: "{ret}" }
                    button { r#type: "submit", "Login" }
                }
            },
        ),
    )
}

pub(crate) fn render_register(ctx: &RequestContext<'_>) -> ResponsePayload {
    html_response(
        "Register",
        form_page(
            current_user(ctx),
            "Register",
            &auth_path("register"),
            Some("📝"),
            None,
            rsx! {
                form { action: "/auth/register", method: "POST", class: "flex flex-col gap-4",
                    label { "Username:"
                        input { required: true, name: "username" }
                    }
                    label { "Password:"
                        input { required: true, r#type: "password", name: "password" }
                    }
                    label { "Confirm:"
                        input { required: true, r#type: "password", name: "password2" }
                    }
                    label { "Email:"
                        input { required: true, r#type: "email", name: "email" }
                    }
                    button { r#type: "submit", "Register" }
                }
            },
        ),
    )
}
