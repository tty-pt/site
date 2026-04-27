/**
 * Client-side progressive enhancement for the site
 * Provides interactive features that degrade gracefully when JS is disabled
 */

(function() {
  'use strict';

  function initTransposeForm() {
    const transposeForm = document.getElementById('transpose-form');
    if (!transposeForm) return;

    const chordData = document.getElementById('chord-data');
    const mediaSlot = document.getElementById('media-slot');
    if (!chordData) return;

    const submitButton = transposeForm.querySelector('button[type="submit"]');
    if (submitButton instanceof HTMLButtonElement) {
      submitButton.hidden = true;
      submitButton.setAttribute('aria-hidden', 'true');
      submitButton.tabIndex = -1;
    }

    async function submitWithFetch() {
      const formData = new FormData(transposeForm);
      const params = new URLSearchParams(formData);
      const actionUrl = new URL(transposeForm.action, window.location.origin);
      const fetchUrl = `${actionUrl.pathname}?${params}`;

      try {
        chordData.style.opacity = '0.5';
        if (submitButton) submitButton.disabled = true;

        const response = await fetch(fetchUrl, { headers: { 'X-Requested-With': 'app.js' } });
        if (!response.ok) throw new Error(`HTTP error! status: ${response.status}`);

        const html = await response.text();
        const parser = new DOMParser();
        const doc = parser.parseFromString(html, 'text/html');
        const newChordData = doc.getElementById('chord-data');
        const newMediaSlot = doc.getElementById('media-slot');

        if (!newChordData) throw new Error('Could not find chord data in response');

        chordData.innerHTML = newChordData.innerHTML;
        if (mediaSlot && newMediaSlot) {
          mediaSlot.innerHTML = newMediaSlot.innerHTML;
        }
        window.history.pushState({ path: fetchUrl }, '', fetchUrl);

        const newTitle = doc.querySelector('h1');
        const currentTitle = document.querySelector('h1');
        if (newTitle && currentTitle) currentTitle.textContent = newTitle.textContent;
      } catch (error) {
        console.error('Error fetching transposed content:', error);
        transposeForm.requestSubmit();
      } finally {
        chordData.style.opacity = '1';
        if (submitButton) submitButton.disabled = false;
      }
    }

    transposeForm.addEventListener('submit', function(e) {
      e.preventDefault();
      submitWithFetch();
    });

    transposeForm.addEventListener('change', function(e) {
      const target = e.target;
      if (!(target instanceof HTMLInputElement || target instanceof HTMLSelectElement)) return;
      submitWithFetch();
    });

    window.addEventListener('popstate', function(e) {
      if (e.state && e.state.path) window.location.reload();
    });
  }

  function initAutoSubmitForms() {
    const forms = document.querySelectorAll('form[data-autosubmit="change"]');
    for (const form of forms) {
      form.addEventListener('change', function(e) {
        const target = e.target;
        if (!(target instanceof HTMLInputElement || target instanceof HTMLSelectElement || target instanceof HTMLTextAreaElement)) {
          return;
        }
        form.requestSubmit();
      });
    }
  }

  function updateSongbookDatalist(formatInput) {
    const listId = formatInput.dataset.songbookSongList;
    if (!listId) return;
    const datalist = document.getElementById(listId);
    if (!(datalist instanceof HTMLDataListElement)) return;

    let source = datalist._originalOptions;
    if (!source) {
      source = Array.from(datalist.querySelectorAll('option')).map((option) => ({
        value: option.value,
        type: option.dataset.chordType || '',
      }));
      datalist._originalOptions = source;
    }

    const selectedFormat = formatInput.value.trim();
    const hasTypedMatches = selectedFormat && source.some((option) => option.type === selectedFormat);
    const filtered = hasTypedMatches
      ? source.filter((option) => option.type === selectedFormat)
      : source;

    datalist.textContent = '';
    for (const option of filtered) {
      const element = document.createElement('option');
      element.value = option.value;
      if (option.type) element.dataset.chordType = option.type;
      datalist.appendChild(element);
    }
  }

  function initSongbookEditRows() {
    const formatInputs = document.querySelectorAll('input[data-songbook-format-input]');
    for (const input of formatInputs) {
      updateSongbookDatalist(input);
      input.addEventListener('input', function() {
        updateSongbookDatalist(input);
      });
    }
  }

  async function enhanceSongbookTranspose(form) {
    const songId = form.dataset.songId;
    const row = form.closest('[data-songbook-item="1"]');
    if (!songId || !row) {
      form.requestSubmit();
      return;
    }

    const chordData = row.querySelector('[data-songbook-chord-data="1"]');
    const targetKey = row.querySelector('[data-songbook-target-key="1"]');
    const select = form.querySelector('select[name="t"]');
    const submitData = new FormData(form);
    const params = new URLSearchParams();
    if (select instanceof HTMLSelectElement) {
      params.set('t', select.value);
      params.set('h', '1');
    }

    try {
      if (chordData) chordData.style.opacity = '0.5';

      const saveResponse = await fetch(form.action, {
        method: 'POST',
        body: submitData,
        headers: { 'X-Requested-With': 'app.js' },
      });
      if (!saveResponse.ok) throw new Error(`HTTP error! status: ${saveResponse.status}`);

      const transposeResponse = await fetch(`/api/song/${encodeURIComponent(songId)}/transpose?${params.toString()}`, {
        headers: { 'X-Requested-With': 'app.js' },
      });
      if (!transposeResponse.ok) throw new Error(`HTTP error! status: ${transposeResponse.status}`);

      const json = await transposeResponse.json();
      if (chordData && typeof json.data === 'string') {
        chordData.innerHTML = json.data;
      }
      if (targetKey && select instanceof HTMLSelectElement) {
        const selected = select.selectedOptions[0];
        targetKey.textContent = selected ? selected.textContent.replace(' (Original)', '') : targetKey.textContent;
      }
    } catch (error) {
      console.error('Error updating songbook transpose:', error);
      form.requestSubmit();
    } finally {
      if (chordData) chordData.style.opacity = '1';
    }
  }

  function initSongbookDetail() {
    const forms = document.querySelectorAll('form[data-songbook-transpose-form="1"]');
    for (const form of forms) {
      const select = form.querySelector('select[name="t"]');
      if (!(select instanceof HTMLSelectElement)) continue;
      select.addEventListener('change', function() {
        enhanceSongbookTranspose(form);
      });
    }
  }

  function init() {
    initTransposeForm();
    initAutoSubmitForms();
    initSongbookEditRows();
    initSongbookDetail();
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
