import test from 'node:test';
import assert from 'node:assert/strict';

// Mock browser DOM environment
const replayedOps = [];
globalThis.window = {
	location: { href: 'http://localhost:8080/', origin: 'http://localhost:8080', pathname: '/', search: '', hash: '' },
	history: {
		pushState(state, title, url) {
			window.location.pathname = url;
		}
	},
	__wasm_capability_registry: {},
	addEventListener() {}
};

const elements = new Map();
function makeElement(tag, id = '', attrs = {}) {
	const el = {
		tagName: tag.toUpperCase(),
		id,
		attributes: new Map(Object.entries(attrs)),
		innerHTML: '',
		textContent: '',
		getAttribute(k) { return this.attributes.get(k) || null; },
		setAttribute(k, v) { this.attributes.set(k, v); },
		hasAttribute(k) { return this.attributes.has(k); },
		closest(selector) {
			if (selector.includes('[data-slot]')) {
				return this.hasAttribute('data-slot') ? this : null;
			}
			return null;
		},
		querySelector() { return null; }
	};
	if (id) elements.set(id, el);
	return el;
}

const chromeRoot = makeElement('div', 'chrome-root', { 'data-slot': '0' });
const budRoot = makeElement('div', 'bud-root', { 'data-slot': '1', 'data-module': 'song_detail' });
const chromeState = makeElement('script', 'chrome-state');
chromeState.textContent = JSON.stringify({ title: 'Song List', path: '/songs' });
const budState = makeElement('script', 'bud-state');
budState.textContent = JSON.stringify({ id: 123, title: 'Sample Song' });

globalThis.document = {
	body: makeElement('body', '', { 'data-modules': 'site_chrome song_detail' }),
	getElementById(id) { return elements.get(id) || null; },
	querySelector(sel) {
		if (sel.includes('#chrome-root') || sel.includes('[data-slot="0"]')) return chromeRoot;
		if (sel.includes('#bud-root') || sel.includes('[data-slot="1"]')) return budRoot;
		if (sel.includes('#chrome-state')) return chromeState;
		if (sel.includes('#bud-state') || sel.includes('#page-state')) return budState;
		return null;
	},
	addEventListener() {},
	removeEventListener() {}
};

// Set up slot bridge factory mock
window.__wasm_slot_bridge_factory = (root) => {
	return {
		root,
		host: {
			replay(ops) {
				replayedOps.push(...ops);
			}
		}
	};
};

// Import client
const { SteleWasmClient } = await import('../../htdocs/stele-client.js');

test('SteleWasmClient instantiates and decodes strings', () => {
	const client = new SteleWasmClient();
	assert.ok(client);
	assert.equal(typeof client.boot, 'function');

	const memory = new WebAssembly.Memory({ initial: 1 });
	const encoded = new TextEncoder().encode('hello stele');
	new Uint8Array(memory.buffer, 16, encoded.length).set(encoded);

	const decoded = client.decodeString(memory, 16, encoded.length);
	assert.equal(decoded, 'hello stele');
});

test('SteleWasmClient slot bridge dispatches single-stream VDOM patches', () => {
	const client = new SteleWasmClient();
	client.slotBridges.set(0, window.__wasm_slot_bridge_factory(chromeRoot));
	client.slotBridges.set(1, window.__wasm_slot_bridge_factory(budRoot));

	const memory = new WebAssembly.Memory({ initial: 1 });
	client.memory = memory;

	function writeMem(ptr, str) {
		const bytes = new TextEncoder().encode(str);
		new Uint8Array(memory.buffer, ptr, bytes.length).set(bytes);
		return bytes.length;
	}

	const opLen = writeMem(100, 'patch-text');
	const aLen = writeMem(200, 'new text');
	const bLen = writeMem(300, 'node-42');

	const imports = client.buildSlotImports(1);
	imports.stele.stl_host_emit_patch(1, 100, opLen, 200, aLen, 300, bLen, 0, 0);

	assert.equal(replayedOps.length, 1);
	assert.deepEqual(replayedOps[0], {
		op: 'patch-text',
		a: 'new text',
		b: 'node-42',
		c: ''
	});
});

