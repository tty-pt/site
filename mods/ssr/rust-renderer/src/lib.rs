use std::ffi::{CString, c_char, c_uchar};
use std::panic::{catch_unwind, AssertUnwindSafe};

mod shared;

include!(concat!(env!("OUT_DIR"), "/generated_routes.rs"));

pub use shared::*;

#[repr(C)]
pub struct ModuleEntryFfi {
    pub id:    *const c_char,
    pub title: *const c_char,
    pub flags: u32,
}

// ── Combined render structs ───────────────────────────────────────────────────

/// Generic page render (home, login, register, error pages).
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
pub struct DeleteRenderFfi {
    pub module: *const c_char,
    pub title:  *const c_char,
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

pub(crate) fn to_ffi(response: ResponsePayload) -> RenderResult {
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

pub(crate) fn internal_error() -> RenderResult {
    to_ffi(html_response_with_status(500, "500", error_page(None, "/", 500, "Internal server error")))
}

/// Generic dispatch helper used by generated per-module FFI wrappers.
pub(crate) fn dispatch_item<T, F>(
    req: *const T,
    name: &str,
    render: F,
) -> RenderResult
where
    F: Fn(&T) -> ResponsePayload,
{
    if req.is_null() {
        eprintln!("{name}: null pointer");
        return internal_error();
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        let r = unsafe { &*req };
        to_ffi(render(r))
    }));
    match result {
        Ok(r) => r,
        Err(e) => {
            let msg: &str = e.downcast_ref::<&str>().copied()
                .or_else(|| e.downcast_ref::<String>().map(String::as_str))
                .unwrap_or("(unknown)");
            eprintln!("{name} panic: {msg}");
            internal_error()
        }
    }
}

// ── Generic FFI entry points (not module-specific) ────────────────────────────

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_page_ffi(req: *const PageRenderFfi) -> RenderResult {
    dispatch_item(req, "ssr_render_page_ffi", |req| {
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
        route(&ctx)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_delete_ffi(req: *const DeleteRenderFfi) -> RenderResult {
    dispatch_item(req, "ssr_render_delete_ffi", |r| {
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
        render_delete_confirm(module_s, id_s, title_s, &ctx)
    })
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
        let msg: &str = e.downcast_ref::<&str>().copied()
            .or_else(|| e.downcast_ref::<String>().map(String::as_str))
            .unwrap_or("(unknown)");
        eprintln!("ssr_free_result_ffi panic: {msg}");
    }
}
