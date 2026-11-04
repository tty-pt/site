/**
 * @file stele-client.js
 * Universal WebAssembly Substrate Client Runtime
 *
 * Implements dynamic on-demand component WASM instantiation with isolated linear
 * memory instances, multi-slot hydration, declarative capability loading, and pure-C
 * event routing with zero site-specific JS.
 */

import { createBudWasmBridge } from './bud-hydrate.js';

window.__wasm_capability_registry = window.__wasm_capability_registry || {};
window.registerWasmCapability = (namespace, factory) => {
	window.__wasm_capability_registry[namespace] = factory;
};

function createSlotBridge(root) {
	if (typeof window.__wasm_slot_bridge_factory === 'function') {
		return window.__wasm_slot_bridge_factory(root);
	}
	if (typeof createBudWasmBridge === 'function') {
		return createBudWasmBridge(root);
	}
	return null;
}

/**
 * Modernizes WASM module instantiation to prioritize WebAssembly.instantiateStreaming(fetch(url), imports)
 * with robust fallback to fetch(url).then(r => r.arrayBuffer()).then(buf => WebAssembly.instantiate(buf, imports)).
 *
 * @param {string} url - WASM resource URL
 * @param {object} imports - WebAssembly import object
 * @returns {Promise<WebAssembly.WebAssemblyInstantiatedSource|null>}
 */
async function instantiateWasmStreamingWithFallback(url, imports) {
	const fullUrl = (typeof window !== 'undefined' && window.location?.origin && url.startsWith('/'))
		? `${window.location.origin}${url}`
		: url;
	try {
		if (typeof WebAssembly.instantiateStreaming === 'function') {
			const res = await fetch(fullUrl);
			if (res.ok) {
				try {
					return await WebAssembly.instantiateStreaming(res.clone(), imports);
				} catch (_) {
					const buf = await res.arrayBuffer();
					return await WebAssembly.instantiate(buf, imports);
				}
			}
		}
	} catch (_) {
		// Fall back to direct arrayBuffer fetch
	}
	try {
		const res = await fetch(fullUrl);
		if (res.ok) {
			const buf = await res.arrayBuffer();
			return await WebAssembly.instantiate(buf, imports);
		}
	} catch (e) {
		console.warn(`WASM fetch/instantiate failed for ${url}:`, e);
	}
	return null;
}

export class SteleWasmClient {
	constructor() {
		this.instance = null;
		this.memory = null;
		this.slotInstances = new Map(); // slot -> { instance, memory, moduleName }
		this.slotBridges = new Map();
		this.activeBridges = [];
	}

	decodeString(memory, ptr, len) {
		const mem = memory || this.memory;
		if (!ptr || len <= 0 || !mem) return '';
		const bytes = new Uint8Array(mem.buffer, ptr, len);
		return new TextDecoder('utf-8').decode(bytes);
	}

	encodeString(instance, memory, str) {
		const inst = instance || this.instance;
		const mem = memory || this.memory;
		if (!inst?.exports.malloc || !mem) return [0, 0];
		const encoded = new TextEncoder().encode(str);
		const ptr = inst.exports.malloc(encoded.length + 1);
		const view = new Uint8Array(mem.buffer, ptr, encoded.length + 1);
		view.set(encoded);
		view[encoded.length] = 0;
		return [ptr, encoded.length];
	}

	async loadCapabilities() {
		const capAttr = [
			document.body?.getAttribute('data-capabilities') || '',
			document.querySelector('[data-slot="1"], [data-capabilities]')?.getAttribute('data-capabilities') || '',
			document.querySelector('[data-slot="0"]')?.getAttribute('data-capabilities') || ''
		].join(' ');
		const declaredCaps = [...new Set(capAttr.trim().split(/\s+/).filter(Boolean))];
		for (const cap of declaredCaps) {
			if (cap === 'stele' || cap === 'stl') continue;
			if (!window.__wasm_capability_registry[cap]) {
				try {
					await import(`/${cap}-bridge.js`);
				} catch (_) {
					// Optional capability bridge script
				}
			}
		}
	}

