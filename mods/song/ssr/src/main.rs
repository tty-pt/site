use std::collections::HashMap;
use std::ffi::CStr;
use std::ffi::CString;
use std::os::raw::c_char;
use std::os::raw::c_int;
use std::os::raw::c_void;
use ndx::ndx_hook_decl;
use crate::NDX;
use indexmap::IndexMap;
use dioxus::prelude::*;
use serde::Deserialize;

use ndc_dioxus_shared::{
    current_user, display_or_id, html_response, html_response_with_status, item_path,
    parse_pairs, split_path, RequestContext, ResponsePayload,
};
use crate::hyle_ssr::render_hyle_list_queried;
use hyle::{load_source, load_typed_item};

// ── NDX hook declarations (outbound calls routed through NDX.call) ───

#[ndx_hook_decl]
pub fn song_get_pref(user: *const c_char, name: *const c_char) -> *mut c_char {}

#[ndx_hook_decl]
pub fn song_get_original_key(id: *const c_char) -> c_int {}

#[ndx_hook_decl]
pub fn song_transpose(id: *const c_char, semi: c_int, fl: c_int, out: *mut *mut c_char) -> c_int {}

// Rust wrapper functions for ergonomic Rust callers
pub fn get_song_pref(user: &str, name: &str) -> Option<String> {
    let user_c = CString::new(user).ok()?;
    let name_c = CString::new(name).ok()?;
    let raw = unsafe { song_get_pref(user_c.as_ptr(), name_c.as_ptr()) };
    if raw.is_null() {
        None
    } else {
        let owned = unsafe { CStr::from_ptr(raw) }
            .to_string_lossy()
            .into_owned();
        unsafe { free(raw.cast()) };
        Some(owned)
    }
}

pub fn get_song_original_key(id: &str) -> i32 {
    let id_c = CString::new(id).unwrap();
    unsafe { song_get_original_key(id_c.as_ptr()) }
}

pub fn get_song_transpose(id: &str, semi: i32, flags: i32) -> Option<String> {
    let id_c = CString::new(id).ok()?;
    let mut raw: *mut c_char = std::ptr::null_mut();
    let rc = unsafe { song_transpose(id_c.as_ptr(), semi, flags, &mut raw) };
    if rc == 0 && !raw.is_null() {
        let owned = unsafe { CStr::from_ptr(raw) }
            .to_string_lossy()
            .into_owned();
        unsafe { free(raw.cast()) };
        Some(owned)
    } else {
        if !raw.is_null() {
            unsafe { free(raw.cast()) };
        }
        None
    }
}

unsafe extern "C" {
    fn free(ptr: *mut c_void);
}

pub fn route(ctx: &RequestContext<'_>) -> Option<ResponsePayload> {
    let parts = split_path(ctx.path);
    match (ctx.method, parts.as_slice()) {
        ("GET", ["song"]) | ("POST", ["song"]) => {
            let mut source = crate::source_query::query_source("song.items", ctx.query, Some(10))?;
            if let Some(types) = crate::source_query::query_source("song.types", "", None) {
                source.extend(types);
            }
            Some(render_hyle_list_queried(
                ctx, "song", "🎸", source,
                &["title", "type", "author"],
                10,
            ))
        }
        ("GET", ["song", id]) if *id != "add" => Some(render_song_detail(ctx, id)),
        ("GET", ["song", id, "edit"]) => Some(render_song_edit(ctx, id)),
        _ => crate::site_ui::default_crud_routes(ctx, "song", "🎸"),
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
            if let Some(b) = get_song_pref(user, "chords-bemol") {
                use_bemol = b.trim() == "1";
            }
            if let Some(l) = get_song_pref(user, "chords-latin") {
                use_latin = l.trim() == "1";
            }
        }
    }

    (transpose, use_bemol, use_latin, show_media)
}

fn render_song_detail(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
	let item: SongRow = match load_typed_item("song.items", id) {
		Ok(Some(p)) => p,
		Ok(None) => return html_response_with_status(404, "404", rsx! { "Song not found" }),
		Err(e) => return html_response("500", rsx! { "Parse error: {e}" }),
	};

	let title = item.title.as_str();
	let author = item.author.as_str();
	let song_type = song_type_display(&item.song_type);
	let is_owner = current_user(ctx).map(|u| u == item.owner).unwrap_or(false);

	let display_title = display_or_id(title, id).to_string();
	let path = item_path("song", id);
	let (transpose, use_bemol, use_latin, show_media) = song_flags(ctx.query, current_user(ctx));

	let original_key = get_song_original_key(id);
	let mut flags = 0x04;
	if use_bemol { flags |= 0x08; }
	if use_latin { flags |= 0x80; }
	let chord_data = get_song_transpose(id, transpose, flags).unwrap_or_else(|| item.data.clone());

	let save_url = if current_user(ctx).is_some() {
		"/api/song/prefs".to_string()
	} else {
		String::new()
	};
	let author_display = if author.is_empty() {
		"N/A".to_string()
	} else {
		author.to_string()
	};

	let detail_body_html = if !song_type.is_empty() || !author_display.is_empty() {
		format!(
			r#"<div class="flex justify-between items-start w-full max-w-xl text-xs text-muted"><div class="italic whitespace-pre-wrap">{}</div><div class="text-right">{}</div></div>"#,
			ndc_dioxus_shared::escape_html(&song_type),
			ndc_dioxus_shared::escape_html(&author_display),
		)
	} else {
		String::new()
	};

	let props = ndc_song_shared::SongDetailPageProps {
		page_user: current_user(ctx).map(|u| u.to_string()),
		display_title: display_title.clone(),
		path,
		is_owner,
		state: ndc_song_shared::SongState {
			song_id: id.to_string(),
			transpose,
			use_bemol,
			use_latin,
			show_media,
			yt: item.yt.clone(),
			audio: item.audio.clone(),
			pdf: item.pdf.clone(),
			chord_html: chord_data,
			original_key,
			save_url: save_url.clone(),
		},
		detail_body_html,
	};

	ndc_dioxus_shared::html_response_with_component(
		&display_title,
		"",
		"song",
		ndc_song_shared::SongDetailPage,
		props,
	)
}

fn render_song_edit(ctx: &RequestContext<'_>, id: &str) -> ResponsePayload {
    let item: SongRow = match load_typed_item("song.items", id) {
        Ok(Some(p)) => p,
        Ok(None) => return html_response_with_status(404, "404", rsx! { "Song not found" }),
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

    crate::hyle_ssr::render_hyle_edit(
        ctx, "song", "🎸", id,
        initial,
        &["title", "type", "author", "yt", "audio", "pdf", "data"],
        "multipart/form-data",
    )
}

fn type_display_map() -> HashMap<String, String> {
	let mut map = HashMap::new();
	if let Some(source) = load_source("song.types") {
		if let Some(types) = source.get("song.types") {
			for row in types.rows() {
				let id = row.get("id").and_then(|v| {
					if let hyle::Value::String(s) = v { Some(s.clone()) } else { None }
				});
				let name = row.get("name").and_then(|v| {
					if let hyle::Value::String(s) = v { Some(s.clone()) } else { None }
				});
				if let (Some(id), Some(name)) = (id, name) {
					map.insert(id, name);
				}
			}
		}
	}
	map
}

fn song_type_display(types: &[String]) -> String {
	let map = type_display_map();
	types.iter()
		.map(|t| map.get(t).map(|s| s.as_str()).unwrap_or(t))
		.collect::<Vec<_>>()
		.join(", ")
}

#[derive(Debug, Deserialize)]
struct SongRow {
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