test('SteleWasmClient preserves native link navigation while delegating in-place WASM actions', () => {
	const client = new SteleWasmClient();
	let capturedActionId = null;
	let capturedActionPayload = null;

	// Mock component instance with wasm_action
	const mockInstance = {
		exports: {
			wasm_action(id, ptr) {
				capturedActionId = id;
			},
			free() {}
		}
	};
	client.slotInstances.set(1, { instance: mockInstance, memory: new WebAssembly.Memory({ initial: 1 }) });

	const clickListeners = [];
	const mockDocument = {
		addEventListener(type, fn) {
			if (type === 'click') clickListeners.push(fn);
		}
	};
	globalThis.document.addEventListener = mockDocument.addEventListener;

	client.attachEventDelegation();
	assert.ok(clickListeners.length > 0, 'Click listeners should be registered');
	const dispatchClick = (evt) => {
		for (const fn of clickListeners) fn(evt);
	};

	// 1. Regular internal link must NOT be intercepted (native navigation preserved)
	let linkPrevented = false;
	const linkTarget = {
		closest(sel) {
			if (sel.includes('button[data-action]') || sel.includes('a[data-action]')) return null;
			if (sel.includes('a[href]')) return {
				getAttribute(attr) { return attr === 'href' ? '/song/123' : null; },
				hasAttribute() { return false; },
				target: '_self'
			};
			return null;
		}
	};
	dispatchClick({
		target: linkTarget,
		button: 0,
		defaultPrevented: false,
		preventDefault() { linkPrevented = true; }
	});
	assert.equal(linkPrevented, false, 'Regular links must NOT preventDefault; native navigation preserved');

	// 2. Interactive action (e.g. table sort data-action="1") MUST be intercepted
	let actionPrevented = false;
	const actionTarget = {
		closest(sel) {
			if (sel.includes('button[data-action]') || sel.includes('a[data-action]')) return {
				getAttribute(attr) {
					if (attr === 'data-action') return '1';
					if (attr === 'data-action-payload') return 'title:asc';
					return null;
				},
				closest() { return budRoot; }
			};
			return null;
		}
	};
	dispatchClick({
		target: actionTarget,
		button: 0,
		defaultPrevented: false,
		preventDefault() { actionPrevented = true; }
	});
	assert.equal(actionPrevented, true, 'Interactive data-actions must preventDefault');
	assert.equal(capturedActionId, 1, 'Action ID 1 must be dispatched to wasm_action');
});

