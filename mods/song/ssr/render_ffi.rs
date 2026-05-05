use std::ffi::c_char;

#[repr(C)]
pub struct SongDetailRenderFfi {
	pub title:        *const c_char,
	pub data:         *const c_char,
	pub yt:           *const c_char,
	pub audio:        *const c_char,
	pub pdf:          *const c_char,
	pub categories:   *const c_char,
	pub author:       *const c_char,
	pub original_key: i32,
	pub viewer_zoom:  i32,
	pub show_media:   bool,
	pub viewer_bemol: bool,
	pub viewer_latin: bool,
	pub owner:        bool,
	pub id:           *const c_char,
	pub query:        *const c_char,
	pub remote_user:  *const c_char,
	pub modules:      *const crate::ModuleEntryFfi,
	pub modules_len:  usize,
	pub csrf_token:   *const c_char,
}
