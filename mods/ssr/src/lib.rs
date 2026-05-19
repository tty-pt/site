mod ctypes;
pub(crate) use ctypes::*;
use std::panic::{AssertUnwindSafe, catch_unwind};

use ndx::prelude::*;

ndx_module!();

// ── QmapProvider: bridges hyle::load_typed_item to C qmap data ───────────────

struct QmapProvider;

impl hyle::SourceProvider for QmapProvider {
	fn load_source(&self, dataset_id: &str) -> Option<hyle::Source> {
		source_query::load_source_full(dataset_id)
	}
}

pub mod blueprint;
pub mod hyle_ssr;
pub mod site_ui;
pub mod source_query;
mod shared;

include!(concat!(env!("OUT_DIR"), "/generated_routes.rs"));

pub use shared::*;

// ── extern "C" declarations for plain ndc/libndc symbols ─────────────────────

type NdcHandlerFn = unsafe extern "C" fn(fd: c_int, body: *mut c_char) -> c_int;

unsafe extern "C" {
    fn ndc_register_handler(path: *const c_char, handler: NdcHandlerFn);
    fn ndc_env_get(fd: c_int, target: *mut c_char, key: *const c_char) -> c_int;
    fn ndc_header_set(fd: c_int, key: *const c_char, value: *const c_char);
    fn ndc_respond(fd: c_int, code: c_int, body: *const c_char);
}

// ── NDX hook declarations (outbound calls routed through NDX.call) ────────────

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

// ── Helpers ───────────────────────────────────────────────────────────────────

fn safe_cstring(s: String) -> CString {
    CString::new(s.replace('\0', "")).unwrap()
}

/// Build the module list by calling index NDX hooks.
fn collect_modules<'a>(buf: &'a mut [ModuleRef<'a>; 64]) -> &'a [ModuleRef<'a>] {
    let count = unsafe { index_get_module_count(0) }.min(64);
    for i in 0..count {
        let id = unsafe { cstr_ref(index_get_module_id(i)) };
        let title = unsafe { cstr_ref(index_get_module_title(i)) };
        let flags = unsafe { index_get_module_flags(i) };
        buf[i] = ModuleRef { id, title, flags };
    }
    &buf[..count]
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
    let ct_c = safe_cstring(response.content_type);
    let body_c = safe_cstring(response.body);
    let key_c = c"Content-Type";
    unsafe {
        ndc_header_set(fd, key_c.as_ptr(), ct_c.as_ptr());
        ndc_respond(fd, response.status as c_int, body_c.as_ptr());
    }
}

fn internal_error(fd: c_int) {
    dispatch_result(
        fd,
        html_response_with_status(
            500,
            "500",
            crate::site_ui::error_page(None, "/", 500, "Internal server error"),
        ),
    );
}

fn catch_dispatch<F>(fd: c_int, name: &str, f: F)
where
    F: FnOnce() -> ResponsePayload,
{
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(r) => dispatch_result(fd, r),
        Err(e) => {
            let msg: &str = e
                .downcast_ref::<&str>()
                .copied()
                .or_else(|| e.downcast_ref::<String>().map(String::as_str))
                .unwrap_or("(unknown)");
            eprintln!("{name} panic: {msg}");
            internal_error(fd);
        }
    }
}

// ── GET handler (served via ndc_register_handler) ────────────────────────────

unsafe extern "C" fn ssr_get_handler(fd: c_int, body: *mut c_char) -> c_int {
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
    let method_str = unsafe { cstr_ref(method) };
    let path_str = unsafe { cstr_ref(path) };

    const CSRF_BUF_SIZE: usize = 33;
    let mut csrf_buf = [0u8; CSRF_BUF_SIZE];
    unsafe { csrf_set_cookie(fd, csrf_buf.as_mut_ptr() as *mut c_char, CSRF_BUF_SIZE) };
    let csrf_end = csrf_buf
        .iter()
        .position(|&b| b == 0)
        .unwrap_or(CSRF_BUF_SIZE);
    let csrf_str = std::str::from_utf8(&csrf_buf[..csrf_end]).unwrap_or("");

    let body_slice: &[u8] = if body.is_null() || body_len == 0 {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(body, body_len) }
    };

    let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef {
        id: "",
        title: "",
        flags: 0,
    });
    let modules_slice = collect_modules(&mut modules_buf);

    let remote_user_str = unsafe { cstr_ref(remote_user) };
    let ctx = RequestContext {
        fd,
        method: method_str,
        path: path_str,
        query: unsafe { cstr_ref(query) },
        body: body_slice,
        remote_user: if remote_user_str.is_empty() {
            None
        } else {
            Some(remote_user_str)
        },
        modules: modules_slice,
        csrf_token: csrf_str,
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
    let mut csrf_buf = [0u8; CSRF_BUF_SIZE];
    let key_qs = c"QUERY_STRING";
    unsafe {
        ndc_env_get(fd, query_buf.as_mut_ptr() as *mut c_char, key_qs.as_ptr());
        csrf_set_cookie(fd, csrf_buf.as_mut_ptr() as *mut c_char, CSRF_BUF_SIZE);
    }

    let query_end = query_buf
        .iter()
        .position(|&b| b == 0)
        .unwrap_or(QUERY_BUF_SIZE);
    let csrf_end = csrf_buf
        .iter()
        .position(|&b| b == 0)
        .unwrap_or(CSRF_BUF_SIZE);
    let query_str = std::str::from_utf8(&query_buf[..query_end]).unwrap_or("");
    let csrf_str = std::str::from_utf8(&csrf_buf[..csrf_end]).unwrap_or("");

    let module_s = unsafe { cstr_ref(module) };
    let id_s = unsafe { cstr_ref(id) };
    let title_s = unsafe { cstr_ref(title) };
    let path_buf = format!("/{}/{}", module_s, id_s);

    let mut modules_buf: [ModuleRef<'_>; 64] = std::array::from_fn(|_| ModuleRef {
        id: "",
        title: "",
        flags: 0,
    });
    let modules_slice = collect_modules(&mut modules_buf);

    let ctx = RequestContext {
        fd,
        method: "GET",
        path: &path_buf,
        query: query_str,
        body: &[],
        remote_user: None,
        modules: modules_slice,
        csrf_token: csrf_str,
    };

    catch_dispatch(fd, "ssr_render_delete", || {
        crate::site_ui::render_delete_confirm(module_s, id_s, title_s, &ctx)
    });
    0
}

ndx_install! {
    let _ = hyle::set_provider(Box::new(QmapProvider));

    unsafe {
        ndc_register_handler(c"GET:/".as_ptr(),              ssr_get_handler);
        ndc_register_handler(c"GET:/auth/login".as_ptr(),    ssr_get_handler);
        ndc_register_handler(c"GET:/auth/register".as_ptr(), ssr_get_handler);
    }
}
