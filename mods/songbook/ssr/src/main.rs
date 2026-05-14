use dioxus::prelude::*;
use serde::Deserialize;
use ndc_dioxus_shared::{
    RequestContext, ResponsePayload, body_str, current_user, display_or_id,
    edit_form_page, edit_path, empty_state, form_actions, get_pair, html_response,
    html_response_with_status, item_menu,
    item_path, key_names, key_transpose_options, layout, parse_dataset_items, parse_pairs,
    split_path,
};

use crate::{load_dataset_json, load_dataset_item_json, load_dataset_json_with_include};

pub fn route(ctx: &RequestContext<'_>) -> Option<ResponsePayload> {
	let parts = split_path(&ctx.path);
	match (ctx.method, parts.as_slice()) {
		("POST", ["songbook"]) => {
			let items = parse_dataset_items(body_str(ctx.body), &["title", "choir"]);
			Some(ndc_dioxus_shared::render_hyle_list(ctx, "songbook", Some("📖"), items, &["title", "choir"], 10))
		}
		("GET", ["songbook", id]) if *id != "add" => Some(render_detail(ctx, id)),
		("POST", ["songbook", id]) if *id != "add" => Some(render_detail(ctx, id)),
		("GET", ["songbook", id, "edit"]) => Some(render_edit(ctx, id)),
		("POST", ["songbook", id, "edit"]) => Some(render_edit(ctx, id)),
		("POST", ["songbook", id, "delete"]) if *id != "add" => {
			Some(ndc_dioxus_shared::render_delete_confirm("songbook", id, "", ctx))
		}
		_ => ndc_dioxus_shared::default_crud_routes(
			ctx,
			"songbook",
			Some("📖"),
			None,
			None,
		),
	}
}

#[derive(Debug, Deserialize)]
struct SongbookDatasetJson {
    rows: Vec<SongbookDatasetRow>,
}

#[derive(Debug, Deserialize)]
struct SongbookDatasetRow {
    id: String,
    #[serde(default)]
    title: String,
    #[serde(default)]
    choir: String,
    #[serde(default)]
    songs: String,
    #[serde(default)]
    owner: String,
}

#[derive(Debug, Deserialize)]
struct SongDatasetJson {
    rows: Vec<SongDatasetRow>,
}

