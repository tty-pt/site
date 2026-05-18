use std::ffi::{CStr, CString, c_char, c_int, c_uchar, c_void};
use std::panic::{catch_unwind, AssertUnwindSafe};

use ndx::prelude::*;

mod shared;
pub mod ffi;
pub mod api;

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

#[ndx_hook_decl]
pub fn dataset_get_item_json(fd: c_int, dataset_id: *const c_char, id: *const c_char, out_json: *mut *mut c_char) -> c_int {}

#[ndx_hook_decl]
pub fn song_get_pref(user: *const c_char, name: *const c_char) -> *mut c_char {}

#[ndx_hook_decl]
pub fn song_get_original_key(id: *const c_char) -> c_int {}

pub fn get_song_pref(user: &str, name: &str) -> Option<String> {
    let user_c = CString::new(user).ok()?;
    let name_c = CString::new(name).ok()?;
    let raw = unsafe { song_get_pref(user_c.as_ptr(), name_c.as_ptr()) };
    if raw.is_null() {
        None
    } else {
        let owned = unsafe { std::ffi::CStr::from_ptr(raw) }.to_string_lossy().into_owned();
        unsafe { free(raw.cast()) };
        Some(owned)
    }
}

#[ndx_hook_decl]
pub fn song_transpose(id: *const c_char, semi: c_int, fl: c_int, out: *mut *mut c_char) -> c_int {}

pub fn get_song_original_key(id: &str) -> i32 {
    let id_c = CString::new(id).unwrap();
    unsafe { song_get_original_key(id_c.as_ptr()) }
}

pub fn get_song_transpose(id: &str, semi: i32, flags: i32) -> Option<String> {
    let id_c = CString::new(id).ok()?;
    let mut raw: *mut c_char = std::ptr::null_mut();
    let rc = unsafe { song_transpose(id_c.as_ptr(), semi, flags, &mut raw) };
    if rc == 0 && !raw.is_null() {
        let owned = unsafe { std::ffi::CStr::from_ptr(raw) }.to_string_lossy().into_owned();
        unsafe { free(raw.cast()) };
        Some(owned)
    } else {
        if !raw.is_null() {
            unsafe { free(raw.cast()) };
        }
        None
    }
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

pub fn load_dataset_item_json(fd: c_int, dataset_id: &str, id: &str) -> Option<String> {
    let dataset_id_c = CString::new(dataset_id).ok()?;
    let id_c = CString::new(id).ok()?;
    let mut raw: *mut c_char = std::ptr::null_mut();
    let rc = unsafe { dataset_get_item_json(fd, dataset_id_c.as_ptr(), id_c.as_ptr(), &mut raw) };
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

pub fn load_dataset_source(dataset_id: &str) -> Option<hyle::Source> {
    let def_ffi = ffi::dataset::find_dataset_def(dataset_id)?;
    let qmap_ptr = unsafe { qmap::Qmap::from_handle(def_ffi.source_hd) };
    
    // Convert FFI def to hyle-source-qmap def
    let fields = unsafe {
        std::slice::from_raw_parts(def_ffi.fields, def_ffi.field_count)
    };
    
    let rust_fields = fields.iter().map(|f| {
        let name = unsafe { CStr::from_ptr(f.name) }.to_string_lossy().into_owned();
        let target_dataset = if f.target_dataset.is_null() {
            None
        } else {
            let full_target = unsafe { CStr::from_ptr(f.target_dataset) }.to_string_lossy();
            Some(full_target.strip_suffix(".items").unwrap_or(&full_target).to_string())
        };
        
        let field_type = match f.field_type {
            1 => hyle_source_qmap::FieldType::Int,
            2 => hyle_source_qmap::FieldType::Bool,
            3 => hyle_source_qmap::FieldType::NullableString,
            4 => hyle_source_qmap::FieldType::Reference,
            5 => hyle_source_qmap::FieldType::MultiReference,
            _ => hyle_source_qmap::FieldType::String,
        };
        
        let inverse_name = if f.inverse_name.is_null() {
            None
        } else {
            Some(unsafe { CStr::from_ptr(f.inverse_name) }.to_string_lossy().into_owned())
        };
        
        hyle_source_qmap::FieldDef {
            name,
            field_type,
            target_dataset,
            inverse_name,
        }
    }).collect();
    
    let model_id = dataset_id.strip_suffix(".items").unwrap_or(dataset_id);
    let rust_def = hyle_source_qmap::DatasetDef {
        id: model_id.to_owned(),
        fields: rust_fields,
        source_hd: def_ffi.source_hd,
    };
    
    let source = hyle_source_qmap::build_source_from_json_qmap(&qmap_ptr, &rust_def);
    if let Some(res) = source.get(model_id) {
        let pid = unsafe { libc::getpid() };
        eprintln!("[{}] load_dataset_source: loaded {} rows for {}", pid, res.rows().len(), model_id);
    }
    std::mem::forget(qmap_ptr);
    Some(source)
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
    let method_str = unsafe { cstr_ref(method) };
    let path_str = unsafe { cstr_ref(path) };
    
    // Debug: log call to /tmp/ssr_render_debug.log
    if let Ok(mut f) = std::fs::OpenOptions::new()
        .create(true).append(true).open("/tmp/ssr_render_debug.log")
    {
        use std::io::Write;
        let _ = writeln!(f, "ssr_render called: method={} path={}", method_str, path_str);
    }

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
        method:      method_str,
        path:        path_str,
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

// Choir detail now rendered via dataset + hyle (no FFI)
// TODO: Add route for GET /choir/:id that loads from dataset

ndx_install! {
    unsafe {
        ndc_register_handler(c"GET:/".as_ptr(),              ssr_get_handler);
        ndc_register_handler(c"GET:/auth/login".as_ptr(),    ssr_get_handler);
        ndc_register_handler(c"GET:/auth/register".as_ptr(), ssr_get_handler);
    }
}
