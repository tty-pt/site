/**
 * @file qsys-client.js
 * Universal WebAssembly Substrate Client Runtime
 *
 * Implements strictly single WebAssembly instantiation, multi-slot hydration,
 * declarative capability loading, and pure-C event routing with zero site-specific JS.
 */

import { createBudWasmBridge } from './bud-hydrate.js?v=2';

window.__wasm_capability_registry = window.__wasm_capability_registry || {};
window.registerWasmCapability = (namespace, factory) => {
	window.__wasm_capability_registry[namespace] = factory;
};

class QsysWasmClient {
	constructor() {
		this.instance = null;
		this.memory = null;
		this.slotBridges = new Map();
		this.activeBridges = [];
	}

	decodeString(ptr, len) {
		if (!ptr || len <= 0 || !this.memory) return '';
		const bytes = new Uint8Array(this.memory.buffer, ptr, len);
		return new TextDecoder('utf-8').decode(bytes);
	}

	encodeString(str) {
		if (!this.instance?.exports.malloc) return [0, 0];
		const encoded = new TextEncoder().encode(str);
		const ptr = this.instance.exports.malloc(encoded.length + 1);
		const view = new Uint8Array(this.memory.buffer, ptr, encoded.length + 1);
		view.set(encoded);
		view[encoded.length] = 0;
		return [ptr, encoded.length];
	}

	async loadCapabilities() {
		const capAttr = [
			document.body?.getAttribute('data-capabilities') || '',
			document.querySelector('#bud-root')?.getAttribute('data-capabilities') || '',
			document.querySelector('#chrome-root')?.getAttribute('data-capabilities') || ''
		].join(' ');
		const declaredCaps = [...new Set(capAttr.trim().split(/\s+/).filter(Boolean))];
		for (const cap of declaredCaps) {
			if (cap === 'bud' || cap === 'qsys') continue;
			if (!window.__wasm_capability_registry[cap]) {
				try {
					await import(`/${cap}-bridge.js`);
				} catch (_) {
					// Optional capability bridge script
				}
			}
		}
	}

	buildImports() {
		const qsysImports = {
			qsys_host_emit_patch: (slot, opPtr, opLen, aPtr, aLen, bPtr, bLen, cPtr, cLen) => {
				const bridge = this.slotBridges.get(slot);
				if (!bridge) return;
				const op = this.decodeString(opPtr, opLen);
				const a = this.decodeString(aPtr, aLen);
				const b = this.decodeString(bPtr, bLen);
				const c = this.decodeString(cPtr, cLen);
				const emitFn = bridge.makeImports()?.bud_host_emit_patch;
				if (typeof emitFn === 'function') {
					emitFn(opPtr, opLen, aPtr, aLen, bPtr, bLen, cPtr, cLen);
				}
			},
			qsys_host_log: (msgPtr, len) => {
				console.log(this.decodeString(msgPtr, len));
			},
			qsys_host_fetch: (urlPtr, len, reqId) => {
				const url = this.decodeString(urlPtr, len);
				fetch(url).then(r => r.text()).then(txt => {
					// Async fetch callback dispatch if needed
				}).catch(err => console.error('qsys_host_fetch failed:', err));
			},
			qsys_host_set_location: (urlPtr, len) => {
				const url = this.decodeString(urlPtr, len);
				if (url) window.location.href = url;
			}
		};

		const imports = {
			env: new Proxy({
				...qsysImports,
				__main_argc_argv: () => 0
			}, {
				get: (target, prop) => {
					if (prop in target) return target[prop];
					return () => 0;
				}
			}),
			qsys: qsysImports,
			wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 })
		};

		const getWasm = () => this.instance ? this.instance.exports : null;
		for (const [ns, factory] of Object.entries(window.__wasm_capability_registry)) {
			try {
				const capBridge = factory(document.body, getWasm);
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

	async boot(wasmUrl = '/qsys.wasm') {
		await this.loadCapabilities();
		const imports = this.buildImports();

		try {
			const res = await fetch(wasmUrl);
			if (!res.ok) {
				console.warn('qsys.wasm not available:', res.status);
				return false;
			}
			const { instance } = await WebAssembly.instantiateStreaming(res, imports);
			this.instance = instance;
			this.memory = instance.exports.memory;

			if (this.instance.exports.qsys_kernel_boot) {
				this.instance.exports.qsys_kernel_boot();
			}

			this.bindSlots();
			this.attachEventDelegation();
			return true;
		} catch (err) {
			console.error('Failed to boot Qsys WebAssembly Substrate:', err);
			return false;
		}
	}

	bindSlots() {
		const modulesAttr = (typeof document !== 'undefined' && document.body?.getAttribute('data-modules')) || '';
		const tokens = modulesAttr.trim().split(/\s+/).filter(Boolean);
		let mountedCount = 0;

		const chromeRoot = document.getElementById('chrome-root');
		if (chromeRoot && (tokens.length === 0 || tokens.includes('site_chrome'))) {
			this.slotBridges.set(0, createBudWasmBridge(chromeRoot));
			const stateEl = document.getElementById('chrome-state');
			const json = stateEl?.textContent || '{}';
			if (this.mountSlot(0, 'site_chrome', json)) mountedCount++;
		}

		const budRoot = document.getElementById('bud-root');
		if (budRoot) {
			const pageModule = tokens.find(t => t !== 'site_chrome') || budRoot.getAttribute('data-module') || '';
			if (pageModule) {
				this.slotBridges.set(1, createBudWasmBridge(budRoot));
				const stateEl = document.getElementById('bud-state');
				const json = stateEl?.textContent || '{}';
				if (this.mountSlot(1, pageModule, json)) {
					mountedCount++;
				} else {
					// Required page module not present in this WASM binary
					return false;
				}
			}
		}
		return mountedCount > 0;
	}

	mountSlot(slot, moduleName, json) {
		if (!this.instance?.exports.qsys_kernel_mount) return false;
		const [namePtr] = this.encodeString(moduleName);
		const [jsonPtr, jsonLen] = this.encodeString(json);
		const rc = this.instance.exports.qsys_kernel_mount(slot, namePtr, jsonPtr, jsonLen);
		if (this.instance.exports.free) {
			this.instance.exports.free(namePtr);
			this.instance.exports.free(jsonPtr);
		}
		return rc === 0;
	}

	attachEventDelegation() {
		document.addEventListener('click', (evt) => {
			const actionEl = evt.target.closest('[data-action]');
			if (!actionEl || !this.instance?.exports.qsys_kernel_action) return;

			const actionId = parseInt(actionEl.getAttribute('data-action'), 10);
			const actionData = actionEl.getAttribute('data-action-payload') || '';
			const slotEl = actionEl.closest('#chrome-root, #bud-root');
			const slot = slotEl?.id === 'chrome-root' ? 0 : 1;

			const [dataPtr] = this.encodeString(actionData);
			this.instance.exports.qsys_kernel_action(slot, actionId, dataPtr);
			if (this.instance.exports.free) {
				this.instance.exports.free(dataPtr);
			}
		});
	}
}

export const qsysClient = new QsysWasmClient();
