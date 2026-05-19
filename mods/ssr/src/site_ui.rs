use dioxus::prelude::*;
use hyle::load_typed_item;

use ndc_dioxus_shared::{
	RequestContext, ResponsePayload, auth_path, body_str, collection_path,
	current_user, get_pair, html_response,
	item_action_path, item_path, login_href, login_redirect, parent_path, parse_pairs,
	split_path,
};

pub fn item_menu(module: &str, id: &str, is_owner: bool) -> Element {
	if !is_owner {
		return rsx! {};
	}
	let edit_href = item_action_path(module, id, "edit");
	let delete_href = item_action_path(module, id, "delete");
	rsx! {
		a { href: "{edit_href}", class: "btn",
			span { "✏️" }
			span { "edit" }
		}
		a { href: "{delete_href}", class: "btn",
			span { "🗑️" }
			span { "delete" }
		}
	}
}

fn menu(user: Option<&str>, path: &str, icon: &str) -> Element {
	let is_home = path == "/" || path.is_empty();
	let login_link = login_href(path);
	let register_href = auth_path("register");
	let up_href = parent_path(path);
	rsx! {
		if !is_home {
			a { class: "btn", href: "{up_href}",
				span { "{icon}" }
				span { "go up" }
			}
		}
		if let Some(user) = user {
			a { class: "btn", href: "/{user}/",
				span { "😊" }
				span { "me" }
			}
			a { class: "btn", href: "/auth/logout",
				span { "🚪" }
				span { "logout" }
			}
		} else {
			a { class: "btn", href: "{login_link}",
				span { "🔑" }
				span { "login" }
			}
			a { class: "btn", href: "{register_href}",
				span { "📝" }
				span { "register" }
			}
		}
	}
}