#[derive(Debug, Deserialize)]
struct SongDatasetRow {
    id: String,
    #[serde(default)]
    title: String,
    #[serde(default)]
    data: String,
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

pub fn render_detail(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
    let json = match load_dataset_item_json(ctx.fd, "songbook.items", id) {
        Some(j) => j,
        None => return html_response_with_status(404, "404", rsx!{ "Songbook not found" }),
    };

    let item: SongbookDatasetRow = match serde_json::from_str(&json) {
        Ok(p) => p,
        Err(e) => return html_response("500", rsx!{ "Parse error: {e}" }),
    };

    let title = &item.title;
    let owner = &item.owner;
    let choir = &item.choir;
    let viewer_zoom = 100;
    let is_owner = current_user(ctx) == Some(owner);
    let path = item_path("songbook", id);
    let page_title = format!("songbook: {title}");
    let choir_href = item_path("choir", choir);
    let save_url: &str = if current_user(ctx).is_some() { "/api/song/prefs" } else { "" };

    let song_entries: Vec<(&str, i32, &str, i32)> = item.songs
        .lines()
        .filter(|line| !line.trim().is_empty())
        .filter_map(|line| {
            let parts: Vec<&str> = line.split(':').collect();
            if parts.len() >= 4 {
                Some((parts[0], parts[1].parse().unwrap_or(0), parts[2], parts[3].parse().unwrap_or(0)))
            } else {
                None
            }
        })
        .collect();

    let display_songs: Vec<DisplaySongbookSong> = song_entries
        .iter()
        .filter_map(|(chord_id, transpose, format, original_key)| {
            let song_json = load_dataset_item_json(ctx.fd, "song.items", chord_id)?;
            let song: SongDatasetRow = serde_json::from_str(&song_json).ok()?;
            let target_idx = ((original_key + transpose) % 12 + 12) % 12;
            let target_key = key_names(false, false)[target_idx as usize].to_string();
            let transpose_options = key_transpose_options(*original_key, false, false);
            let display_title = display_or_id(&song.title, chord_id).to_string();
            Some(DisplaySongbookSong {
                chord_id: chord_id.to_string(),
                transpose: *transpose,
                format_name: format.to_string(),
                display_title,
                chord_data: song.data,
                target_key,
                transpose_options,
            })
        })
        .collect();
    html_response(
        &page_title,
        layout(
            current_user(ctx),
            &page_title,
            &path,
            Some("📖"),
            Some(rsx! {
                { ndc_dioxus_shared::viewer_controls("songbook", viewer_zoom, save_url) }
                { item_menu("songbook", id, is_owner) }
            }),
            rsx! {
                div { class: "flex flex-col gap-1", "data-detail-viewer-scope": "1",
                    if !choir.is_empty() {
                        div { class: "flex justify-end text-xs text-muted",
                            span { class: "grow", "" }
                            a { href: "{choir_href}", class: "text-muted", "{choir}" }
                        }
                    }
                    if display_songs.is_empty() {
                        { empty_state("No songs yet.") }
                    } else {
                        div { class: "flex flex-col gap-4",
                            for (index, song) in display_songs.into_iter().enumerate() {
                                div {
                                    id: "{index}",
                                    class: "flex flex-col gap-2",
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
                                                        input { r#type: "hidden", name: "csrf_token", value: "{ctx.csrf_token}" }
                                                        select { name: "t", class: "p-1",
                                                            for (semitones, option_label) in song.transpose_options.iter() {
                                                                option { value: "{semitones}", selected: *semitones == song.transpose, "{option_label}" }
                                                            }
                                                        }
                                                        input { r#type: "hidden", name: "n", value: "{index}" }
                                                        button { r#type: "submit", class: "btn py-1 px-2", "Apply" }
                                                    }
                                                    form { method: "POST", action: "/songbook/{id}/randomize", enctype: "multipart/form-data",
                                                        input { r#type: "hidden", name: "csrf_token", value: "{ctx.csrf_token}" }
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
                                                class: "font-mono whitespace-pre-wrap bg-surface p-4 rounded",
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

pub fn render_edit(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
    let sb_json = match load_dataset_item_json(ctx.fd, "songbook.items", id) {
        Some(j) => j,
        None => return html_response_with_status(404, "404", rsx!{ "Songbook not found" }),
    };
    let sb_item: SongbookDatasetRow = match serde_json::from_str(&sb_json) {
        Ok(p) => p,
        Err(_) => return html_response("500", rsx!{ "Parse error" }),
    };

    let songs_json = match load_dataset_json(ctx.fd, "song.items") {
        Some(j) => j,
        None => String::new(),
    };
    let song_parsed: SongDatasetJson = serde_json::from_str(&songs_json).unwrap_or(SongDatasetJson { rows: vec![] });
    let all_chords: Vec<SongbookEditChord> = song_parsed.rows.iter()
        .map(|s| SongbookEditChord { id: s.id.clone(), title: s.title.clone(), r#type: String::new() })
        .collect();

    let types_json = load_dataset_json(ctx.fd, "song.types");
    let all_types: Vec<String> = types_json
        .and_then(|j| serde_json::from_str::<serde_json::Value>(&j).ok())
        .map(|v| {
            v.as_array()
                .map(|arr| {
                    arr.iter()
                        .filter_map(|item| item.get("name").and_then(|n| n.as_str()).map(String::from))
                        .collect()
                })
                .unwrap_or_default()
        })
        .unwrap_or_default();

    let title = &sb_item.title;
    let has_songs = !sb_item.songs.trim().is_empty();
    let parsed_songs: Vec<SongbookEditSong> = sb_item.songs
        .lines()
        .filter(|line| !line.trim().is_empty())
        .map(parse_songbook_edit_song)
        .collect::<Vec<_>>();

    // Add default blank row if no songs exist (matching main's C behavior)
    let songs_with_default: Vec<SongbookEditSong> = if has_songs {
        parsed_songs
    } else {
        vec![SongbookEditSong {
            chord_id: "".to_string(),
            transpose: 0,
            format: "any".to_string(),
            original_key: 0,
        }]
    };

    let path = edit_path("songbook", id);
    let heading = format!("Edit {title}");
    let rows: Vec<DisplaySongbookEditRow> = songs_with_default
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
                input { r#type: "hidden", name: "csrf_token", value: "{ctx.csrf_token}" }
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
                input { r#type: "hidden", name: "amount", value: "{songs_with_default.len()}" }
                { form_actions(&item_path("songbook", id), "Save Changes", extra) }
            }
        },
    )
}
