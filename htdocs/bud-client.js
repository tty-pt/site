import { createBudWasmBridge } from './bud-hydrate.js?v=2';

window.bud_data = {};
window.hydrate_queue = [];
window.__wasm_capability_registry = window.__wasm_capability_registry || {};
window.registerWasmCapability = (namespace, factory) => {
	window.__wasm_capability_registry[namespace] = factory;
};

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
	const activeBridges = [];

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
		const getWasm = () => instance ? instance.exports : null;

		// Dynamically load declared capability bridge scripts if specified on body or root
		const capAttr = [
			document.body.getAttribute('data-capabilities') || '',
			document.querySelector('#bud-root')?.getAttribute('data-capabilities') || '',
			spec.root?.getAttribute('data-capabilities') || ''
		].join(' ');
		const declaredCaps = [...new Set(capAttr.trim().split(/\s+/).filter(Boolean))];
		for (const cap of declaredCaps) {
			if (cap === 'bud') continue;
			if (!window.__wasm_capability_registry[cap]) {
				try {
					await import(`/${cap}-bridge.js`);
				} catch (_) {
					// Optional capability bridge script
				}
			}
		}

		const wasmModule = await WebAssembly.compile(wasmBytes);
		const declaredImports = WebAssembly.Module.imports(wasmModule);
		const importModules = new Set(declaredImports.map(imp => imp.module));

		const budImports = bridge.makeImports();
		const imports = {
			env: {
				...budImports,
				__main_argc_argv: () => 0
			},
			wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 })
		};

		if (importModules.has('bud')) {
			imports.bud = budImports;
		}

		for (const [ns, factory] of Object.entries(window.__wasm_capability_registry)) {
			try {
				const capBridge = factory(spec.root, getWasm);
				if (capBridge) {
					activeBridges.push(capBridge);
					if (typeof capBridge.makeImports === 'function') {
						const capImports = capBridge.makeImports();
						imports[ns] = capImports;
						Object.assign(imports.env, capImports);
					}
				}
			} catch (e) {
				console.error(`Failed to init capability bridge for ${ns}:`, e);
			}
		}

		const origEmitPatch = budImports.bud_host_emit_patch;
		if (typeof origEmitPatch === 'function') {
			const hookedEmitPatch = function(...args) {
				const res = origEmitPatch.apply(this, args);
				for (const cap of activeBridges) {
					if (typeof cap.mount === 'function') cap.mount();
				}
				return res;
			};
			budImports.bud_host_emit_patch = hookedEmitPatch;
			if (imports.env) imports.env.bud_host_emit_patch = hookedEmitPatch;
			if (imports.bud) imports.bud.bud_host_emit_patch = hookedEmitPatch;
		}

		const origFlush = budImports.bud_host_flush;
		if (typeof origFlush === 'function') {
			const hookedFlush = function(...args) {
				const res = origFlush.apply(this, args);
				for (const cap of activeBridges) {
					if (typeof cap.mount === 'function') cap.mount();
				}
				return res;
			};
			budImports.bud_host_flush = hookedFlush;
			if (imports.env) imports.env.bud_host_flush = hookedFlush;
			if (imports.bud) imports.bud.bud_host_flush = hookedFlush;
		}

		for (const imp of declaredImports) {
			if (!imports[imp.module]) {
				imports[imp.module] = {};
			}
			if (typeof imports[imp.module][imp.name] === 'undefined') {
				if (imports.env && typeof imports.env[imp.name] !== 'undefined') {
					imports[imp.module][imp.name] = imports.env[imp.name];
				} else if (imports.bud && typeof imports.bud[imp.name] !== 'undefined') {
					imports[imp.module][imp.name] = imports.bud[imp.name];
				} else {
					imports[imp.module][imp.name] = () => 0;
				}
			}
		}

		instance = await WebAssembly.instantiate(wasmModule, imports);

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
		window.__active_bridges = (window.__active_bridges || []).concat(activeBridges);
		for (const cap of activeBridges) {
			if (typeof cap.mount === 'function') cap.mount();
		}

		let rafId = 0;
		let lastTime = 0;
		if (typeof instance.exports.wasm_tick === 'function' && !spec.global) {
			const tick = (now) => {
				if (document.hidden) {
					rafId = requestAnimationFrame(tick);
					return;
				}
				if (!lastTime) lastTime = now;
				const dt = Math.min((now - lastTime) / 1000, 0.1);
				lastTime = now;
				const w = (spec.root && spec.root.clientWidth) || window.innerWidth || 640;
				const h = (spec.root && spec.root.clientHeight) || window.innerHeight || 480;
				try {
					instance.exports.wasm_tick(dt, w, h);
				} catch (e) {
					console.error('wasm_tick error:', e);
				}
				rafId = requestAnimationFrame(tick);
			};
			rafId = requestAnimationFrame(tick);
		}

		setTimeout(() => bridge.autoDebug(), 100);
		return {
			name: spec.name,
			bridge,
			global: spec.global,
			activeBridges,
			stopTick() { if (rafId) cancelAnimationFrame(rafId); }
		};
	} catch (error) {
		if (bridge) bridge.destroy();
		for (const cap of activeBridges) {
			if (typeof cap.destroy === 'function') cap.destroy();
		}
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
