use dioxus::prelude::*;
use indexmap::IndexMap;
use ndc_dioxus_shared::{
    RequestContext, ResponsePayload, body_str, current_user, display_or_id,
    html_response, html_response_with_status, item_menu, item_path, layout,
    parse_dataset_items, render_hyle_edit, render_hyle_list, split_path,
};

use crate::{load_dataset_json_with_include, load_dataset_item_json};

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
            Vec::new(),
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
        }
    } else {
        return html_response_with_status(404, "404", rsx!{ "Choir not found" });
    }

    let mut display_songbooks: Vec<(String, String)> = Vec::new();

    if let Some(json) = songbook_json {
        if let Ok(data) = serde_json::from_str::<serde_json::Value>(&json) {
            if let Some(rows) = data.get("rows").and_then(|r| r.as_array()) {
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
            }
        }
    }

    let mut display_repertoire: Vec<(String, String)> = Vec::new();
    if let Some(json) = choir_json {
        if let Ok(data) = serde_json::from_str::<serde_json::Value>(&json) {
            if let Some(songs_raw) = data.get("songs").and_then(|v| v.as_str()) {
                for line in songs_raw.lines() {
                    let parts: Vec<&str> = line.split(':').collect();
                    if parts.len() >= 1 {
                        let s_id = parts[0];
                        if !s_id.is_empty() {
                            let song_json = load_dataset_item_json(ctx.fd, "song.items", s_id);
                            let mut s_title = s_id.to_string();
                            if let Some(sj) = song_json {
                                if let Ok(sd) = serde_json::from_str::<serde_json::Value>(&sj) {
                                    if let Some(t) = sd.get("title").and_then(|v| v.as_str()) {
                                        s_title = t.to_string();
                                    }
                                }
                            }
                            display_repertoire.push((s_id.to_string(), s_title));
                        }
                    }
                }
            }
        }
    }

    let is_owner = current_user(ctx).map(|u| u == owner).unwrap_or(false);

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
                    if display_repertoire.is_empty() {
                        p { class: "text-muted", "No songs in repertoire yet." }
                    } else {
                        div { class: "center",
                            for (s_id, s_title) in display_repertoire {
                                a { href: "/choir/{id}/song/{s_id}", class: "btn", "{s_title}" }
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
    if let Some(json) = load_dataset_item_json(ctx.fd, "choir.items", id) {
        if let Ok(data) = serde_json::from_str::<serde_json::Value>(&json) {
            if let Some(title) = data.get("title").and_then(|v| v.as_str()) {
                fields.insert("title".to_string(), title.to_string());
            }
            if let Some(format) = data.get("format").and_then(|v| v.as_str()) {
                fields.insert("format".to_string(), format.to_string());
            }
        }
    }
    render_hyle_edit(
        ctx,
        "choir",
        Some("🎶"),
        id,
        fields,
        &["title", "format"],
        "multipart/form-data",
        Vec::new(),
    )
}