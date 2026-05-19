pub use ndc_dioxus_shared::*;

use crate::{CStr, c_char};

pub(crate) unsafe fn cstr_ref<'a>(ptr: *const c_char) -> &'a str {
    if ptr.is_null() {
        return "";
    }
    unsafe { CStr::from_ptr(ptr) }.to_str().unwrap_or("")
}
