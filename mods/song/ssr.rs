use dioxus::prelude::*;
use serde::Deserialize;

use crate::{
    RequestContext, ResponsePayload, current_user, error_page, form_actions, form_page,
    html_response, html_response_with_status, item_menu, item_path, key_names, parse_json_body,
    parse_pairs,
};

pub(crate) fn route(ctx: &RequestContext) -> Option<ResponsePayload> {
	let parts = crate::split_path(&ctx.path);
	match (ctx.method.as_str(), parts.as_slice()) {
		("GET", ["song", "add"]) => Some(crate::index::render_add_form(ctx, "song", Vec::new())),
		("POST", ["song"]) => Some(crate::index::render_list(ctx, "song")),
		("POST", ["song", id, "delete"]) => Some(crate::index::render_delete_confirm(ctx, "song", id)),
		("POST", ["song", id]) => Some(render_detail(ctx, id)),
		("POST", ["song", id, "edit"]) => Some(render_edit(ctx, id)),
		_ => None,
	}
}

#[derive(Clone, Debug, Deserialize)]
#[allow(non_snake_case)]
struct SongPayload {
    data: Option<String>,
    title: Option<String>,
    yt: Option<String>,
    audio: Option<String>,
    pdf: Option<String>,
    originalKey: Option<i32>,
    owner: Option<bool>,
    categories: Option<String>,
    author: Option<String>,
}

fn song_flags(query: &str) -> (i32, bool, bool, bool) {
    let pairs = parse_pairs(query);
    let transpose = crate::get_pair(&pairs, "t")
        .and_then(|v| v.parse().ok())
        .unwrap_or(0);
    let use_bemol = crate::get_pair(&pairs, "b") == Some("1");
    let use_latin = crate::get_pair(&pairs, "l") == Some("1");
    let show_media = crate::get_pair(&pairs, "m") == Some("1");
    (transpose, use_bemol, use_latin, show_media)
}

