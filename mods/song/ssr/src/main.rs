use dioxus::prelude::*;

use ndc_dioxus_shared::{
    RequestContext, ResponsePayload, SongItem, body_str, current_user, display_or_id,
    html_response, item_menu, item_path, key_transpose_options, parse_pairs,
    parse_index_items_rich, parse_json_array, render_hyle_edit, render_hyle_list, split_path,
};

pub fn route(ctx: &RequestContext<'_>) -> Option<ResponsePayload> {
    let parts = split_path(ctx.path);
    match (ctx.method, parts.as_slice()) {
        ("POST", ["song"]) => Some(render_song_list(ctx)),
        _ => ndc_dioxus_shared::default_crud_routes(
            ctx, "song", Some("🎸"),
            None::<ndc_dioxus_shared::CrudHandler>,
            Some(render_edit as ndc_dioxus_shared::CrudHandler),
        ),
    }
}

fn render_song_list(ctx: &RequestContext<'_>) -> ResponsePayload {
    let items = parse_index_items_rich(body_str(ctx.body), &["type"]);
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

pub fn render_detail(payload: &SongItem<'_>, id: &str, ctx: &RequestContext<'_>) -> ResponsePayload {
    let title        = payload.title;
    let chord_data   = payload.data;
    let yt_raw       = payload.yt;
    let audio_raw    = payload.audio;
    let pdf_raw      = payload.pdf;
    let categories   = payload.categories;
    let author       = payload.author;

    let display_title = display_or_id(title, id).to_string();
    let path = item_path("song", id);
    let (transpose, use_bemol, use_latin, show_media_query) = song_flags(ctx.query);
    let viewer_zoom  = payload.viewer_zoom.clamp(70, 170);
    let viewer_bemol = payload.viewer_bemol || use_bemol;
    let viewer_latin = payload.viewer_latin || use_latin;
    let original_key = payload.original_key;
    let owner        = payload.owner;
    let show_media   = payload.show_media || show_media_query;

    let key_options = key_transpose_options(original_key, viewer_bemol, viewer_latin);
    let author_display = if author.is_empty() { "N/A" } else { author };
    let yt    = if yt_raw.is_empty()    { None } else { Some(yt_raw) };
    let audio = if audio_raw.is_empty() { None } else { Some(audio_raw) };
    let pdf   = if pdf_raw.is_empty()   { None } else { Some(pdf_raw) };
    let save_url: &str = if current_user(ctx).is_some() { "/api/song/prefs" } else { "" };

    let menu_items = Some(rsx! {
        { ndc_dioxus_shared::viewer_controls("song", viewer_zoom, save_url) }
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
                input { r#type: "checkbox", name: "b", value: "1", checked: viewer_bemol }
                span { "Flats (♭)" }
            }
            label {
                input { r#type: "checkbox", name: "l", value: "1", checked: viewer_latin }
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
                        if !categories.is_empty() || !author.is_empty() {
                            div { class: "flex justify-between items-start w-full max-w-xl text-xs text-muted",
                                div { class: "italic whitespace-pre-wrap", "{categories}" }
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

pub fn render_edit(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
    let pairs = parse_pairs(body_str(ctx.body));
    let all_types = parse_json_array::<String>(
        ndc_dioxus_shared::get_pair(&pairs, "allTypes").unwrap_or("[]"),
    );
    let fields = ["title", "author", "type", "yt", "audio", "pdf", "data"]
        .iter()
        .map(|&k| {
            (
                k.to_owned(),
                ndc_dioxus_shared::get_pair(&pairs, k).unwrap_or("").to_owned(),
            )
        })
        .collect();
    render_hyle_edit(
        ctx,
        "song",
        Some("🎸"),
        id,
        fields,
        &["title", "type", "author", "yt", "audio", "pdf", "data"],
        "application/x-www-form-urlencoded",
        all_types,
    )
}
