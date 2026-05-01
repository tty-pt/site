use std::ffi::{CString, c_char, c_uchar};
use std::panic::{AssertUnwindSafe, catch_unwind};

mod shared;

include!(concat!(env!("OUT_DIR"), "/generated_routes.rs"));

pub use shared::*;

#[repr(C)]
pub struct ModuleEntryFfi {
    pub id:    *const c_char,
    pub title: *const c_char,
    pub flags: u32,
}

// ── Leaf payload types (used by C callers, kept for cbindgen) ────────────────

/// C callers (song.c) still build this and pass it to the ssr_render_song_detail NDX hook.
#[repr(C)]
pub struct SongItemFfi {
    pub title:        *const c_char,
    pub data:         *const c_char,
    pub yt:           *const c_char,
    pub audio:        *const c_char,
    pub pdf:          *const c_char,
    pub categories:   *const c_char,
    pub author:       *const c_char,
    pub original_key: i32,
    pub viewer_zoom:  i32,
    pub show_media:   bool,
    pub viewer_bemol: bool,
    pub viewer_latin: bool,
    pub owner:        bool,
}

/// C callers (poem.c) still build this and pass it to poem NDX hooks.
#[repr(C)]
pub struct PoemItemFfi {
    pub title:        *const c_char,
    pub head_content: *const c_char,
    pub body_content: *const c_char,
    pub owner:        bool,
}

/// C callers (various modules) pass this to ssr_render_delete NDX hook.
#[repr(C)]
pub struct DeleteItemFfi {
    pub title: *const c_char,
}

/// C callers (songbook.c) still build this and pass it to ssr_render_songbook_detail.
#[repr(C)]
pub struct SongbookItemFfi {
    pub title:      *const c_char,
    pub owner:      *const c_char,
    pub choir:      *const c_char,
    pub viewer_zoom: i32,
    pub songs:      *const SongbookSongFfi,
    pub songs_len:  usize,
}

/// C callers (choir.c) still build this and pass it to ssr_render_choir_detail.
#[repr(C)]
pub struct ChoirItemFfi {
    pub title:         *const c_char,
    pub owner_name:    *const c_char,
    pub counter:       *const c_char,
    pub formats:       *const c_char,
    pub songs:         *const ChoirSongFfi,
    pub songs_len:     usize,
    pub all_songs:     *const ChoirEntryFfi,
    pub all_songs_len: usize,
    pub songbooks:     *const ChoirEntryFfi,
    pub songbooks_len: usize,
}

#[repr(C)]
pub struct SongbookSongFfi {
    pub chord_id:    *const c_char,
    pub format:      *const c_char,
    pub chord_title: *const c_char,
    pub chord_data:  *const c_char,
    pub transpose:   i32,
    pub original_key: i32,
}

#[repr(C)]
pub struct ChoirSongFfi {
    pub id:            *const c_char,
    pub title:         *const c_char,
    pub format:        *const c_char,
    pub preferred_key: i32,
    pub original_key:  i32,
}

#[repr(C)]
pub struct ChoirEntryFfi {
    pub id:    *const c_char,
    pub title: *const c_char,
}

// ── Combined render structs ───────────────────────────────────────────────────

/// Generic page render (home, login, register, error pages).
/// Replaces RenderRequest + ssr_render_ffi.
#[repr(C)]
pub struct PageRenderFfi {
    pub method:      *const c_char,
    pub path:        *const c_char,
    pub query:       *const c_char,
    pub body:        *const c_uchar,
    pub body_len:    usize,
    pub remote_user: *const c_char,
    pub modules:     *const ModuleEntryFfi,
    pub modules_len: usize,
}

#[repr(C)]
pub struct SongDetailRenderFfi {
    // payload
    pub title:        *const c_char,
    pub data:         *const c_char,
    pub yt:           *const c_char,
    pub audio:        *const c_char,
    pub pdf:          *const c_char,
    pub categories:   *const c_char,
    pub author:       *const c_char,
    pub original_key: i32,
    pub viewer_zoom:  i32,
    pub show_media:   bool,
    pub viewer_bemol: bool,
    pub viewer_latin: bool,
    pub owner:        bool,
    // context
    pub id:          *const c_char,
    pub query:       *const c_char,
    pub remote_user: *const c_char,
    pub modules:     *const ModuleEntryFfi,
    pub modules_len: usize,
}

