use dioxus::prelude::*;
use indexmap::IndexMap;
use ndc_dioxus_shared::{
    RequestContext, ResponsePayload, current_user, display_or_id,
    html_response, html_response_with_status, item_path, key_names,
    key_transpose_options, split_path,
};
use crate::site_ui::{item_menu, layout};
use crate::hyle_ssr::render_hyle_edit;
use crate::hyle_ssr::render_hyle_list_queried;
use serde::Deserialize;

use std::ffi::CString;
use hyle::{load_source, load_typed_item, load_typed_rows, find_item, Value};
use crate::song::song_get_original_key;

#[derive(Debug, Deserialize)]
struct ChoirItem {
    #[serde(default)]
    title: String,
    #[serde(default)]
    format: String,
    #[serde(default)]
    owner: String,
}

#[derive(Debug, Deserialize)]
struct RepertoireItem {
    #[serde(default)]
    song: String,
    #[serde(default)]
    transpose: String,
    #[serde(default)]
    format: String,
}

#[derive(Debug, Deserialize)]
struct SongItem {
    id: String,
    #[serde(default)]
    title: String,
}

pub fn route(ctx: &RequestContext<'_>) -> Option<ResponsePayload> {
    let parts = split_path(ctx.path);
    match (ctx.method, parts.as_slice()) {
        ("GET", ["choir", "add"]) => Some(render_hyle_edit(
            ctx, "choir", "🎶", "", IndexMap::new(), &["title", "format"],
            "multipart/form-data",
        )),
        ("GET", ["choir"]) | ("POST", ["choir"]) => {
            let source = crate::source_query::query_source("choir.items", ctx.query, Some(10))?;
            Some(render_hyle_list_queried(ctx, "choir", "🎶", source, &["title", "format"], 10))
        }
        ("GET", ["choir", id, "edit"]) => Some(render_edit(ctx, id)),
        ("POST", ["choir", id, "edit"]) => Some(render_edit(ctx, id)),
        (_, ["choir", id]) if *id != "add" && *id != "edit" => Some(render_choir_detail(ctx, id)),
        _ => crate::site_ui::default_crud_routes(ctx, "choir", "🎶"),
    }
}

