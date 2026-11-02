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
				// A canvas that asked to fit (data-bud-canvas-fit) is drawn at its
				// own on-screen size; otherwise the root's box is the frame size.
				const fit = spec.root && spec.root.querySelector
					? spec.root.querySelector('[data-bud-canvas-fit]') : null;
				const box = (fit && fit.clientWidth && fit.clientHeight) ? fit : spec.root;
				const w = (box && box.clientWidth) || window.innerWidth || 640;
				const h = (box && box.clientHeight) || window.innerHeight || 480;
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

function decodeWasmString(memory, ptr, len) {
	if (!ptr || len <= 0 || !memory) return '';
	return new TextDecoder('utf-8').decode(new Uint8Array(memory.buffer, ptr, len));
}

async function loadSiteSubstrate(names) {
	try {
		const res = await fetch('/site.wasm');
		if (!res.ok) return false;
		const wasmBytes = await res.arrayBuffer();

		const chromeRoot = document.querySelector('#chrome-root');
		const budRoot = document.querySelector('#bud-root');
		const pageBridge = budRoot ? createBudWasmBridge(budRoot) : null;
		const chromeBridge = chromeRoot ? createBudWasmBridge(chromeRoot) : null;
		const activeBridge = pageBridge || chromeBridge;
		if (!activeBridge) return false;

		const activeBridges = [];
		let instance = null;
		const getWasm = () => instance ? instance.exports : null;

		const capAttr = [
			document.body.getAttribute('data-capabilities') || '',
			budRoot?.getAttribute('data-capabilities') || '',
			chromeRoot?.getAttribute('data-capabilities') || ''
		].join(' ');
		const declaredCaps = [...new Set(capAttr.trim().split(/\s+/).filter(Boolean))];
		for (const cap of declaredCaps) {
			if (cap === 'bud' || cap === 'qsys') continue;
			if (!window.__wasm_capability_registry[cap]) {
				try {
					await import(`/${cap}-bridge.js`);
				} catch (_) {}
			}
		}

		const wasmModule = await WebAssembly.compile(wasmBytes);
		const declaredImports = WebAssembly.Module.imports(wasmModule);
		const budImports = activeBridge.makeImports();

		const emitToBridges = (op_ptr, op_len, a_ptr, a_len, b_ptr, b_len, c_ptr, c_len) => {
			if (pageBridge && pageBridge.memory) {
				const op = decodeWasmString(pageBridge.memory, op_ptr, op_len);
				const a = decodeWasmString(pageBridge.memory, a_ptr, a_len);
				const b = decodeWasmString(pageBridge.memory, b_ptr, b_len);
				const c = decodeWasmString(pageBridge.memory, c_ptr, c_len);
				pageBridge.host.replay([{ op, a, b, c }]);
			}
			if (chromeBridge && chromeBridge.memory) {
				const op = decodeWasmString(chromeBridge.memory, op_ptr, op_len);
				const a = decodeWasmString(chromeBridge.memory, a_ptr, a_len);
				const b = decodeWasmString(chromeBridge.memory, b_ptr, b_len);
				const c = decodeWasmString(chromeBridge.memory, c_ptr, c_len);
				chromeBridge.host.replay([{ op, a, b, c }]);
			}
		};

		const handleFetch = (url_ptr, url_len, request_id) => {
			if (!instance) return;
			const url = decodeWasmString(instance.exports.memory, url_ptr, url_len);
			fetch(url)
				.then(r => r.ok ? r.text() : '')
				.then(text => {
					if (!instance) return;
					const malloc = instance.exports.malloc;
					const fn = instance.exports.wasm_fetch_callback;
					if (!malloc || !fn) return;
					const bytes = new TextEncoder().encode(text + '\0');
					const ptr = malloc(bytes.length);
					if (ptr) {
						new Uint8Array(instance.exports.memory.buffer, ptr, bytes.length).set(bytes);
						fn(request_id, ptr, bytes.length - 1);
						if (instance.exports.free) instance.exports.free(ptr);
					}
				})
				.catch(err => console.error('fetch error:', err));
		};

		const handleSetLocation = (url_ptr, url_len) => {
			if (!instance) return;
			const url = decodeWasmString(instance.exports.memory, url_ptr, url_len);
			if (url) history.replaceState(null, '', url);
		};

		const handleLog = (msg_ptr, msg_len) => {
			if (!instance) return;
			const msg = decodeWasmString(instance.exports.memory, msg_ptr, msg_len);
			console.debug('[wasm]', msg);
		};

		const imports = {
			env: {
				...budImports,
				bud_host_emit_patch: emitToBridges,
				bud_host_fetch: handleFetch,
				bud_host_set_location: handleSetLocation,
				bud_host_log: handleLog,
				__main_argc_argv: () => 0
			},
			bud: {
				...budImports,
				bud_host_emit_patch: emitToBridges,
				bud_host_fetch: handleFetch,
				bud_host_set_location: handleSetLocation,
				bud_host_log: handleLog
			},
			qsys: {
				qsys_host_emit_patch: (slot, op_ptr, op_len, a_ptr, a_len, b_ptr, b_len, c_ptr, c_len) => {
					const target = slot === 0 ? chromeBridge : pageBridge;
					if (target && target.memory) {
						const op = decodeWasmString(target.memory, op_ptr, op_len);
						const a = decodeWasmString(target.memory, a_ptr, a_len);
						const b = decodeWasmString(target.memory, b_ptr, b_len);
						const c = decodeWasmString(target.memory, c_ptr, c_len);
						target.host.replay([{ op, a, b, c }]);
					}
				},
				qsys_host_log: handleLog,
				qsys_host_fetch: handleFetch,
				qsys_host_set_location: handleSetLocation
			},
			wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 })
		};

		for (const [ns, factory] of Object.entries(window.__wasm_capability_registry)) {
			try {
				const capBridge = factory(budRoot || chromeRoot, getWasm);
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

		for (const imp of declaredImports) {
			if (!imports[imp.module]) imports[imp.module] = {};
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

		if (instance.exports._initialize) instance.exports._initialize();
		else if (instance.exports.__wasm_call_ctors) instance.exports.__wasm_call_ctors();

		if (instance.exports.qsys_kernel_boot) {
			instance.exports.qsys_kernel_boot();
		}

		// Mount Slot 0: site_chrome
		if (chromeBridge && instance.exports.qsys_kernel_mount) {
			chromeBridge.setWasm(instance.exports);
			const chromeStateEl = document.getElementById('chrome-state');
			const chromeJson = chromeStateEl?.textContent || '{}';
			const nameBytes = new TextEncoder().encode('site_chrome\0');
			const jsonBytes = new TextEncoder().encode(chromeJson + '\0');
			const namePtr = instance.exports.malloc(nameBytes.length);
			const jsonPtr = instance.exports.malloc(jsonBytes.length);
			new Uint8Array(instance.exports.memory.buffer, namePtr, nameBytes.length).set(nameBytes);
			new Uint8Array(instance.exports.memory.buffer, jsonPtr, jsonBytes.length).set(jsonBytes);
			instance.exports.qsys_kernel_mount(0, namePtr, jsonPtr, jsonBytes.length - 1);
			if (instance.exports.free) {
				instance.exports.free(namePtr);
				instance.exports.free(jsonPtr);
			}
			chromeBridge.mount();
		}

		// Mount Slot 1: page module
		const pageModule = names.find(n => n !== 'site_chrome') || budRoot?.getAttribute('data-module') || '';
		if (pageBridge && pageModule && instance.exports.qsys_kernel_mount) {
			pageBridge.setWasm(instance.exports);
			const budStateEl = document.getElementById('bud-state');
			const budJson = budStateEl?.textContent || '{}';
			const nameBytes = new TextEncoder().encode(pageModule + '\0');
			const jsonBytes = new TextEncoder().encode(budJson + '\0');
			const namePtr = instance.exports.malloc(nameBytes.length);
			const jsonPtr = instance.exports.malloc(jsonBytes.length);
			new Uint8Array(instance.exports.memory.buffer, namePtr, nameBytes.length).set(nameBytes);
			new Uint8Array(instance.exports.memory.buffer, jsonPtr, jsonBytes.length).set(jsonBytes);
			instance.exports.qsys_kernel_mount(1, namePtr, jsonPtr, jsonBytes.length - 1);
			if (instance.exports.free) {
				instance.exports.free(namePtr);
				instance.exports.free(jsonPtr);
			}
			pageBridge.mount();
		}

		window.__bud_bridges = {
			site_chrome: chromeBridge,
			[pageModule]: pageBridge
		};
		window.__bud_bridge = pageBridge || chromeBridge;

		// Frame loop contract (wasm_tick)
		let rafId = 0;
		let lastTime = 0;
		if (typeof instance.exports.wasm_tick === 'function') {
			const tick = (now) => {
				if (document.hidden) {
					rafId = requestAnimationFrame(tick);
					return;
				}
				if (!lastTime) lastTime = now;
				const dt = Math.min((now - lastTime) / 1000, 0.1);
				lastTime = now;
				const box = budRoot || chromeRoot || document.body;
				const w = (box && box.clientWidth) || window.innerWidth || 640;
				const h = (box && box.clientHeight) || window.innerHeight || 480;
				try {
					instance.exports.wasm_tick(dt, w, h);
				} catch (e) {
					console.error('wasm_tick error:', e);
				}
				rafId = requestAnimationFrame(tick);
			};
			rafId = requestAnimationFrame(tick);
		}

		if (pageBridge) setTimeout(() => pageBridge.autoDebug(), 100);
		document.body.setAttribute('data-wasm-loaded', '');
		return true;
	} catch (err) {
		console.error('Failed to load site substrate WASM:', err);
		return false;
	}
}

async function init() {
	const value = document.body.getAttribute('data-modules') || '';
	const names = [...new Set(value.trim().split(/\s+/).filter(Boolean))];

	// Dedicated game/engine reactors bypass single-kernel probe
	const isDedicated = names.includes('nd');

	if (!isDedicated && names.length > 0) {
		const booted = await loadSiteSubstrate(names);
		if (booted) return;
	}

	// Fall back to existing multi-instance per-module loader
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
