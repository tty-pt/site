/**
 * @file stele-client.js
 * Universal WebAssembly Substrate Client Runtime
 *
 * Implements dynamic on-demand component WASM instantiation with isolated linear
 * memory instances, multi-slot hydration, declarative capability loading, and pure-C
 * event routing with zero site-specific JS.
 */

const globalScope = typeof window !== 'undefined' ? window : globalThis;
globalScope.__wasm_capability_registry = globalScope.__wasm_capability_registry || {};
globalScope.registerWasmCapability = (namespace, factory) => {
	globalScope.__wasm_capability_registry[namespace] = factory;
};

let s_bridgeFactory = null;

function createSlotBridge(root) {
	if (typeof s_bridgeFactory === 'function') {
		return s_bridgeFactory(root);
	}
	if (typeof globalScope.__wasm_slot_bridge_factory === 'function') {
		return globalScope.__wasm_slot_bridge_factory(root);
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
	static registerBridgeFactory(factory) {
		s_bridgeFactory = factory;
	}

	_createBridge(root) {
		return createSlotBridge(root);
	}

	constructor() {
		this.instance = null;
		this.sharedMemory = (typeof WebAssembly !== 'undefined' && typeof WebAssembly.Memory === 'function')
			? new WebAssembly.Memory({ initial: 256, maximum: 4096 })
			: null;
		this.memory = this.sharedMemory;
		this.slotInstances = new Map(); // slot -> { instance, memory, moduleName }
		this.slotBridges = new Map();
		this.activeBridges = [];
		this.eventListeners = new Map();
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

	writeBytes(instance, memory, bytes) {
		const inst = instance || this.instance;
		const mem = memory || this.memory;
		if (!inst?.exports.malloc || !mem || !bytes) return [0, 0];
		const ptr = inst.exports.malloc(bytes.length);
		const view = new Uint8Array(mem.buffer, ptr, bytes.length);
		view.set(bytes);
		return [ptr, bytes.length];
	}

	extractElementState(stateEl) {
		if (!stateEl) return '{}';
		const typeAttr = stateEl.getAttribute?.('type') || '';
		const encoding = stateEl.getAttribute?.('data-encoding') || '';
		const text = stateEl.textContent?.trim() || '';

		if (typeAttr === 'application/octet-stream' || encoding === 'base64' || stateEl.hasAttribute?.('data-state-bin')) {
			const b64 = stateEl.getAttribute?.('data-state-bin') || text;
			try {
				if (typeof atob === 'function') {
					const binStr = atob(b64);
					const bytes = new Uint8Array(binStr.length);
					for (let i = 0; i < binStr.length; i++) bytes[i] = binStr.charCodeAt(i);
					return bytes;
				} else if (typeof Buffer !== 'undefined') {
					const buf = Buffer.from(b64, 'base64');
					return new Uint8Array(buf);
				}
			} catch (e) {
				console.warn('Failed to decode Base64 binary state:', e);
			}
		}
		return text || '{}';
	}

	async resolveCapability(cap) {
		if (!cap || cap === 'stele' || cap === 'stl' || cap === 'env' || cap === 'wasi_snapshot_preview1') return;
		if (globalScope.__wasm_capability_registry[cap]) return;

		const capabilityMap = {
			'axil': '/axil-bridge.js',
			'axil-ws': '/axil-bridge.js',
			'axil-tty': '/axil-tty-bridge.js',
			'tty': '/axil-tty-bridge.js',
			'bud': '/bud-hydrate.js',
			'hyle': '/hyle-fragments.js',
			'lamina': '/lamina-bridge.js',
			'xylem': '/xylem-bridge.js',
			'webgl': '/webgl-bridge.js'
		};
		const knownUrl = capabilityMap[cap];
		const candidates = knownUrl ? [knownUrl] : [
			`/${cap}-bridge.js`,
			cap.includes('-') ? `/${cap.split('-')[0]}-bridge.js` : null,
			cap === 'tty' ? '/axil-tty-bridge.js' : null,
			`/${cap}-hydrate.js`
		].filter(Boolean);

		for (const url of candidates) {
			try {
				const mod = await import(url);
				if (globalScope.__wasm_capability_registry[cap]) break;
				if (typeof mod?.createSlotBridge === 'function') {
					SteleWasmClient.registerBridgeFactory(mod.createSlotBridge);
					break;
				}
			} catch (_) {}
		}
	}

	async loadCapabilities() {
		const capAttr = [
			document.body?.getAttribute('data-capabilities') || '',
			document.querySelector('[data-slot="1"], [data-capabilities]')?.getAttribute('data-capabilities') || '',
			document.querySelector('[data-slot="0"]')?.getAttribute('data-capabilities') || ''
		].join(' ');
		const declaredCaps = [...new Set(capAttr.trim().split(/\s+/).filter(Boolean))];
		for (const cap of declaredCaps) {
			await this.resolveCapability(cap);
		}

		if (!s_bridgeFactory && typeof globalScope.__wasm_slot_bridge_factory !== 'function') {
			const bridgeSrc = document.body?.getAttribute('data-bridge') ||
			                  document.querySelector('[data-bridge]')?.getAttribute('data-bridge') ||
			                  document.querySelector('meta[name="slot-bridge"]')?.getAttribute('content');
			if (bridgeSrc) {
				try {
					const mod = await import(bridgeSrc);
					if (typeof mod?.createSlotBridge === 'function') {
						SteleWasmClient.registerBridgeFactory(mod.createSlotBridge);
					}
				} catch (_) {}
			}
		}
	}

	buildSlotImports(slot, wasmModule = null) {
		const hostEmitPatch = (_slot, opPtr, opLen, aPtr, aLen, bPtr, bLen, cPtr, cLen) => {
			const targetSlot = (typeof slot === 'number' && slot >= 0) ? slot : (typeof _slot === 'number' && _slot >= 0 ? _slot : 0);
			const bridge = this.slotBridges.get(targetSlot) || this.slotBridges.get(slot);
			if (!bridge) return;
			const mem = this.slotInstances.get(slot)?.memory || this.slotInstances.get(targetSlot)?.memory || this.memory;
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
			const comp = this.slotInstances.get(slot);
			const inst = comp?.instance || this.instance;
			const mem = comp?.memory || this.memory;
			if (!inst || !mem) return;
			const url = this.decodeString(mem, urlPtr, len);
			const fullUrl = (typeof window !== 'undefined' && window.location?.origin && url.startsWith('/'))
				? `${window.location.origin}${url}`
				: url;
			fetch(fullUrl).then(async (r) => {
				const status = r.status;
				const malloc = inst.exports.malloc;
				const fn = inst.exports.wasm_fetch_callback;
				if (!malloc || !fn) return;
				if (!r.ok) {
					fn(reqId, 0, -status);
					return;
				}
				const ab = await r.arrayBuffer();
				const bytes = new Uint8Array(ab);
				const ptr = malloc(bytes.length + 1);
				if (ptr) {
					new Uint8Array(mem.buffer, ptr, bytes.length).set(bytes);
					new Uint8Array(mem.buffer, ptr + bytes.length, 1)[0] = 0;
					fn(reqId, ptr, bytes.length);
					if (inst.exports.free) inst.exports.free(ptr);
				}
			}).catch(err => {
				console.error('stl_host_fetch failed:', err);
				const fn = inst.exports.wasm_fetch_callback;
				if (fn) fn(reqId, 0, -1);
			});
		};

		const hostSetLocation = (urlPtr, len) => {
			const mem = this.slotInstances.get(slot)?.memory || this.memory;
			const url = this.decodeString(mem, urlPtr, len);
			if (!url) return;
			const resolved = new URL(url, window.location.href);
			const nextPath = resolved.pathname + resolved.search;
			if (window.location.pathname + window.location.search !== nextPath) {
				if (typeof history !== 'undefined' && history.pushState) {
					history.pushState(null, '', nextPath);
				} else if (typeof history !== 'undefined' && history.replaceState) {
					history.replaceState(null, '', nextPath);
				} else if (typeof window !== 'undefined') {
					window.location.href = nextPath;
				}
			}
		};

		const hostInvalidate = (s) => {
			const targetSlot = typeof s === 'number' && s >= 0 ? s : slot;
			this.updateSlot(targetSlot);
		};

		const hostEventEmit = (topicPtr, topicLen, payloadPtr, payloadLen) => {
			const senderSlot = slot;
			const senderComp = this.slotInstances.get(senderSlot);
			const mem = senderComp?.memory || this.memory;
			const topic = this.decodeString(mem, topicPtr, topicLen);
			const payloadBytes = (payloadPtr && payloadLen > 0 && mem)
				? new Uint8Array(mem.buffer.slice(payloadPtr, payloadPtr + payloadLen))
				: new Uint8Array(0);

			// 1. Deliver to sibling WASM slots
			for (const [targetSlot, comp] of this.slotInstances.entries()) {
				if (targetSlot === senderSlot) continue;
				const onEventFn = comp.instance?.exports.stl_on_event || comp.instance?.exports.stele_on_event;
				if (onEventFn) {
					const tBuf = comp.instance.exports.malloc ? comp.instance.exports.malloc(topic.length + 1) : 0;
					const pBuf = (payloadBytes.length > 0 && comp.instance.exports.malloc)
						? comp.instance.exports.malloc(payloadBytes.length)
						: 0;
					if (tBuf) {
						new Uint8Array(comp.memory.buffer, tBuf, topic.length + 1).set(new TextEncoder().encode(topic + '\0'));
						if (pBuf && payloadBytes.length > 0) {
							new Uint8Array(comp.memory.buffer, pBuf, payloadBytes.length).set(payloadBytes);
						}
						try {
							onEventFn(tBuf, topic.length, pBuf, payloadBytes.length);
						} catch (err) {
							console.error(`[stele-client] Event delivery to slot ${targetSlot} failed:`, err);
						}
						if (comp.instance.exports.free) {
							comp.instance.exports.free(tBuf);
							if (pBuf) comp.instance.exports.free(pBuf);
						}
					}
				}
			}

			// 2. Dispatch to registered host JS listeners
			const listeners = this.eventListeners.get(topic);
			if (listeners) {
				for (const cb of listeners) {
					try { cb(topic, payloadBytes, senderSlot); } catch (e) { console.error(e); }
				}
			}
		};

		const steleImports = {
			stl_host_emit_patch: hostEmitPatch,
			stl_host_log: hostLog,
			stl_host_fetch: hostFetch,
			stl_host_set_location: hostSetLocation,
			stl_host_invalidate: hostInvalidate,
			stl_host_event_emit: hostEventEmit,
			stele_host_emit_patch: hostEmitPatch,
			stele_host_log: hostLog,
			stele_host_fetch: hostFetch,
			stele_host_set_location: hostSetLocation,
			stele_host_invalidate: hostInvalidate,
			stele_host_event_emit: hostEventEmit
		};

		const imports = {
			env: new Proxy({
				memory: this.sharedMemory,
				...steleImports,
				__main_argc_argv: () => 0
			}, {
				get: (target, prop) => {
					if (prop in target) return target[prop];
					if (prop === 'memory') return this.sharedMemory;
					return () => 0;
				}
			}),
			stele: steleImports,
			stl: steleImports,
			wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 })
		};

		// 1. Give the active slot bridge a chance to provide its own namespace
		const bridge = this.slotBridges.get(slot);
		if (bridge && typeof bridge.makeImports === 'function') {
			const bridgeImports = bridge.makeImports();
			const ns = bridge.namespace || 'slot';
			imports[ns] = bridgeImports;
			Object.assign(imports.env, bridgeImports);
		}

		// 2. Dynamically satisfy any declared external capability imports
		const getWasm = () => {
			return this.slotInstances.get(slot)?.instance?.exports || (this.instance ? this.instance.exports : null);
		};
		const slotRoot = this.slotBridges.get(slot)?.root || (slot === 0 ? (document.querySelector('[data-slot="0"]') || document.getElementById('chrome-root')) : document.querySelector('[data-slot="1"], [data-slot]'));

		const candidateBridges = [];
		const instantiatedFactories = new Set();

		if (wasmModule && typeof WebAssembly.Module.imports === 'function') {
			const requiredNamespaces = new Set(
				WebAssembly.Module.imports(wasmModule).map(i => i.module)
			);
			for (const ns of requiredNamespaces) {
				if (imports[ns]) continue;
				const factory = globalScope.__wasm_capability_registry[ns];
				if (typeof factory === 'function') {
					if (instantiatedFactories.has(factory)) continue;
					instantiatedFactories.add(factory);
					try {
						const root = slotRoot || document.body;
						const capBridge = factory(root, getWasm);
						if (capBridge) {
							candidateBridges.push(capBridge);
							if (typeof capBridge.makeImports === 'function') {
								const capImports = capBridge.makeImports();
								imports[ns] = capImports;
								Object.assign(imports.env, capImports);
							}
						}
					} catch (e) {
						console.error(`Failed to init dynamic capability bridge for ${ns}:`, e);
					}
				}
			}
		}

		// Also discover from data-capabilities attribute
		const slotCapsAttr = [
			slotRoot?.getAttribute('data-capabilities') || '',
			slot === 1 ? (document.body?.getAttribute('data-capabilities') || '') : ''
		].join(' ');
		const slotDeclared = new Set(slotCapsAttr.trim().split(/\s+/).filter(Boolean));

		for (const [ns, factory] of Object.entries(globalScope.__wasm_capability_registry)) {
			if (imports[ns]) continue;
			if (slotDeclared.size > 0 && !slotDeclared.has(ns)) continue;
			if (slotDeclared.size === 0 && slot === 0) continue;
			if (instantiatedFactories.has(factory)) continue;
			instantiatedFactories.add(factory);
			try {
				const root = slotRoot || document.body;
				const capBridge = factory(root, getWasm);
				if (capBridge) {
					candidateBridges.push(capBridge);
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
		imports.__candidateBridges = candidateBridges;
		return imports;
	}

	async boot(wasmUrl = null) {
		await this.loadCapabilities();

		if (wasmUrl) {
			try {
				const imports = this.buildSlotImports(0);
				const source = await instantiateWasmStreamingWithFallback(wasmUrl, imports);
				if (source?.instance) {
					this.instance = source.instance;
					this.memory = source.instance.exports.memory || this.sharedMemory;

					const bootFn = this.instance.exports.stl_kernel_boot || this.instance.exports.stele_kernel_boot;
					if (bootFn) {
						bootFn();
					}
				}
			} catch (_) {
				// Standalone kernel optional when dynamic component WASMs are used
			}
		}

		await this.bindSlots();
		this.attachEventDelegation();
		return true;
	}

	async bindSlots() {
		const modulesAttr = (typeof document !== 'undefined' && document.body?.getAttribute('data-modules')) || '';
		const tokens = modulesAttr.trim().split(/\s+/).filter(Boolean);
		let mountedCount = 0;

		const chromeRoot = document.querySelector('[data-slot="0"]') || document.getElementById('chrome-root');
		if (chromeRoot && (tokens.length === 0 || tokens.includes('site_chrome'))) {
			const bridge = createSlotBridge(chromeRoot);
			if (bridge) this.slotBridges.set(0, bridge);
			const stateEl = document.getElementById('chrome-state');
			let state = null;
			if (stateEl && stateEl.hasAttribute('data-title')) {
				state = {
					title: stateEl.getAttribute('data-title') || '',
					path: stateEl.getAttribute('data-path') || '',
					icon: stateEl.getAttribute('data-icon') || '',
					user: stateEl.getAttribute('data-user') || '',
					lang: stateEl.getAttribute('data-lang') || 'en'
				};
			} else {
				state = this.extractElementState(stateEl);
			}
			if (await this.mountSlot(0, 'site_chrome', state)) mountedCount++;
		}

		const pageRoot = document.querySelector('[data-slot="1"], [data-slot], [data-module]');
		if (pageRoot) {
			const pageModule = tokens.find(t => t !== 'site_chrome') || pageRoot.getAttribute('data-module') || '';
			if (pageModule) {
				const bridge = createSlotBridge(pageRoot);
				if (bridge) this.slotBridges.set(1, bridge);

				let state = null;
				const binSrcMeta = typeof document !== 'undefined'
					? (document.querySelector('meta[name="state-src"]') || document.querySelector('meta[name="page-state-src"]'))
					: null;
				const binUrl = binSrcMeta?.getAttribute?.('content');
				if (binUrl && typeof fetch === 'function') {
					try {
						const res = await fetch(binUrl);
						if (res && res.ok) {
							const ab = await res.arrayBuffer();
							state = new Uint8Array(ab);
						}
					} catch (e) {
						console.warn('Failed to fetch binary state from', binUrl, e);
					}
				}

				if (!state) {
					const stateEl = document.querySelector('[data-state="1"], [data-state], #page-state');
					state = this.extractElementState(stateEl);
				}

				if (await this.mountSlot(1, pageModule, state)) {
					mountedCount++;
				}
			}
		}
		const lazyRoots = typeof document !== 'undefined' ? (document.querySelectorAll?.('[data-lazy-module]') || []) : [];
		let lazySlotIdx = 2;
		for (const el of lazyRoots) {
			const mod = el.getAttribute('data-lazy-module');
			if (!mod) continue;
			const trigger = el.getAttribute('data-lazy-trigger') || 'interaction';
			const slotId = parseInt(el.getAttribute('data-slot') || `${lazySlotIdx++}`, 10);
			const stateEl = el.querySelector?.('[data-state]') || el;
			const json = stateEl?.getAttribute?.('data-state-json') || stateEl?.textContent || '{}';
			this.lazyMountSlot(slotId, mod, el, { trigger, stateJson: json });
		}

		if (mountedCount > 0 && typeof document !== 'undefined' && document.body) {
			document.body.setAttribute('data-wasm-loaded', '');
		}
		return mountedCount > 0;
	}

	lazyMountSlot(slot, moduleName, target, options = {}) {
		const trigger = options.trigger || 'interaction';
		const stateJson = options.stateJson || '{}';

		const doMount = async () => {
			if (this.slotInstances.has(slot)) return true;
			const targetEl = typeof target === 'string' && typeof document !== 'undefined' ? document.querySelector(target) : target;
			if (targetEl && !this.slotBridges.has(slot)) {
				try {
					const bridge = createSlotBridge(targetEl);
					if (bridge) this.slotBridges.set(slot, bridge);
				} catch (_) {}
			}
			return await this.mountSlot(slot, moduleName, stateJson);
		};

		if (trigger === 'manual') {
			return doMount();
		}

		if (typeof document === 'undefined') return doMount();

		const el = typeof target === 'string' ? document.querySelector(target) : target;
		if (!el) return doMount();

		if (trigger === 'visible' && typeof IntersectionObserver !== 'undefined') {
			const observer = new IntersectionObserver((entries) => {
				for (const entry of entries) {
					if (entry.isIntersecting) {
						observer.disconnect();
						doMount();
						break;
					}
				}
			}, { rootMargin: options.rootMargin || '50px' });
			observer.observe(el);
			return () => observer.disconnect();
		}

		const events = ['pointerdown', 'click', 'focusin', 'mouseenter'];
		const handler = () => {
			for (const ev of events) {
				if (typeof el.removeEventListener === 'function') {
					el.removeEventListener(ev, handler, { capture: true });
				}
			}
			doMount();
		};
		for (const ev of events) {
			if (typeof el.addEventListener === 'function') {
				el.addEventListener(ev, handler, { capture: true, once: true });
			}
		}
		return () => {
			for (const ev of events) {
				if (typeof el.removeEventListener === 'function') {
					el.removeEventListener(ev, handler, { capture: true });
				}
			}
		};
	}

	async mountSlot(slot, moduleName, json) {
		// 1. Try on-demand dynamic component WASM instantiation with shared linear memory substrate
		try {
			const candidateUrls = [`/${moduleName}.wasm`, `/${moduleName}_detail.wasm`, '/site.wasm'];
			for (const compUrl of candidateUrls) {
				let wasmModule = null;
				let wasmBuffer = null;
				try {
					const fullUrl = (typeof window !== 'undefined' && window.location?.origin && compUrl.startsWith('/'))
						? `${window.location.origin}${compUrl}`
						: compUrl;
					const res = await fetch(fullUrl);
					if (res.ok) {
						wasmBuffer = await res.arrayBuffer();
						if (typeof WebAssembly.compile === 'function') {
							wasmModule = await WebAssembly.compile(wasmBuffer);
						}
					}
				} catch (_) {}

				if (wasmModule && typeof WebAssembly.Module.imports === 'function') {
					const requiredNamespaces = new Set(
						WebAssembly.Module.imports(wasmModule).map(i => i.module)
					);
					for (const ns of requiredNamespaces) {
						if (!globalScope.__wasm_capability_registry[ns]) {
							await this.resolveCapability(ns);
						}
					}
				}

				const compImports = this.buildSlotImports(slot, wasmModule);
				let instance = null;
				if (wasmModule) {
					instance = (await WebAssembly.instantiate(wasmModule, compImports));
				} else if (wasmBuffer) {
					const res = await WebAssembly.instantiate(wasmBuffer, compImports);
					instance = res.instance;
				} else {
					const source = await instantiateWasmStreamingWithFallback(compUrl, compImports);
					instance = source?.instance;
				}
				if (instance) {
					const compMemory = instance.exports.memory || this.sharedMemory;
					this.slotInstances.set(slot, { instance, memory: compMemory, moduleName });

					if (compImports.__candidateBridges) {
						this.activeBridges.push(...compImports.__candidateBridges);
					}

					if (instance.exports._initialize) instance.exports._initialize();

					const isBin = (json instanceof Uint8Array) || (typeof json === 'object' && json?.type === 'bin');
					const binBytes = isBin ? (json instanceof Uint8Array ? json : json.data) : null;
					const jsonStr = isBin ? '{}' : (typeof json === 'string' ? json : (json?.data || JSON.stringify(json || {})));

					if (isBin && binBytes && (instance.exports.wasm_init_bin || instance.exports.stl_kernel_mount_bin || instance.exports.stele_kernel_mount_bin)) {
						const [binPtr, binLen] = this.writeBytes(instance, compMemory, binBytes);
						if (instance.exports.wasm_init_bin) {
							instance.exports.wasm_init_bin(binPtr, binLen);
						} else {
							const mountBinFn = instance.exports.stl_kernel_mount_bin || instance.exports.stele_kernel_mount_bin;
							if (mountBinFn) {
								const [namePtr] = this.encodeString(instance, compMemory, moduleName);
								mountBinFn(slot, namePtr, binPtr, binLen);
								if (instance.exports.free) instance.exports.free(namePtr);
							}
						}
						if (instance.exports.free) instance.exports.free(binPtr);
					} else if (instance.exports.wasm_init) {
						const [jsonPtr, jsonLen] = this.encodeString(instance, compMemory, jsonStr);
						instance.exports.wasm_init(jsonPtr, jsonLen);
						if (instance.exports.free) instance.exports.free(jsonPtr);
					} else {
						const mountFn = instance.exports.stl_kernel_mount || instance.exports.stele_kernel_mount;
						if (mountFn) {
							const [namePtr] = this.encodeString(instance, compMemory, moduleName);
							const [jsonPtr, jsonLen] = this.encodeString(instance, compMemory, jsonStr);
							mountFn(slot, namePtr, jsonPtr, jsonLen);
							if (instance.exports.free) {
								instance.exports.free(namePtr);
								instance.exports.free(jsonPtr);
							}
						}
					}

					const bridge = this.slotBridges.get(slot);
					if (bridge) {
						bridge.wasm = instance.exports;
						bridge.memory = compMemory;
						if (typeof bridge.setWasm === 'function') {
							bridge.setWasm(instance.exports, compMemory);
						}
						if (typeof bridge.mount === 'function') {
							bridge.mount();
						}
					}

					if (typeof window !== 'undefined') {
						window.__slot_bridges = window.__slot_bridges || {};
						window.__slot_bridges[slot] = bridge || { wasm: instance.exports, memory: compMemory };
					}

					for (const cap of this.activeBridges) {
						if (typeof cap.mount === 'function') {
							try { cap.mount(); } catch (err) { console.error('cap.mount error:', err); }
						}
					}

					let rafId = 0;
					let lastTime = 0;
					if (typeof instance.exports.wasm_tick === 'function') {
						const tick = (now) => {
							if (document.hidden) {
								rafId = requestAnimationFrame(tick);
								return;
							}
							const dt = lastTime ? (now - lastTime) / 1000 : 0.016;
							lastTime = now;
							const rootEl = this.slotBridges.get(slot)?.root || document.body;
							const w = rootEl.clientWidth || window.innerWidth || 800;
							const h = rootEl.clientHeight || window.innerHeight || 600;
							try {
								instance.exports.wasm_tick(dt, w, h);
							} catch (e) {
								console.error('wasm_tick error:', e);
							}
							rafId = requestAnimationFrame(tick);
						};
						rafId = requestAnimationFrame(tick);
					}
					return true;
				}
			}
		} catch (err) {
			console.warn(`Dynamic component WASM load failed for ${moduleName}:`, err);
		}

		// 2. Fall back to shared substrate kernel slot mount if available
		const isBin = (json instanceof Uint8Array) || (typeof json === 'object' && json?.type === 'bin');
		const binBytes = isBin ? (json instanceof Uint8Array ? json : json.data) : null;
		const jsonStr = isBin ? '{}' : (typeof json === 'string' ? json : (json?.data || JSON.stringify(json || {})));

		if (isBin && binBytes && (this.instance?.exports.stl_kernel_mount_bin || this.instance?.exports.stele_kernel_mount_bin)) {
			const mountBinFn = this.instance.exports.stl_kernel_mount_bin || this.instance.exports.stele_kernel_mount_bin;
			const [namePtr] = this.encodeString(this.instance, this.memory, moduleName);
			const [binPtr, binLen] = this.writeBytes(this.instance, this.memory, binBytes);
			const rc = mountBinFn(slot, namePtr, binPtr, binLen);
			if (this.instance.exports.free) {
				this.instance.exports.free(namePtr);
				this.instance.exports.free(binPtr);
			}
			return rc === 0;
		}

		const mountFn = this.instance?.exports.stl_kernel_mount || this.instance?.exports.stele_kernel_mount;
		if (mountFn) {
			const [namePtr] = this.encodeString(this.instance, this.memory, moduleName);
			const [jsonPtr, jsonLen] = this.encodeString(this.instance, this.memory, jsonStr);
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
			return;
		}
		const bridge = this.slotBridges.get(slot);
		if (bridge && typeof bridge.update === 'function') {
			bridge.update(slot);
		}
	}

	on(topic, callback) {
		if (!this.eventListeners.has(topic)) this.eventListeners.set(topic, new Set());
		this.eventListeners.get(topic).add(callback);
		return () => this.off(topic, callback);
	}

	off(topic, callback) {
		this.eventListeners.get(topic)?.delete(callback);
	}

	emit(topic, payload = '') {
		const payloadBytes = typeof payload === 'string'
			? new TextEncoder().encode(payload)
			: (payload instanceof Uint8Array ? payload : new TextEncoder().encode(JSON.stringify(payload)));

		for (const [targetSlot, comp] of this.slotInstances.entries()) {
			const onEventFn = comp.instance?.exports.stl_on_event || comp.instance?.exports.stele_on_event;
			if (onEventFn) {
				const tBuf = comp.instance.exports.malloc ? comp.instance.exports.malloc(topic.length + 1) : 0;
				const pBuf = (payloadBytes.length > 0 && comp.instance.exports.malloc)
					? comp.instance.exports.malloc(payloadBytes.length)
					: 0;
				if (tBuf) {
					new Uint8Array(comp.memory.buffer, tBuf, topic.length + 1).set(new TextEncoder().encode(topic + '\0'));
					if (pBuf && payloadBytes.length > 0) {
						new Uint8Array(comp.memory.buffer, pBuf, payloadBytes.length).set(payloadBytes);
					}
					try {
						onEventFn(tBuf, topic.length, pBuf, payloadBytes.length);
					} catch (err) {
						console.error(`[stele-client] Event delivery to slot ${targetSlot} failed:`, err);
					}
					if (comp.instance.exports.free) {
						comp.instance.exports.free(tBuf);
						if (pBuf) comp.instance.exports.free(pBuf);
					}
				}
			}
		}

		const listeners = this.eventListeners.get(topic);
		if (listeners) {
			for (const cb of listeners) {
				try { cb(topic, payloadBytes, -1); } catch (e) { console.error(e); }
			}
		}
	}

	attachEventDelegation() {
		document.addEventListener('click', (evt) => {
			const actionEl = evt.target.closest('button[data-action], a[data-action]');
			const rawAction = actionEl?.getAttribute('data-action');
			let actionId = rawAction ? parseInt(rawAction, 10) : NaN;
			if (isNaN(actionId)) {
				if (rawAction === 'sort') actionId = 1;
				else if (rawAction === 'page') actionId = 2;
			}
			if (actionEl && !isNaN(actionId)) {
				evt.preventDefault();
				const actionData = actionEl.getAttribute('data-action-payload') ||
				                   actionEl.getAttribute('data-field') ||
				                   actionEl.getAttribute('data-page') || '';
				const slotEl = actionEl.closest('[data-slot]');
				let slot = slotEl ? parseInt(slotEl.getAttribute('data-slot'), 10) : (actionEl.closest('#chrome-root') ? 0 : 1);
				let comp = this.slotInstances.get(slot);
				if (!comp && this.slotInstances.has(1)) {
					slot = 1;
					comp = this.slotInstances.get(1);
				}

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
		});

		document.addEventListener('submit', async (evt) => {
			const form = evt.target;
			if (!form || form.tagName !== 'FORM') return;
			const method = (form.getAttribute('method') || 'GET').toUpperCase();

			const slotEl = form.closest('[data-slot]');
			let slot = slotEl ? parseInt(slotEl.getAttribute('data-slot'), 10) : (form.closest('#chrome-root') ? 0 : 1);
			let comp = this.slotInstances.get(slot);
			if (!comp && this.slotInstances.has(1)) {
				slot = 1;
				comp = this.slotInstances.get(1);
			}

			// Handle GET search/filter forms
			if (method === 'GET') {
				if (comp?.instance?.exports?.wasm_action) {
					evt.preventDefault();
					const formData = new FormData(form);
					if (evt.submitter && evt.submitter.name && evt.submitter.value) {
						formData.set(evt.submitter.name, evt.submitter.value);
					}
					const params = new URLSearchParams();
					for (const [k, v] of formData.entries()) {
						if (v !== '') params.append(k, v);
					}
					const qs = params.toString();
					const [dataPtr] = this.encodeString(comp.instance, comp.memory, qs);
					comp.instance.exports.wasm_action(3, dataPtr);
					if (comp.instance.exports.free) comp.instance.exports.free(dataPtr);
					return;
				}
				return;
			}

			// Handle POST forms with optimistic mutations & rollback
			if (method === 'POST') {
				const isOptimistic = form.hasAttribute('data-optimistic') || form.hasAttribute('data-dataset') || form.hasAttribute('data-table') || form.hasAttribute('data-hyle-dataset');
				const actionUrl = form.getAttribute('action') || window.location.href;
				const dataset = form.getAttribute('data-dataset') || form.getAttribute('data-table') || form.getAttribute('data-hyle-dataset') || '';
				const itemId = form.getAttribute('data-item') || form.getAttribute('data-hyle-item') || '';

				if (!isOptimistic && !comp?.instance?.exports?.wasm_action) return;

				evt.preventDefault();
				const formData = new FormData(form);
				if (evt.submitter && evt.submitter.name && evt.submitter.value) {
					formData.set(evt.submitter.name, evt.submitter.value);
				}

				// 1. Optimistic mutation in WASM
				const patchObj = Object.fromEntries(formData.entries());
				const patchJson = JSON.stringify(patchObj);

				const optUpdateFn = comp?.instance?.exports?.stl_optimistic_update || comp?.instance?.exports?.stele_optimistic_update;
				if (optUpdateFn && dataset && itemId) {
					const [dPtr] = this.encodeString(comp.instance, comp.memory, dataset);
					const [iPtr] = this.encodeString(comp.instance, comp.memory, itemId);
					const [pPtr] = this.encodeString(comp.instance, comp.memory, patchJson);
					optUpdateFn(dPtr, iPtr, pPtr);
					if (comp.instance.exports.free) {
						comp.instance.exports.free(dPtr);
						comp.instance.exports.free(iPtr);
						comp.instance.exports.free(pPtr);
					}
					this.updateSlot(slot);
					this.emit('stl:mutate', { dataset, itemId, patch: patchObj, slot });
					this.emit('hyle:mutate', { dataset, itemId, patch: patchObj, slot });
				} else if (comp?.instance?.exports?.wasm_action) {
					const [dataPtr] = this.encodeString(comp.instance, comp.memory, patchJson);
					comp.instance.exports.wasm_action(4, dataPtr); // action 4 = optimistic submit
					if (comp.instance.exports.free) comp.instance.exports.free(dataPtr);
					this.updateSlot(slot);
					this.emit('stl:mutate', { dataset, itemId, patch: patchObj, slot });
					this.emit('hyle:mutate', { dataset, itemId, patch: patchObj, slot });
				}

				// 2. Background HTTP fetch
				try {
					const resp = await fetch(actionUrl, {
						method: 'POST',
						body: formData
					});

					if (!resp.ok) {
						throw new Error(`HTTP ${resp.status}`);
					}
					this.emit('stl:saved', { dataset, itemId, status: resp.status, slot });
					this.emit('hyle:saved', { dataset, itemId, status: resp.status });
				} catch (err) {
					console.warn('[stele-client] Form mutation failed, executing rollback:', err);
					// 3. Rollback on failure
					const optRollbackFn = comp?.instance?.exports?.stl_optimistic_rollback || comp?.instance?.exports?.stele_optimistic_rollback;
					if (optRollbackFn && dataset && itemId) {
						const [dPtr] = this.encodeString(comp.instance, comp.memory, dataset);
						const [iPtr] = this.encodeString(comp.instance, comp.memory, itemId);
						optRollbackFn(dPtr, iPtr);
						if (comp.instance.exports.free) {
							comp.instance.exports.free(dPtr);
							comp.instance.exports.free(iPtr);
						}
					} else if (comp?.instance?.exports?.wasm_action) {
						const [dataPtr] = this.encodeString(comp.instance, comp.memory, itemId);
						comp.instance.exports.wasm_action(5, dataPtr); // action 5 = rollback
						if (comp.instance.exports.free) comp.instance.exports.free(dataPtr);
					}
					this.updateSlot(slot);
					this.emit('stl:rollback', { dataset, itemId, error: err.message, slot });
					this.emit('hyle:rollback', { dataset, itemId, error: err.message });
				}
			}
		});
	}
}

export const steleClient = new SteleWasmClient();
export const stlClient = steleClient;

if (typeof window !== 'undefined') {
	window.SteleWasmClient = SteleWasmClient;
	window.registerSlotBridge = (factory) => SteleWasmClient.registerBridgeFactory(factory);
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