pub fn render_choir_detail(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
    let choir: ChoirItem = match load_typed_item("choir.items", id) {
        Ok(Some(c)) => c,
        Ok(None) => return html_response_with_status(404, "404", rsx! { "Choir not found" }),
        Err(e) => return html_response("500", rsx! { "Parse error: {e}" }),
    };

    let title = if choir.title.is_empty() { id.to_string() } else { choir.title.clone() };
    let owner = &choir.owner;
    let formats: Vec<&str> = choir.format.lines().filter(|l| !l.trim().is_empty()).collect();

    let songbook_source = load_source("songbook.items");
    let mut display_songbooks: Vec<(String, String)> = Vec::new();

    if let Some(source) = songbook_source {
        if let Some(result) = source.get("songbook") {
            for row in result.rows() {
                let choir_ref = match row.get("choir") {
                    Some(Value::String(s)) => s.as_str(),
                    _ => continue,
                };
                let sb_id = match row.get("id") {
                    Some(Value::String(s)) => s.as_str(),
                    _ => continue,
                };
                let sb_title = match row.get("title") {
                    Some(Value::String(s)) => s.as_str(),
                    _ => continue,
                };
                if choir_ref == id {
                    let label = display_or_id(sb_title, sb_id).to_string();
                    display_songbooks.push((sb_id.to_string(), label));
                }
            }
        }
    }

    let mut display_repertoire: Vec<RepertoireDisplayItem> = Vec::new();
    let repertoire_entries: Vec<String> = if let Some(source) = load_source("choir.items") {
        if let Some(row) = find_item(&source, "choir", id) {
            if let Some(Value::Array(arr)) = row.get("repertoire") {
                arr.iter().filter_map(|v| {
                    if let Value::String(s) = v { Some(s.clone()) } else { None }
                }).collect()
            } else {
                Vec::new()
            }
        } else {
            Vec::new()
        }
    } else {
        Vec::new()
    };
    for entry_id in &repertoire_entries {
        let entry: RepertoireItem = match load_typed_item("choir.repertoire", entry_id) {
            Ok(Some(e)) => e,
            _ => continue,
        };
        let s_id = &entry.song;
        let preferred_key = entry.transpose.parse().unwrap_or(0);
        let format = if entry.format.is_empty() { "any".to_string() } else { entry.format.clone() };

        let s_title = match load_typed_item::<SongItem>("song.items", s_id) {
            Ok(Some(s)) => s.title,
            _ => s_id.to_string(),
        };

        let original_key = unsafe {
            let c_id = CString::new(s_id.as_str()).unwrap();
            song_get_original_key(c_id.as_ptr())
        };

        let target_key_name = {
            let target_idx = ((original_key + preferred_key) % 12 + 12) % 12;
            key_names(false, false)[target_idx as usize].to_string()
        };

        display_repertoire.push(RepertoireDisplayItem {
            id: s_id.to_string(),
            title: s_title,
            format,
            preferred_key,
            target_key_name,
            transpose_options: key_transpose_options(original_key, false, false),
        });
    }

    let is_owner = current_user(ctx).map(|u| u == owner).unwrap_or(false);

    let all_songs: Vec<(String, String)> = if is_owner {
        load_typed_rows::<SongItem>("song.items")
            .into_iter()
            .map(|s| (s.id.clone(), format!("{} [{}]", s.title, s.id)))
            .collect()
    } else {
        Vec::new()
    };

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
        layout(
            current_user(ctx), &title, &item_path("choir", id),
            "🎶",
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

                    if is_owner {
                        div { class: "bg-surface p-4 rounded mb-4 w-full max-w-2xl",
                            h4 { class: "mt-0 mb-2", "Add Song" }
                            form {
                                method: "POST",
                                action: "/api/choir/{id}/songs",
                                class: "flex flex-col gap-2",
                                input { r#type: "hidden", name: "csrf_token", value: "{ctx.csrf_token}" }
                                div { class: "flex gap-2",
                                    label { class: "flex-1",
                                        "Song:"
                                        input {
                                            list: "all-songs",
                                            name: "song_id",
                                            class: "w-full",
                                            placeholder: "Search song..."
                                        }
                                        datalist { id: "all-songs",
                                            for (_sid, label) in all_songs {
                                                option { value: "{label}" }
                                            }
                                        }
                                    }
                                    label { class: "w-40",
                                        "Format:"
                                        select { name: "format", class: "w-full",
                                            for fmt in formats {
                                                option { value: "{fmt}", "{fmt}" }
                                            }
                                        }
                                    }
                                }
                                button { r#type: "submit", class: "btn btn-primary self-end", "Add to Repertoire" }
                            }
                        }
                    }

                    if display_repertoire.is_empty() {
                        p { class: "text-muted", "No songs in repertoire yet." }
                    } else {
                        div { class: "flex flex-col gap-2 w-full max-w-2xl",
                            for item in display_repertoire {
                                div { class: "flex justify-between items-center p-2 bg-surface rounded",
                                    div { class: "flex flex-col",
                                        a { href: "/choir/{id}/song/{item.id}", class: "font-bold", "{item.title}" }
                                        span { class: "text-xs text-muted",
                                            "{item.format} • Key: {item.target_key_name}"
                                        }
                                    }
                                    if is_owner {
                                        div { class: "flex gap-2",
                                            form {
                                                method: "POST",
                                                action: "/api/choir/{id}/song/{item.id}/key",
                                                class: "flex gap-1 items-center",
                                                input { r#type: "hidden", name: "csrf_token", value: "{ctx.csrf_token}" }
                                                select { name: "key", class: "text-xs p-1",
                                                    for (semitones, label) in item.transpose_options {
                                                        option {
                                                            value: "{semitones}",
                                                            selected: semitones == item.preferred_key,
                                                            "{label}"
                                                        }
                                                    }
                                                }
                                                button { r#type: "submit", class: "btn text-xs py-1 px-2", "Set" }
                                            }
                                            form {
                                                method: "POST",
                                                action: "/api/choir/{id}/song/{item.id}/remove",
                                                input { r#type: "hidden", name: "csrf_token", value: "{ctx.csrf_token}" }
                                                button { r#type: "submit", class: "btn btn-danger text-xs py-1 px-2", "Remove" }
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
    let choir: ChoirItem = match load_typed_item("choir.items", id) {
        Ok(Some(c)) => c,
        Ok(None) => return html_response_with_status(404, "404", rsx! { "Choir not found" }),
        Err(e) => return html_response("500", rsx! { "Parse error: {e}" }),
    };

    let is_owner = current_user(ctx).map(|u| u == choir.owner).unwrap_or(false);
    if !is_owner {
        return html_response_with_status(403, "403", rsx! { "Forbidden" });
    }

    let fields = IndexMap::from([
        ("title".to_string(), choir.title),
        ("format".to_string(), choir.format),
    ]);

    render_hyle_edit(ctx, "choir", "🎶", id, fields,
        &["title", "format"],
        "multipart/form-data",
    )
}

struct RepertoireDisplayItem {
    id: String,
    title: String,
    format: String,
    preferred_key: i32,
    target_key_name: String,
    transpose_options: Vec<(i32, String)>,
}