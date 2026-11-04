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

// 4. Test bridge instantiation and imports
const memory = new WebAssembly.Memory({ initial: 1 });
const encoder = new TextEncoder();
const testStr = 'Hello from WASM!';
const encoded = encoder.encode(testStr);
new Uint8Array(memory.buffer, 128, encoded.length).set(encoded);

const bridge = createAxilTtyBridge(null, () => ({ memory }));
bridge.mount();
const imports = bridge.makeImports();
assert.equal(typeof imports.term_write, 'function');

imports.term_write(128, encoded.length);
assert.deepEqual(written, [testStr]);

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