/// Reused for both poem detail and poem edit.
#[repr(C)]
pub struct PoemRenderFfi {
    // payload
    pub title:        *const c_char,
    pub head_content: *const c_char,
    pub body_content: *const c_char,
    pub owner:        bool,
    // context
    pub id:          *const c_char,
    pub query:       *const c_char,
    pub remote_user: *const c_char,
    pub modules:     *const ModuleEntryFfi,
    pub modules_len: usize,
}

#[repr(C)]
pub struct DeleteRenderFfi {
    // payload
    pub module: *const c_char,
    pub title:  *const c_char,
    // context
    pub id:          *const c_char,
    pub query:       *const c_char,
    pub remote_user: *const c_char,
    pub modules:     *const ModuleEntryFfi,
    pub modules_len: usize,
}

#[repr(C)]
pub struct SongbookDetailRenderFfi {
    // payload
    pub sb_title:    *const c_char,
    pub owner:       *const c_char,
    pub choir:       *const c_char,
    pub viewer_zoom: i32,
    pub songs:       *const SongbookSongFfi,
    pub songs_len:   usize,
    // context
    pub id:          *const c_char,
    pub query:       *const c_char,
    pub remote_user: *const c_char,
    pub modules:     *const ModuleEntryFfi,
    pub modules_len: usize,
}

#[repr(C)]
pub struct ChoirDetailRenderFfi {
    // payload
    pub title:         *const c_char,
    pub owner_name:    *const c_char,
    pub counter:       *const c_char,
    pub formats:       *const c_char,
    pub songs:         *const ChoirSongFfi,
    pub songs_len:     usize,
    pub all_songs:     *const ChoirEntryFfi,
    pub all_songs_len: usize,
    pub songbooks:     *const ChoirEntryFfi,
    pub songbooks_len: usize,
    // context
    pub id:          *const c_char,
    pub query:       *const c_char,
    pub remote_user: *const c_char,
    pub modules:     *const ModuleEntryFfi,
    pub modules_len: usize,
}

// ── Result type ───────────────────────────────────────────────────────────────

#[repr(C)]
pub struct RenderResult {
    status: u16,
    content_type: *mut c_char,
    location: *mut c_char,
    body: *mut c_char,
}

fn safe_cstring(s: String) -> CString {
    let sanitised = s.replace('\0', "");
    CString::new(sanitised).unwrap()
}

fn to_ffi(response: ResponsePayload) -> RenderResult {
    RenderResult {
        status: response.status,
        content_type: safe_cstring(response.content_type).into_raw(),
        location: response
            .location
            .and_then(|value| CString::new(value).ok())
            .map_or(std::ptr::null_mut(), CString::into_raw),
        body: safe_cstring(response.body).into_raw(),
    }
}

fn internal_error() -> RenderResult {
    to_ffi(html_response_with_status(500, "500", error_page(None, "/", 500, "Internal server error")))
}

