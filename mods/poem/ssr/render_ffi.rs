use std::ffi::c_char;

/// Reused for both poem detail and poem edit.
#[repr(C)]
pub struct PoemRenderFfi {
	pub title:        *const c_char,
	pub head_content: *const c_char,
	pub body_content: *const c_char,
	pub owner:        bool,
	pub id:           *const c_char,
	pub query:        *const c_char,
	pub remote_user:  *const c_char,
	pub modules:      *const crate::ModuleEntryFfi,
	pub modules_len:  usize,
	pub csrf_token:   *const c_char,
}
