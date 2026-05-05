pub use ndc_dioxus_shared::*;

use std::ffi::c_char;

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