test('SteleWasmClient mountSlot skips duplicate bud capability bridge and BudWasmBridge guards against recursive DOM event dispatch', async () => {
	globalThis.Node = { ELEMENT_NODE: 1, TEXT_NODE: 3 };
	globalThis.Event = class Event {
		constructor(type, opts = {}) {
			this.type = type;
			this.bubbles = !!opts.bubbles;
		}
	};
	const { createBudWasmBridge } = await import('../../htdocs/bud-cap.js');
	window.__wasm_capability_registry['bud'] = (root) => createBudWasmBridge(root);
	window.__wasm_slot_bridge_namespaces = ['bud'];

	const slotListeners = new Map();
	const btnListeners = new Map();
	let btnDispatchCount = 0;

	const childBtn = {
		nodeType: 1,
		tagName: 'BUTTON',
		id: 'settings-msaa-toggle',
		attributes: new Map([['id', 'settings-msaa-toggle'], ['data-bud-id', '42'], ['data-bud-on', 'click:1']]),
		childNodes: [],
		getAttribute(k) { return this.attributes.get(k) || null; },
		setAttribute(k, v) { this.attributes.set(k, String(v)); },
		hasAttribute(k) { return this.attributes.has(k); },
		addEventListener(type, fn) {
			if (!btnListeners.has(type)) btnListeners.set(type, []);
			btnListeners.get(type).push(fn);
		},
		dispatchEvent(evt) {
			btnDispatchCount++;
			if (btnDispatchCount > 10) {
				throw new RangeError('Infinite recursive DOM dispatch detected');
			}
			const fns = slotListeners.get(evt.type) || [];
			for (const fn of fns) {
				fn({ type: evt.type, target: childBtn, currentTarget: slotEl, bubbles: true, stopPropagation() {}, preventDefault() {} });
			}
			return true;
		}
	};

	const slotEl = {
		nodeType: 1,
		tagName: 'DIV',
		id: 'bud-root',
		attributes: new Map([
			['data-slot', '1'],
			['data-capabilities', 'lamina bud']
		]),
		childNodes: [childBtn],
		getAttribute(k) { return this.attributes.get(k) || null; },
		setAttribute(k, v) { this.attributes.set(k, String(v)); },
		hasAttribute(k) { return this.attributes.has(k); },
		querySelector(sel) {
			if (sel === '#settings-msaa-toggle' || sel === '[data-bud-id="42"]') return childBtn;
			return null;
		},
		querySelectorAll(sel) {
			if (sel === '[data-bud-persist-key]') return [];
			return [childBtn];
		},
		addEventListener(type, fn) {
			if (!slotListeners.has(type)) slotListeners.set(type, []);
			slotListeners.get(type).push(fn);
		}
	};
	childBtn.parentNode = slotEl;
	childBtn.parentElement = slotEl;

	let budCapFactoryCalls = 0;
	window.__wasm_capability_registry['bud'] = (root) => {
		budCapFactoryCalls++;
		return createBudWasmBridge(root);
	};

	const client = new SteleWasmClient();
	client.workerReady = true;
	client._pendingMountPromises = new Map();
	const dispatchedToWorker = [];
	client.worker = {
		postMessage(msg) {
			if (msg && msg.type === 'stele:mount_slot') {
				const resolve = client._pendingMountPromises.get(msg.slot);
				if (resolve) resolve(true);
			} else if (msg && msg.type === 'stele:dispatch') {
				dispatchedToWorker.push(msg);
			}
		}
	};
	const primaryBridge = createBudWasmBridge(slotEl);
	client.slotBridges.set(1, primaryBridge);

	await client.mountSlot(1, 'test_mod', slotEl, null);
	assert.equal(budCapFactoryCalls, 0, 'mountSlot must skip instantiating a duplicate bud capability bridge on slotRoot');

	// Verify that clicking childBtn dispatches once to onDispatch and never recurses into _dispatchDomFallback
	childBtn.dispatchEvent({ type: 'click', bubbles: true });
	assert.equal(btnDispatchCount, 1, 'DOM click must not trigger recursive node.dispatchEvent');
	assert.equal(dispatchedToWorker.length, 1, 'Worker onDispatch must receive exactly one click dispatch');
	assert.equal(dispatchedToWorker[0].nodeId, 42);
	assert.equal(dispatchedToWorker[0].eventName, 'click');

	// Also verify that even an unconfigured BudWasmBridge (no onDispatch, no wasm) ignores physical DOM events cleanly
	// and guards programmatic dispatch() against recursive _dispatchDomFallback
	const unconfiguredBridge = createBudWasmBridge(slotEl);
	btnDispatchCount = 0;
	childBtn.dispatchEvent({ type: 'click', bubbles: true });
	assert.equal(btnDispatchCount, 1, 'Unconfigured BudWasmBridge must never enter recursive _dispatchDomFallback on DOM events');
	btnDispatchCount = 0;
	assert.equal(unconfiguredBridge.dispatch(42, 'click', true), true);
	assert.equal(btnDispatchCount, 1, 'Programmatic fallback dispatch must fire DOM event at most once without recursion');

	// Verify localStorage user settings persistence:
	// 1) Initial mount seeds default Medium settings into localStorage when no saved entry exists
	// 2) Subsequent mount with saved localStorage settings posts stele:mount_slot BEFORE stele:dispatch (restore-persist)
	const store = new Map();
	window.localStorage = {
		getItem(k) { return store.has(k) ? store.get(k) : null; },
		setItem(k, v) { store.set(k, String(v)); },
		removeItem(k) { store.delete(k); }
	};
	const defaultMediumStyle = '--nd-res:75;--nd-det:75;--nd-cloud:2;--nd-msaa:0;--nd-fps:60;--nd-zoom:100;';
	const settingsCard = {
		nodeType: 1,
		tagName: 'DIV',
		id: 'settings',
		attributes: new Map([
			['id', 'settings'],
			['data-bud-id', '99'],
			['data-bud-persist-key', 'nd-settings-quality'],
			['class', 'rpg-modal-card settings-card select-none'],
			['style', defaultMediumStyle]
		]),
		childNodes: [],
		getAttribute(k) { return this.attributes.get(k) || null; },
		setAttribute(k, v) { this.attributes.set(k, String(v)); },
		removeAttribute(k) { this.attributes.delete(k); },
		hasAttribute(k) { return this.attributes.has(k); },
		addEventListener() {}
	};
	slotEl.querySelectorAll = (sel) => {
		if (sel === '[data-bud-persist-key]') return [settingsCard];
		return [childBtn, settingsCard];
	};

	const firstLoadBridge = createBudWasmBridge(slotEl);
	firstLoadBridge.mount();
	const seededRaw = window.localStorage.getItem('bud:layout:nd-settings-quality');
	assert.ok(seededRaw, 'First mount must seed default Medium settings into localStorage');
	assert.equal(JSON.parse(seededRaw).style, defaultMediumStyle);

	// Now simulate user saving custom settings in localStorage and reloading the page in Worker mode
	const customSavedStyle = '--nd-res:60;--nd-det:90;--nd-cloud:1;--nd-msaa:0;--nd-fps:30;--nd-zoom:120;';
	window.localStorage.setItem('bud:layout:nd-settings-quality', JSON.stringify({
		style: customSavedStyle,
		class: 'rpg-modal-card settings-card select-none'
	}));
	settingsCard.setAttribute('style', defaultMediumStyle);

	const workerMessageLog = [];
	const reloadClient = new SteleWasmClient();
	reloadClient.workerReady = true;
	reloadClient._pendingMountPromises = new Map();
	reloadClient.worker = {
		postMessage(msg) {
			workerMessageLog.push(msg);
			if (msg && msg.type === 'stele:mount_slot') {
				const resolve = reloadClient._pendingMountPromises.get(msg.slot);
				if (resolve) resolve(true);
			}
		}
	};
	const reloadBridge = createBudWasmBridge(slotEl);
	reloadClient.slotBridges.set(1, reloadBridge);
	await reloadClient.mountSlot(1, 'nd', slotEl, null);

	const mountIdx = workerMessageLog.findIndex((m) => m.type === 'stele:mount_slot');
	const restoreIdx = workerMessageLog.findIndex((m) => m.type === 'stele:dispatch' && m.eventName === 'restore-persist');
	assert.ok(mountIdx >= 0, 'Worker must receive stele:mount_slot');
	assert.ok(restoreIdx > mountIdx, 'restore-persist dispatch from localStorage must be queued AFTER stele:mount_slot');
	assert.ok(workerMessageLog[restoreIdx].valueStr.includes(customSavedStyle), 'restore-persist must carry saved localStorage settings');
	assert.equal(settingsCard.getAttribute('style'), customSavedStyle);
});

