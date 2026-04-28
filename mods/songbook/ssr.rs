use dioxus::prelude::*;
use serde::Deserialize;

use crate::{
    RequestContext, ResponsePayload, current_user, edit_form_page, empty_state, error_page,
    form_actions, html_response, html_response_with_status, item_menu, item_path, key_names,
    parse_json_body, parse_pairs,
};

pub(crate) fn route(ctx: &RequestContext) -> Option<ResponsePayload> {
	let parts = crate::split_path(&ctx.path);
	match (ctx.method.as_str(), parts.as_slice()) {
		("GET", ["songbook", "add"]) => {
			let pairs = parse_pairs(&ctx.query);
			let choir = crate::get_pair(&pairs, "choir").unwrap_or("").to_string();
			let extra = if choir.is_empty() {
				Vec::new()
			} else {
				vec![("choir", choir)]
			};
			Some(crate::index::render_add_form(ctx, "songbook", extra))
		}
		("POST", ["songbook"]) => Some(crate::index::render_list(ctx, "songbook")),
		("POST", ["songbook", id, "delete"]) => {
			Some(crate::index::render_delete_confirm(ctx, "songbook", id))
		}
		("POST", ["songbook", id]) => Some(render_detail(ctx, id)),
		("POST", ["songbook", id, "edit"]) => Some(render_edit(ctx, id)),
		_ => None,
	}
}

#[derive(Clone, Debug, Deserialize)]
#[allow(non_snake_case)]
struct SongbookSong {
    chordId: Option<String>,
    transpose: Option<i32>,
    format: Option<String>,
    chordTitle: Option<String>,
    chordData: Option<String>,
    originalKey: Option<i32>,
}

#[derive(Clone, Debug, Deserialize)]
#[allow(non_snake_case)]
struct SongbookPayload {
    title: Option<String>,
    owner: Option<String>,
    choir: Option<String>,
    viewerZoom: Option<i32>,
    songs: Option<Vec<SongbookSong>>,
}

#[derive(Clone, Debug, Deserialize)]
struct SongbookEditChord {
    id: String,
    title: String,
    #[serde(default)]
    r#type: String,
}

#[derive(Clone, Debug)]
struct SongbookEditSong {
    chord_id: String,
    transpose: i32,
    format: String,
    original_key: i32,
}

#[derive(Clone, Debug)]
struct DisplaySongbookSong {
    chord_id: String,
    transpose: i32,
    format_name: String,
    display_title: String,
    chord_data: String,
    target_key: String,
    transpose_options: Vec<(i32, String)>,
}

#[derive(Clone, Debug)]
struct DisplaySongbookEditRow {
    index: usize,
    format_value: String,
    display_song: String,
    original_key: i32,
    target_key: i32,
}

fn parse_songbook_edit_song(line: &str) -> SongbookEditSong {
    let mut parts = line.split(':');
    SongbookEditSong {
        chord_id: parts.next().unwrap_or("").to_string(),
        transpose: parts.next().unwrap_or("0").parse().unwrap_or(0),
        format: parts.next().unwrap_or("any").to_string(),
        original_key: parts.next().unwrap_or("0").parse().unwrap_or(0),
    }
}

fn parse_json_array<T: for<'de> Deserialize<'de>>(raw: &str) -> Vec<T> {
    serde_json::from_str(raw).unwrap_or_default()
}

