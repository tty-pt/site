use std::ffi::{CString, c_char, c_int, c_uchar, c_void};
use std::panic::{catch_unwind, AssertUnwindSafe};

use ndx::prelude::*;

mod shared;

include!(concat!(env!("OUT_DIR"), "/generated_routes.rs"));

pub use shared::*;

ndx_module!();

// ── extern "C" declarations for plain ndc/libndc symbols ─────────────────────

type NdcHandlerFn = unsafe extern "C" fn(fd: c_int, body: *mut c_char) -> c_int;

unsafe extern "C" {
    fn ndc_register_handler(path: *const c_char, handler: NdcHandlerFn);
    fn ndc_env_get(fd: c_int, target: *mut c_char, key: *const c_char) -> c_int;
    fn ndc_header_set(fd: c_int, key: *const c_char, value: *const c_char);
    fn ndc_respond(fd: c_int, code: c_int, body: *const c_char);
    fn free(ptr: *mut c_void);
}

// ── NDX hook declarations (outbound calls routed through NDX.call) ────────────

#[ndx_hook_decl]
pub fn get_request_user(fd: c_int) -> *const c_char {}

#[ndx_hook_decl]
pub fn csrf_set_cookie(fd: c_int, out: *mut c_char, len: usize) -> c_int {}

#[ndx_hook_decl]
pub fn core_get(fd: c_int, body: *mut c_char) -> c_int {}

#[ndx_hook_decl]
pub fn index_get_module_count(dummy: c_int) -> usize {}

#[ndx_hook_decl]
pub fn index_get_module_id(i: usize) -> *const c_char {}

#[ndx_hook_decl]
pub fn index_get_module_title(i: usize) -> *const c_char {}

#[ndx_hook_decl]
pub fn index_get_module_flags(i: usize) -> u32 {}

#[ndx_hook_decl]
pub fn dataset_get_json(fd: c_int, dataset_id: *const c_char, include: *const c_char, out_json: *mut *mut c_char) -> c_int {}

// ── ModuleEntryFfi (shared with render_ffi submodules via crate::) ────────────

#[repr(C)]
pub struct ModuleEntryFfi {
    pub id:    *const c_char,
    pub title: *const c_char,
    pub flags: u32,
}

// ── Helpers ───────────────────────────────────────────────────────────────────

fn safe_cstring(s: String) -> CString {
    CString::new(s.replace('\0', "")).unwrap()
}

/// Build the module list by calling index NDX hooks.
fn collect_modules<'a>(buf: &'a mut [ModuleRef<'a>; 64]) -> &'a [ModuleRef<'a>] {
    let count = unsafe { index_get_module_count(0) }.min(64);
    for i in 0..count {
        let id    = unsafe { cstr_ref(index_get_module_id(i)) };
        let title = unsafe { cstr_ref(index_get_module_title(i)) };
        let flags = unsafe { index_get_module_flags(i) };
        buf[i] = ModuleRef { id, title, flags };
    }
    &buf[..count]
}

pub fn load_dataset_json_with_include(
    fd: c_int,
    dataset_id: &str,
    include: Option<&str>,
) -> Option<String> {
    let dataset_id_c = CString::new(dataset_id).ok()?;
    let include_c = include.and_then(|s| CString::new(s).ok());
    let include_ptr = include_c
        .as_ref()
        .map(|s| s.as_ptr())
        .unwrap_or(std::ptr::null());
    let mut raw: *mut c_char = std::ptr::null_mut();
    let rc = unsafe { dataset_get_json(fd, dataset_id_c.as_ptr(), include_ptr, &mut raw) };
    let json = if rc == 0 && !raw.is_null() {
        let owned = unsafe { std::ffi::CStr::from_ptr(raw) }.to_string_lossy().into_owned();
        unsafe { free(raw.cast()) };
        Some(owned)
    } else {
        if !raw.is_null() {
            unsafe { free(raw.cast()) };
        }
        None
    };
    json
}

pub fn load_dataset_json(fd: c_int, dataset_id: &str) -> Option<String> {
    load_dataset_json_with_include(fd, dataset_id, None)
}

fn dispatch_result(fd: c_int, response: ResponsePayload) {
    if let Some(loc) = response.location.filter(|l| !l.is_empty()) {
        let loc_c = safe_cstring(loc);
        let key_c = c"Location";
        unsafe {
            ndc_header_set(fd, key_c.as_ptr(), loc_c.as_ptr());
            ndc_respond(fd, response.status as c_int, c"".as_ptr());
        }
        return;
    }
    let ct_c   = safe_cstring(response.content_type);
    let body_c = safe_cstring(response.body);
    let key_c  = c"Content-Type";
    unsafe {
        ndc_header_set(fd, key_c.as_ptr(), ct_c.as_ptr());
        ndc_respond(fd, response.status as c_int, body_c.as_ptr());
    }
}

fn internal_error(fd: c_int) {
    dispatch_result(
        fd,
        html_response_with_status(500, "500", error_page(None, "/", 500, "Internal server error")),
    );
}

fn catch_dispatch<F>(fd: c_int, name: &str, f: F)
where
    F: FnOnce() -> ResponsePayload,
{
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(r) => dispatch_result(fd, r),
        Err(e) => {
            let msg: &str = e.downcast_ref::<&str>().copied()
                .or_else(|| e.downcast_ref::<String>().map(String::as_str))
                .unwrap_or("(unknown)");
            eprintln!("{name} panic: {msg}");
            internal_error(fd);
        }
    }
}

