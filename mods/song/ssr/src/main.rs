use indexmap::IndexMap;
use dioxus::prelude::*;

use ndc_dioxus_shared::{
    RequestContext, ResponsePayload, body_str, current_user, display_or_id,
    html_response, html_response_with_status, item_menu, item_path, key_transpose_options,
    parse_pairs, parse_dataset_items, render_hyle_edit, render_hyle_list,
    split_path,
};
use crate::load_dataset_json;
use serde::Deserialize;

pub fn route(ctx: &RequestContext<'_>) -> Option<ResponsePayload> {
    let parts = split_path(ctx.path);
    match (ctx.method, parts.as_slice()) {
        ("POST", ["song"]) => Some(render_song_list(ctx)),
        (_, ["song", id]) if *id != "add" => Some(render_song_detail(ctx, id)),
        ("GET", ["song", id, "edit"]) => Some(render_song_edit(ctx, id)),
        _ => ndc_dioxus_shared::default_crud_routes(
            ctx, "song", Some("🎸"),
            None::<ndc_dioxus_shared::CrudHandler>,
            None::<ndc_dioxus_shared::CrudHandler>,
        ),
    }
}

fn render_song_list(ctx: &RequestContext<'_>) -> ResponsePayload {
    let items = parse_dataset_items(body_str(ctx.body), &["title"]);
    render_hyle_list(ctx, "song", Some("🎸"), items, &["title", "type"], 10)
}

fn song_flags(query: &str) -> (i32, bool, bool, bool) {
    let pairs = parse_pairs(query);
    let transpose = ndc_dioxus_shared::get_pair(&pairs, "t")
        .and_then(|v| v.parse().ok())
        .unwrap_or(0);
    let use_bemol = ndc_dioxus_shared::get_pair(&pairs, "b") == Some("1");
    let use_latin = ndc_dioxus_shared::get_pair(&pairs, "l") == Some("1");
    let show_media = ndc_dioxus_shared::get_pair(&pairs, "m") == Some("1");
    (transpose, use_bemol, use_latin, show_media)
}

fn render_song_detail(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
    let json = match load_dataset_json(ctx.fd, "song.items") {
        Some(j) => j,
        None => return html_response("500", rsx!{ "Dataset error" }),
    };

    let parsed: DatasetJson = match serde_json::from_str(&json) {
        Ok(p) => p,
        Err(_) => return html_response("500", rsx!{ "Parse error" }),
    };

    let item = match parsed.rows.iter().find(|p| p.id == id) {
        Some(i) => i,
        None => return html_response_with_status(404, "404", rsx!{ "Song not found" }),
    };

    let title = item.title.as_str();
    let chord_data = item.data.as_str();
    let yt_raw = item.yt.as_str();
    let audio_raw = item.audio.as_str();
    let pdf_raw = item.pdf.as_str();
    let author = item.author.as_str();
    let song_type = item.song_type.as_str();
    let owner = current_user(ctx).map(|u| u == item.owner).unwrap_or(false);

    let display_title = display_or_id(title, id).to_string();
    let path = item_path("song", id);
    let (transpose, use_bemol, use_latin, show_media_query) = song_flags(ctx.query);
    let show_media = show_media_query;

    let key_options = key_transpose_options(0, use_bemol, use_latin);
    let author_display = if author.is_empty() { "N/A" } else { author };
    let yt = if yt_raw.is_empty() { None } else { Some(yt_raw) };
    let audio = if audio_raw.is_empty() { None } else { Some(audio_raw) };
    let pdf = if pdf_raw.is_empty() { None } else { Some(pdf_raw) };
    let save_url: &str = if current_user(ctx).is_some() { "/api/song/prefs" } else { "" };

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
        { item_menu("song", id, owner) }
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
    let json = match load_dataset_json(ctx.fd, "song.items") {
        Some(j) => j,
        None => return html_response("500", rsx!{ "Dataset error" }),
    };

    let parsed: DatasetJson = match serde_json::from_str(&json) {
        Ok(p) => p,
        Err(_) => return html_response("500", rsx!{ "Parse error" }),
    };

    let item = parsed.rows.iter().find(|p| p.id == id);
    let fields = IndexMap::from([
        ("title".to_owned(), item.as_ref().map(|p| p.title.clone()).unwrap_or_default()),
        ("type".to_owned(), item.as_ref().map(|p| p.song_type.clone()).unwrap_or_default()),
        ("author".to_owned(), item.as_ref().map(|p| p.author.clone()).unwrap_or_default()),
        ("yt".to_owned(), item.as_ref().map(|p| p.yt.clone()).unwrap_or_default()),
        ("audio".to_owned(), item.as_ref().map(|p| p.audio.clone()).unwrap_or_default()),
        ("pdf".to_owned(), item.as_ref().map(|p| p.pdf.clone()).unwrap_or_default()),
        ("data".to_owned(), item.map(|p| p.data.clone()).unwrap_or_default()),
    ]);

    let types_json = load_dataset_json(ctx.fd, "song.types");
    let all_types: Vec<String> = if let Some(j) = types_json {
        serde_json::from_str::<TypesJson>(&j)
            .map(|v| v.rows.into_iter().map(|r| r.name).collect())
            .unwrap_or_default()
    } else {
        Vec::new()
    };

    render_hyle_edit(
        ctx,
        "song",
        Some("🎸"),
        id,
        fields,
        &["title", "type", "author", "yt", "audio", "pdf", "data"],
        "multipart/form-data",
        all_types,
    )
}

#[derive(Debug, Deserialize)]
struct DatasetJson {
    rows: Vec<SongRow>,
}

#[derive(Debug, Deserialize)]
struct SongRow {
    id: String,
    #[serde(default)]
    title: String,
    #[serde(default, rename = "type")]
    song_type: String,
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

#[derive(Debug, Deserialize)]
struct TypesJson {
    rows: Vec<TypeRow>,
}

#[derive(Debug, Deserialize)]
struct TypeRow {
    name: String,
}