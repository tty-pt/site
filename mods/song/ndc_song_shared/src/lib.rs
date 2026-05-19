use dioxus::prelude::*;
use serde::Deserialize;
use ndc_dioxus_shared::key_transpose_options;

#[cfg(target_arch = "wasm32")]
use gloo_net::http::Request;

// ── Song Detail Page ─────────────────────────────────────────────────

#[derive(Clone, PartialEq)]
pub struct SongState {
    pub song_id: String,
    pub transpose: i32,
    pub use_bemol: bool,
    pub use_latin: bool,
    pub show_media: bool,
    pub yt: String,
    pub audio: String,
    pub pdf: String,
    pub chord_html: String,
    pub original_key: i32,
    pub save_url: String,
}

#[derive(Clone, PartialEq, Props)]
pub struct SongDetailPageProps {
    pub page_user: Option<String>,
    pub display_title: String,
    pub path: String,
    pub is_owner: bool,
    pub state: SongState,
    pub detail_body_html: String,
}

pub fn build_transpose_url(state: &SongState) -> String {
    let mut url = format!("/api/song/{}/transpose?t={}&h=1", state.song_id, state.transpose);
    if state.use_bemol { url.push_str("&b=1"); }
    if state.use_latin { url.push_str("&l=1"); }
    if state.show_media { url.push_str("&m=1"); }
    url
}

#[derive(Deserialize)]
pub struct TransposeResponse {
    pub data: String,
    #[serde(default)]
    pub show_media: Option<bool>,
    #[serde(default)]
    pub yt: Option<String>,
    #[serde(default)]
    pub audio: Option<String>,
    #[serde(default)]
    pub pdf: Option<String>,
}

pub fn apply_transpose(state: &mut SongState, resp: &TransposeResponse) {
    state.chord_html = resp.data.clone();
    if let Some(sm) = resp.show_media {
        state.show_media = sm;
    }
    if let Some(ref y) = resp.yt {
        state.yt.clone_from(y);
    }
    if let Some(ref a) = resp.audio {
        state.audio.clone_from(a);
    }
    if let Some(ref p) = resp.pdf {
        state.pdf.clone_from(p);
    }
}

#[cfg(target_arch = "wasm32")]
async fn fetch_transpose_url(url: &str) -> Option<TransposeResponse> {
    Request::get(url).send().await.ok()?.json::<TransposeResponse>().await.ok()
}

