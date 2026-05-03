pub use ndc_dioxus_shared::*;

use std::ffi::c_char;

/// Builds a `RequestContext` for a typed item FFI handler.
/// Declares `id_s`, `path_buf`, `modules_buf`, and `ctx` in the caller's scope.
///
/// Usage: `make_item_ctx!(r, "song", id_s, ctx);`
macro_rules! make_item_ctx {
    ($r:expr, $module:expr, $id_s:ident, $ctx:ident) => {
        let $id_s = unsafe { crate::cstr_ref($r.id) };
        let path_buf = ::std::format!("/{}/{}", $module, $id_s);
        let mut modules_buf: [crate::ModuleRef<'_>; 64] =
            ::std::array::from_fn(|_| crate::ModuleRef { id: "", title: "", flags: 0 });
        let modules_slice =
            unsafe { crate::fill_modules(&mut modules_buf, $r.modules, $r.modules_len) };
        let remote_user_str = unsafe { crate::cstr_ref($r.remote_user) };
        let $ctx = crate::RequestContext {
            method:      "GET",
            path:        &path_buf,
            query:       unsafe { crate::cstr_ref($r.query) },
            body:        &[],
            remote_user: if remote_user_str.is_empty() { None } else { Some(remote_user_str) },
            modules:     modules_slice,
        };
    };
}
pub(crate) use make_item_ctx;

/// Zero-allocation borrow of a C string. Lifetime is tied to the caller's scope.
/// Returns "" for null or invalid UTF-8.
pub(crate) unsafe fn cstr_ref<'a>(ptr: *const c_char) -> &'a str {
    if ptr.is_null() {
        return "";
    }
    unsafe { std::ffi::CStr::from_ptr(ptr) }.to_str().unwrap_or("")
}

pub(crate) unsafe fn fill_modules<'a>(
    buf: &'a mut [ModuleRef<'a>; 64],
    ptr: *const super::ModuleEntryFfi,
    len: usize,
) -> &'a [ModuleRef<'a>] {
    if ptr.is_null() || len == 0 {
        return &[];
    }
    let count = len.min(64);
    let entries = unsafe { std::slice::from_raw_parts(ptr, count) };
    for (i, e) in entries.iter().enumerate() {
        buf[i] = ModuleRef {
            id:    unsafe { cstr_ref(e.id) },
            title: unsafe { cstr_ref(e.title) },
            flags: e.flags,
        };
    }
    &buf[..count]
}