pub(crate) fn render_detail(ctx: &RequestContext, id: &str) -> ResponsePayload {
    match parse_json_body::<SongPayload>(&ctx.body) {
        Ok(payload) => {
            let path = item_path("song", id);
            let title = payload.title.clone().unwrap_or_default();
            let display_title = if title.is_empty() { id.to_string() } else { title };
            let (transpose, use_bemol, use_latin, show_media) = song_flags(&ctx.query);
            let original_key = payload.originalKey.unwrap_or(0);
            let owner = payload.owner.unwrap_or(false);
            let categories = payload.categories.unwrap_or_default();
            let author = payload.author.unwrap_or_default();
            let chord_data = payload.data.unwrap_or_default();
            let keys = key_names(use_bemol, use_latin);
            let key_options: Vec<(i32, String)> = keys
                .iter()
                .enumerate()
                .map(|(i, key)| {
                    let semitones = ((i as i32 - original_key) % 12 + 12) % 12;
                    let suffix = if semitones == 0 { " (Original)" } else { "" };
                    (semitones, format!("{key}{suffix}"))
                })
                .collect();
            let author_display = if author.is_empty() {
                "N/A".to_string()
            } else {
                author.clone()
            };
            let yt = payload.yt.filter(|s| !s.is_empty());
            let audio = payload.audio.filter(|s| !s.is_empty());
            let pdf = payload.pdf.filter(|s| !s.is_empty());
            let menu_items = Some(rsx! {
                form {
                    id: "transpose-form",
                    method: "GET",
                    action: "{path}",
                    class: "flex flex-col gap-2",
                    "data-song-id": "{id}",
                    "data-song-transpose-runtime": "wasm",
                    label {
                        "Key:"
                        select { name: "t",
                            for (semitones, option_label) in key_options {
                                option {
                                    value: "{semitones}",
                                    selected: semitones == transpose,
                                    "{option_label}"
                                }
                            }
                        }
                    }
                    label {
                        input { r#type: "checkbox", name: "b", value: "1", checked: use_bemol }
                        span { "Flats (♭)" }
                    }
                    label {
                        input { r#type: "checkbox", name: "l", value: "1", checked: use_latin }
                        span { "Latin" }
                    }
                    label {
                        input { r#type: "checkbox", name: "m", value: "1", checked: show_media }
                        span { "▶️ Video" }
                    }
                    button { r#type: "submit", class: "btn", "Apply" }
                }
                { item_menu("song", id, owner) }
            });

            html_response(
                &display_title,
                crate::layout(
                    current_user(ctx),
                    &display_title,
                    &path,
                    Some("🎸"),
                    menu_items,
                    rsx! {
                        div { class: "center flex flex-col gap-4",
                            div { id: "song-detail-body", class: "contents",
                                if !categories.is_empty() || !author.is_empty() {
                                    div { class: "flex justify-between items-start w-full max-w-xl text-xs text-muted",
                                        div { class: "italic whitespace-pre-wrap", "{categories}" }
                                        div { class: "text-right", "{author_display}" }
                                    }
                                }
                                div {
                                    id: "chord-data",
                                    class: "whitespace-pre-wrap font-mono p-4 rounded w-full max-w-xl chord-data",
                                    dangerous_inner_html: "{chord_data}"
                                }
                                div { id: "media-slot", class: "contents",
                                    if show_media && (yt.is_some() || audio.is_some() || pdf.is_some()) {
                                        div { id: "media-container", class: "flex flex-col gap-4 w-full max-w-xl",
                                            if let Some(yt) = yt {
                                                iframe {
                                                    src: "https://www.youtube.com/embed/{yt}",
                                                    class: "w-full aspect-video border-none",
                                                    allowfullscreen: true
                                                }
                                            }
                                            if let Some(audio) = audio {
                                                audio { controls: true, class: "w-full",
                                                    source { src: "{audio}", r#type: "audio/mpeg" }
                                                }
                                            }
                                            if let Some(pdf) = pdf {
                                                a { href: "{pdf}", target: "_blank", rel: "noopener", class: "text-blue-600", "📄 View PDF" }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    },
                ),
            )
        }
        Err(err) => html_response_with_status(
            err.status,
            &err.status.to_string(),
            error_page(current_user(ctx), &item_path("song", id), err.status, &err.message),
        ),
    }
}

pub(crate) fn render_edit(ctx: &RequestContext, id: &str) -> ResponsePayload {
    let pairs = parse_pairs(&ctx.body);
    let title = crate::get_pair(&pairs, "title").unwrap_or("").to_string();
    let author = crate::get_pair(&pairs, "author").unwrap_or("").to_string();
    let r#type = crate::get_pair(&pairs, "type").unwrap_or("").to_string();
    let yt = crate::get_pair(&pairs, "yt").unwrap_or("").to_string();
    let audio = crate::get_pair(&pairs, "audio").unwrap_or("").to_string();
    let pdf = crate::get_pair(&pairs, "pdf").unwrap_or("").to_string();
    let data = crate::get_pair(&pairs, "data").unwrap_or("").to_string();
    let action = format!("/song/{id}/edit");
    html_response(
        &format!("Edit {}", if title.is_empty() { id } else { title.as_str() }),
        form_page(
            current_user(ctx),
            &format!("Edit {}", if title.is_empty() { id } else { title.as_str() }),
            &action,
            Some("🎸"),
            Some("Edit Song"),
            rsx! {
                form { method: "POST", action: "{action}", enctype: "application/x-www-form-urlencoded", class: "flex flex-col gap-4 w-full max-w-2xl",
                    { form_field("Title:", "title", &title, None, "text", "w-full") }
                    { form_field("Author:", "author", &author, None, "text", "w-full") }
                    { form_field("Type (e.g., entrada, santo, comunhao):", "type", &r#type, Some(3), "text", "w-full font-mono") }
                    { form_field("YouTube URL:", "yt", &yt, None, "text", "w-full") }
                    { form_field("Audio URL:", "audio", &audio, None, "text", "w-full") }
                    { form_field("PDF URL:", "pdf", &pdf, None, "text", "w-full") }
                    { form_field("Chord Data:", "data", &data, Some(20), "text", "w-full font-mono whitespace-pre") }
                    { form_actions(&item_path("song", id), "Save Changes", None) }
                }
            },
        ),
    )
}

pub(crate) fn form_field(
    label: &str,
    name: &str,
    value: &str,
    textarea_rows: Option<usize>,
    input_type: &str,
    extra_class: &str,
) -> Element {
    match textarea_rows {
        Some(rows) => rsx! {
            label {
                "{label}"
                textarea { name: "{name}", rows: rows as i64, class: "{extra_class}", "{value}" }
            }
        },
        None => rsx! {
            label {
                "{label}"
                input { r#type: "{input_type}", name: "{name}", value: "{value}", class: "{extra_class}" }
            }
        },
    }
}
