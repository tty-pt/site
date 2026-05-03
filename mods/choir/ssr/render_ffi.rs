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
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_choir_detail_ffi(
	req: *const ChoirDetailRenderFfi,
) -> crate::RenderResult {
	crate::dispatch_item(req, "ssr_render_choir_detail_ffi", |r| {
		crate::make_item_ctx!(r, "choir", id_s, ctx);
		let ffi_songs: &[ChoirSongFfi] = if r.songs.is_null() || r.songs_len == 0 {
			&[]
		} else {
			unsafe { std::slice::from_raw_parts(r.songs, r.songs_len) }
		};
		let ffi_all_songs: &[ChoirEntryFfi] = if r.all_songs.is_null() || r.all_songs_len == 0 {
			&[]
		} else {
			unsafe { std::slice::from_raw_parts(r.all_songs, r.all_songs_len) }
		};
		let ffi_songbooks: &[ChoirEntryFfi] = if r.songbooks.is_null() || r.songbooks_len == 0 {
			&[]
		} else {
			unsafe { std::slice::from_raw_parts(r.songbooks, r.songbooks_len) }
		};
		let view = crate::ChoirItem {
			title:      unsafe { crate::cstr_ref(r.title) },
			owner_name: unsafe { crate::cstr_ref(r.owner_name) },
			formats:    unsafe { crate::cstr_ref(r.formats) },
			songs: ffi_songs.iter().map(|s| crate::ChoirSong {
				id:            unsafe { crate::cstr_ref(s.id) },
				title:         unsafe { crate::cstr_ref(s.title) },
				format:        unsafe { crate::cstr_ref(s.format) },
				preferred_key: s.preferred_key,
				original_key:  s.original_key,
			}).collect(),
			all_songs: ffi_all_songs.iter().map(|e| crate::ChoirEntry {
				id:    unsafe { crate::cstr_ref(e.id) },
				title: unsafe { crate::cstr_ref(e.title) },
			}).collect(),
			songbooks: ffi_songbooks.iter().map(|e| crate::ChoirEntry {
				id:    unsafe { crate::cstr_ref(e.id) },
				title: unsafe { crate::cstr_ref(e.title) },
			}).collect(),
		};
		crate::choir::render_detail(&view, id_s, &ctx)
	})
}
