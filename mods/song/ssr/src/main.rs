use indexmap::IndexMap;
use dioxus::prelude::*;
use serde::Deserialize;

use ndc_dioxus_shared::{
    current_user, display_or_id, html_response, html_response_with_status, item_menu, item_path,
    key_transpose_options, parse_pairs, render_hyle_list_with_source, split_path, RequestContext,
    ResponsePayload,
};
use super::{load_dataset_item_json, load_dataset_source};

pub fn route(ctx: &RequestContext<'_>) -> Option<ResponsePayload> {
    let parts = split_path(ctx.path);
    match (ctx.method, parts.as_slice()) {
        ("GET", ["song"]) | ("POST", ["song"]) => {
            let mut source = load_dataset_source("song.items")?;
            if let Some(types) = load_dataset_source("song.types") {
                source.extend(types);
            }
            Some(render_hyle_list_with_source(
                ctx,
                "song",
                Some("🎸"),
                source,
                &["title", "type", "author"],
                10,
            ))
        }
        ("GET", ["song", id]) if *id != "add" => Some(render_song_detail(ctx, id)),
        ("GET", ["song", id, "edit"]) => Some(render_song_edit(ctx, id)),
        ("GET", ["song", id, "delete"]) if *id != "add" => {
            let json = load_dataset_item_json(ctx.fd, "song.items", id)?;
            let item: SongRow = serde_json::from_str(&json).ok()?;
            Some(ndc_dioxus_shared::render_delete_confirm("song", id, &item.title, ctx))
        }
        _ => ndc_dioxus_shared::default_crud_routes(
            ctx,
            "song",
            Some("🎸"),
            None::<ndc_dioxus_shared::CrudHandler>,
            None::<ndc_dioxus_shared::CrudHandler>,
        ),
    }
}

fn song_flags(query: &str, user: Option<&str>) -> (i32, bool, bool, bool) {
    let pairs = parse_pairs(query);
    let transpose = ndc_dioxus_shared::get_pair(&pairs, "t")
        .and_then(|v| v.parse().ok())
        .unwrap_or(0);
    let mut use_bemol = ndc_dioxus_shared::get_pair(&pairs, "b") == Some("1");
    let mut use_latin = ndc_dioxus_shared::get_pair(&pairs, "l") == Some("1");
    let show_media = ndc_dioxus_shared::get_pair(&pairs, "m") == Some("1");

    if query.is_empty() {
        if let Some(user) = user {
            if let Some(b) = crate::get_song_pref(user, "chords-bemol") {
                use_bemol = b.trim() == "1";
            }
            if let Some(l) = crate::get_song_pref(user, "chords-latin") {
                use_latin = l.trim() == "1";
            }
        }
    }

    (transpose, use_bemol, use_latin, show_media)
}

fn render_song_detail(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
    let json = match load_dataset_item_json(ctx.fd, "song.items", id) {
        Some(j) => j,
        None => return html_response_with_status(404, "404", rsx! { "Song not found" }),
    };

    let item: SongRow = match serde_json::from_str(&json) {
        Ok(p) => p,
        Err(e) => return html_response("500", rsx! { "Parse error: {e}" }),
    };

    let title = item.title.as_str();
    let yt_raw = item.yt.as_str();
    let audio_raw = item.audio.as_str();
    let pdf_raw = item.pdf.as_str();
    let author = item.author.as_str();
    let song_type = song_type_display(&item.song_type);
    let is_owner = current_user(ctx).map(|u| u == item.owner).unwrap_or(false);

    let display_title = display_or_id(title, id).to_string();
    let path = item_path("song", id);
    let (transpose, use_bemol, use_latin, show_media_query) = song_flags(ctx.query, current_user(ctx));
    let show_media = show_media_query;

    let original_key = crate::get_song_original_key(id);
    let mut flags = 0x04; // TRANSP_HTML
    if use_bemol { flags |= 0x08; } // TRANSP_BEMOL
    if use_latin { flags |= 0x80; } // TRANSP_LATIN
    
    let chord_data = crate::get_song_transpose(id, transpose, flags).unwrap_or_else(|| item.data.clone());

    let key_options = key_transpose_options(original_key, use_bemol, use_latin);
    let author_display = if author.is_empty() { "N/A" } else { author };
    let yt = if yt_raw.is_empty() { None } else { Some(yt_raw) };
    let audio = if audio_raw.is_empty() { None } else { Some(audio_raw) };
    let pdf = if pdf_raw.is_empty() { None } else { Some(pdf_raw) };
    let save_url: &str = if current_user(ctx).is_some() {
        "/api/song/prefs"
    } else {
        ""
    };

    let menu_items = Some(rsx! {
        { ndc_dioxus_shared::viewer_controls("song", 100, save_url) }
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
        { item_menu("song", id, is_owner) }
    });

    html_response(
        &display_title,
        ndc_dioxus_shared::layout(
            current_user(ctx),
            &display_title,
            &path,
            Some("🎸"),
            menu_items,
            rsx! {
                div { class: "center flex flex-col gap-4",
                    div { id: "song-detail-body", class: "contents", "data-detail-viewer-scope": "1",
                        if !song_type.is_empty() || !author.is_empty() {
                            div { class: "flex justify-between items-start w-full max-w-xl text-xs text-muted",
                                div { class: "italic whitespace-pre-wrap", "{song_type}" }
                                div { class: "text-right", "{author_display}" }
                            }
                        }
                        div { class: "detail-viewer-scroll w-full max-w-xl", "data-detail-viewer-scroll": "1",
                            pre {
                                id: "chord-data",
                                "data-detail-viewer-target": "1",
                                class: "whitespace-pre-wrap font-mono p-4 rounded chord-data",
                                dangerous_inner_html: "{chord_data}"
                            }
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

fn render_song_edit(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
    let json = match load_dataset_item_json(ctx.fd, "song.items", id) {
        Some(j) => j,
        None => return html_response_with_status(404, "404", rsx! { "Song not found" }),
    };

    let item: SongRow = match serde_json::from_str(&json) {
        Ok(p) => p,
        Err(e) => return html_response("500", rsx! { "Parse error: {e}" }),
    };

    let is_owner = current_user(ctx) == Some(&item.owner);
    if !is_owner {
        return html_response_with_status(403, "403", rsx! { "Forbidden" });
    }

    let initial = IndexMap::from([
        ("title".to_owned(), item.title.clone()),
        ("type".to_owned(), song_type_display(&item.song_type)),
        ("author".to_owned(), item.author.clone()),
        ("yt".to_owned(), item.yt.clone()),
        ("audio".to_owned(), item.audio.clone()),
        ("pdf".to_owned(), item.pdf.clone()),
        ("data".to_owned(), item.data.clone()),
    ]);

    ndc_dioxus_shared::render_hyle_edit(
        ctx,
        "song",
        Some("🎸"),
        id,
        initial,
        &["title", "type", "author", "yt", "audio", "pdf", "data"],
        "multipart/form-data",
    )
}

fn song_type_display(types: &[String]) -> String {
    types.join(", ")
}

#[derive(Debug, Deserialize)]
struct SongRow {
    #[allow(dead_code)]
    id: String,
    #[serde(default)]
    title: String,
    #[serde(default, rename = "type")]
    song_type: Vec<String>,
    #[serde(default)]
    author: String,
    #[serde(default)]
    yt: String,
    #[serde(default)]
    audio: String,
    #[serde(default)]
    pdf: String,
    #[serde(default)]
    data: String,
    #[serde(default)]
    owner: String,
}
