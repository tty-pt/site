use std::collections::HashMap;

use dioxus::prelude::*;
use ndc_dioxus_shared::{
    RequestContext, ResponsePayload, current_user, display_or_id,
    edit_path, html_response, html_response_with_module,
    html_response_with_status,
    item_path, key_names, key_transpose_options, split_path,
};
use crate::site_ui::{edit_form_page, empty_state, form_actions, item_menu, layout, viewer_controls};

use hyle::{load_typed_item, load_typed_rows};

pub fn route(ctx: &RequestContext<'_>) -> Option<ResponsePayload> {
    let parts = split_path(ctx.path);
    match (ctx.method, parts.as_slice()) {
        ("GET", ["songbook"]) | ("POST", ["songbook"]) => {
            let source = crate::source_query::query_source("songbook.items", ctx.query, Some(10))?;
            Some(crate::hyle_ssr::render_hyle_list_queried(
                ctx, "songbook", "📖", source,
                &["title", "choir"],
                10,
            ))
        }
        ("GET", ["songbook", id]) if *id != "add" => Some(render_detail(ctx, id)),
        ("GET", ["songbook", id, "edit"]) => Some(render_edit(ctx, id)),
        _ => crate::site_ui::default_crud_routes(ctx, "songbook", "📖"),
    }
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

pub fn render_detail(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
    let empty_vec = vec![];

    let item: serde_json::Value = match load_typed_item("songbook.items", id) {
        Ok(Some(p)) => p,
        Ok(None) => return html_response_with_status(404, "404", rsx!{ "Songbook not found" }),
        Err(e) => return html_response("500", rsx!{ "Parse error: {e}" }),
    };

    let title = item["title"].as_str().unwrap_or("");
    let choir = item["choir"].as_str().unwrap_or("");
    let owner = item["owner"].as_str().unwrap_or("");
    let is_owner = current_user(ctx) == Some(owner);
    let path = item_path("songbook", id);
    let page_title = format!("songbook: {title}");
    let choir_href = item_path("choir", choir);
    let save_url: &str = if current_user(ctx).is_some() { "/api/song/prefs" } else { "" };

    let mut viewer_zoom = 100;
    let mut use_bemol = false;
    let mut use_latin = false;
    if let Some(user) = current_user(ctx) {
        if let Some(z) = crate::song::get_song_pref(user, "chords-zoom") {
            viewer_zoom = z.parse().unwrap_or(100);
        }
        if let Some(b) = crate::song::get_song_pref(user, "chords-bemol") {
            use_bemol = b.trim() == "1";
        }
        if let Some(l) = crate::song::get_song_pref(user, "chords-latin") {
            use_latin = l.trim() == "1";
        }
    }

    // item_songs is the resolved inverse → Vec<Value> of entry IDs
    let item_song_ids: &Vec<serde_json::Value> = item["item_songs"]
        .as_array().unwrap_or(&empty_vec);

    // Load all item_songs entries
    let entry_map: HashMap<String, serde_json::Value> =
        load_typed_rows::<serde_json::Value>("songbook.item_songs")
            .into_iter()
            .filter(|e| e["songbook"].as_str() == Some(id))
            .filter_map(|e| {
                let eid = e["id"].as_str()?.to_string();
                Some((eid, e))
            })
            .collect();

    // Load repertoire entries for this choir
    let all_repo: HashMap<String, serde_json::Value> =
        load_typed_rows::<serde_json::Value>("choir.repertoire")
            .into_iter()
            .filter(|r| r["choir"].as_str() == Some(choir))
            .filter_map(|r| {
                let rid = r["id"].as_str()?.to_string();
                Some((rid, r))
            })
            .collect();

    // Build sorted repertoire options for "Add Song" dropdown
    let all_repo_options: Vec<(String, String)> = {
        let mut options: Vec<(String, String)> = all_repo.iter()
            .filter_map(|(rid, r)| {
                let sid = r["song"].as_str()?;
                let stitle = load_typed_item::<serde_json::Value>("song.items", sid)
                    .ok().flatten()
                    .and_then(|s| s["title"].as_str().map(String::from))
                    .unwrap_or_else(|| sid.to_string());
                Some((rid.clone(), stitle))
            })
            .collect();
        options.sort_by(|a, b| a.1.cmp(&b.1));
        options
    };

    let display_songs: Vec<DisplaySongbookSong> = item_song_ids
        .iter()
        .filter_map(|entry_id_val| {
            let entry_id = entry_id_val.as_str()?;
            let entry = entry_map.get(entry_id)?;
            let repo_id = entry["song"].as_str().unwrap_or("");
            let transpose: i32 = entry["transpose"].as_str().unwrap_or("0").parse().unwrap_or(0);
            let format = entry["format"].as_str().unwrap_or("any");

            if repo_id.is_empty() {
                return Some(DisplaySongbookSong {
                    chord_id: String::new(),
                    transpose,
                    format_name: format.to_string(),
                    display_title: format.to_uppercase(),
                    chord_data: String::new(),
                    target_key: String::new(),
                    transpose_options: Vec::new(),
                });
            }

            let repo_entry = all_repo.get(repo_id)?;
            let chord_id = repo_entry["song"].as_str().unwrap_or("").to_string();
            let title = load_typed_item::<serde_json::Value>("song.items", &chord_id)
                .ok().flatten()
                .and_then(|s| s["title"].as_str().map(String::from))
                .unwrap_or_else(|| chord_id.clone());

            let mut flags = 0x04; // TRANSP_HTML
            if use_bemol { flags |= 0x08; }
            if use_latin { flags |= 0x80; }
            let chord_data = crate::song::get_song_transpose(&chord_id, transpose, flags)
                .unwrap_or_default();

            let actual_original_key = crate::song::get_song_original_key(&chord_id);
            let target_idx = ((actual_original_key + transpose) % 12 + 12) % 12;
            let target_key = key_names(use_bemol, use_latin)[target_idx as usize].to_string();
            let transpose_options = key_transpose_options(actual_original_key, use_bemol, use_latin);
            let display_title = display_or_id(&title, &chord_id).to_string();

            Some(DisplaySongbookSong {
                chord_id,
                transpose,
                format_name: format.to_string(),
                display_title,
                chord_data,
                target_key,
                transpose_options,
            })
        })
        .collect();

    html_response_with_module(
        &page_title,
        layout(
            current_user(ctx),
            &page_title,
            &path,
            "📖",
            Some(rsx! {
                { viewer_controls("songbook", viewer_zoom, save_url) }
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
                                        div { class: "flex justify-between items-center p-4 bg-surface rounded",
                                            h3 { class: "m-0", "{song.format_name.to_uppercase()}" }
                                            if is_owner {
                                                div { class: "flex gap-2 items-center",
                                                    form { method: "POST", action: "/songbook/{id}/randomize", enctype: "multipart/form-data",
                                                        input { r#type: "hidden", name: "csrf_token", value: "{ctx.csrf_token}" }
                                                        input { r#type: "hidden", name: "n", value: "{index}" }
                                                        button { r#type: "submit", class: "btn py-1 px-2", "🎲" }
                                                    }
                                                    form { method: "POST", action: "/api/songbook/{id}/song/{index}/remove",
                                                        input { r#type: "hidden", name: "csrf_token", value: "{ctx.csrf_token}" }
                                                        button { r#type: "submit", class: "btn py-1 px-2 text-danger", "Remove" }
                                                    }
                                                }
                                            }
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
                                                div { class: "flex gap-2 items-center flex-wrap",
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
                                                    form { method: "POST", action: "/api/songbook/{id}/song/{index}/remove",
                                                        input { r#type: "hidden", name: "csrf_token", value: "{ctx.csrf_token}" }
                                                        button { r#type: "submit", class: "btn py-1 px-2 text-danger", "Remove" }
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
                    if is_owner {
                        div { class: "mt-4 p-4 bg-surface rounded",
                            h4 { "Add Song" }
                            form { method: "POST", action: "/api/songbook/{id}/songs", enctype: "application/x-www-form-urlencoded",
                                input { r#type: "hidden", name: "csrf_token", value: "{ctx.csrf_token}" }
                                div { class: "flex gap-2 items-center",
                                    select { name: "song_id", class: "flex-1 p-1",
                                        for (repo_id, stitle) in all_repo_options.iter() {
                                            option { value: "{repo_id}", "{stitle}" }
                                        }
                                    }
                                    button { r#type: "submit", class: "btn btn-primary", "Add Song" }
                                }
                            }
                        }
                    }
                }
            },
        ),
        "songbook",
    )
}

pub fn render_edit(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
    let item: serde_json::Value = match load_typed_item("songbook.items", id) {
        Ok(Some(p)) => p,
        Ok(None) => return html_response_with_status(404, "404", rsx! { "Songbook not found" }),
        Err(_) => return html_response("500", rsx! { "Parse error" }),
    };

    let owner = item["owner"].as_str().unwrap_or("");
    let is_owner = current_user(ctx) == Some(owner);
    if !is_owner {
        return html_response_with_status(403, "403", rsx! { "Forbidden" });
    }

    let title = item["title"].as_str().unwrap_or("");
    let path = edit_path("songbook", id);
    let heading = format!("Edit {title}");

    edit_form_page(
        current_user(ctx), &heading, &path,
        "📖",
        rsx! {
            form { method: "POST", action: "{path}", enctype: "multipart/form-data",
                input { r#type: "hidden", name: "csrf_token", value: "{ctx.csrf_token}" }
                label { "Title",
                    input { name: "title", value: "{title}", class: "w-full" }
                }
                { form_actions(&item_path("songbook", id), "Save Changes", None) }
            }
        },
    )
}
