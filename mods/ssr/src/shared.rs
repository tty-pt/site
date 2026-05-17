pub use ndc_dioxus_shared::*;

use std::ffi::c_char;

pub(crate) unsafe fn cstr_ref<'a>(ptr: *const c_char) -> &'a str {
    if ptr.is_null() {
        return "";
    }
    unsafe { std::ffi::CStr::from_ptr(ptr) }.to_str().unwrap_or("")
}
