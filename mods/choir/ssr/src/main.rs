use dioxus::prelude::*;
use ndc_dioxus_shared::{
    ChoirItem, RequestContext, ResponsePayload, body_str, current_user, display_or_id,
    edit_form_page, edit_path, form_actions, get_pair, html_response, item_menu, item_path,
    key_names, layout, parse_pairs,
};

pub fn route(ctx: &RequestContext<'_>) -> Option<ResponsePayload> {
	ndc_dioxus_shared::default_crud_routes(ctx, "choir", render_detail_stub, render_edit)
}

/// Placeholder — choir detail is rendered directly via ssr_render_choir_detail FFI.
fn render_detail_stub(_ctx: &RequestContext<'_>, _id: &str) -> ResponsePayload {
	ndc_dioxus_shared::ResponsePayload {
		status: 404,
		content_type: "text/plain".to_string(),
		location: None,
		body: "Not found".to_string(),
	}
}

pub fn render_detail(payload: &ChoirItem<'_>, id: &str, ctx: &RequestContext<'_>) -> ResponsePayload {
	let title = payload.title;
	let owner = payload.owner_name;
	let is_owner = current_user(ctx) == Some(owner);
	let path = item_path("choir", id);
	let formats = payload.formats;

	let mut all_songs: Vec<(&str, &str)> = payload
		.all_songs
		.iter()
		.map(|e| (e.id, e.title))
		.collect();
	all_songs.sort_by_key(|(_, t)| *t);

	let display_songbooks: Vec<(String, String)> = payload
		.songbooks
		.iter()
		.map(|sb| {
			let label = display_or_id(sb.title, sb.id).to_string();
			(sb.id.to_string(), label)
		})
		.collect();

	let display_songs: Vec<(String, String, String)> = payload
		.songs
		.iter()
		.map(|song| {
			let label = display_or_id(song.title, song.id).to_string();
			let key_idx = ((if song.preferred_key != 0 {
				song.preferred_key
			} else {
				song.original_key
			}) % 12
				+ 12) % 12;
			(
				song.id.to_string(),
				label,
				key_names(false, false)[key_idx as usize].to_string(),
			)
		})
		.collect();

	let menu_items = Some(rsx! {
		{ item_menu("choir", id, is_owner) }
		if is_owner {
			a { href: "/songbook/add?choir={id}", class: "btn",
				span { "➕" }
				label { "add songbook" }
			}
		}
	});

	html_response(
		title,
		layout(
			current_user(ctx),
			title,
			&path,
			Some("🎶"),
			menu_items,
			rsx! {
				div { class: "center",
					if !owner.is_empty() {
						div { class: "flex justify-end text-xs text-muted w-full",
							a { href: "/{owner}/", class: "text-muted", "{owner}" }
						}
					}
					h3 { "Songbooks" }
					if display_songbooks.is_empty() {
						p { class: "text-muted", "No songbooks yet." }
					} else {
						div { class: "center",
							for (songbook_id, songbook_label) in display_songbooks {
								a { href: "/songbook/{songbook_id}", class: "btn", "{songbook_label}" }
							}
						}
					}
					h3 { "Repertoire" }
					if display_songs.is_empty() {
						p { class: "text-muted", "No songs in repertoire yet." }
					} else {
						ul { class: "list-none p-0 text-left w-full max-w-lg mx-auto",
							for (song_id, song_label, key_label) in display_songs {
								li { class: "p-2 border-b border-muted flex justify-between items-center",
									a { href: "/choir/{id}/song/{song_id}", class: "flex-1", "{song_label}" }
									span { class: "text-muted mr-4", "{key_label}" }
									if is_owner {
										form { method: "POST", action: "/api/choir/{id}/song/{song_id}/remove", class: "inline",
											button { r#type: "submit", class: "btn btn-danger py-1 px-2 text-xs", "Remove" }
										}
									}
								}
							}
						}
					}
					if is_owner {
						div { class: "w-full max-w-lg",
							details {
								summary { class: "cursor-pointer text-blue-600", "Add song to repertoire" }
								form { method: "POST", action: "/api/choir/{id}/songs",
									datalist { id: "choir-songs",
										for (song_id, song_title) in all_songs {
											option { value: "{song_title} [{song_id}]" }
										}
									}
									div { class: "btn-row",
										input { list: "choir-songs", name: "song_id", placeholder: "Search song...", required: true }
										button { r#type: "submit", class: "btn", "Add" }
									}
								}
							}
						}
					}
					h3 { "Song Formats" }
					pre { class: "bg-surface p-4 rounded text-left w-full max-w-lg", "{formats}" }
				}
			},
		),
	)
}

pub fn render_edit(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
	let pairs = parse_pairs(body_str(ctx.body));
	let title = get_pair(&pairs, "title").unwrap_or("").to_string();
	let formats = get_pair(&pairs, "format").unwrap_or("").to_string();
	let path = edit_path("choir", id);
	let heading = format!("Edit {}", title);
	edit_form_page(
		current_user(ctx),
		&heading,
		&path,
		Some("🎶"),
		rsx! {
			form { method: "POST", action: "/api/choir/{id}/edit", enctype: "multipart/form-data", class: "flex flex-col gap-4 w-full max-w-lg",
				label { "Choir Name:"
					input { r#type: "text", name: "title", value: "{title}", required: true, class: "w-full" }
				}
				label { "Song Formats (one per line):"
					textarea { name: "format", rows: 10, class: "w-full font-mono", "{formats}" }
				}
				{ form_actions(&item_path("choir", id), "Save Changes", None) }
			}
		},
	)
}
