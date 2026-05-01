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
		let id_s = unsafe { crate::cstr_ref(r.id) };
		let path_buf = std::format!("/song/{}", id_s);
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
		super::render_detail(&view, id_s, &ctx)
	})
}