pub fn layout(
	user: Option<&str>,
	title: &str,
	path: &str,
	icon: &str,
	menu_items: Option<Element>,
	children: Element,
) -> Element {
	rsx! {
		main { class: "main",
			h1 { "{title}" }
			{ children }
		}
		nav { class: "menu",
			input { id: "menu-functions", name: "functions", r#type: "checkbox", class: "hidden" }
			label { r#for: "menu-functions", class: "menu-overlay", aria_label: "Close Menu" }
			span { class: "functions flex-1 fixed right-0 z-50 h-full overflow-y-auto text-sm capitalize flex flex-col p-4",
				div { class: "relative z-20 flex flex-col gap-2",
					{ menu(user, path, icon) }
					if let Some(menu_items) = menu_items {
						div { class: "menu-separator" }
						div { class: "module-menu", { menu_items } }
					}
				}
				label { r#for: "menu-functions", class: "absolute inset-0 z-10 cursor-pointer", aria_label: "Close Menu" }
			}
			span { class: "fixed top-0 right-0 z-30 p-2 flex items-center gap-4",
				label { r#for: "menu-functions", class: "menu-toggle flex items-center justify-center cursor-pointer text-base btn", aria_label: "Menu", "data-menu-toggle": "1", "⚙️" }
			}
		}
	}
}

pub fn empty_state(message: &str) -> Element {
	rsx! { p { class: "text-muted", "{message}" } }
}

pub fn viewer_controls(module: &str, zoom: i32, save_url: &str) -> Element {
	rsx! {
		div {
			class: "viewer-controls",
			"data-detail-viewer-controls": "{module}",
			"data-detail-viewer-save-url": "{save_url}",
			label {
				"Zoom"
				input {
					r#type: "range",
					min: "70",
					max: "170",
					step: "10",
					value: "{zoom}",
					"data-detail-viewer-zoom": "1"
				}
			}
			p { class: "text-xs text-muted", "data-detail-viewer-zoom-label": "1", "{zoom}%" }
			label {
				input {
					r#type: "checkbox",
					checked: true,
					"data-detail-viewer-wrap": "1"
				}
				span { "Wrap lines" }
			}
		}
	}
}

pub fn form_actions(cancel_href: &str, submit_label: &str, extra: Option<Element>) -> Element {
	rsx! {
		div { class: "flex gap-2",
			button { r#type: "submit", class: "btn btn-primary", "{submit_label}" }
			if let Some(extra) = extra {
				{ extra }
			}
			a { href: "{cancel_href}", class: "btn btn-secondary", "Cancel" }
		}
	}
}

pub fn form_page(
	user: Option<&str>,
	title: &str,
	path: &str,
	icon: &str,
	heading: Option<&str>,
	children: Element,
) -> Element {
	layout(
		user,
		title,
		path,
		icon,
		None,
		rsx! {
			div { class: "center",
				if let Some(heading) = heading {
					h1 { "{heading}" }
				}
				{ children }
			}
		},
	)
}

pub fn error_page(user: Option<&str>, path: &str, status: u16, message: &str) -> Element {
	layout(
		user,
		&status.to_string(),
		path,
		"🏠",
		None,
		rsx! { p { "{message}" } },
	)
}

pub fn edit_form_page(
	user: Option<&str>,
	title: &str,
	path: &str,
	icon: &str,
	children: Element,
) -> ResponsePayload {
	html_response(title, form_page(user, title, path, icon, None, children))
}

pub fn render_add_form(
	ctx: &RequestContext<'_>,
	module: &str,
	icon: &str,
	extra_fields: Vec<(&str, String)>,
) -> ResponsePayload {
	render_add_form_with_error(ctx, module, icon, extra_fields, None)
}

pub fn render_add_form_with_error(
	ctx: &RequestContext<'_>,
	module: &str,
	icon: &str,
	extra_fields: Vec<(&str, String)>,
	error: Option<&str>,
) -> ResponsePayload {
	if current_user(ctx).is_none() {
		return login_redirect(ctx);
	}
	let path = format!("/{module}/add");
	let error = error.map(|s| s.to_string());
	html_response(
		"Add Item",
		layout(
			current_user(ctx),
			"Add Item",
			&path,
			icon,
			None,
			rsx! {
				if let Some(msg) = error {
					p { class: "text-error", "{msg}" }
				}
				form {
					action: "{path}",
					method: "POST",
					enctype: "multipart/form-data",
					class: "flex flex-col gap-4",
					input { r#type: "hidden", name: "csrf_token", value: "{ctx.csrf_token}" }
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

pub fn default_crud_routes(
	ctx: &RequestContext<'_>,
	module: &str,
	icon: &str,
) -> Option<ResponsePayload> {
	let parts = split_path(&ctx.path);
	match (ctx.method, parts.as_slice()) {
		("GET" | "POST", [m, id, "delete"]) if *m == module && *id != "add" => {
			let items_path = format!("{module}.items");
			let title = load_typed_item::<serde_json::Value>(&items_path, id)
				.ok()
				.flatten()
				.and_then(|v| v.get("title").and_then(|t| t.as_str().map(String::from)))
				.unwrap_or_default();
			Some(render_delete_confirm(module, id, &title, ctx))
		}
		("GET", [m, "add"]) if *m == module => {
			let pairs = parse_pairs(ctx.query);
			let extra = pairs
				.iter()
				.map(|(k, v)| (k.as_str(), v.to_string()))
				.collect();
			Some(render_add_form(ctx, module, icon, extra))
		}
		("POST", [m, "add"]) if *m == module => {
			let body = body_str(ctx.body);
			let pairs = parse_pairs(body);
			let error = get_pair(&pairs, "error").unwrap_or("An error occurred");
			Some(render_add_form_with_error(
				ctx,
				module,
				icon,
				Vec::new(),
				Some(error),
			))
		}
		_ => None,
	}
}

pub fn render_delete_confirm(
	module: &str,
	id: &str,
	title: &str,
	ctx: &RequestContext<'_>,
) -> ResponsePayload {
	let display_title = if title.is_empty() { id } else { title };
	let path = format!("/{module}/{id}/delete");
	html_response(
		&format!("Delete {display_title}"),
		layout(
			current_user(ctx),
			&format!("Delete {display_title}"),
			&path,
			"🏠",
			None,
			rsx! {
				div { class: "center",
					p {
						"Are you sure you want to delete "
						strong { "{display_title}" }
						"?"
					}
					form { method: "POST", action: "{path}", enctype: "multipart/form-data",
						input { r#type: "hidden", name: "csrf_token", value: "{ctx.csrf_token}" }
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
