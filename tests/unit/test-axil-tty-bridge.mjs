import assert from 'node:assert/strict';

// 1. Verify export from htdocs/axil-tty-bridge.js
const { createAxilTtyBridge } = await import('../../external/axil-tty/htdocs/axil-tty-bridge.js');
assert.equal(typeof createAxilTtyBridge, 'function', 'createAxilTtyBridge should be exported');

// 2. Verify export from CJS bundle
const cjsModule = await import('../../external/axil-tty/cjs/axil-tty.js');
const cjs = cjsModule.default || cjsModule;
assert.equal(typeof cjs.createAxilTtyBridge, 'function', 'createAxilTtyBridge exported from CJS');
assert.equal(typeof cjs.TerminalController, 'function', 'TerminalController exported from CJS');

// 3. Mock DOM environment
const written = [];
const mockTerm = {
	cols: 80,
	rows: 25,
	element: { addEventListener() {} },
	open() {},
	write(txt) { written.push(txt); },
	onData() {},
	onResize() {},
	loadAddon() {},
	dispose() {}
};
globalThis.Terminal = class {
	constructor() {
		return mockTerm;
	}
};
globalThis.window = {
	Terminal: globalThis.Terminal,
	FitAddon: class {
		fit() {}
	},
	location: { protocol: 'http:', hostname: 'localhost', port: '8080' },
	__wasm_capability_registry: {}
};
globalThis.document = {
	querySelector(sel) {
		if (sel === '[data-bud-terminal]') return { __attached: false, addEventListener() {}, getAttribute() { return 'false'; } };
		return null;
	},
	createElement() {
		return { setAttribute() {}, addEventListener() {} };
	},
	head: { appendChild() {} }
};

// 4. Test bridge instantiation, imports, and unmount/remount lifecycle
const memory = new WebAssembly.Memory({ initial: 1 });
const encoder = new TextEncoder();
const testStr = 'Hello from WASM!';
const encoded = encoder.encode(testStr);
new Uint8Array(memory.buffer, 128, encoded.length).set(encoded);

let activeContainer = {
	id: 'term-1',
	__attached: false,
	addEventListener() {},
	getAttribute() { return 'false'; }
};
const liveElements = new Set([activeContainer]);

globalThis.document.contains = (el) => liveElements.has(el);
globalThis.document.querySelector = (sel) => {
	if (sel === '[data-bud-terminal]') return activeContainer;
	return null;
};

const termsCreated = [];
globalThis.Terminal = class {
	constructor(options) {
		this.options = options;
		this.written = [];
		this.element = { addEventListener() {} };
		this.disposed = false;
		this.open = (el) => {
			this.element = el;
			if (!this.element.addEventListener) {
				this.element.addEventListener = () => {};
			}
		};
		this.write = (txt) => {
			this.written.push(txt);
			written.push(txt);
		};
		this.onData = () => {};
		this.onResize = () => {};
		this.loadAddon = () => {};
		this.dispose = () => {
			this.disposed = true;
		};
		termsCreated.push(this);
	}
};
globalThis.window.Terminal = globalThis.Terminal;

const bridge = createAxilTtyBridge(null, () => ({ memory }));
bridge.mount();
const imports = bridge.makeImports();
assert.equal(typeof imports.term_write, 'function');
assert.equal(termsCreated.length, 1, 'First terminal should be created');
assert.equal(termsCreated[0].element, activeContainer, 'Terminal 1 mounted to activeContainer 1');

imports.term_write(128, encoded.length);
assert.deepEqual(termsCreated[0].written, [testStr]);

// Test unmount/remount lifecycle:
// Simulate region switch where activeContainer is removed from DOM and replaced by a fresh one
liveElements.delete(activeContainer);
const newContainer = {
	id: 'term-2',
	__attached: false,
	addEventListener() {},
	getAttribute() { return 'false'; }
};
liveElements.add(newContainer);
activeContainer = newContainer;

// Call mount() again as replay() or host would after region returns to terminal
bridge.mount();

assert.equal(termsCreated.length, 2, 'Second terminal should be created when previous element was detached');
assert.equal(termsCreated[0].disposed, true, 'First terminal should have been disposed');
assert.equal(termsCreated[1].element, newContainer, 'Terminal 2 mounted to newContainer');

// Writes should now go to terminal 2
const testStr2 = 'Welcome back to terminal!';
const encoded2 = encoder.encode(testStr2);
new Uint8Array(memory.buffer, 256, encoded2.length).set(encoded2);
imports.term_write(256, encoded2.length);
assert.deepEqual(termsCreated[1].written, [testStr2]);

bridge.destroy();

// 5. Test TerminalController class
globalThis.ResizeObserver = class { observe() {} disconnect() {} };
globalThis.location = { protocol: 'http:', hostname: 'localhost', port: '8080' };
globalThis.WebSocket = class {
	constructor(url, proto) {
		this.url = url;
		this.proto = proto;
	}
	send() {}
	close() {}
};
const dummyEl = { appendChild() {} };
const ctrl = new cjs.TerminalController(dummyEl);
assert.ok(ctrl);
assert.equal(typeof ctrl.write, 'function');
assert.equal(typeof ctrl.send, 'function');
assert.equal(typeof ctrl.resize, 'function');
assert.equal(typeof ctrl.close, 'function');
ctrl.close();

console.log('PASS: axil-tty bridge successfully mounts and writes from WASM memory');
