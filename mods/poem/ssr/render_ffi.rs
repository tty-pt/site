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
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_poem_detail_ffi(
	req: *const PoemRenderFfi,
) -> crate::RenderResult {
	crate::dispatch_item(req, "ssr_render_poem_detail_ffi", |r| {
		let id_s = unsafe { crate::cstr_ref(r.id) };
		let path_buf = std::format!("/poem/{}", id_s);
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
		let view = crate::PoemItem {
			title:        unsafe { crate::cstr_ref(r.title) },
			head_content: unsafe { crate::cstr_ref(r.head_content) },
			body_content: unsafe { crate::cstr_ref(r.body_content) },
			owner:        r.owner,
		};
		super::render_detail(&view, id_s, &ctx)
	})
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_poem_edit_ffi(
	req: *const PoemRenderFfi,
) -> crate::RenderResult {
	crate::dispatch_item(req, "ssr_render_poem_edit_ffi", |r| {
		let id_s = unsafe { crate::cstr_ref(r.id) };
		let path_buf = std::format!("/poem/{}", id_s);
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
		let view = crate::PoemItem {
			title:        unsafe { crate::cstr_ref(r.title) },
			head_content: unsafe { crate::cstr_ref(r.head_content) },
			body_content: unsafe { crate::cstr_ref(r.body_content) },
			owner:        r.owner,
		};
		super::render_edit_typed(&view, id_s, &ctx)
	})
}
