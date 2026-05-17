use std::ffi::c_char;

#[repr(C)]
pub struct DatasetFieldFfi {
    pub name: *const c_char,
    pub file: *const c_char,
    pub field_type: u32,
    pub writable: i32,
    pub target_dataset: *const c_char,
    pub inverse_name: *const c_char,
}

#[repr(C)]
pub struct DatasetDefFfi {
    pub id: *const c_char,
    pub key_field: *const c_char,
    pub items_path: *const c_char,
    pub access_policy: u32,
    pub fields: *const DatasetFieldFfi,
    pub field_count: usize,
    pub source_hd: u32,
    pub user: *mut std::ffi::c_void,
}

use ndx::prelude::*;
use crate::NDX;

#[ndx_hook_decl]
pub fn dataset_find(id: *const c_char) -> *const DatasetDefFfi {}

pub fn find_dataset_def(id: &str) -> Option<&'static DatasetDefFfi> {
    let id_c = std::ffi::CString::new(id).ok()?;
    let ptr = unsafe { dataset_find(id_c.as_ptr()) };
    if ptr.is_null() {
        None
    } else {
        Some(unsafe { &*ptr })
    }
}
