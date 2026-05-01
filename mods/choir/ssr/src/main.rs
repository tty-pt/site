use dioxus::prelude::*;
use serde::Deserialize;

use ndc_dioxus_shared::{
    RequestContext, ResponsePayload, current_user, display_or_id, edit_form_page, edit_path,
    body_str, form_actions, html_response, item_menu, item_path, key_names, parse_json_body,
    parse_pairs,
};

pub fn route(ctx: &RequestContext<'_>) -> Option<ResponsePayload> {
	ndc_dioxus_shared::default_crud_routes(ctx, "choir", render_detail, render_edit)
}

#[derive(Clone, Debug, Deserialize)]
#[allow(non_snake_case)]
struct ChoirSong {
    id: String,
    title: String,
    preferredKey: i32,
    originalKey: i32,

}

#[derive(Clone, Debug, Deserialize)]
struct SongEntry {
    id: String,
    title: String,
}

#[derive(Clone, Debug, Deserialize)]
#[allow(non_snake_case)]
struct ChoirPayload {
    title: Option<String>,
    owner_name: Option<String>,
    formats: Option<String>,
    songs: Option<Vec<ChoirSong>>,
    allSongs: Option<Vec<SongEntry>>,
    songbooks: Option<Vec<SongEntry>>,
}

pub fn render_detail(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
    match parse_json_body::<ChoirPayload>(body_str(ctx.body)) {
        Ok(mut payload) => {
            let title = payload.title.take().unwrap_or_default();
            let owner = payload.owner_name.take().unwrap_or_default();
            let is_owner = current_user(ctx) == Some(owner.as_str());
            let mut all_songs = payload.allSongs.take().unwrap_or_default();
            all_songs.sort_by(|a, b| a.title.cmp(&b.title));
            let songbooks = payload.songbooks.take().unwrap_or_default();
            let songs = payload.songs.take().unwrap_or_default();
            let formats = payload.formats.take().unwrap_or_default();
            let path = item_path("choir", id);
            let display_songbooks: Vec<(String, String)> = songbooks
                .iter()
                .map(|sb| {
                    let label = display_or_id(&sb.title, &sb.id).to_string();
                    (sb.id.clone(), label)
                })
                .collect();
            let display_songs: Vec<(String, String, String)> = songs
                .iter()
                .map(|song| {
                    let label = display_or_id(&song.title, &song.id).to_string();
                    let key_idx = ((if song.preferredKey != 0 {
                        song.preferredKey
                    } else {
                        song.originalKey
                    }) % 12
                        + 12)
                        % 12;
                    (
                        song.id.clone(),
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
                &title,
                ndc_dioxus_shared::layout(
                    current_user(ctx),
                    &title,
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
                                                for song in all_songs {
                                                    option { value: "{song.title} [{song.id}]" }
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
        Err(err) => ndc_dioxus_shared::render_item_error(ctx, &item_path("choir", id), &err),
    }
}

pub fn render_edit(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
    let pairs = parse_pairs(body_str(ctx.body));
    let title = ndc_dioxus_shared::get_pair(&pairs, "title").unwrap_or("").to_string();
    let formats = ndc_dioxus_shared::get_pair(&pairs, "format").unwrap_or("").to_string();
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
