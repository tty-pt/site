use std::ffi::{CStr, CString, c_char, c_uchar};
use std::panic::{AssertUnwindSafe, catch_unwind};

use base64::Engine;
use base64::engine::general_purpose::STANDARD as BASE64;

mod shared;
include!(concat!(env!("OUT_DIR"), "/generated_routes.rs"));

pub(crate) use shared::*;

#[repr(C)]
pub struct RenderRequest {
    method: *const c_char,
    path: *const c_char,
    query: *const c_char,
    body: *const c_uchar,
    body_len: usize,
    remote_user: *const c_char,
    forwarded_host: *const c_char,
    modules_header: *const c_char,
}

#[repr(C)]
pub struct RenderResult {
    status: u16,
    content_type: *mut c_char,
    location: *mut c_char,
    body: *mut c_char,
}

fn cstr(ptr: *const c_char) -> String {
    if ptr.is_null() {
        String::new()
    } else {
        unsafe { CStr::from_ptr(ptr) }.to_string_lossy().into_owned()
    }
}

fn opt_cstr(ptr: *const c_char) -> Option<String> {
    let s = cstr(ptr);
    if s.is_empty() { None } else { Some(s) }
}

fn body_string(ptr: *const c_uchar, len: usize) -> String {
    if ptr.is_null() || len == 0 {
        return String::new();
    }
    let bytes = unsafe { std::slice::from_raw_parts(ptr, len) };
    String::from_utf8_lossy(bytes).into_owned()
}

fn normalize_path(path: &str) -> String {
    let trimmed = path.trim();
    if trimmed.is_empty() || trimmed == "/" {
        "/".to_string()
    } else {
        format!("/{}", trimmed.trim_matches('/'))
    }
}

fn parse_modules(header: Option<String>) -> Vec<ModuleEntry> {
    let Some(raw) = header else {
        return Vec::new();
    };
    let Ok(decoded) = BASE64.decode(raw.as_bytes()) else {
        return Vec::new();
    };
    serde_json::from_slice::<Vec<ModuleEntry>>(&decoded).unwrap_or_default()
}

fn to_ffi(response: ResponsePayload) -> RenderResult {
    RenderResult {
        status: response.status,
        content_type: CString::new(response.content_type).unwrap().into_raw(),
        location: response
            .location
            .and_then(|value| CString::new(value).ok())
            .map_or(std::ptr::null_mut(), CString::into_raw),
        body: CString::new(response.body).unwrap().into_raw(),
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_ffi(request: *const RenderRequest) -> RenderResult {
    let result = catch_unwind(AssertUnwindSafe(|| {
        let req = unsafe { &*request };
        let ctx = RequestContext {
            method: cstr(req.method),
            path: normalize_path(&cstr(req.path)),
            query: cstr(req.query),
            body: body_string(req.body, req.body_len),
            remote_user: opt_cstr(req.remote_user),
            modules: parse_modules(opt_cstr(req.modules_header)),
        };
        to_ffi(route(&ctx))
    }));

    match result {
        Ok(payload) => payload,
        Err(_) => to_ffi(html_response_with_status(
            500,
            "500",
            error_page(None, "/", 500, "Internal server error"),
        )),
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_free_result_ffi(result: *mut RenderResult) {
    if result.is_null() {
        return;
    }

    let result = unsafe { &mut *result };

    if !result.content_type.is_null() {
        unsafe {
            drop(CString::from_raw(result.content_type));
        }
        result.content_type = std::ptr::null_mut();
    }

    if !result.location.is_null() {
        unsafe {
            drop(CString::from_raw(result.location));
        }
        result.location = std::ptr::null_mut();
    }

    if !result.body.is_null() {
        unsafe {
            drop(CString::from_raw(result.body));
        }
        result.body = std::ptr::null_mut();
    }
}
