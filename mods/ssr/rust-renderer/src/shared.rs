pub use ndc_dioxus_shared::*;

use std::ffi::c_char;

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

pub(crate) unsafe fn item_context_to_request<'a>(
    ictx: &'a super::ItemContext,
    path_buf: &'a str,
    modules_buf: &'a mut [ModuleRef<'a>; 64],
) -> RequestContext<'a> {
    let modules_slice = unsafe { fill_modules(modules_buf, ictx.modules, ictx.modules_len) };
    let remote_user_str = unsafe { cstr_ref(ictx.remote_user) };
    RequestContext {
        method:      "POST",
        path:        path_buf,
        query:       unsafe { cstr_ref(ictx.query) },
        body:        &[],
        remote_user: if remote_user_str.is_empty() { None } else { Some(remote_user_str) },
        modules:     modules_slice,
    }
}

pub(crate) unsafe fn song_item_to_view<'a>(p: &'a super::SongItemFfi) -> ndc_dioxus_shared::SongItem<'a> {
    ndc_dioxus_shared::SongItem {
        title:        unsafe { cstr_ref(p.title) },
        data:         unsafe { cstr_ref(p.data) },
        yt:           unsafe { cstr_ref(p.yt) },
        audio:        unsafe { cstr_ref(p.audio) },
        pdf:          unsafe { cstr_ref(p.pdf) },
        categories:   unsafe { cstr_ref(p.categories) },
        author:       unsafe { cstr_ref(p.author) },
        original_key: p.original_key,
        viewer_zoom:  p.viewer_zoom,
        show_media:   p.show_media,
        viewer_bemol: p.viewer_bemol,
        viewer_latin: p.viewer_latin,
        owner:        p.owner,
    }
}

pub(crate) unsafe fn poem_item_to_view<'a>(p: &'a super::PoemItemFfi) -> ndc_dioxus_shared::PoemItem<'a> {
    ndc_dioxus_shared::PoemItem {
        title:        unsafe { cstr_ref(p.title) },
        head_content: unsafe { cstr_ref(p.head_content) },
        body_content: unsafe { cstr_ref(p.body_content) },
        owner:        p.owner,
    }
}

