use dioxus::prelude::*;
use indexmap::IndexMap;
use ndc_dioxus_shared::{
    RequestContext, ResponsePayload, body_str, current_user, display_or_id,
    html_response, html_response_with_status, item_menu, item_path, key_names,
    key_transpose_options, layout, parse_dataset_items, render_hyle_edit,
    render_hyle_list, split_path,
};

use super::{load_dataset_json, load_dataset_json_with_include, load_dataset_item_json, song_get_original_key};

pub fn route(ctx: &RequestContext<'_>) -> Option<ResponsePayload> {
    let parts = split_path(ctx.path);
    match (ctx.method, parts.as_slice()) {
        ("GET", ["choir", "add"]) => Some(render_hyle_edit(
            ctx,
            "choir",
            Some("🎶"),
            "",
            IndexMap::new(),
            &["title", "format"],
            "multipart/form-data",
        )),
        ("POST", ["choir"]) => {
            let items = parse_dataset_items(body_str(ctx.body), &["title", "format"]);
            Some(render_hyle_list(ctx, "choir", Some("🎶"), items, &["title", "format"], 10))
        }
        ("GET", ["choir", id, "edit"]) => Some(render_edit(ctx, id)),
        ("POST", ["choir", id, "edit"]) => Some(render_edit(ctx, id)),
        (_, ["choir", id]) if *id != "add" && *id != "edit" => Some(render_choir_detail(ctx, id)),
        _ => ndc_dioxus_shared::default_crud_routes(
            ctx, "choir", Some("🎶"),
            None::<ndc_dioxus_shared::CrudHandler>,
            None::<ndc_dioxus_shared::CrudHandler>,
        ),
    }
}

pub fn render_choir_detail(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
    let choir_json = load_dataset_item_json(ctx.fd, "choir.items", id);
    let songbook_json = load_dataset_json_with_include(ctx.fd, "songbook.items", None);

    let mut title = id.to_string();
    let mut owner = String::new();
    let mut formats_raw = String::new();

    if let Some(ref json) = choir_json {
        if let Ok(data) = serde_json::from_str::<serde_json::Value>(&json) {
            if let Some(t) = data.get("title").and_then(|v| v.as_str()) {
                if !t.is_empty() {
                    title = t.to_string();
                }
            }
            if let Some(o) = data.get("owner").and_then(|v| v.as_str()) {
                owner = o.to_string();
            }
            if let Some(f) = data.get("format").and_then(|v| v.as_str()) {
                formats_raw = f.to_string();
            }
        }
    } else {
        return html_response_with_status(404, "404", rsx! { "Choir not found" });
    }

    let formats: Vec<&str> = formats_raw.lines().filter(|l| !l.trim().is_empty()).collect();

    let mut display_songbooks: Vec<(String, String)> = Vec::new();

    if let Some(json) = songbook_json {
        if let Ok(data) = serde_json::from_str::<serde_json::Value>(&json) {
            // hyle::Source is Map<String, ModelResult>
            // ModelResult has a 'result' field which is Vec<Row> for Many
            if let Some(rows) = data.get("songbook")
                .and_then(|v| v.get("result"))
                .and_then(|v| v.as_array()) {
                for row in rows {
                    if let (Some(choir), Some(sb_id), Some(sb_title)) = (
                        row.get("choir").and_then(|v| v.as_str()),
                        row.get("id").and_then(|v| v.as_str()),
                        row.get("title").and_then(|v| v.as_str()),
                    ) {
                        if choir == id {
                            let label = display_or_id(sb_title, sb_id).to_string();
                            display_songbooks.push((sb_id.to_string(), label));
                        }
                    }
                }
            } else {
                eprintln!("render_choir_detail: 'songbook.result' NOT FOUND in JSON: {}", json);
            }
        }
    }

    let mut display_repertoire: Vec<RepertoireDisplayItem> = Vec::new();
    if let Some(json) = choir_json {
        if let Ok(data) = serde_json::from_str::<serde_json::Value>(&json) {
            if let Some(songs_raw) = data.get("songs").and_then(|v| v.as_str()) {
                for line in songs_raw.lines() {
                    let parts: Vec<&str> = line.split(':').collect();
                    if parts.len() >= 1 {
                        let s_id = parts[0];
                        if !s_id.is_empty() {
                            let preferred_key = parts.get(1).and_then(|v| v.parse().ok()).unwrap_or(0);
                            let format = parts.get(2).unwrap_or(&"any").to_string();

                            let song_json = load_dataset_item_json(ctx.fd, "song.items", s_id);
                            let mut s_title = s_id.to_string();
                            if let Some(sj) = song_json {
                                if let Ok(sd) = serde_json::from_str::<serde_json::Value>(&sj) {
                                    if let Some(t) = sd.get("title").and_then(|v| v.as_str()) {
                                        s_title = t.to_string();
                                    }
                                }
                            }

                            let original_key = unsafe {
                                let c_id = std::ffi::CString::new(s_id).unwrap();
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
                                original_key,
                                target_key_name,
                                transpose_options: key_transpose_options(original_key, false, false),
                            });
                        }
                    }
                }
            }
        }
    }

    let is_owner = current_user(ctx).map(|u| u == owner).unwrap_or(false);

    let all_songs_json = if is_owner {
        load_dataset_json(ctx.fd, "song.items")
    } else {
        None
    };

    let all_songs: Vec<(String, String)> = all_songs_json
        .and_then(|j| serde_json::from_str::<serde_json::Value>(&j).ok())
        .and_then(|v| v.get("rows").and_then(|r| r.as_array()).cloned())
        .map(|rows| {
            rows.iter()
                .filter_map(|r| {
                    let sid = r.get("id").and_then(|s| s.as_str())?;
                    let stitle = r.get("title").and_then(|s| s.as_str()).unwrap_or(sid);
                    Some((sid.to_string(), format!("{} [{}]", stitle, sid)))
                })
                .collect()
        })
        .unwrap_or_default();

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
            current_user(ctx),
            &title,
            &item_path("choir", id),
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
    let mut fields = IndexMap::new();
    let mut owner = String::new();
    if let Some(json) = load_dataset_item_json(ctx.fd, "choir.items", id) {
        if let Ok(data) = serde_json::from_str::<serde_json::Value>(&json) {
            if let Some(title) = data.get("title").and_then(|v| v.as_str()) {
                fields.insert("title".to_string(), title.to_string());
            }
            if let Some(format) = data.get("format").and_then(|v| v.as_str()) {
                fields.insert("format".to_string(), format.to_string());
            }
            if let Some(o) = data.get("owner").and_then(|v| v.as_str()) {
                owner = o.to_string();
            }
        }
    }

    let is_owner = current_user(ctx) == Some(&owner);
    if !is_owner {
        return html_response_with_status(403, "403", rsx! { "Forbidden" });
    }

    render_hyle_edit(
        ctx,
        "choir",
        Some("🎶"),
        id,
        fields,
        &["title", "format"],
        "multipart/form-data",
    )
}

struct RepertoireDisplayItem {
    id: String,
    title: String,
    format: String,
    preferred_key: i32,
    original_key: i32,
    target_key_name: String,
    transpose_options: Vec<(i32, String)>,
}