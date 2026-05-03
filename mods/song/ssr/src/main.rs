use dioxus::prelude::*;

use ndc_dioxus_shared::{
    RequestContext, ResponsePayload, SongItem, current_user, display_or_id, edit_form_page,
    edit_path, form_actions, form_field, html_response, item_menu, item_path,
    key_transpose_options, parse_pairs, body_str,
};

pub fn route(ctx: &RequestContext<'_>) -> Option<ResponsePayload> {
	ndc_dioxus_shared::default_crud_routes(ctx, "song", Some("🎸"), None::<ndc_dioxus_shared::CrudHandler>, Some(render_edit as ndc_dioxus_shared::CrudHandler))
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
    let title = ndc_dioxus_shared::get_pair(&pairs, "title").unwrap_or("").to_string();
    let author = ndc_dioxus_shared::get_pair(&pairs, "author").unwrap_or("").to_string();
    let r#type = ndc_dioxus_shared::get_pair(&pairs, "type").unwrap_or("").to_string();
    let yt = ndc_dioxus_shared::get_pair(&pairs, "yt").unwrap_or("").to_string();
    let audio = ndc_dioxus_shared::get_pair(&pairs, "audio").unwrap_or("").to_string();
    let pdf = ndc_dioxus_shared::get_pair(&pairs, "pdf").unwrap_or("").to_string();
    let data = ndc_dioxus_shared::get_pair(&pairs, "data").unwrap_or("").to_string();
    let action = edit_path("song", id);
    let heading = format!("Edit {}", display_or_id(&title, id));
    edit_form_page(
        current_user(ctx),
        &heading,
        &action,
        Some("🎸"),
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
    )
}
