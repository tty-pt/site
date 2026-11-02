/**
 * Creates standard isomorphic site page layout scaffolding
 */
export function createPageLayout({ title = "Site", activeNav = "home", theme = "auto" } = {}) {
  return {
    chromeRoot: "#chrome-root",
    budRoot: "#bud-root",
    meta: { title, activeNav, theme },
    renderHtml: () => `
<div id="site-wrapper" data-theme="${theme}">
  <header id="chrome-root"></header>
  <main id="bud-root"></main>
  <footer id="site-footer"></footer>
</div>`.trim()
  };
}

/**
 * Creates audio/media player UI control state
 */
export function createMediaControls({ mediaUrl = "", duration = 0 } = {}) {
  return {
    url: mediaUrl,
    duration,
    isPlaying: false,
    currentTime: 0,
    play() {
      this.isPlaying = true;
    },
    pause() {
      this.isPlaying = false;
    },
    seek(time) {
      this.currentTime = Math.max(0, Math.min(time, this.duration));
    }
  };
}

/**
 * Creates form action buttons (Submit / Cancel / Extra actions) mirroring site_forms.c
 */
export function createFormActions({ cancelHref = "", submitLabel = "Submit", extraHtml = "" } = {}) {
  return {
    cancelHref,
    submitLabel,
    extraHtml,
    renderHtml: () => `
<div class="flex gap-2">
  <button type="submit" class="btn btn-primary">${submitLabel}</button>
  ${extraHtml}
  <a href="${cancelHref}" class="btn btn-secondary">Cancel</a>
</div>`.trim()
  };
}

/**
 * Creates delete confirmation modal / form scaffolding mirroring site_forms.c
 */
export function createDeleteConfirmForm({ module = "", id = "", title = "", csrfToken = "" } = {}) {
  const actionPath = `/${module}/${id}/delete`;
  const cancelPath = `/${module}/${id}`;
  return {
    actionPath,
    cancelPath,
    title,
    csrfToken,
    renderHtml: () => `
<form method="POST" action="${actionPath}" class="delete-confirm-form">
  <input type="hidden" name="csrf_token" value="${csrfToken}" />
  <p>Are you sure you want to delete <strong>${title}</strong>?</p>
  <div class="flex gap-2">
    <button type="submit" class="btn btn-danger">Confirm Delete</button>
    <a href="${cancelPath}" class="btn btn-secondary">Cancel</a>
  </div>
</form>`.trim()
  };
}

export function version() {
  return "0.1.0";
}
