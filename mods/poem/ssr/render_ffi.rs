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

fn dispatch_poem<F>(req: *const PoemRenderFfi, name: &str, render_fn: F) -> crate::RenderResult
where
	F: Fn(&crate::PoemItem<'_>, &str, &crate::RequestContext<'_>) -> crate::ResponsePayload,
{
	crate::dispatch_item(req, name, |r| {
		crate::make_item_ctx!(r, "poem", id_s, ctx);
		let view = crate::PoemItem {
			title:        unsafe { crate::cstr_ref(r.title) },
			head_content: unsafe { crate::cstr_ref(r.head_content) },
			body_content: unsafe { crate::cstr_ref(r.body_content) },
			owner:        r.owner,
		};
		render_fn(&view, id_s, &ctx)
	})
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_poem_detail_ffi(req: *const PoemRenderFfi) -> crate::RenderResult {
	dispatch_poem(req, "ssr_render_poem_detail_ffi", crate::poem::render_detail)
}

#[unsafe(no_mangle)]
pub extern "C" fn ssr_render_poem_edit_ffi(req: *const PoemRenderFfi) -> crate::RenderResult {
	dispatch_poem(req, "ssr_render_poem_edit_ffi", crate::poem::render_edit_typed)
}