// ── GET handler (served via ndc_register_handler) ────────────────────────────

unsafe extern "C" fn ssr_get_handler(fd: c_int, body: *mut c_char) -> c_int {
    // diagnostic: log NDX.call state before dispatch
    if let Ok(mut f) = std::fs::OpenOptions::new()
        .create(true).append(true).open("/tmp/ssr_diag.log")
        .or_else(|_| std::fs::OpenOptions::new()
            .create(true).append(true).open("tmp/ssr_diag.log"))
    {
        use std::io::Write;
        let call_ptr: usize = unsafe {
            NDX.call.map(|f| f as usize).unwrap_or(0)
        };
        let _ = writeln!(f, "ssr_get_handler: NDX.call={:#x}", call_ptr);
    }
    unsafe { core_get(fd, body) }
}

// ── NDX listeners ─────────────────────────────────────────────────────────────

#[ndx_listener]
pub fn ssr_render(
    fd: c_int,
    method: *const c_char,
    path: *const c_char,
    query: *const c_char,
    body: *const c_uchar,
    body_len: usize,
    remote_user: *const c_char,
) -> c_int {
    const CSRF_BUF_SIZE: usize = 33;
    let mut csrf_buf = [0u8; CSRF_BUF_SIZE];
    unsafe { csrf_set_cookie(fd, csrf_buf.as_mut_ptr() as *mut c_char, CSRF_BUF_SIZE) };
    let csrf_end = csrf_buf.iter().position(|&b| b == 0).unwrap_or(CSRF_BUF_SIZE);
    let csrf_str = std::str::from_utf8(&csrf_buf[..csrf_end]).unwrap_or("");

    let body_slice: &[u8] = if body.is_null() || body_len == 0 {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(body, body_len) }
    };

    let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
    let modules_slice = collect_modules(&mut modules_buf);

    let remote_user_str = unsafe { cstr_ref(remote_user) };
    let ctx = RequestContext {
        fd,
        method:      unsafe { cstr_ref(method) },
        path:        unsafe { cstr_ref(path) },
        query:       unsafe { cstr_ref(query) },
        body:        body_slice,
        remote_user: if remote_user_str.is_empty() { None } else { Some(remote_user_str) },
        modules:     modules_slice,
        csrf_token:  csrf_str,
    };

    catch_dispatch(fd, "ssr_render", || route(&ctx));
    0
}

#[ndx_listener]
pub fn ssr_render_delete(
    fd: c_int,
    module: *const c_char,
    id: *const c_char,
    title: *const c_char,
) -> c_int {
    const QUERY_BUF_SIZE: usize = 512;
    const CSRF_BUF_SIZE: usize = 33;
    let mut query_buf = [0u8; QUERY_BUF_SIZE];
    let mut csrf_buf  = [0u8; CSRF_BUF_SIZE];
    let key_qs = c"QUERY_STRING";
    unsafe {
        ndc_env_get(fd, query_buf.as_mut_ptr() as *mut c_char, key_qs.as_ptr());
        csrf_set_cookie(fd, csrf_buf.as_mut_ptr() as *mut c_char, CSRF_BUF_SIZE);
    }

    let query_end = query_buf.iter().position(|&b| b == 0).unwrap_or(QUERY_BUF_SIZE);
    let csrf_end  = csrf_buf.iter().position(|&b| b == 0).unwrap_or(CSRF_BUF_SIZE);
    let query_str = std::str::from_utf8(&query_buf[..query_end]).unwrap_or("");
    let csrf_str  = std::str::from_utf8(&csrf_buf[..csrf_end]).unwrap_or("");

    let module_s = unsafe { cstr_ref(module) };
    let id_s     = unsafe { cstr_ref(id) };
    let title_s  = unsafe { cstr_ref(title) };
    let path_buf = format!("/{}/{}", module_s, id_s);

    let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
    let modules_slice = collect_modules(&mut modules_buf);

    let ctx = RequestContext {
        fd,
        method:      "GET",
        path:        &path_buf,
        query:       query_str,
        body:        &[],
        remote_user: None,
        modules:     modules_slice,
        csrf_token:  csrf_str,
    };

    catch_dispatch(fd, "ssr_render_delete", || render_delete_confirm(module_s, id_s, title_s, &ctx));
    0
}