pub(crate) fn render_detail(ctx: &RequestContext, id: &str) -> ResponsePayload {
    match parse_json_body::<SongbookPayload>(&ctx.body) {
        Ok(payload) => {
            let title = payload.title.unwrap_or_default();
            let owner = payload.owner.unwrap_or_default();
            let choir = payload.choir.unwrap_or_default();
            let viewer_zoom = payload.viewerZoom.unwrap_or(100).clamp(70, 170);
            let songs = payload.songs.unwrap_or_default();
            let is_owner = current_user(ctx) == Some(owner.as_str());
            let path = item_path("songbook", id);
            let page_title = format!("songbook: {title}");
            let choir_href = item_path("choir", &choir);
            let save_url = if current_user(ctx).is_some() {
                "/api/song/prefs"
            } else {
                ""
            };
            let display_songs: Vec<DisplaySongbookSong> = songs
                .into_iter()
                .map(|song| {
                    let chord_id = song.chordId.unwrap_or_default();
                    let transpose = song.transpose.unwrap_or(0);
                    let format_name = song.format.unwrap_or_default();
                    let chord_title = song.chordTitle.unwrap_or_default();
                    let chord_data = song.chordData.unwrap_or_default();
                    let original_key = song.originalKey.unwrap_or(0);
                    let target_idx = ((original_key + transpose) % 12 + 12) % 12;
                    let target_key = key_names(false, false)[target_idx as usize].to_string();
                    let transpose_options = key_names(false, false)
                        .iter()
                        .enumerate()
                        .map(|(i, key)| {
                            let semitones = ((i as i32 - original_key) % 12 + 12) % 12;
                            let suffix = if semitones == 0 { " (Original)" } else { "" };
                            (semitones, format!("{key}{suffix}"))
                        })
                        .collect();
                    let display_title = if chord_title.is_empty() {
                        chord_id.clone()
                    } else {
                        chord_title
                    };
                    DisplaySongbookSong {
                        chord_id,
                        transpose,
                        format_name,
                        display_title,
                        chord_data,
                        target_key,
                        transpose_options,
                    }
                })
                .collect();
            html_response(
                &page_title,
                crate::layout(
                    current_user(ctx),
                    &page_title,
                    &path,
                    Some("📖"),
                    Some(rsx! {
                        div {
                            class: "viewer-controls",
                            "data-detail-viewer-controls": "songbook",
                            "data-detail-viewer-save-url": "{save_url}",
                            label {
                                "Zoom"
                                input {
                                    r#type: "range",
                                    min: "70",
                                    max: "170",
                                    step: "10",
                                    value: "{viewer_zoom}",
                                    "data-detail-viewer-zoom": "1"
                                }
                            }
                            p { class: "text-xs text-muted", "data-detail-viewer-zoom-label": "1", "{viewer_zoom}%" }
                            label {
                                input {
                                    r#type: "checkbox",
                                    checked: true,
                                    "data-detail-viewer-wrap": "1"
                                }
                                span { "Wrap lines" }
                            }
                        }
                        { item_menu("songbook", id, is_owner) }
                    }),
                    rsx! {
                        div { class: "flex flex-col gap-1", "data-detail-viewer-scope": "1",
                            if !choir.is_empty() {
                                div { class: "flex justify-end text-xs text-muted",
                                    a { href: "{choir_href}", class: "text-muted", "{choir}" }
                                }
                            }
                            h3 { "Songs" }
                            if display_songs.is_empty() {
                                { empty_state("No songs yet.") }
                            } else {
                                div { class: "flex flex-col gap-1",
                                    for (index, song) in display_songs.into_iter().enumerate() {
                                        div {
                                            id: "{index}",
                                            class: "separator flex flex-col gap-2",
                                            "data-songbook-item": "1",
                                            "data-songbook-id": "{id}",
                                            "data-song-id": "{song.chord_id}",
                                            if song.chord_id.is_empty() {
                                                div { class: "p-4 bg-surface rounded",
                                                    h3 { "{song.format_name.to_uppercase()}" }
                                                }
                                            } else {
                                                div { class: "flex justify-between items-center",
                                                    div { class: "flex flex-col gap-1",
                                                        h4 { class: "m-0",
                                                            a { href: "/song/{song.chord_id}", target: "_blank", "{song.display_title}" }
                                                        }
                                                        p {
                                                            class: "text-sm text-muted",
                                                            span { "data-songbook-format": "1", "{song.format_name}" }
                                                            " • Key: "
                                                            span { "data-songbook-target-key": "1", "{song.target_key}" }
                                                        }
                                                    }
                                                    if is_owner {
                                                        div { class: "flex gap-2 items-center",
                                                            form {
                                                                method: "POST",
                                                                action: "/songbook/{id}/transpose",
                                                                "data-songbook-transpose-form": "1",
                                                                "data-song-id": "{song.chord_id}",
                                                                "data-row-index": "{index}",
                                                                select { name: "t", class: "p-1",
                                                                    for (semitones, option_label) in song.transpose_options.iter() {
                                                                        option { value: "{semitones}", selected: *semitones == song.transpose, "{option_label}" }
                                                                    }
                                                                }
                                                                input { r#type: "hidden", name: "n", value: "{index}" }
                                                                button { r#type: "submit", class: "btn py-1 px-2", "Apply" }
                                                            }
                                                            form { method: "POST", action: "/songbook/{id}/randomize", enctype: "multipart/form-data",
                                                                input { r#type: "hidden", name: "n", value: "{index}" }
                                                                button { r#type: "submit", class: "btn py-1 px-2", "🎲" }
                                                            }
                                                        }
                                                    }
                                                }
                                                div { class: "detail-viewer-scroll", "data-detail-viewer-scroll": "1",
                                                    pre {
                                                        "data-detail-viewer-target": "1",
                                                        "data-songbook-chord-data": "1",
                                                        class: "font-mono text-sm whitespace-pre-wrap bg-surface p-4 rounded",
                                                        dangerous_inner_html: "{song.chord_data}"
                                                    }
                                                }
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
            error_page(current_user(ctx), "/songbook/", err.status, &err.message),
        ),
    }
}

pub(crate) fn render_edit(ctx: &RequestContext, id: &str) -> ResponsePayload {
    let pairs = parse_pairs(&ctx.body);
    let title = crate::get_pair(&pairs, "title").unwrap_or("").to_string();
    let songs = crate::get_pair(&pairs, "songs")
        .unwrap_or("")
        .lines()
        .filter(|line| !line.trim().is_empty())
        .map(parse_songbook_edit_song)
        .collect::<Vec<_>>();
    let all_chords =
        parse_json_array::<SongbookEditChord>(crate::get_pair(&pairs, "allChords").unwrap_or("[]"));
    let all_types =
        parse_json_array::<String>(crate::get_pair(&pairs, "allTypes").unwrap_or("[]"));
    let path = format!("/songbook/{id}/edit");
    let heading = format!("Edit {title}");
    let rows: Vec<DisplaySongbookEditRow> = songs
        .iter()
        .enumerate()
        .map(|(index, song)| {
            let target_key = ((song.original_key + song.transpose) % 12 + 12) % 12;
            let match_song = all_chords.iter().find(|chord| chord.id == song.chord_id);
            let display = match match_song {
                Some(chord) => format!("{} [{}]", chord.title, chord.id),
                None if !song.chord_id.is_empty() => format!("{} [{}]", song.chord_id, song.chord_id),
                None => String::new(),
            };
            DisplaySongbookEditRow {
                index,
                format_value: song.format.clone(),
                display_song: display,
                original_key: song.original_key,
                target_key,
            }
        })
        .collect();
    let extra = Some(rsx! {
        button { r#type: "submit", name: "action", value: "add_row", class: "btn btn-action", "+ Add Row" }
    });
    edit_form_page(
        current_user(ctx),
        &heading,
        &path,
        Some("📖"),
        rsx! {
            form { method: "POST", action: "{path}", enctype: "multipart/form-data", class: "flex flex-col gap-2 w-full",
                for row in rows {
                    div { class: "flex gap-2 items-center", "data-songbook-edit-row": "1",
                        datalist { id: "types-{row.index}",
                            for kind in all_types.iter() {
                                option { value: "{kind}" }
                            }
                        }
                        datalist { id: "songs-{row.index}",
                            for chord in all_chords.iter() {
                                option { value: "{chord.title} [{chord.id}]", "data-chord-type": "{chord.r#type}" }
                            }
                        }
                        label { class: "w-[150px] shrink-0",
                            "{row.index + 1}. Format:"
                            input {
                                list: "types-{row.index}",
                                name: "fmt_{row.index}",
                                value: "{row.format_value}",
                                class: "w-full",
                                "data-songbook-format-input": "1",
                                "data-songbook-song-list": "songs-{row.index}"
                            }
                        }
                        label { class: "flex-1",
                            "Song:"
                            input {
                                list: "songs-{row.index}",
                                name: "song_{row.index}",
                                value: "{row.display_song}",
                                class: "w-full",
                                "data-songbook-song-input": "1"
                            }
                        }
                        label { class: "w-20 shrink-0",
                            "Key:"
                            select { name: "key_{row.index}", class: "w-full",
                                for (key_idx, name) in key_names(false, false).iter().enumerate() {
                                    option { value: "{key_idx}", selected: key_idx as i32 == row.target_key, "{name}" }
                                }
                            }
                        }
                        input { r#type: "hidden", name: "orig_{row.index}", value: "{row.original_key}" }
                    }
                }
                input { r#type: "hidden", name: "amount", value: "{songs.len()}" }
                { form_actions(&item_path("songbook", id), "Save Changes", extra) }
            }
        },
    )
}
