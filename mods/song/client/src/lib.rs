use dioxus::prelude::VirtualDom;
use dioxus_web::launch::launch_virtual_dom;
use wasm_bindgen::prelude::*;
use wasm_bindgen::JsCast;
use web_sys::{Document, Window};

fn window() -> Option<Window> {
	web_sys::window()
}

fn document() -> Option<Document> {
	window().and_then(|w| w.document())
}

fn get_mount(id: &str) -> Option<web_sys::Element> {
	document().and_then(|d| d.get_element_by_id(id))
}

fn get_current_user() -> Option<String> {
	let links = document()?.query_selector_all("nav a").ok()?;
	let mut i: u32 = 0;

	while i < links.length() {
		let node = links.item(i)?;
		let el: web_sys::Element = node.dyn_into().ok()?;
		let href = el.get_attribute("href")?;
		let text = el.text_content().unwrap_or_default();

		if href.starts_with("/auth/") {
			i += 1;
			continue;
		}

		if text.contains("me") {
			let user = href.trim_matches('/').trim_end_matches('/');
			if !user.is_empty() {
				return Some(user.to_string());
			}
		}

		i += 1;
	}

	None
}

fn get_page_title() -> String {
	document()
		.and_then(|d| d.query_selector("main h1").ok().flatten())
		.and_then(|el| el.text_content())
		.unwrap_or_default()
}

fn get_pathname() -> String {
	window()
		.and_then(|w| w.location().pathname().ok())
		.unwrap_or_else(|| "/".to_string())
}

fn read_song_state_from_element(mount: &web_sys::Element) -> ndc_song_shared::SongState {
	ndc_song_shared::SongState {
		song_id: mount.get_attribute("data-song-id").unwrap_or_default(),
		transpose: mount.get_attribute("data-transpose").and_then(|v| v.parse().ok()).unwrap_or(0),
		use_bemol: mount.get_attribute("data-use-bemol").as_deref() == Some("1"),
		use_latin: mount.get_attribute("data-use-latin").as_deref() == Some("1"),
		show_media: mount.get_attribute("data-show-media").as_deref() == Some("1"),
		yt: mount.get_attribute("data-yt").unwrap_or_default(),
		audio: mount.get_attribute("data-audio").unwrap_or_default(),
		pdf: mount.get_attribute("data-pdf").unwrap_or_default(),
		chord_html: mount.get_attribute("data-chord-data").unwrap_or_default(),
		original_key: mount.get_attribute("data-original-key").and_then(|v| v.parse().ok()).unwrap_or(0),
		save_url: mount.get_attribute("data-save-url").unwrap_or_default(),
	}
}

fn launch() {
	web_sys::console::log_1(&"song launch called".into());

	if let Some(mount) = get_mount("main") {
		web_sys::console::log_1(&"main found, launching Dioxus".into());
		let page_user = get_current_user();
		let title = get_page_title();
		let path = get_pathname();
		let is_owner = mount.get_attribute("data-is-owner").as_deref() == Some("1");
		let detail_body_html = get_mount("song-detail-body")
			.map(|el| el.inner_html())
			.unwrap_or_default();
		let state = read_song_state_from_element(&mount);

		let props = ndc_song_shared::SongDetailPageProps {
			page_user,
			display_title: title,
			path,
			is_owner,
			state,
			detail_body_html,
		};

		let body = document()
			.and_then(|d| d.body())
			.expect("body element");
		let vdom = VirtualDom::new_with_props(ndc_song_shared::SongDetailPage, props);
		let cfg = dioxus::web::Config::new().hydrate(true).rootelement(body.into());
		launch_virtual_dom(vdom, cfg);
	} else {
		web_sys::console::log_1(&"mount #main NOT found".into());
	}
}

#[wasm_bindgen(start)]
pub fn main() {
	web_sys::console::log_1(&"song main called".into());
	launch();
}
