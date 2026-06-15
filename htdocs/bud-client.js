import { createBudWasmBridge } from './bud-hydrate.js';

async function init() {
	const body = document.body;
	const mod = body.getAttribute('data-modules');
	if (!mod) return;
	const [module] = mod.split(/\s+/);

	const root = document.querySelector('#bud-root');
	if (!root) return;

	try {
		const res = await fetch(`/${module}_fe.wasm`);
		const wasmBytes = await res.arrayBuffer();
		const bridge = createBudWasmBridge(root);

		const { instance } = await WebAssembly.instantiate(wasmBytes, {
			env: {
				...bridge.makeImports(),
				__main_argc_argv: () => 0
			},
			wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 })
		});

		bridge.setWasm(instance.exports);

		if (instance.exports._initialize)
			instance.exports._initialize();
		else if (instance.exports.__wasm_call_ctors)
			instance.exports.__wasm_call_ctors();

		const stateEl = document.getElementById('bud-state');
		if (stateEl && instance.exports.malloc && instance.exports.wasm_init) {
			const text = stateEl.textContent;
			if (text) {
				const enc = new TextEncoder();
				const bytes = enc.encode(text + '\0');
				const ptr = instance.exports.malloc(bytes.length);
				if (ptr) {
					new Uint8Array(instance.exports.memory.buffer, ptr, bytes.length).set(bytes);
					instance.exports.wasm_init(ptr, bytes.length - 1);
					instance.exports.free(ptr);
				}
			}
		}

		bridge.mount();
		body.setAttribute('data-wasm-loaded', '');
		window.__bud_bridge = bridge;

		/* Auto-debug: compare trees on every page load */
		setTimeout(() => bridge.autoDebug(), 100);
	} catch (err) {
		console.error(`Failed to load ${module} WASM:`, err);
	}
}

document.addEventListener('DOMContentLoaded', init);
