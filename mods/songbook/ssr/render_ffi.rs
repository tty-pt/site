use std::ffi::c_char;

#[repr(C)]
pub struct SongbookSongFfi {
	pub chord_id:     *const c_char,
	pub format:       *const c_char,
	pub chord_title:  *const c_char,
	pub chord_data:   *const c_char,
	pub transpose:    i32,
	pub original_key: i32,
}

#[repr(C)]
pub struct SongbookDetailRenderFfi {
	pub sb_title:    *const c_char,
	pub owner:       *const c_char,
	pub choir:       *const c_char,
	pub viewer_zoom: i32,
	pub songs:       *const SongbookSongFfi,
	pub songs_len:   usize,
	pub id:          *const c_char,
	pub query:       *const c_char,
	pub remote_user: *const c_char,
	pub modules:     *const crate::ModuleEntryFfi,
	pub modules_len: usize,
	pub csrf_token:  *const c_char,
}
