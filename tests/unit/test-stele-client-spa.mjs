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
	addEventListener() {}
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
