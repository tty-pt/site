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
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_song_detail_ffi(
	req: *const SongDetailRenderFfi,
) -> crate::RenderResult {
	crate::dispatch_item(req, "ssr_render_song_detail_ffi", |r| {
		crate::make_item_ctx!(r, "song", id_s, ctx);
		let view = crate::SongItem {
			title:        unsafe { crate::cstr_ref(r.title) },
			data:         unsafe { crate::cstr_ref(r.data) },
			yt:           unsafe { crate::cstr_ref(r.yt) },
			audio:        unsafe { crate::cstr_ref(r.audio) },
			pdf:          unsafe { crate::cstr_ref(r.pdf) },
			categories:   unsafe { crate::cstr_ref(r.categories) },
			author:       unsafe { crate::cstr_ref(r.author) },
			original_key: r.original_key,
			viewer_zoom:  r.viewer_zoom,
			show_media:   r.show_media,
			viewer_bemol: r.viewer_bemol,
			viewer_latin: r.viewer_latin,
			owner:        r.owner,
		};
		crate::song::render_detail(&view, id_s, &ctx)
	})
}
