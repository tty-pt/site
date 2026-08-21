import { createBudWasmBridge } from './bud-hydrate.js?v=1';

window.bud_data = {};
window.hydrate_queue = [];

function moduleSpec(name) {
	if (name === 'site_chrome') {
		return {
			name,
			root: document.querySelector('#chrome-root'),
			state: document.getElementById('chrome-state'),
			global: true
		};
	}
	return {
		name,
		root: document.querySelector('#bud-root'),
		state: document.getElementById('bud-state'),
		global: false
	};
}

async function loadModule(spec) {
	let bridge = null;
	let statePtr = 0;
	let instance = null;
	try {
		const response = await fetch(`/${spec.name}.wasm`);
		if (!response.ok) {
			if (spec.global) {
				console.warn('Global site chrome WASM unavailable; using sticky SSR chrome');
			}
			return null;
		}
		const wasmBytes = await response.arrayBuffer();
		bridge = createBudWasmBridge(spec.root);
		({ instance } = await WebAssembly.instantiate(wasmBytes, {
			env: {
				...bridge.makeImports(),
				__main_argc_argv: () => 0
			},
			wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 })
		}));

		bridge.setWasm(instance.exports);
		if (instance.exports._initialize)
			instance.exports._initialize();
		else if (instance.exports.__wasm_call_ctors)
			instance.exports.__wasm_call_ctors();

		if (spec.state && instance.exports.malloc && instance.exports.wasm_init) {
			const text = spec.state.textContent;
			if (text) {
				const bytes = new TextEncoder().encode(text + '\0');
				statePtr = instance.exports.malloc(bytes.length);
				if (statePtr) {
					new Uint8Array(
						instance.exports.memory.buffer, statePtr,
						bytes.length).set(bytes);
					instance.exports.wasm_init(statePtr, bytes.length - 1);
				}
			}
		}
		bridge.mount();
		setTimeout(() => bridge.autoDebug(), 100);
		return { name: spec.name, bridge, global: spec.global };
	} catch (error) {
		if (bridge) bridge.destroy();
		console.error(`Failed to load ${spec.name} WASM:`, error);
		return null;
	} finally {
		if (statePtr && instance && instance.exports.free)
			instance.exports.free(statePtr);
	}
}

async function init() {
	const value = document.body.getAttribute('data-modules') || '';
	const names = [...new Set(value.trim().split(/\s+/).filter(Boolean))];
	const specs = names.map(moduleSpec).filter((spec) => spec.root);
	const localRoots = new Set();
	const uniqueSpecs = specs.filter((spec) => {
		if (spec.global) return true;
		if (localRoots.has(spec.root)) return false;
		localRoots.add(spec.root);
		return true;
	});
	const settled = await Promise.allSettled(uniqueSpecs.map(loadModule));
	const bridges = {};
	let localBridge = null;

	for (const result of settled) {
		if (result.status !== 'fulfilled' || !result.value) continue;
		bridges[result.value.name] = result.value.bridge;
		if (!result.value.global) localBridge = result.value.bridge;
	}
	window.__bud_bridges = bridges;
	window.__bud_bridge = localBridge || bridges.site_chrome || null;
	if (Object.keys(bridges).length > 0)
		document.body.setAttribute('data-wasm-loaded', '');
}

document.addEventListener('DOMContentLoaded', init);