test('SteleWasmClient prefers data-wasm-bundle, reuses one instance across slots, and falls back for absent modules', async () => {
	const fs = await import('node:fs');
	const path = await import('node:path');
	const { fileURLToPath } = await import('node:url');
	const dir = path.dirname(fileURLToPath(import.meta.url));
	const siteWasmBytes = fs.readFileSync(path.resolve(dir, '../../htdocs/site.wasm'));
	const songWasmBytes = fs.readFileSync(path.resolve(dir, '../../htdocs/song.wasm'));

	const fetchedUrls = [];
	const origFetch = globalThis.fetch;
	globalThis.fetch = async (url) => {
		const s = String(url);
		fetchedUrls.push(s);
		if (s.endsWith('/site.wasm')) {
			return { ok: true, status: 200, arrayBuffer: async () => siteWasmBytes.buffer.slice(siteWasmBytes.byteOffset, siteWasmBytes.byteOffset + siteWasmBytes.byteLength) };
		}
		if (s.endsWith('/custom_only.wasm')) {
			return { ok: true, status: 200, arrayBuffer: async () => songWasmBytes.buffer.slice(songWasmBytes.byteOffset, songWasmBytes.byteOffset + songWasmBytes.byteLength) };
		}
		return { ok: false, status: 404, arrayBuffer: async () => new ArrayBuffer(0) };
	};

	const origBundle = document.body.getAttribute('data-wasm-bundle');
	document.body.setAttribute('data-wasm-bundle', '/site.wasm');
	try {
		const client = new SteleWasmClient({ useWorker: false });
		const ok0 = await client.mountSlot(0, 'site_chrome', { title: 'T', path: '/song', icon: '', user: '', lang: 'en' });
		const ok1 = await client.mountSlot(1, 'song_detail', { id: 's1', title: 'Song', transpose: 0, zoom: 100, use_latin: 0 });
		assert.equal(ok0, true, 'slot 0 site_chrome mounts from /site.wasm');
		assert.equal(ok1, true, 'slot 1 song_detail mounts from /site.wasm');
		assert.equal(client.slotInstances.get(0).instance, client.slotInstances.get(1).instance, 'slots 0 and 1 must share one /site.wasm instance');
		assert.equal(client.slotInstances.get(0).isKernelBundle, true);
		assert.equal(client.slotInstances.get(1).isKernelBundle, true);
		assert.equal(fetchedUrls.filter(u => u.endsWith('/site.wasm')).length, 1, '/site.wasm must be fetched only once');
		assert.equal(fetchedUrls.some(u => u.endsWith('/site_chrome.wasm') || u.endsWith('/song_detail.wasm')), false, 'must not fetch per-module WASM when present in /site.wasm');
		assert.equal(client.invokeSlotAction(1, 0, ''), true, 'invokeSlotAction routes via stl_kernel_action');
		assert.equal(client.dispatchToSlot(0, 0, 'click', false, ''), 0, 'slot 0 dispatch routes via stl_kernel_dispatch_event');
		assert.equal(client.dispatchToSlot(1, 0, 'click', false, ''), 0, 'slot 1 dispatch routes via stl_kernel_dispatch_event');

		// Module absent from /site.wasm must fall back cleanly to /<module>.wasm
		const ok2 = await client.mountSlot(2, 'custom_only', { id: 's2', title: 'Fallback', transpose: 0, zoom: 100, use_latin: 0 });
		assert.equal(ok2, true, 'absent module falls back to /custom_only.wasm');
		assert.equal(fetchedUrls.some(u => u.endsWith('/custom_only.wasm')), true, 'must fetch /custom_only.wasm fallback');
		assert.notEqual(client.slotInstances.get(2).instance, client.slotInstances.get(0).instance);
	} finally {
		globalThis.fetch = origFetch;
		if (origBundle) document.body.setAttribute('data-wasm-bundle', origBundle);
		else document.body.attributes.delete('data-wasm-bundle');
	}
});