#[ndx_listener]
pub fn ssr_render_song_detail(fd: c_int, req: *const song_ffi::SongDetailRenderFfi) -> c_int {
    if req.is_null() { internal_error(fd); return 0; }
    let r = unsafe { &*req };
    let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
    let modules_slice = unsafe { fill_modules(&mut modules_buf, r.modules, r.modules_len) };
    let id_s = unsafe { cstr_ref(r.id) };
    let remote_user_str = unsafe { cstr_ref(r.remote_user) };
    let ctx = RequestContext {
        fd,
        method:      "GET",
        path:        &format!("/song/{id_s}"),
        query:       unsafe { cstr_ref(r.query) },
        body:        &[],
        remote_user: if remote_user_str.is_empty() { None } else { Some(remote_user_str) },
        modules:     modules_slice,
        csrf_token:  unsafe { cstr_ref(r.csrf_token) },
    };
    let item = SongItem {
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
    catch_dispatch(fd, "ssr_render_song_detail", || song::render_detail(&item, id_s, &ctx));
    0
}

#[ndx_listener]
pub fn ssr_render_poem_detail(fd: c_int, req: *const poem_ffi::PoemRenderFfi) -> c_int {
    if req.is_null() { internal_error(fd); return 0; }
    let r = unsafe { &*req };
    let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
    let modules_slice = unsafe { fill_modules(&mut modules_buf, r.modules, r.modules_len) };
    let id_s = unsafe { cstr_ref(r.id) };
    let remote_user_str = unsafe { cstr_ref(r.remote_user) };
    let ctx = RequestContext {
        fd,
        method:      "GET",
        path:        &format!("/poem/{id_s}"),
        query:       unsafe { cstr_ref(r.query) },
        body:        &[],
        remote_user: if remote_user_str.is_empty() { None } else { Some(remote_user_str) },
        modules:     modules_slice,
        csrf_token:  unsafe { cstr_ref(r.csrf_token) },
    };
    let item = PoemItem {
        title:        unsafe { cstr_ref(r.title) },
        head_content: unsafe { cstr_ref(r.head_content) },
        body_content: unsafe { cstr_ref(r.body_content) },
        owner:        r.owner,
    };
    catch_dispatch(fd, "ssr_render_poem_detail", || poem::render_detail(&item, id_s, &ctx));
    0
}

#[ndx_listener]
pub fn ssr_render_poem_edit(fd: c_int, req: *const poem_ffi::PoemRenderFfi) -> c_int {
    if req.is_null() { internal_error(fd); return 0; }
    let r = unsafe { &*req };
    let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
    let modules_slice = unsafe { fill_modules(&mut modules_buf, r.modules, r.modules_len) };
    let id_s = unsafe { cstr_ref(r.id) };
    let remote_user_str = unsafe { cstr_ref(r.remote_user) };
    let ctx = RequestContext {
        fd,
        method:      "GET",
        path:        &format!("/poem/{id_s}/edit"),
        query:       unsafe { cstr_ref(r.query) },
        body:        &[],
        remote_user: if remote_user_str.is_empty() { None } else { Some(remote_user_str) },
        modules:     modules_slice,
        csrf_token:  unsafe { cstr_ref(r.csrf_token) },
    };
    let item = PoemItem {
        title:        unsafe { cstr_ref(r.title) },
        head_content: unsafe { cstr_ref(r.head_content) },
        body_content: unsafe { cstr_ref(r.body_content) },
        owner:        r.owner,
    };
    catch_dispatch(fd, "ssr_render_poem_edit", || poem::render_edit_typed(&item, id_s, &ctx));
    0
}

#[ndx_listener]
pub fn ssr_render_songbook_detail(fd: c_int, req: *const songbook_ffi::SongbookDetailRenderFfi) -> c_int {
    if req.is_null() { internal_error(fd); return 0; }
    let r = unsafe { &*req };
    let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
    let modules_slice = unsafe { fill_modules(&mut modules_buf, r.modules, r.modules_len) };
    let id_s = unsafe { cstr_ref(r.id) };
    let remote_user_str = unsafe { cstr_ref(r.remote_user) };
    let ctx = RequestContext {
        fd,
        method:      "GET",
        path:        &format!("/songbook/{id_s}"),
        query:       unsafe { cstr_ref(r.query) },
        body:        &[],
        remote_user: if remote_user_str.is_empty() { None } else { Some(remote_user_str) },
        modules:     modules_slice,
        csrf_token:  unsafe { cstr_ref(r.csrf_token) },
    };
    let songs_raw = if r.songs.is_null() { &[] } else {
        unsafe { std::slice::from_raw_parts(r.songs, r.songs_len) }
    };
    let songs: Vec<SongbookSong<'_>> = songs_raw.iter().map(|s| SongbookSong {
        chord_id:     unsafe { cstr_ref(s.chord_id) },
        format:       unsafe { cstr_ref(s.format) },
        chord_title:  unsafe { cstr_ref(s.chord_title) },
        chord_data:   unsafe { cstr_ref(s.chord_data) },
        transpose:    s.transpose,
        original_key: s.original_key,
    }).collect();
    let item = SongbookItem {
        title:       unsafe { cstr_ref(r.sb_title) },
        owner:       unsafe { cstr_ref(r.owner) },
        choir:       unsafe { cstr_ref(r.choir) },
        viewer_zoom: r.viewer_zoom,
        songs,
    };
    catch_dispatch(fd, "ssr_render_songbook_detail", || songbook::render_detail(&item, id_s, &ctx));
    0
}