	buildSlotImports(slot) {
		const hostEmitPatch = (_slot, opPtr, opLen, aPtr, aLen, bPtr, bLen, cPtr, cLen) => {
			const targetSlot = typeof _slot === 'number' && _slot >= 0 ? _slot : slot;
			const bridge = this.slotBridges.get(targetSlot);
			if (!bridge) return;
			const mem = this.slotInstances.get(targetSlot)?.memory || this.memory;
			const op = this.decodeString(mem, opPtr, opLen);
			const a = this.decodeString(mem, aPtr, aLen);
			const b = this.decodeString(mem, bPtr, bLen);
			const c = this.decodeString(mem, cPtr, cLen);
			if (typeof bridge.emitPatch === 'function') {
				bridge.emitPatch(op, a, b, c);
			} else if (typeof bridge.host?.replay === 'function') {
				bridge.host.replay([{ op, a, b, c }]);
			}
		};

		const hostLog = (msgPtr, len) => {
			const mem = this.slotInstances.get(slot)?.memory || this.memory;
			console.log(this.decodeString(mem, msgPtr, len));
		};

		const hostFetch = (urlPtr, len, reqId) => {
			const mem = this.slotInstances.get(slot)?.memory || this.memory;
			const url = this.decodeString(mem, urlPtr, len);
			fetch(url).then(r => r.text()).then(txt => {
				// Async fetch callback dispatch if needed
			}).catch(err => console.error('stl_host_fetch failed:', err));
		};

		const hostSetLocation = (urlPtr, len) => {
			const mem = this.slotInstances.get(slot)?.memory || this.memory;
			const url = this.decodeString(mem, urlPtr, len);
			if (url) window.location.href = url;
		};

		const hostInvalidate = (s) => {
			const targetSlot = typeof s === 'number' && s >= 0 ? s : slot;
			this.updateSlot(targetSlot);
		};

		const steleImports = {
			stl_host_emit_patch: hostEmitPatch,
			stl_host_log: hostLog,
			stl_host_fetch: hostFetch,
			stl_host_set_location: hostSetLocation,
			stl_host_invalidate: hostInvalidate,
			stele_host_emit_patch: hostEmitPatch,
			stele_host_log: hostLog,
			stele_host_fetch: hostFetch,
			stele_host_set_location: hostSetLocation,
			stele_host_invalidate: hostInvalidate
		};

		const imports = {
			env: new Proxy({
				...steleImports,
				__main_argc_argv: () => 0
			}, {
				get: (target, prop) => {
					if (prop in target) return target[prop];
					return () => 0;
				}
			}),
			stele: steleImports,
			stl: steleImports,
			wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 })
		};

		const getWasm = () => {
			return this.slotInstances.get(slot)?.instance?.exports || (this.instance ? this.instance.exports : null);
		};
		for (const [ns, factory] of Object.entries(window.__wasm_capability_registry)) {
			try {
				const root = this.slotBridges.get(slot)?.root || document.body;
				const capBridge = factory(root, getWasm);
				if (capBridge) {
					this.activeBridges.push(capBridge);
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
		return imports;
	}

	async boot(wasmUrl = '/stele.wasm') {
		await this.loadCapabilities();

		try {
			const imports = this.buildSlotImports(0);
			const source = await instantiateWasmStreamingWithFallback(wasmUrl, imports);
			if (source?.instance) {
				this.instance = source.instance;
				this.memory = source.instance.exports.memory;

				const bootFn = this.instance.exports.stl_kernel_boot || this.instance.exports.stele_kernel_boot;
				if (bootFn) {
					bootFn();
				}
			}
		} catch (_) {
			// Standalone kernel optional when dynamic component WASMs are used
		}

		await this.bindSlots();
		this.attachEventDelegation();
		return true;
	}

	async bindSlots() {
		const modulesAttr = (typeof document !== 'undefined' && document.body?.getAttribute('data-modules')) || '';
		const tokens = modulesAttr.trim().split(/\s+/).filter(Boolean);
		let mountedCount = 0;

		const chromeRoot = document.getElementById('chrome-root') || document.querySelector('[data-slot="0"]');
		if (chromeRoot && (tokens.length === 0 || tokens.includes('site_chrome'))) {
			const bridge = createSlotBridge(chromeRoot);
			if (bridge) this.slotBridges.set(0, bridge);
			const stateEl = document.getElementById('chrome-state');
			const json = stateEl?.textContent || '{}';
			if (await this.mountSlot(0, 'site_chrome', json)) mountedCount++;
		}

		const pageRoot = document.querySelector('[data-slot="1"], [data-module]');
		if (pageRoot) {
			const pageModule = tokens.find(t => t !== 'site_chrome') || pageRoot.getAttribute('data-module') || '';
			if (pageModule) {
				const bridge = createSlotBridge(pageRoot);
				if (bridge) this.slotBridges.set(1, bridge);
				const stateEl = document.querySelector('[data-state="1"], #page-state');
				const json = stateEl?.textContent || '{}';
				if (await this.mountSlot(1, pageModule, json)) {
					mountedCount++;
				}
			}
		}
		return mountedCount > 0;
	}

	async mountSlot(slot, moduleName, json) {
		// 1. Try on-demand dynamic component WASM instantiation with isolated linear memory
		try {
			const candidateUrls = [`/${moduleName}.wasm`, `/${moduleName}_detail.wasm`];
			for (const compUrl of candidateUrls) {
				const compImports = this.buildSlotImports(slot);
				const source = await instantiateWasmStreamingWithFallback(compUrl, compImports);
				if (source?.instance) {
					const instance = source.instance;
					const compMemory = instance.exports.memory;
					this.slotInstances.set(slot, { instance, memory: compMemory, moduleName });

					if (instance.exports._initialize) instance.exports._initialize();

					if (instance.exports.wasm_init) {
						const [jsonPtr, jsonLen] = this.encodeString(instance, compMemory, json);
						instance.exports.wasm_init(jsonPtr, jsonLen);
						if (instance.exports.free) instance.exports.free(jsonPtr);
					}
					this.updateSlot(slot);
					return true;
				}
			}
		} catch (err) {
			console.warn(`Dynamic component WASM load failed for ${moduleName}:`, err);
		}

		// 2. Fall back to shared substrate kernel slot mount if available
		const mountFn = this.instance?.exports.stl_kernel_mount || this.instance?.exports.stele_kernel_mount;
		if (mountFn) {
			const [namePtr] = this.encodeString(this.instance, this.memory, moduleName);
			const [jsonPtr, jsonLen] = this.encodeString(this.instance, this.memory, json);
			const rc = mountFn(slot, namePtr, jsonPtr, jsonLen);
			if (this.instance.exports.free) {
				this.instance.exports.free(namePtr);
				this.instance.exports.free(jsonPtr);
			}
			return rc === 0;
		}
		return false;
	}

	updateSlot(slot) {
		const comp = this.slotInstances.get(slot);
		if (comp?.instance) {
			if (comp.instance.exports.wasm_update) {
				comp.instance.exports.wasm_update(slot);
				return;
			}
			const updateFn = comp.instance.exports.stl_kernel_update || comp.instance.exports.stele_kernel_update;
			if (updateFn) {
				updateFn(slot);
				return;
			}
		}
		const instUpdateFn = this.instance?.exports.stl_kernel_update || this.instance?.exports.stele_kernel_update;
		if (instUpdateFn) {
			instUpdateFn(slot);
		}
	}

	attachEventDelegation() {
		document.addEventListener('click', (evt) => {
			const actionEl = evt.target.closest('[data-action]');
			if (actionEl) {
				const actionId = parseInt(actionEl.getAttribute('data-action'), 10);
				const actionData = actionEl.getAttribute('data-action-payload') || '';
				const slotEl = actionEl.closest('[data-slot]');
				const slot = slotEl ? parseInt(slotEl.getAttribute('data-slot'), 10) : 0;

				const comp = this.slotInstances.get(slot);
				if (comp?.instance?.exports.wasm_action) {
					const [dataPtr] = this.encodeString(comp.instance, comp.memory, actionData);
					comp.instance.exports.wasm_action(actionId, dataPtr);
					if (comp.instance.exports.free) comp.instance.exports.free(dataPtr);
					this.updateSlot(slot);
					return;
				}

				const actionFn = this.instance?.exports.stl_kernel_action || this.instance?.exports.stele_kernel_action;
				if (actionFn) {
					const [dataPtr] = this.encodeString(this.instance, this.memory, actionData);
					actionFn(slot, actionId, dataPtr);
					if (this.instance.exports.free) this.instance.exports.free(dataPtr);
				}
				return;
			}

			// Zero-reload warm-memory slot navigation
			const linkEl = evt.target.closest('a[href]');
			if (!linkEl) return;
			if (evt.defaultPrevented || evt.button !== 0 || evt.metaKey || evt.ctrlKey || evt.shiftKey || evt.altKey) return;
			if (linkEl.target && linkEl.target !== '_self') return;
			if (linkEl.hasAttribute('download')) return;

			const href = linkEl.getAttribute('href');
			if (!href || href.startsWith('#') || href.startsWith('javascript:')) return;

			const targetUrl = new URL(href, window.location.href);
			if (targetUrl.origin !== window.location.origin) return;

			evt.preventDefault();
			this.navigate(targetUrl.pathname + targetUrl.search + targetUrl.hash);
		});

		window.addEventListener('popstate', () => {
			this.navigate(window.location.pathname + window.location.search + window.location.hash, { pushHistory: false });
		});
	}

	async navigate(url, { pushHistory = true } = {}) {
		try {
			const res = await fetch(url, {
				headers: { 'Accept': 'text/html' }
			});
			if (!res.ok) {
				window.location.href = url;
				return;
			}

			const html = await res.text();
			const parser = new DOMParser();
			const doc = parser.parseFromString(html, 'text/html');

			if (doc.title) {
				document.title = doc.title;
			}

			const newModulesAttr = doc.body.getAttribute('data-modules') || '';
			const newTokens = newModulesAttr.trim().split(/\s+/).filter(Boolean);
			const newPageModule = newTokens.find(t => t !== 'site_chrome') || doc.querySelector('[data-slot="1"], [data-module]')?.getAttribute('data-module') || '';

			const newStateEl = doc.querySelector('[data-state="1"], #page-state, #bud-state');
			const newJson = newStateEl?.textContent || '{}';

			const newChromeStateEl = doc.getElementById('chrome-state');
			if (newChromeStateEl) {
				const chromeJson = newChromeStateEl.textContent;
				const chromeComp = this.slotInstances.get(0);
				if (chromeComp?.instance?.exports.wasm_init) {
					const [jsonPtr, jsonLen] = this.encodeString(chromeComp.instance, chromeComp.memory, chromeJson);
					chromeComp.instance.exports.wasm_init(jsonPtr, jsonLen);
					if (chromeComp.instance.exports.free) chromeComp.instance.exports.free(jsonPtr);
					this.updateSlot(0);
				}
			}

			// Unmount current Slot 1 component
			const currentComp = this.slotInstances.get(1);
			if (currentComp?.instance?.exports.wasm_unmount) {
				currentComp.instance.exports.wasm_unmount(1);
			}

			// Swap Slot 1 DOM
			const currentPageRoot = document.querySelector('[data-slot="1"], [data-module], #bud-root');
			const newPageRoot = doc.querySelector('[data-slot="1"], [data-module], #bud-root');
			if (currentPageRoot && newPageRoot) {
				currentPageRoot.innerHTML = newPageRoot.innerHTML;
				if (newPageModule) currentPageRoot.setAttribute('data-module', newPageModule);
				const newBridge = createSlotBridge(currentPageRoot);
				if (newBridge) this.slotBridges.set(1, newBridge);
			}

			if (newPageModule) {
				await this.mountSlot(1, newPageModule, newJson);
			}

			if (pushHistory) {
				if (typeof history !== 'undefined' && history.pushState) {
					history.pushState(null, '', url);
				} else if (typeof window !== 'undefined' && window.history?.pushState) {
					window.history.pushState(null, '', url);
				}
			}
		} catch (err) {
			console.warn('[stele-client] SPA navigation failed, falling back to full navigation:', err);
			window.location.href = url;
		}
	}
}

export const steleClient = new SteleWasmClient();
export const stlClient = steleClient;

if (typeof window !== 'undefined') {
	window.SteleWasmClient = SteleWasmClient;
	window.steleClient = steleClient;
	window.stlClient = steleClient;

	if (typeof document !== 'undefined') {
		const init = () => {
			steleClient.boot().catch(err => console.error('[stele-client] Boot failed:', err));
		};
		if (document.readyState === 'loading') {
			document.addEventListener('DOMContentLoaded', init);
		} else {
			init();
		}
	}
}
