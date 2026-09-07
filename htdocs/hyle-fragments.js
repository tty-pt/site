/*
 * hyle-fragments.js — generic partial-refresh transport.
 *
 * Zero picker knowledge: adopts containers carrying
 * data-hyle-frag-url, intercepts their native GET controls, and swaps
 * [data-hyle-slot] targets from {"slots":...} envelopes. Infinite
 * scroll appends {"rows":...} chunks at [data-hyle-frag-sentinel].
 * Self-gating: does nothing without fetch + IntersectionObserver.
 * See docs/SSR-CONTRACT.md and OMNI-DROPDOWN.md §1.4.
 */
(function () {
	'use strict';

	if (!window.fetch || !window.IntersectionObserver)
		return;

	document.documentElement.classList.add('hyle-frag-active');

	var MAX_APPENDS = 10;
	var DEBOUNCE_MS = 250;

	function sub(tmpl, q, page, sel) {
		return tmpl
			.replace('{q}', encodeURIComponent(q || ''))
			.replace('{page}', String(page))
			.replace('{sel}', encodeURIComponent(sel || ''));
	}

	function state(root) {
		if (!root.__frag)
			root.__frag = { seq: 0, busy: false, eof: false,
				cursor: 0, appends: 0, io: null, timer: null };
		return root.__frag;
	}

	function checkedSlugs(root) {
		var out = [], i;
		var inputs = root.querySelectorAll(
			'.hyle-picker-rows input:checked');
		for (i = 0; i < inputs.length; i++)
			out.push(inputs[i].value);
		return out.join(',');
	}

	function labelOf(input) {
		var l = input.closest('label');
		return (l && l.textContent.trim()) || input.value;
	}

	function syncSummary(root) {
		var slot = root.querySelector('[data-hyle-slot="values"]');
		var inputs = root.querySelectorAll(
			'.hyle-picker-rows input:checked');
		var labels = [], i;
		for (i = 0; i < inputs.length; i++)
			labels.push(labelOf(inputs[i]));
		slot.textContent = labels.join('; ');
	}

	function swapSlot(root, name, html) {
		var slot = root.querySelector(
			'[data-hyle-slot="' + name + '"]');
		if (!slot) return null;
		var tpl = document.createElement('template');
		tpl.innerHTML = html.trim();
		var next = tpl.content.firstElementChild;
		if (!next) return null;

		var oldSearch = (name === 'panel') ? searchOf(slot) : null;
		var newSearch = (name === 'panel') ? searchOf(next) : null;

		if (oldSearch && newSearch) {
			newSearch.remove();
			var newAdd = next.querySelector('.hyle-picker-add');
			if (newAdd) newAdd.remove();
			var oldAdd = slot.querySelector('.hyle-picker-add');
			if (oldAdd) oldAdd.remove();

			while (oldSearch.previousSibling)
				slot.removeChild(oldSearch.previousSibling);
			while (oldSearch.nextSibling)
				slot.removeChild(oldSearch.nextSibling);
			while (next.firstChild)
				slot.appendChild(next.firstChild);
			for (var i = slot.attributes.length - 1; i >= 0; i--) {
				var oldAttr = slot.attributes[i].name;
				if (!next.hasAttribute(oldAttr))
					slot.removeAttribute(oldAttr);
			}
			for (var j = 0; j < next.attributes.length; j++) {
				var newAttr = next.attributes[j];
				slot.setAttribute(newAttr.name, newAttr.value);
			}
			updateAddButton(root, oldSearch.value);
			return slot;
		}

		slot.parentNode.replaceChild(next, slot);
		return next;
	}

	function searchOf(root) {
		return root.querySelector('input.hyle-picker-search');
	}

	function resetFetch(root, page) {
		var st = state(root);
		var searchEl = searchOf(root);
		var q = searchEl ? searchEl.value : '';
		var url = sub(root.getAttribute('data-hyle-frag-url'),
			q, page, checkedSlugs(root));
		var seq = ++st.seq;

		st.busy = true;
		fetch(url, { credentials: 'same-origin' })
			.then(function (r) { return r.json(); })
			.then(function (env) {
				if (!env || !env.slots || seq !== st.seq) return;
				var hadFocus = document.activeElement ===
					searchOf(root);
				var caret = hadFocus
					? searchOf(root).selectionStart : 0;
				swapSlot(root, 'panel', env.slots.panel);
				swapSlot(root, 'values', env.slots.values);
				st.cursor = page + 1;
				st.appends = 0;
				st.eof = false;
				reobserve(root);
				var box = searchOf(root);
				if (hadFocus && box && document.activeElement !== box) {
					box.focus();
					var end = box.value.length;
					try {
						box.setSelectionRange(
							caret <= end ? caret : end,
							caret <= end ? caret : end);
					} catch (e) { /* not focusable */ }
				}
				if (box && box.value !== q && !st.timer) {
					st.timer = setTimeout(function () {
						resetFetch(root, 0);
					}, DEBOUNCE_MS);
				}
			})
			.catch(function () {})
			.then(function () { st.busy = false; });
	}

	function appendFetch(root) {
		var st = state(root);
		if (st.busy || st.eof || st.appends >= MAX_APPENDS)
			return;
		var q = searchOf(root) ? searchOf(root).value : '';
		var url = sub(root.getAttribute('data-hyle-frag-url'),
			q, st.cursor, '') + '&more=1&page=' + st.cursor;
		var seq = ++st.seq;

		st.busy = true;
		fetch(url, { credentials: 'same-origin' })
			.then(function (r) { return r.json(); })
			.then(function (env) {
				if (!env || typeof env.rows !== 'string' ||
						seq !== st.seq)
					return;
				var rows = root.querySelector(
					'.hyle-picker-rows');
				rows.insertAdjacentHTML('beforeend',
					env.rows);
				if (env.eof || st.appends + 1 >= MAX_APPENDS) {
					st.eof = true;
					/* Resurface native paging: the cap
					 * means the client should refine or
					 * page natively instead. */
					root.classList.remove(
						'hyle-frag-active');
				} else {
					st.cursor++;
					st.appends++;
					reobserve(root);
				}
			})
			.catch(function () {})
			.then(function () { st.busy = false; });
	}

	function reobserve(root) {
		var st = state(root);
		if (st.io) st.io.disconnect();
		var sentinel = root.querySelector(
			'[data-hyle-frag-sentinel]');
		if (!sentinel) return;
		st.io = new IntersectionObserver(function (entries) {
			for (var i = 0; i < entries.length; i++) {
				if (!entries[i].isIntersecting) continue;
				var d = root.querySelector('details');
				if (d && !d.open) continue;
				appendFetch(root);
			}
		});
		st.io.observe(sentinel);
	}

	function adopt(root) {
		if (root.__frag) return;
		state(root);
		root.classList.add('hyle-frag-active');

		/* Start the cursor one past whatever page SSR rendered
		 * (native Prev/Next buttons carry page-1 / page+1). */
		var btns = root.querySelectorAll('.hyle-picker-page-btn');
		var cur = btns.length
			? parseInt(btns[btns.length - 1].value, 10) : 1;
		state(root).cursor = isNaN(cur) || cur < 1 ? 1 : cur;

		var details = root.querySelector('details');
		if (details && !details.open)
			details.addEventListener('toggle', function () {
				if (!details.open) return;
				reobserve(root);
				var box = searchOf(root);
				if (box) box.focus();
			});
		else
			reobserve(root);
	}

	function escapeHtml(s) {
		return String(s || '')
			.replace(/&/g, '&amp;')
			.replace(/</g, '&lt;')
			.replace(/>/g, '&gt;')
			.replace(/"/g, '&quot;');
	}

	function escapeAttr(s) {
		return String(s || '')
			.replace(/&/g, '&amp;')
			.replace(/"/g, '&quot;')
			.replace(/'/g, '&#39;')
			.replace(/</g, '&lt;')
			.replace(/>/g, '&gt;');
	}

	function updateAddButton(root, text) {
		var isAddable = root.hasAttribute('data-hyle-picker-addable');
		if (!isAddable) return;
		var source = root.getAttribute('data-hyle-picker-source');
		var key = root.getAttribute('data-hyle-picker-key');
		if (!source || !key) return;
		var panel = root.querySelector('[data-hyle-slot="panel"]');
		if (!panel) return;
		var addDiv = panel.querySelector('.hyle-picker-add');
		var q = (text || '').trim();
		if (!q) {
			if (addDiv) addDiv.remove();
			return;
		}
		if (!addDiv) {
			addDiv = document.createElement('div');
			addDiv.className = 'hyle-picker-add';
			var search = searchOf(root);
			if (search && search.nextSibling) {
				panel.insertBefore(addDiv, search.nextSibling);
			} else {
				panel.appendChild(addDiv);
			}
		}
		addDiv.innerHTML = '<button type="button" class="hyle-picker-add-btn" data-hyle-picker-add="" ' +
			'data-hyle-picker-key="' + escapeAttr(key) + '" ' +
			'data-hyle-picker-source="' + escapeAttr(source) + '" ' +
			'data-hyle-picker-name="' + escapeAttr(q) + '">' +
			'+ Add “' + escapeHtml(q) + '”</button>';
	}

	document.addEventListener('input', function (e) {
		var root = e.target.closest &&
			e.target.closest('[data-hyle-frag-url]');
		if (!root ||
				!e.target.classList.contains(
					'hyle-picker-search'))
			return;
		adopt(root);
		updateAddButton(root, e.target.value);
		clearTimeout(state(root).timer);
		state(root).timer = setTimeout(function () {
			resetFetch(root, 0);
		}, DEBOUNCE_MS);
	});

	function cancelPendingFetch(root) {
		var st = state(root);
		if (st.timer) clearTimeout(st.timer);
		st.seq++;
	}

	function submitPickerAction(input) {
		var root = input.closest && input.closest('[data-hyle-frag-url]');
		if (root) cancelPendingFetch(root);
		var f = input.closest('form');
		if (!f) return;
		if (f.requestSubmit)
			f.requestSubmit();
		else
			f.submit();
	}

	function createPickerItem(root, source, key, name) {
		var multi = root.getAttribute('data-hyle-picker-multi') === '1';
		if (!source || !name) return;

		var params = new URLSearchParams();
		params.append('name', name);
		var csrfInput = document.querySelector('input[name="csrf_token"]');
		if (csrfInput && csrfInput.value)
			params.append('csrf_token', csrfInput.value);

		fetch('/api/dataset/' + encodeURIComponent(source), {
			method: 'POST',
			headers: {
				'Content-Type': 'application/x-www-form-urlencoded'
			},
			body: params.toString(),
			credentials: 'same-origin'
		})
		.then(function (r) {
			if (r.status === 409) {
				return { id: name.toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, ''), name: name, ok: true };
			}
			return r.json();
		})
		.then(function (data) {
			if (!data || (!data.id && !data.ok)) return;
			var newId = data.id || name.toLowerCase().replace(/[^a-z0-9]+/g, '_');
			var newName = (data.name && typeof data.name === 'string') ? data.name :
			              (data.title && typeof data.title === 'string') ? data.title : name;

			var rows = root.querySelector('.hyle-picker-rows');
			if (rows) {
				var empty = rows.querySelector('.hyle-picker-empty');
				if (empty) empty.remove();

				if (!multi) {
					var prevRadios = root.querySelectorAll('.hyle-picker-rows input[type="radio"]');
					for (var i = 0; i < prevRadios.length; i++)
						prevRadios[i].checked = false;
				}

				var existing = rows.querySelector('input[value="' + newId + '"]');
				if (existing) {
					existing.checked = true;
				} else {
					var newOption = document.createElement('label');
					newOption.className = 'hyle-picker-option';
					newOption.innerHTML = '<input type="' + (multi ? 'checkbox' : 'radio') + '" name="' + escapeAttr(key) + '" value="' + escapeAttr(newId) + '" checked/> ' + escapeHtml(newName);
					rows.insertBefore(newOption, rows.firstChild);
				}
			}

			syncSummary(root);

			var search = searchOf(root);
			if (search) search.value = '';

			var addRow = root.querySelector('.hyle-picker-add');
			if (addRow) addRow.remove();

			if (!multi) {
				var details = root.querySelector('details');
				if (details) details.removeAttribute('open');
			}

			resetFetch(root, 0);
		})
		.catch(function () {});
	}

	function handlePickerAdd(addBtn) {
		var root = addBtn.closest('[data-hyle-frag-url]');
		if (!root) return;
		var source = addBtn.getAttribute('data-hyle-picker-source');
		var key = addBtn.getAttribute('data-hyle-picker-key');
		var name = addBtn.getAttribute('data-hyle-picker-name');
		createPickerItem(root, source, key, name);
	}

	document.addEventListener('keydown', function (e) {
		var root = e.target.closest &&
			e.target.closest('[data-hyle-frag-url]');
		if (!root || e.key !== 'Enter' ||
				e.target.tagName !== 'INPUT')
			return;
		adopt(root);
		var opt = e.target.closest('.hyle-picker-option');
		if (!opt) return;
		/* Enter on an option selects it and closes the dropdown
		 * instead of submitting the surrounding form. */
		e.preventDefault();
		e.target.checked = true;
		syncSummary(root);
		var details = root.querySelector('details');
		if (details) details.removeAttribute('open');
		if (e.target.type === 'radio' && e.target.closest('[data-hyle-auto-submit]')) {
			submitPickerAction(e.target);
		}
	});

	document.addEventListener('keydown', function (e) {
		if (e.key !== 'Enter') return;
		var root = e.target.closest &&
			e.target.closest('[data-hyle-frag-url]');
		if (!root ||
				!e.target.classList.contains(
					'hyle-picker-search'))
			return;
		adopt(root);
		e.preventDefault();
		var q = e.target.value.trim();
		var isAddable = root.hasAttribute('data-hyle-picker-addable');
		if (isAddable) {
			var addBtn = root.querySelector('[data-hyle-picker-add]');
			if (addBtn) {
				handlePickerAdd(addBtn);
				return;
			}
			if (q) {
				var source = root.getAttribute('data-hyle-picker-source');
				var key = root.getAttribute('data-hyle-picker-key');
				if (source && key) {
					createPickerItem(root, source, key, q);
					return;
				}
			}
		}
		clearTimeout(state(root).timer);
		resetFetch(root, 0);
	});

	/* Auto-close other open pickers when one opens */
	document.addEventListener('toggle', function (e) {
		if (!e.target || !e.target.open) return;
		if (!e.target.classList.contains('hyle-picker-details') &&
		    !e.target.classList.contains('hyle-multiselect') &&
		    !e.target.classList.contains('hyle-singleselect'))
			return;
		var openDetails = document.querySelectorAll(
			'details.hyle-picker-details[open], details.hyle-multiselect[open], details.hyle-singleselect[open]');
		for (var i = 0; i < openDetails.length; i++) {
			var d = openDetails[i];
			if (d !== e.target) {
				d.removeAttribute('open');
			}
		}
	}, true);

	/* Close open pickers on click-outside */
	document.addEventListener('click', function (e) {
		var openDetails = document.querySelectorAll(
			'details.hyle-picker-details[open], details.hyle-multiselect[open], details.hyle-singleselect[open]');
		for (var i = 0; i < openDetails.length; i++) {
			var d = openDetails[i];
			if (!d.contains(e.target)) {
				d.removeAttribute('open');
			}
		}
	});

	/* Close open pickers on Escape */
	document.addEventListener('keydown', function (e) {
		if (e.key !== 'Escape') return;
		var openDetails = document.querySelectorAll(
			'details.hyle-picker-details[open], details.hyle-multiselect[open], details.hyle-singleselect[open]');
		for (var i = 0; i < openDetails.length; i++) {
			openDetails[i].removeAttribute('open');
		}
	});

	document.addEventListener('click', function (e) {
		var addBtn = e.target.closest &&
			e.target.closest('[data-hyle-picker-add]');
		if (addBtn) {
			e.preventDefault();
			handlePickerAdd(addBtn);
			return;
		}
		var opt = e.target.closest &&
			e.target.closest('.hyle-picker-option');
		if (opt) {
			var optRoot = opt.closest('[data-hyle-frag-url]');
			if (optRoot && optRoot.getAttribute('data-hyle-picker-multi') !== '1') {
				var radio = opt.querySelector('input[type="radio"]');
				if (radio) {
					radio.checked = true;
					adopt(optRoot);
					syncSummary(optRoot);
					var d = optRoot.querySelector('details');
					if (d) d.removeAttribute('open');
					if (optRoot.hasAttribute('data-hyle-auto-submit') || optRoot.closest('[data-hyle-auto-submit]')) {
						submitPickerAction(radio);
						return;
					}
				}
			}
		}
		var root = e.target.closest &&
			e.target.closest('[data-hyle-frag-url]');
		if (!root) return;
		adopt(root);
		var btn = e.target.closest('.hyle-picker-page-btn');
		if (!btn) return;
		e.preventDefault();
		resetFetch(root, parseInt(btn.value, 10) || 0);
	});

	document.addEventListener('change', function (e) {
		var root = e.target.closest &&
			e.target.closest('[data-hyle-frag-url]');
		if (!root ||
				e.target.tagName !== 'INPUT')
			return;
		adopt(root);
		syncSummary(root);
		if (e.target.type === 'radio') {
			var details = root.querySelector('details');
			if (details) details.removeAttribute('open');
			if (e.target.closest('[data-hyle-auto-submit]')) {
				submitPickerAction(e.target);
			}
		}
	});

	var nodes = document.querySelectorAll('[data-hyle-frag-url]');
	for (var i = 0; i < nodes.length; i++)
		adopt(nodes[i]);
})();