#[ndx_listener]
pub fn ssr_render_choir_detail(fd: c_int, req: *const choir_ffi::ChoirDetailRenderFfi) -> c_int {
    if req.is_null() { internal_error(fd); return 0; }
    let r = unsafe { &*req };
    let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
    let modules_slice = unsafe { fill_modules(&mut modules_buf, r.modules, r.modules_len) };
    let id_s = unsafe { cstr_ref(r.id) };
    let remote_user_str = unsafe { cstr_ref(r.remote_user) };
    let ctx = RequestContext {
        fd,
        method:      "GET",
        path:        &format!("/choir/{id_s}"),
        query:       unsafe { cstr_ref(r.query) },
        body:        &[],
        remote_user: if remote_user_str.is_empty() { None } else { Some(remote_user_str) },
        modules:     modules_slice,
        csrf_token:  unsafe { cstr_ref(r.csrf_token) },
    };
    let songs_raw = if r.songs.is_null() { &[] } else {
        unsafe { std::slice::from_raw_parts(r.songs, r.songs_len) }
    };
    let all_songs_raw = if r.all_songs.is_null() { &[] } else {
        unsafe { std::slice::from_raw_parts(r.all_songs, r.all_songs_len) }
    };
    let songbooks_raw = if r.songbooks.is_null() { &[] } else {
        unsafe { std::slice::from_raw_parts(r.songbooks, r.songbooks_len) }
    };
    let songs: Vec<ChoirSong<'_>> = songs_raw.iter().map(|s| ChoirSong {
        id:            unsafe { cstr_ref(s.id) },
        title:         unsafe { cstr_ref(s.title) },
        format:        unsafe { cstr_ref(s.format) },
        preferred_key: s.preferred_key,
        original_key:  s.original_key,
    }).collect();
    let all_songs: Vec<ChoirEntry<'_>> = all_songs_raw.iter().map(|e| ChoirEntry {
        id:    unsafe { cstr_ref(e.id) },
        title: unsafe { cstr_ref(e.title) },
    }).collect();
    let songbooks: Vec<ChoirEntry<'_>> = songbooks_raw.iter().map(|e| ChoirEntry {
        id:    unsafe { cstr_ref(e.id) },
        title: unsafe { cstr_ref(e.title) },
    }).collect();
    let item = ChoirItem {
        title:      unsafe { cstr_ref(r.title) },
        owner_name: unsafe { cstr_ref(r.owner_name) },
        formats:    unsafe { cstr_ref(r.formats) },
        songs,
        all_songs,
        songbooks,
    };
    catch_dispatch(fd, "ssr_render_choir_detail", || choir::render_detail(&item, id_s, &ctx));
    0
}

// ── ndx_install ───────────────────────────────────────────────────────────────

ndx_install! {
    unsafe {
        ndc_register_handler(c"GET:/".as_ptr(),              ssr_get_handler);
        ndc_register_handler(c"GET:/auth/login".as_ptr(),    ssr_get_handler);
        ndc_register_handler(c"GET:/auth/register".as_ptr(), ssr_get_handler);
    }
}
