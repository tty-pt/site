use std::ffi::{CString, c_char, c_int};
use crate::load_dataset_source;

use ndx::prelude::*;

#[ndx_listener]
pub unsafe fn ssr_dataset_get_json(
    _fd: c_int,
    dataset_id: *const c_char,
    _include: *const c_char,
    out_json: *mut *mut c_char,
) -> c_int {
    let dataset_id_str = unsafe { std::ffi::CStr::from_ptr(dataset_id) }.to_string_lossy();
    
    if let Some(source) = load_dataset_source(&dataset_id_str) {
        if let Ok(json) = serde_json::to_string(&source) {
            let c_json = CString::new(json).unwrap();
            unsafe {
                *out_json = libc::strdup(c_json.as_ptr());
            }
            return 0;
        }
    }
    
    404
}

#[ndx_listener]
pub unsafe fn ssr_dataset_get_item_json(
    _fd: c_int,
    dataset_id: *const c_char,
    id: *const c_char,
    out_json: *mut *mut c_char,
) -> c_int {
    let dataset_id_str = unsafe { std::ffi::CStr::from_ptr(dataset_id) }.to_string_lossy();
    let id_str = unsafe { std::ffi::CStr::from_ptr(id) }.to_string_lossy();
    
    if let Some(source) = load_dataset_source(&dataset_id_str) {
        let model_id = dataset_id_str.strip_suffix(".items").unwrap_or(&dataset_id_str);
        if let Some(result) = source.get(model_id) {
            let rows = result.rows();
            if let Some(row) = rows.iter().find(|r: &&hyle::Row| {
                r.get("id").map_or(false, |v| {
                    if let hyle::Value::String(s) = v {
                        s == id_str.as_ref()
                    } else {
                        false
                    }
                })
            }) {
                if let Ok(json) = serde_json::to_string(&row) {
                    let c_json = CString::new(json).unwrap();
                    unsafe {
                        *out_json = libc::strdup(c_json.as_ptr());
                    }
                    return 0;
                }
            } else {
                eprintln!("ssr_dataset_get_item_json: item {} NOT FOUND in {} rows of {}", id_str, rows.len(), model_id);
            }
        } else {
            eprintln!("ssr_dataset_get_item_json: model {} NOT FOUND in source keys: {:?}", model_id, source.keys());
        }
    } else {
        eprintln!("ssr_dataset_get_item_json: failed to load source for {}", dataset_id_str);
    }
    
    404
}
