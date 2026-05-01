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

/// Shared context passed to every per-module item FFI entry point.
#[repr(C)]
pub struct ItemContext {
    pub id:          *const c_char,
    pub query:       *const c_char,
    pub remote_user: *const c_char,
    pub modules:     *const ModuleEntryFfi,
    pub modules_len: usize,
}

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

#[repr(C)]
pub struct PoemItemFfi {
    pub title:        *const c_char,
    pub head_content: *const c_char,
    pub body_content: *const c_char,
    pub owner:        bool,
}

/// Used by the generic delete confirmation page (all modules).
#[repr(C)]
pub struct DeleteItemFfi {
    pub title: *const c_char,
}

#[repr(C)]
pub struct RenderRequest {
    method: *const c_char,
    path: *const c_char,
    query: *const c_char,
    body: *const c_uchar,
    body_len: usize,
    remote_user: *const c_char,
    modules: *const ModuleEntryFfi,
    modules_len: usize,
}

#[repr(C)]
pub struct RenderItemRequest {
    module: *const c_char,
    action: *const c_char,
    id: *const c_char,
    query: *const c_char,
    json: *const c_char,
    remote_user: *const c_char,
    modules: *const ModuleEntryFfi,
    modules_len: usize,
}

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

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_ffi(request: *const RenderRequest) -> RenderResult {
    if request.is_null() {
        eprintln!("ssr_render_ffi: null request pointer");
        return to_ffi(html_response_with_status(
            500, "500", error_page(None, "/", 500, "Internal server error"),
        ));
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let req = unsafe { &*request };
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
        Ok(payload) => payload,
        Err(e) => {
            let msg = e.downcast_ref::<&str>().copied()
                .or_else(|| e.downcast_ref::<String>().map(String::as_str))
                .unwrap_or("(unknown)");
            eprintln!("ssr_render_ffi panic: {msg}");
            to_ffi(html_response_with_status(
                500, "500", error_page(None, "/", 500, "Internal server error"),
            ))
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_item_ffi(request: *const RenderItemRequest) -> RenderResult {
    if request.is_null() {
        eprintln!("ssr_render_item_ffi: null request pointer");
        return to_ffi(html_response_with_status(
            500, "500", error_page(None, "/", 500, "Internal server error"),
        ));
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let req = unsafe { &*request };
        let module_s = unsafe { cstr_ref(req.module) };
        let action_s = unsafe { cstr_ref(req.action) };
        let id_s     = unsafe { cstr_ref(req.id) };
        let path_buf = format!("/{}/{}", module_s, id_s);
        let json_slice: &[u8] = if req.json.is_null() {
            &[]
        } else {
            let len = unsafe { std::ffi::CStr::from_ptr(req.json).to_bytes().len() };
            unsafe { std::slice::from_raw_parts(req.json as *const u8, len) }
        };
        let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
        let modules_slice = unsafe { fill_modules(&mut modules_buf, req.modules, req.modules_len) };
        let remote_user_str = unsafe { cstr_ref(req.remote_user) };
        let ctx = RequestContext {
            method:      "POST",
            path:        &path_buf,
            query:       unsafe { cstr_ref(req.query) },
            body:        json_slice,
            remote_user: if remote_user_str.is_empty() { None } else { Some(remote_user_str) },
            modules:     modules_slice,
        };
        match dispatch_item(module_s, action_s, id_s, &ctx) {
            Some(res) => to_ffi(res),
            None => to_ffi(html_response_with_status(
                404,
                "404",
                error_page(current_user(&ctx), &ctx.path, 404, "Not found"),
            )),
        }
    }));

    match result {
        Ok(payload) => payload,
        Err(e) => {
            let msg = e.downcast_ref::<&str>().copied()
                .or_else(|| e.downcast_ref::<String>().map(String::as_str))
                .unwrap_or("(unknown)");
            eprintln!("ssr_render_item_ffi panic: {msg}");
            to_ffi(html_response_with_status(
                500, "500", error_page(None, "/", 500, "Internal server error"),
            ))
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_song_detail_ffi(
    payload: *const SongItemFfi,
    ictx: *const ItemContext,
) -> RenderResult {
    if payload.is_null() || ictx.is_null() {
        eprintln!("ssr_render_song_detail_ffi: null pointer");
        return to_ffi(html_response_with_status(500, "500", error_page(None, "/", 500, "Internal server error")));
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        let payload = unsafe { &*payload };
        let ictx    = unsafe { &*ictx };
        let id_s    = unsafe { cstr_ref(ictx.id) };
        let path_buf = format!("/song/{}", id_s);
        let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
        let ctx = unsafe { item_context_to_request(ictx, &path_buf, &mut modules_buf) };
        let view = unsafe { song_item_to_view(payload) };
        to_ffi(song::render_detail(&view, id_s, &ctx))
    }));
    match result {
        Ok(r) => r,
        Err(e) => {
            eprintln!("ssr_render_song_detail_ffi panic: {}", e.downcast_ref::<&str>().copied().unwrap_or("(unknown)"));
            to_ffi(html_response_with_status(500, "500", error_page(None, "/", 500, "Internal server error")))
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_poem_detail_ffi(
    payload: *const PoemItemFfi,
    ictx: *const ItemContext,
) -> RenderResult {
    if payload.is_null() || ictx.is_null() {
        eprintln!("ssr_render_poem_detail_ffi: null pointer");
        return to_ffi(html_response_with_status(500, "500", error_page(None, "/", 500, "Internal server error")));
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        let payload = unsafe { &*payload };
        let ictx    = unsafe { &*ictx };
        let id_s    = unsafe { cstr_ref(ictx.id) };
        let path_buf = format!("/poem/{}", id_s);
        let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
        let ctx = unsafe { item_context_to_request(ictx, &path_buf, &mut modules_buf) };
        let view = unsafe { poem_item_to_view(payload) };
        to_ffi(poem::render_detail(&view, id_s, &ctx))
    }));
    match result {
        Ok(r) => r,
        Err(e) => {
            eprintln!("ssr_render_poem_detail_ffi panic: {}", e.downcast_ref::<&str>().copied().unwrap_or("(unknown)"));
            to_ffi(html_response_with_status(500, "500", error_page(None, "/", 500, "Internal server error")))
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_poem_edit_ffi(
    payload: *const PoemItemFfi,
    ictx: *const ItemContext,
) -> RenderResult {
    if payload.is_null() || ictx.is_null() {
        eprintln!("ssr_render_poem_edit_ffi: null pointer");
        return to_ffi(html_response_with_status(500, "500", error_page(None, "/", 500, "Internal server error")));
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        let payload = unsafe { &*payload };
        let ictx    = unsafe { &*ictx };
        let id_s    = unsafe { cstr_ref(ictx.id) };
        let path_buf = format!("/poem/{}", id_s);
        let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
        let ctx = unsafe { item_context_to_request(ictx, &path_buf, &mut modules_buf) };
        let view = unsafe { poem_item_to_view(payload) };
        to_ffi(poem::render_edit_typed(&view, id_s, &ctx))
    }));
    match result {
        Ok(r) => r,
        Err(e) => {
            eprintln!("ssr_render_poem_edit_ffi panic: {}", e.downcast_ref::<&str>().copied().unwrap_or("(unknown)"));
            to_ffi(html_response_with_status(500, "500", error_page(None, "/", 500, "Internal server error")))
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_delete_ffi(
    module: *const c_char,
    payload: *const DeleteItemFfi,
    ictx: *const ItemContext,
) -> RenderResult {
    if payload.is_null() || ictx.is_null() {
        eprintln!("ssr_render_delete_ffi: null pointer");
        return to_ffi(html_response_with_status(
            500, "500", error_page(None, "/", 500, "Internal server error"),
        ));
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        let module_s  = unsafe { cstr_ref(module) };
        let payload   = unsafe { &*payload };
        let ictx      = unsafe { &*ictx };
        let id_s      = unsafe { cstr_ref(ictx.id) };
        let title_s   = unsafe { cstr_ref(payload.title) };
        let path_buf  = format!("/{}/{}", module_s, id_s);
        let mut modules_buf: [ModuleRef<'_>; 64] =
            std::array::from_fn(|_| ModuleRef { id: "", title: "", flags: 0 });
        let ctx = unsafe { item_context_to_request(ictx, &path_buf, &mut modules_buf) };
        to_ffi(render_delete_confirm(module_s, id_s, title_s, &ctx))
    }));
    match result {
        Ok(r) => r,
        Err(e) => {
            let msg = e.downcast_ref::<&str>().copied()
                .or_else(|| e.downcast_ref::<String>().map(String::as_str))
                .unwrap_or("(unknown)");
            eprintln!("ssr_render_delete_ffi panic: {msg}");
            to_ffi(html_response_with_status(
                500, "500", error_page(None, "/", 500, "Internal server error"),
            ))
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