#[component]
pub fn SongDetailPage(props: SongDetailPageProps) -> Element {
    let sig = use_context_provider(|| Signal::new(props.state.clone()));

    let key_options;
    let bemol_val;
    let latin_val;
    let media_val;
    let is_owner_val;
    let chord;
    let show_media;
    let has_media;
    let yt;
    let audio;
    let pdf;
    let save_url;
    let song_id;

    {
        let s = sig.read();
        key_options = key_transpose_options(s.original_key, s.use_bemol, s.use_latin);
        bemol_val = if s.use_bemol { "1" } else { "0" };
        latin_val = if s.use_latin { "1" } else { "0" };
        media_val = if s.show_media { "1" } else { "0" };
        is_owner_val = if props.is_owner { "1" } else { "0" };
        chord = s.chord_html.clone();
        show_media = s.show_media;
        has_media = !s.yt.is_empty() || !s.audio.is_empty() || !s.pdf.is_empty();
        yt = s.yt.clone();
        audio = s.audio.clone();
        pdf = s.pdf.clone();
        save_url = s.save_url.clone();
        song_id = s.song_id.clone();
    }

    let detail_html = props.detail_body_html.as_str();

    let is_home = props.path == "/" || props.path.is_empty();
    let up_href = ndc_dioxus_shared::parent_path(&props.path);
    let login_link = ndc_dioxus_shared::login_href(&props.path);
    let register_href = ndc_dioxus_shared::auth_path("register");
    let edit_href = ndc_dioxus_shared::item_action_path("song", &song_id, "edit");
    let delete_href = ndc_dioxus_shared::item_action_path("song", &song_id, "delete");

    rsx! {
        main { class: "main",
            h1 { "{props.display_title}" }
            div { class: "center flex flex-col gap-4",
                div { id: "main",
                    "data-song-id": "{song_id}",
                    "data-transpose": "{sig.read().transpose}",
                    "data-use-bemol": "{bemol_val}",
                    "data-use-latin": "{latin_val}",
                    "data-show-media": "{media_val}",
                    "data-yt": "{yt}",
                    "data-audio": "{audio}",
                    "data-pdf": "{pdf}",
                    "data-chord-data": "{chord}",
                    "data-original-key": "{sig.read().original_key}",
                    "data-save-url": "{save_url}",
                    "data-detail-viewer-controls": "song",
                    "data-is-owner": "{is_owner_val}",
                    div { class: "flex flex-col gap-4 w-full max-w-xl",
                        div { class: "detail-viewer-scroll w-full max-w-xl", "data-detail-viewer-scroll": "1",
                            pre {
                                id: "chord-data",
                                "data-detail-viewer-target": "1",
                                class: "whitespace-pre-wrap font-mono p-4 rounded chord-data",
                                dangerous_inner_html: "{chord}"
                            }
                        }
                        if show_media && has_media {
                            div { id: "media-slot", class: "flex flex-col gap-4 w-full max-w-xl",
                                if !yt.is_empty() {
                                    div { id: "media-container", class: "flex flex-col gap-4 w-full max-w-xl",
                                        iframe {
                                            src: "https://www.youtube.com/embed/{yt}",
                                            class: "w-full aspect-video border-none",
                                            allowfullscreen: true
                                        }
                                    }
                                }
                                if !audio.is_empty() {
                                    div { id: "media-container", class: "flex flex-col gap-4 w-full max-w-xl",
                                        audio { controls: true, class: "w-full",
                                            source { src: "{audio}", r#type: "audio/mpeg" }
                                        }
                                    }
                                }
                                if !pdf.is_empty() {
                                    div { id: "media-container", class: "flex flex-col gap-4 w-full max-w-xl",
                                        a { href: "{pdf}", target: "_blank", rel: "noopener", class: "text-blue-600", "View PDF" }
                                    }
                                }
                            }
                        }
                    }
                }
                div { id: "song-detail-body", class: "contents", "data-detail-viewer-scope": "1",
                    dangerous_inner_html: "{detail_html}"
                }
            }
        }
        nav { class: "menu",
            input { id: "menu-functions", name: "functions", r#type: "checkbox", class: "hidden" }
            label { r#for: "menu-functions", class: "menu-overlay", aria_label: "Close Menu" }
            span { class: "functions flex-1 fixed right-0 z-50 h-full overflow-y-auto text-sm capitalize flex flex-col p-4",
                div { class: "relative z-20 flex flex-col gap-2",
                    if !is_home {
                        a { class: "btn", href: "{up_href}",
                            span { "\u{1f3b8}" }
                            span { "go up" }
                        }
                    }
                    if let Some(ref user) = props.page_user {
                        a { class: "btn", href: "/{user}/",
                            span { "\u{1f60a}" }
                            span { "me" }
                        }
                        a { class: "btn", href: "/auth/logout",
                            span { "\u{1f6aa}" }
                            span { "logout" }
                        }
                    } else {
                        a { class: "btn", href: "{login_link}",
                            span { "🔑" }
                            span { "login" }
                        }
                        a { class: "btn", href: "{register_href}",
                            span { "📝" }
                            span { "register" }
                        }
                    }
                    div { class: "menu-separator" }
                    div { class: "module-menu",
                        form {
                            id: "transpose-form",
                            method: "GET",
                            action: "{props.path}",
                            "data-song-id": "{song_id}",
                            label {
                                "Key:"
                                select { name: "t",
                                    value: "{sig.read().transpose}",
                                    onchange: move |evt| {
                                        #[cfg(target_arch = "wasm32")] {
                                            let val: i32 = evt.value().parse().unwrap_or(0);
                                            let mut s = sig.clone();
                                            spawn(async move {
                                                s.write().transpose = val;
                                                let url = { let r = s.read(); build_transpose_url(&r) };
                                                if let Some(resp) = fetch_transpose_url(&url).await {
                                                    apply_transpose(&mut *s.write(), &resp);
                                                }
                                            });
                                        }
                                        #[cfg(not(target_arch = "wasm32"))] {
                                            let _ = evt;
                                        }
                                    },
                                    for (semitones, option_label) in &key_options {
                                        option {
                                            value: "{semitones}",
                                            selected: *semitones == sig.read().transpose,
                                            "{option_label}"
                                        }
                                    }
                                }
                            }
                            label {
                                input { r#type: "checkbox", name: "b", checked: sig.read().use_bemol, oninput: move |evt| {
                                    #[cfg(target_arch = "wasm32")] {
                                        let checked = evt.checked();
                                        let mut s = sig.clone();
                                        spawn(async move {
                                            s.write().use_bemol = checked;
                                            let url = { let r = s.read(); build_transpose_url(&r) };
                                            if let Some(resp) = fetch_transpose_url(&url).await {
                                                apply_transpose(&mut *s.write(), &resp);
                                            }
                                        });
                                    }
                                    #[cfg(not(target_arch = "wasm32"))] {
                                        let _ = evt;
                                    }
                                } }
                                span { "Flats (\u{266d})" }
                            }
                            label {
                                input { r#type: "checkbox", name: "l", checked: sig.read().use_latin, oninput: move |evt| {
                                    #[cfg(target_arch = "wasm32")] {
                                        let checked = evt.checked();
                                        let mut s = sig.clone();
                                        spawn(async move {
                                            s.write().use_latin = checked;
                                            let url = { let r = s.read(); build_transpose_url(&r) };
                                            if let Some(resp) = fetch_transpose_url(&url).await {
                                                apply_transpose(&mut *s.write(), &resp);
                                            }
                                            let (sv, lv, bv) = {
                                                let r = s.read();
                                                (r.save_url.clone(), if r.use_latin { "1" } else { "0" }, if r.use_bemol { "1" } else { "0" })
                                            };
                                            if !sv.is_empty() {
                                                if let Ok(req) = Request::post(&sv)
                                                    .header("Content-Type", "application/x-www-form-urlencoded")
                                                    .body(format!("v=100&b={bv}&l={lv}"))
                                                {
                                                    let _ = req.send().await;
                                                }
                                            }
                                        });
                                    }
                                    #[cfg(not(target_arch = "wasm32"))] {
                                        let _ = evt;
                                    }
                                } }
                                span { "Latin" }
                            }
                            label {
                                input { r#type: "checkbox", name: "m", checked: sig.read().show_media, oninput: move |evt| {
                                    #[cfg(target_arch = "wasm32")] {
                                        let checked = evt.checked();
                                        let mut s = sig.clone();
                                        spawn(async move {
                                            s.write().show_media = checked;
                                            let url = { let r = s.read(); build_transpose_url(&r) };
                                            if let Some(resp) = fetch_transpose_url(&url).await {
                                                apply_transpose(&mut *s.write(), &resp);
                                            }
                                        });
                                    }
                                    #[cfg(not(target_arch = "wasm32"))] {
                                        let _ = evt;
                                    }
                                } }
                                span { "Video" }
                            }
                            button { r#type: "submit", class: "btn", "Apply" }
                        }
                        div {
                            class: "viewer-controls",
                            "data-detail-viewer-controls": "song",
                            "data-detail-viewer-save-url": "{save_url}",
                            label {
                                "Zoom"
                                input {
                                    r#type: "range",
                                    min: "70",
                                    max: "170",
                                    step: "10",
                                    value: "100",
                                    "data-detail-viewer-zoom": "1"
                                }
                            }
                            p { class: "text-xs text-muted", "data-detail-viewer-zoom-label": "1", "100%" }
                            label {
                                input {
                                    r#type: "checkbox",
                                    checked: true,
                                    "data-detail-viewer-wrap": "1"
                                }
                                span { "Wrap lines" }
                            }
                        }
                        if props.is_owner {
                            a { href: "{edit_href}", class: "btn",
                                span { "✏️" }
                                span { "edit" }
                            }
                            a { href: "{delete_href}", class: "btn",
                                span { "🗑️" }
                                span { "delete" }
                            }
                        }
                    }
                }
                label { r#for: "menu-functions", class: "absolute inset-0 z-10 cursor-pointer", aria_label: "Close Menu" }
            }
            span { class: "fixed top-0 right-0 z-30 p-2 flex items-center gap-4",
                label { r#for: "menu-functions", class: "menu-toggle flex items-center justify-center cursor-pointer text-base btn", aria_label: "Menu", "data-menu-toggle": "1", "⚙️" }
            }
        }
    }
}
