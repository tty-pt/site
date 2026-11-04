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
	assert.equal(typeof client.navigate, 'function');

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

test('SteleWasmClient zero-reload SPA navigate handles internal page transitions', async () => {
	const client = new SteleWasmClient();
	client.slotBridges.set(0, window.__wasm_slot_bridge_factory(chromeRoot));
	client.slotBridges.set(1, window.__wasm_slot_bridge_factory(budRoot));

	// Mock fetch returning new page HTML
	globalThis.fetch = async (url) => {
		return {
			ok: true,
			text: async () => `
				<!DOCTYPE html>
				<html>
				<head><title>Gig Detail - Live</title></head>
				<body data-modules="site_chrome gig_detail">
					<script type="application/json" id="chrome-state">{"title":"Gigs","path":"/gigs"}</script>
					<div id="chrome-root" data-slot="0"><nav>Updated Nav</nav></div>
					<div id="bud-root" data-slot="1" data-module="gig_detail"><main>Gig Details</main></div>
					<script type="application/json" id="bud-state">{"id":456,"venue":"Concert Hall"} </script>
				</body>
				</html>
			`,
			arrayBuffer: async () => new ArrayBuffer(0),
			clone() { return this; }
		};
	};

	globalThis.DOMParser = class {
		parseFromString(html) {
			const gigDocRoot = makeElement('div', 'bud-root', { 'data-slot': '1', 'data-module': 'gig_detail' });
			gigDocRoot.innerHTML = '<main>Gig Details</main>';
			const gigState = makeElement('script', 'bud-state');
			gigState.textContent = '{"id":456,"venue":"Concert Hall"}';
			const gigChromeState = makeElement('script', 'chrome-state');
			gigChromeState.textContent = '{"title":"Gigs","path":"/gigs"}';

			return {
				title: 'Gig Detail - Live',
				body: makeElement('body', '', { 'data-modules': 'site_chrome gig_detail' }),
				getElementById(id) {
					if (id === 'chrome-state') return gigChromeState;
					if (id === 'bud-state') return gigState;
					return null;
				},
				querySelector(sel) {
					if (sel.includes('#bud-root') || sel.includes('[data-slot="1"]')) return gigDocRoot;
					if (sel.includes('#bud-state') || sel.includes('#page-state')) return gigState;
					return null;
				}
			};
		}
	};

	await client.navigate('/gig/456');

	assert.equal(globalThis.document.title, 'Gig Detail - Live');
	assert.equal(budRoot.innerHTML, '<main>Gig Details</main>');
	assert.equal(budRoot.getAttribute('data-module'), 'gig_detail');
	assert.equal(window.location.pathname, '/gig/456');
});
