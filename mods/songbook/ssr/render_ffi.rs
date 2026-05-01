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
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_songbook_detail_ffi(
	req: *const SongbookDetailRenderFfi,
) -> crate::RenderResult {
	crate::dispatch_item(req, "ssr_render_songbook_detail_ffi", |r| {
		let id_s = unsafe { crate::cstr_ref(r.id) };
		let path_buf = std::format!("/songbook/{}", id_s);
		let mut modules_buf: [crate::ModuleRef<'_>; 64] =
			std::array::from_fn(|_| crate::ModuleRef { id: "", title: "", flags: 0 });
		let modules_slice = unsafe { crate::fill_modules(&mut modules_buf, r.modules, r.modules_len) };
		let remote_user_str = unsafe { crate::cstr_ref(r.remote_user) };
		let ctx = crate::RequestContext {
			method:      "GET",
			path:        &path_buf,
			query:       unsafe { crate::cstr_ref(r.query) },
			body:        &[],
			remote_user: if remote_user_str.is_empty() { None } else { Some(remote_user_str) },
			modules:     modules_slice,
		};
		let ffi_songs: &[SongbookSongFfi] = if r.songs.is_null() || r.songs_len == 0 {
			&[]
		} else {
			unsafe { std::slice::from_raw_parts(r.songs, r.songs_len) }
		};
		let view = crate::SongbookItem {
			title:       unsafe { crate::cstr_ref(r.sb_title) },
			owner:       unsafe { crate::cstr_ref(r.owner) },
			choir:       unsafe { crate::cstr_ref(r.choir) },
			viewer_zoom: r.viewer_zoom,
			songs: ffi_songs.iter().map(|s| crate::SongbookSong {
				chord_id:     unsafe { crate::cstr_ref(s.chord_id) },
				format:       unsafe { crate::cstr_ref(s.format) },
				chord_title:  unsafe { crate::cstr_ref(s.chord_title) },
				chord_data:   unsafe { crate::cstr_ref(s.chord_data) },
				transpose:    s.transpose,
				original_key: s.original_key,
			}).collect(),
		};
		super::render_detail(&view, id_s, &ctx)
	})
}
