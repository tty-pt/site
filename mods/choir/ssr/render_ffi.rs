use std::ffi::c_char;

#[repr(C)]
pub struct ChoirSongFfi {
	pub id:            *const c_char,
	pub title:         *const c_char,
	pub format:        *const c_char,
	pub preferred_key: i32,
	pub original_key:  i32,
}

#[repr(C)]
pub struct ChoirEntryFfi {
	pub id:    *const c_char,
	pub title: *const c_char,
}

#[repr(C)]
pub struct ChoirDetailRenderFfi {
	pub title:         *const c_char,
	pub owner_name:    *const c_char,
	pub formats:       *const c_char,
	pub songs:         *const ChoirSongFfi,
	pub songs_len:     usize,
	pub all_songs:     *const ChoirEntryFfi,
	pub all_songs_len: usize,
	pub songbooks:     *const ChoirEntryFfi,
	pub songbooks_len: usize,
	pub id:            *const c_char,
	pub query:         *const c_char,
	pub remote_user:   *const c_char,
	pub modules:       *const crate::ModuleEntryFfi,
	pub modules_len:   usize,
	pub csrf_token:    *const c_char,
}