// ── FFI entry points ──────────────────────────────────────────────────────────

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_page_ffi(req: *const PageRenderFfi) -> RenderResult {
    if req.is_null() {
        eprintln!("ssr_render_page_ffi: null pointer");
        return internal_error();
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        let req = unsafe { &*req };
        let body_slice: &[u8] = if req.body.is_null() || req.body_len == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(req.body, req.body_len) }
        };
        let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
        let modules_slice = unsafe { fill_modules(&mut modules_buf, req.modules, req.modules_len) };
        let remote_user_str = unsafe { cstr_ref(req.remote_user) };
        let ctx = RequestContext {
            method:      unsafe { cstr_ref(req.method) },
            path:        unsafe { cstr_ref(req.path) },
            query:       unsafe { cstr_ref(req.query) },
            body:        body_slice,
            remote_user: if remote_user_str.is_empty() { None } else { Some(remote_user_str) },
            modules:     modules_slice,
        };
        to_ffi(route(&ctx))
    }));
    match result {
        Ok(r) => r,
        Err(e) => {
            eprintln!("ssr_render_page_ffi panic: {}", e.downcast_ref::<&str>().copied().unwrap_or("(unknown)"));
            internal_error()
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_song_detail_ffi(req: *const SongDetailRenderFfi) -> RenderResult {
    if req.is_null() {
        eprintln!("ssr_render_song_detail_ffi: null pointer");
        return internal_error();
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        let r = unsafe { &*req };
        let id_s = unsafe { cstr_ref(r.id) };
        let path_buf = format!("/song/{}", id_s);
        let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
        let modules_slice = unsafe { fill_modules(&mut modules_buf, r.modules, r.modules_len) };
        let remote_user_str = unsafe { cstr_ref(r.remote_user) };
        let ctx = RequestContext {
            method:      "GET",
            path:        &path_buf,
            query:       unsafe { cstr_ref(r.query) },
            body:        &[],
            remote_user: if remote_user_str.is_empty() { None } else { Some(remote_user_str) },
            modules:     modules_slice,
        };
        let view = ndc_dioxus_shared::SongItem {
            title:        unsafe { cstr_ref(r.title) },
            data:         unsafe { cstr_ref(r.data) },
            yt:           unsafe { cstr_ref(r.yt) },
            audio:        unsafe { cstr_ref(r.audio) },
            pdf:          unsafe { cstr_ref(r.pdf) },
            categories:   unsafe { cstr_ref(r.categories) },
            author:       unsafe { cstr_ref(r.author) },
            original_key: r.original_key,
            viewer_zoom:  r.viewer_zoom,
            show_media:   r.show_media,
            viewer_bemol: r.viewer_bemol,
            viewer_latin: r.viewer_latin,
            owner:        r.owner,
        };
        to_ffi(song::render_detail(&view, id_s, &ctx))
    }));
    match result {
        Ok(r) => r,
        Err(e) => {
            eprintln!("ssr_render_song_detail_ffi panic: {}", e.downcast_ref::<&str>().copied().unwrap_or("(unknown)"));
            internal_error()
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_poem_detail_ffi(req: *const PoemRenderFfi) -> RenderResult {
    if req.is_null() {
        eprintln!("ssr_render_poem_detail_ffi: null pointer");
        return internal_error();
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        let r = unsafe { &*req };
        let id_s = unsafe { cstr_ref(r.id) };
        let path_buf = format!("/poem/{}", id_s);
        let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
        let modules_slice = unsafe { fill_modules(&mut modules_buf, r.modules, r.modules_len) };
        let remote_user_str = unsafe { cstr_ref(r.remote_user) };
        let ctx = RequestContext {
            method:      "GET",
            path:        &path_buf,
            query:       unsafe { cstr_ref(r.query) },
            body:        &[],
            remote_user: if remote_user_str.is_empty() { None } else { Some(remote_user_str) },
            modules:     modules_slice,
        };
        let view = ndc_dioxus_shared::PoemItem {
            title:        unsafe { cstr_ref(r.title) },
            head_content: unsafe { cstr_ref(r.head_content) },
            body_content: unsafe { cstr_ref(r.body_content) },
            owner:        r.owner,
        };
        to_ffi(poem::render_detail(&view, id_s, &ctx))
    }));
    match result {
        Ok(r) => r,
        Err(e) => {
            eprintln!("ssr_render_poem_detail_ffi panic: {}", e.downcast_ref::<&str>().copied().unwrap_or("(unknown)"));
            internal_error()
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_poem_edit_ffi(req: *const PoemRenderFfi) -> RenderResult {
    if req.is_null() {
        eprintln!("ssr_render_poem_edit_ffi: null pointer");
        return internal_error();
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        let r = unsafe { &*req };
        let id_s = unsafe { cstr_ref(r.id) };
        let path_buf = format!("/poem/{}", id_s);
        let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
        let modules_slice = unsafe { fill_modules(&mut modules_buf, r.modules, r.modules_len) };
        let remote_user_str = unsafe { cstr_ref(r.remote_user) };
        let ctx = RequestContext {
            method:      "GET",
            path:        &path_buf,
            query:       unsafe { cstr_ref(r.query) },
            body:        &[],
            remote_user: if remote_user_str.is_empty() { None } else { Some(remote_user_str) },
            modules:     modules_slice,
        };
        let view = ndc_dioxus_shared::PoemItem {
            title:        unsafe { cstr_ref(r.title) },
            head_content: unsafe { cstr_ref(r.head_content) },
            body_content: unsafe { cstr_ref(r.body_content) },
            owner:        r.owner,
        };
        to_ffi(poem::render_edit_typed(&view, id_s, &ctx))
    }));
    match result {
        Ok(r) => r,
        Err(e) => {
            eprintln!("ssr_render_poem_edit_ffi panic: {}", e.downcast_ref::<&str>().copied().unwrap_or("(unknown)"));
            internal_error()
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_delete_ffi(req: *const DeleteRenderFfi) -> RenderResult {
    if req.is_null() {
        eprintln!("ssr_render_delete_ffi: null pointer");
        return internal_error();
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        let r = unsafe { &*req };
        let module_s = unsafe { cstr_ref(r.module) };
        let id_s     = unsafe { cstr_ref(r.id) };
        let title_s  = unsafe { cstr_ref(r.title) };
        let path_buf = format!("/{}/{}", module_s, id_s);
        let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
        let modules_slice = unsafe { fill_modules(&mut modules_buf, r.modules, r.modules_len) };
        let remote_user_str = unsafe { cstr_ref(r.remote_user) };
        let ctx = RequestContext {
            method:      "GET",
            path:        &path_buf,
            query:       unsafe { cstr_ref(r.query) },
            body:        &[],
            remote_user: if remote_user_str.is_empty() { None } else { Some(remote_user_str) },
            modules:     modules_slice,
        };
        to_ffi(render_delete_confirm(module_s, id_s, title_s, &ctx))
    }));
    match result {
        Ok(r) => r,
        Err(e) => {
            eprintln!("ssr_render_delete_ffi panic: {}", e.downcast_ref::<&str>().copied().unwrap_or("(unknown)"));
            internal_error()
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_songbook_detail_ffi(req: *const SongbookDetailRenderFfi) -> RenderResult {
    if req.is_null() {
        eprintln!("ssr_render_songbook_detail_ffi: null pointer");
        return internal_error();
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        let r = unsafe { &*req };
        let id_s = unsafe { cstr_ref(r.id) };
        let path_buf = format!("/songbook/{}", id_s);
        let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
        let modules_slice = unsafe { fill_modules(&mut modules_buf, r.modules, r.modules_len) };
        let remote_user_str = unsafe { cstr_ref(r.remote_user) };
        let ctx = RequestContext {
            method:      "GET",
            path:        &path_buf,
            query:       unsafe { cstr_ref(r.query) },
            body:        &[],
            remote_user: if remote_user_str.is_empty() { None } else { Some(remote_user_str) },
            modules:     modules_slice,
        };
        let ffi_songs: &[SongbookSongFfi] = if r.songs.is_null() || r.songs_len == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(r.songs, r.songs_len) }
        };
        let view = ndc_dioxus_shared::SongbookItem {
            title:       unsafe { cstr_ref(r.sb_title) },
            owner:       unsafe { cstr_ref(r.owner) },
            choir:       unsafe { cstr_ref(r.choir) },
            viewer_zoom: r.viewer_zoom,
            songs: ffi_songs.iter().map(|s| ndc_dioxus_shared::SongbookSong {
                chord_id:    unsafe { cstr_ref(s.chord_id) },
                format:      unsafe { cstr_ref(s.format) },
                chord_title: unsafe { cstr_ref(s.chord_title) },
                chord_data:  unsafe { cstr_ref(s.chord_data) },
                transpose:   s.transpose,
                original_key: s.original_key,
            }).collect(),
        };
        to_ffi(songbook::render_detail(&view, id_s, &ctx))
    }));
    match result {
        Ok(r) => r,
        Err(e) => {
            eprintln!("ssr_render_songbook_detail_ffi panic: {}", e.downcast_ref::<&str>().copied().unwrap_or("(unknown)"));
            internal_error()
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_choir_detail_ffi(req: *const ChoirDetailRenderFfi) -> RenderResult {
    if req.is_null() {
        eprintln!("ssr_render_choir_detail_ffi: null pointer");
        return internal_error();
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        let r = unsafe { &*req };
        let id_s = unsafe { cstr_ref(r.id) };
        let path_buf = format!("/choir/{}", id_s);
        let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
        let modules_slice = unsafe { fill_modules(&mut modules_buf, r.modules, r.modules_len) };
        let remote_user_str = unsafe { cstr_ref(r.remote_user) };
        let ctx = RequestContext {
            method:      "GET",
            path:        &path_buf,
            query:       unsafe { cstr_ref(r.query) },
            body:        &[],
            remote_user: if remote_user_str.is_empty() { None } else { Some(remote_user_str) },
            modules:     modules_slice,
        };
        let ffi_songs: &[ChoirSongFfi] = if r.songs.is_null() || r.songs_len == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(r.songs, r.songs_len) }
        };
        let ffi_all_songs: &[ChoirEntryFfi] = if r.all_songs.is_null() || r.all_songs_len == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(r.all_songs, r.all_songs_len) }
        };
        let ffi_songbooks: &[ChoirEntryFfi] = if r.songbooks.is_null() || r.songbooks_len == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(r.songbooks, r.songbooks_len) }
        };
        let view = ndc_dioxus_shared::ChoirItem {
            title:      unsafe { cstr_ref(r.title) },
            owner_name: unsafe { cstr_ref(r.owner_name) },
            counter:    unsafe { cstr_ref(r.counter) },
            formats:    unsafe { cstr_ref(r.formats) },
            songs: ffi_songs.iter().map(|s| ndc_dioxus_shared::ChoirSong {
                id:            unsafe { cstr_ref(s.id) },
                title:         unsafe { cstr_ref(s.title) },
                format:        unsafe { cstr_ref(s.format) },
                preferred_key: s.preferred_key,
                original_key:  s.original_key,
            }).collect(),
            all_songs: ffi_all_songs.iter().map(|e| ndc_dioxus_shared::ChoirEntry {
                id:    unsafe { cstr_ref(e.id) },
                title: unsafe { cstr_ref(e.title) },
            }).collect(),
            songbooks: ffi_songbooks.iter().map(|e| ndc_dioxus_shared::ChoirEntry {
                id:    unsafe { cstr_ref(e.id) },
                title: unsafe { cstr_ref(e.title) },
            }).collect(),
        };
        to_ffi(choir::render_detail(&view, id_s, &ctx))
    }));
    match result {
        Ok(r) => r,
        Err(e) => {
            eprintln!("ssr_render_choir_detail_ffi panic: {}", e.downcast_ref::<&str>().copied().unwrap_or("(unknown)"));
            internal_error()
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_free_result_ffi(result: *mut RenderResult) {
    if result.is_null() {
        return;
    }

    let r = catch_unwind(AssertUnwindSafe(|| {
        let result = unsafe { &mut *result };

        if !result.content_type.is_null() {
            unsafe { drop(CString::from_raw(result.content_type)); }
            result.content_type = std::ptr::null_mut();
        }

        if !result.location.is_null() {
            unsafe { drop(CString::from_raw(result.location)); }
            result.location = std::ptr::null_mut();
        }

        if !result.body.is_null() {
            unsafe { drop(CString::from_raw(result.body)); }
            result.body = std::ptr::null_mut();
        }
    }));

    if let Err(e) = r {
        let msg = e.downcast_ref::<&str>().copied()
            .or_else(|| e.downcast_ref::<String>().map(String::as_str))
            .unwrap_or("(unknown)");
        eprintln!("ssr_free_result_ffi panic: {msg}");
    }
}
