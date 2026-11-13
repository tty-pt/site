/**
 * @file stele-client.js
 * Universal WebAssembly Substrate Client Runtime
 *
 * Implements dynamic on-demand component WASM instantiation with isolated linear
 * memory instances, multi-slot hydration, declarative capability loading, and pure-C
 * event routing with zero application-specific JS.
 */

const globalScope = typeof window !== 'undefined' ? window : globalThis;
globalScope.__wasm_capability_registry = globalScope.__wasm_capability_registry || {};
globalScope.registerWasmCapability = (namespace, factory) => {
	globalScope.__wasm_capability_registry[namespace] = factory;
};

const isWorkerContext = typeof WorkerGlobalScope !== 'undefined' && globalScope instanceof WorkerGlobalScope;

let s_bridgeFactory = null;
const s_wasmModuleCache = new Map();

function createSlotBridge(root) {
	if (typeof s_bridgeFactory === 'function') {
		return s_bridgeFactory(root);
	}
	if (typeof globalScope.__wasm_slot_bridge_factory === 'function') {
		return globalScope.__wasm_slot_bridge_factory(root);
	}
	return null;
}

function getOrigin() {
	if (typeof window !== 'undefined' && window.location?.origin) return window.location.origin;
	if (globalScope.__stele_origin) return globalScope.__stele_origin;
	if (typeof self !== 'undefined' && self.location?.origin && self.location.origin !== 'null') return self.location.origin;
	return '';
}

function resolveFullUrl(url) {
	if (typeof url !== 'string' || !url) return '';
	if (!url.startsWith('/')) return url;
	const origin = getOrigin();
	return origin ? `${origin}${url}` : '';
}

/**
 * Compiles and caches a WebAssembly.Module using WebAssembly.compileStreaming
 * with fallback to arrayBuffer + WebAssembly.compile.
 *
 * @param {string} url - WASM resource URL
 * @returns {Promise<WebAssembly.Module|null>}
 */
async function compileWasmModuleCached(url) {
	const fullUrl = resolveFullUrl(url);
	if (!fullUrl) return null;
	if (s_wasmModuleCache.has(fullUrl)) {
		return s_wasmModuleCache.get(fullUrl);
	}
	const promise = (async () => {
		try {
			const res = await fetch(fullUrl);
			if (!res || !res.ok) return null;
			if (typeof WebAssembly.compileStreaming === 'function' && typeof res.clone === 'function') {
				try {
					return await WebAssembly.compileStreaming(res.clone());
				} catch (_) {
					// Fall back to arrayBuffer compile
				}
			}
			const buf = await res.arrayBuffer();
			if (typeof WebAssembly.compile === 'function') {
				return await WebAssembly.compile(buf);
			}
		} catch (_) {}
		return null;
	})();
	s_wasmModuleCache.set(fullUrl, promise);
	const mod = await promise;
	if (!mod) s_wasmModuleCache.delete(fullUrl);
	return mod;
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
	const fullUrl = resolveFullUrl(url);
	if (!fullUrl) return null;
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

	constructor(options = {}) {
		this.instance = null;
		this.sharedMemory = (typeof WebAssembly !== 'undefined' && typeof WebAssembly.Memory === 'function')
			? new WebAssembly.Memory({ initial: 256, maximum: 4096 })
			: null;
		this.memory = this.sharedMemory;
		this.slotInstances = new Map(); // slot -> { instance, memory, moduleName, hasTick }
		this.slotBridges = new Map();
		this.activeBridges = [];
		if (typeof window !== 'undefined') {
			window.__active_bridges = this.activeBridges;
		}
		this.eventListeners = new Map();
		this.capabilityUrls = {
			...(typeof globalScope.__wasm_capability_urls === 'object' ? globalScope.__wasm_capability_urls : {}),
			...(options.capabilityUrls || {})
		};
		this.fallbackWasmUrl = options.fallbackWasmUrl || null;
		this.useWorker = options.useWorker !== undefined ? options.useWorker : !isWorkerContext;
		this.isWorkerHost = !!options.isWorkerHost || isWorkerContext;
		this.worker = null;
		this._turnDepth = 0;
		this._pendingPatches = new Map(); // slot -> Array<{ op, a, b, c }>
		this._slotDimensions = new Map(); // slot -> { w, h }
		this._hidden = false;
	}

	beginTurn() {
		this._turnDepth++;
	}

	flushTurn() {
		if (this._turnDepth > 0) {
			this._turnDepth--;
		}
		if (this._turnDepth > 0) return;
		if (this._pendingPatches.size === 0) return;

		for (const [targetSlot, ops] of this._pendingPatches.entries()) {
			if (!ops || ops.length === 0) continue;
			if (this.isWorkerHost && typeof globalScope.__stele_worker_post === 'function') {
				globalScope.__stele_worker_post({
					type: 'stele:patches',
					slot: targetSlot,
					ops
				});
				continue;
			}
			const bridge = this.slotBridges.get(targetSlot);
			if (!bridge) continue;
			if (typeof bridge.host?.replay === 'function') {
				bridge.host.replay(ops);
			} else if (typeof bridge.emitPatch === 'function') {
				for (const p of ops) {
					bridge.emitPatch(p.op, p.a, p.b, p.c);
				}
			}
		}
		this._pendingPatches.clear();
		if (!this.isWorkerHost) {
			this._scheduleBatchRemount();
		}
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

		const attrUrl = typeof document !== 'undefined'
			? (document.body?.getAttribute?.(`data-capability-${cap}`) ||
			   document.querySelector?.(`meta[name="capability-${cap}"]`)?.getAttribute?.('content') ||
			   null)
			: null;
		const mappedUrl = this.capabilityUrls?.[cap] || globalScope.__wasm_capability_urls?.[cap] || attrUrl;
		const prefix = cap.includes('-') ? cap.split('-')[0] : null;
		const candidates = [...new Set([
			mappedUrl,
			`/${cap}-cap.js`,
			prefix ? `/${prefix}-cap.js` : null,
			`/${cap}-bridge.js`
		].filter(Boolean))];

		for (const url of candidates) {
			try {
				const fullUrl = resolveFullUrl(url) || url;
				const mod = await import(fullUrl);
				if (globalScope.__wasm_capability_registry[cap]) break;
				if (typeof mod?.createSlotBridge === 'function') {
					SteleWasmClient.registerBridgeFactory(mod.createSlotBridge);
					break;
				}
			} catch (_) {}
		}
	}

	async loadCapabilities(declaredList = null) {
		let declaredCaps = Array.isArray(declaredList) ? declaredList : [];
		if (typeof document !== 'undefined' && declaredCaps.length === 0) {
			const capAttr = [
				document.body?.getAttribute?.('data-capabilities') || '',
				document.querySelector?.('[data-slot="1"], [data-capabilities]')?.getAttribute?.('data-capabilities') || '',
				document.querySelector?.('[data-slot="0"]')?.getAttribute?.('data-capabilities') || ''
			].join(' ');
			declaredCaps = [...new Set(capAttr.trim().split(/\s+/).filter(Boolean))];
		}
		for (const cap of declaredCaps) {
			if (this.worker && !this.isWorkerHost && cap !== 'axil-tty' && cap !== 'bud') {
				// Worker-side capabilities are loaded inside the Worker; still load axil on main thread if needed for __axil_ws bus
				if (cap === 'axil-ws' || cap === 'axil') {
					await this.resolveCapability(cap);
				}
				continue;
			}
			await this.resolveCapability(cap);
		}

		if (!this.isWorkerHost && !s_bridgeFactory && typeof globalScope.__wasm_slot_bridge_factory !== 'function' && typeof document !== 'undefined') {
			const bridgeSrc = document.body?.getAttribute?.('data-bridge') ||
			                  document.querySelector?.('[data-bridge]')?.getAttribute?.('data-bridge') ||
			                  document.querySelector?.('meta[name="slot-bridge"]')?.getAttribute?.('content') ||
			                  '/bud-cap.js';
			if (bridgeSrc) {
				try {
					const mod = await import(resolveFullUrl(bridgeSrc) || bridgeSrc);
					if (typeof mod?.createSlotBridge === 'function') {
						SteleWasmClient.registerBridgeFactory(mod.createSlotBridge);
					}
				} catch (_) {}
			}
		}
	}

	buildSlotImports(slot, wasmModule = null, extraCaps = []) {
		const hostEmitPatch = (_slot, opPtr, opLen, aPtr, aLen, bPtr, bLen, cPtr, cLen) => {
			const targetSlot = (typeof slot === 'number' && slot >= 0) ? slot : (typeof _slot === 'number' && _slot >= 0 ? _slot : 0);
			const mem = this.slotInstances.get(slot)?.memory || this.slotInstances.get(targetSlot)?.memory || this.memory;
			const op = this.decodeString(mem, opPtr, opLen);
			const a = this.decodeString(mem, aPtr, aLen);
			const b = this.decodeString(mem, bPtr, bLen);
			const c = this.decodeString(mem, cPtr, cLen);
			const record = { op, a, b, c };

			if (this._turnDepth > 0) {
				let list = this._pendingPatches.get(targetSlot);
				if (!list) {
					list = [];
					this._pendingPatches.set(targetSlot, list);
				}
				list.push(record);
				return;
			}

			if (this.isWorkerHost && typeof globalScope.__stele_worker_post === 'function') {
				globalScope.__stele_worker_post({
					type: 'stele:patches',
					slot: targetSlot,
					ops: [record]
				});
				return;
			}

			const bridge = this.slotBridges.get(targetSlot) || this.slotBridges.get(slot);
			if (!bridge) return;
			if (typeof bridge.emitPatch === 'function') {
				bridge.emitPatch(op, a, b, c);
			} else if (typeof bridge.host?.replay === 'function') {
				bridge.host.replay([record]);
			}
			this._scheduleBatchRemount();
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
			const fullUrl = resolveFullUrl(url) || url;
			fetch(fullUrl).then(async (r) => {
				const status = r.status;
				const malloc = inst.exports.malloc;
				const fn = inst.exports.wasm_fetch_callback;
				if (!malloc || !fn) return;
				if (!r.ok) {
					this.beginTurn();
					try { fn(reqId, 0, -status); } finally { this.flushTurn(); }
					return;
				}
				const ab = await r.arrayBuffer();
				const bytes = new Uint8Array(ab);
				const ptr = malloc(bytes.length + 1);
				if (ptr) {
					new Uint8Array(mem.buffer, ptr, bytes.length).set(bytes);
					new Uint8Array(mem.buffer, ptr + bytes.length, 1)[0] = 0;
					this.beginTurn();
					try {
						fn(reqId, ptr, bytes.length);
					} finally {
						this.flushTurn();
					}
					if (inst.exports.free) inst.exports.free(ptr);
				}
			}).catch(err => {
				console.error('stl_host_fetch failed:', err);
				const fn = inst.exports.wasm_fetch_callback;
				if (fn) {
					this.beginTurn();
					try { fn(reqId, 0, -1); } finally { this.flushTurn(); }
				}
			});
		};

		const hostSetLocation = (urlPtr, len) => {
			const mem = this.slotInstances.get(slot)?.memory || this.memory;
			const url = this.decodeString(mem, urlPtr, len);
			if (!url) return;
			if (this.isWorkerHost && typeof globalScope.__stele_worker_post === 'function') {
				globalScope.__stele_worker_post({ type: 'stele:set_location', url });
				return;
			}
			if (typeof window !== 'undefined' && window.location) {
				const resolved = new URL(url, window.location.href);
				const nextPath = resolved.pathname + resolved.search;
				if (window.location.pathname + window.location.search !== nextPath) {
					if (typeof history !== 'undefined' && history.pushState) {
						history.pushState(null, '', nextPath);
					} else if (typeof history !== 'undefined' && history.replaceState) {
						history.replaceState(null, '', nextPath);
					} else {
						window.location.href = nextPath;
					}
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
						this.beginTurn();
						try {
							onEventFn(tBuf, topic.length, pBuf, payloadBytes.length);
						} catch (err) {
							console.error(`[stele-client] Event delivery to slot ${targetSlot} failed:`, err);
						} finally {
							this.flushTurn();
						}
						if (comp.instance.exports.free) {
							comp.instance.exports.free(tBuf);
							if (pBuf) comp.instance.exports.free(pBuf);
						}
					}
				}
			}

			if (this.isWorkerHost && typeof globalScope.__stele_worker_post === 'function') {
				globalScope.__stele_worker_post({
					type: 'stele:event_emit',
					topic,
					payload: payloadBytes,
					senderSlot
				});
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
		const hasDoc = typeof document !== 'undefined';
		const slotRoot = this.slotBridges.get(slot)?.root || (hasDoc
			? (slot === 0 ? (document.querySelector?.('[data-slot="0"]') || document.getElementById?.('chrome-root')) : document.querySelector?.('[data-slot="1"], [data-slot]'))
			: null);

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
						const root = slotRoot || (hasDoc ? document.body : null);
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

		// Also discover from data-capabilities attribute or extraCaps
		const slotCapsAttr = [
			slotRoot?.getAttribute?.('data-capabilities') || '',
			(slot === 1 && hasDoc) ? (document.body?.getAttribute?.('data-capabilities') || '') : '',
			Array.isArray(extraCaps) ? extraCaps.join(' ') : ''
		].join(' ');
		const slotDeclared = new Set(slotCapsAttr.trim().split(/\s+/).filter(Boolean));

		for (const [ns, factory] of Object.entries(globalScope.__wasm_capability_registry)) {
			if (imports[ns]) continue;
			if (slotDeclared.size > 0 && !slotDeclared.has(ns)) continue;
			if (slotDeclared.size === 0 && slot === 0) continue;
			if (instantiatedFactories.has(factory)) continue;
			instantiatedFactories.add(factory);
			try {
				const root = slotRoot || (hasDoc ? document.body : null);
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

	_initWorkerCoordinator() {
		if (this.worker || !this.useWorker || this.isWorkerHost) return false;
		if (typeof window === 'undefined' || typeof Worker === 'undefined') return false;
		if (!window.location?.origin || window.location.origin === 'null') return false;
		if (this.slotInstances.size > 0) return false;

		try {
			const workerUrl = new URL(import.meta.url, window.location.href).href;
			this.worker = new Worker(workerUrl, { type: 'module' });
		} catch (e) {
			console.warn('[stele-client] Worker spawn failed, falling back to in-process execution:', e);
			this.worker = null;
			return false;
		}

		this._pendingMountPromises = new Map();

		window.__stele_ws_send = (data) => {
			if (this.worker) {
				this.worker.postMessage({ type: 'stele:ws_send', data });
			}
		};
		window.__stele_tty_input = (data) => {
			if (this.worker) {
				this.worker.postMessage({ type: 'stele:tty_input', data });
			}
		};

		this.worker.addEventListener('message', (evt) => {
			const msg = evt.data;
			if (!msg || typeof msg.type !== 'string') return;

			switch (msg.type) {
			case 'stele:mounted': {
				const resolver = this._pendingMountPromises?.get(msg.slot);
				if (resolver) {
					this._pendingMountPromises.delete(msg.slot);
					resolver(!!msg.ok);
				}
				break;
			}
			case 'stele:patches': {
				const bridge = this.slotBridges.get(msg.slot);
				if (bridge && Array.isArray(msg.ops) && msg.ops.length > 0) {
					if (typeof bridge.host?.replay === 'function') {
						bridge.host.replay(msg.ops);
					} else if (typeof bridge.emitPatch === 'function') {
						for (const p of msg.ops) {
							bridge.emitPatch(p.op, p.a, p.b, p.c);
						}
					}
					this._transferSlotCanvases(msg.slot);
					this._scheduleBatchRemount();
				}
				break;
			}
			case 'stele:set_location': {
				const url = msg.url;
				if (url && typeof window !== 'undefined' && window.location) {
					const resolved = new URL(url, window.location.href);
					const nextPath = resolved.pathname + resolved.search;
					if (window.location.pathname + window.location.search !== nextPath) {
						if (typeof history !== 'undefined' && history.pushState) {
							history.pushState(null, '', nextPath);
						} else if (typeof history !== 'undefined' && history.replaceState) {
							history.replaceState(null, '', nextPath);
						} else {
							window.location.href = nextPath;
						}
					}
				}
				break;
			}
			case 'stele:event_emit': {
				const listeners = this.eventListeners.get(msg.topic);
				if (listeners) {
					for (const cb of listeners) {
						try { cb(msg.topic, msg.payload, msg.senderSlot); } catch (e) { console.error(e); }
					}
				}
				break;
			}
			case 'stele:ws_connect': {
				if (window.__axil_ws && typeof window.__axil_ws.connectFromWorker === 'function') {
					window.__axil_ws.connectFromWorker(msg.url);
				}
				break;
			}
			case 'stele:ws_send_from_worker': {
				if (window.__axil_ws && typeof window.__axil_ws.send === 'function') {
					window.__axil_ws.send(msg.data);
				}
				break;
			}
			case 'stele:ws_close_from_worker': {
				if (window.__axil_ws && typeof window.__axil_ws.closeFromWorker === 'function') {
					window.__axil_ws.closeFromWorker();
				}
				break;
			}
			case 'stele:ws_stream0': {
				if (window.__axil_ws && typeof window.__axil_ws.deliverStream0 === 'function') {
					window.__axil_ws.deliverStream0(msg.bytes, msg.isText);
				}
				break;
			}
			case 'stele:tty_write': {
				if (window.__axil_tty_bridge && typeof window.__axil_tty_bridge.writeText === 'function') {
					window.__axil_tty_bridge.writeText(msg.text);
				}
				break;
			}
			case 'stele:tick_state': {
				this._workerTickActive = !!msg.active;
				break;
			}
			case 'stele:canvas_bitmap': {
				const bridge = this.slotBridges.get(msg.slot);
				const root = bridge?.root || (typeof document !== 'undefined' ? document : null);
				if (root && msg.bmp) {
					let el = null;
					if (msg.id && typeof document !== 'undefined') {
						el = document.getElementById(msg.id);
					}
					if (!el && msg.budId && typeof root.querySelector === 'function') {
						el = root.querySelector(`canvas[data-bud-id="${msg.budId}"]`);
					}
					if (!el && typeof root.querySelector === 'function') {
						el = root.querySelector(msg.fit ? 'canvas[data-bud-canvas-fit]' : 'canvas[data-bud-canvas]');
					}
					if (el) {
						if (el.width !== msg.width) el.width = msg.width;
						if (el.height !== msg.height) el.height = msg.height;
						if (!el.__stele_bmp_ctx) {
							try {
								el.__stele_bmp_ctx = el.getContext('bitmaprenderer');
							} catch (_) {}
						}
						if (el.__stele_bmp_ctx && typeof el.__stele_bmp_ctx.transferFromImageBitmap === 'function') {
							try {
								el.__stele_bmp_ctx.transferFromImageBitmap(msg.bmp);
								break;
							} catch (_) {}
						}
					}
					if (typeof msg.bmp.close === 'function') msg.bmp.close();
				}
				break;
			}
			default:
				break;
			}
		});

		this.worker.postMessage({
			type: 'stele:init',
			origin: window.location.origin,
			capabilityUrls: this.capabilityUrls,
			fallbackWasmUrl: this.fallbackWasmUrl || (typeof document !== 'undefined' && document.body?.getAttribute?.('data-wasm-bundle')) || null
		});

		this._setupViewportSync();
		return true;
	}

	_setupViewportSync() {
		if (!this.worker || typeof window === 'undefined' || typeof document === 'undefined') return;
		const sendResize = (slot, el) => {
			if (!this.worker) return;
			const w = el?.clientWidth || window.innerWidth || 800;
			const h = el?.clientHeight || window.innerHeight || 600;
			this.worker.postMessage({
				type: 'stele:resize',
				slot,
				width: w,
				height: h,
				hidden: !!document.hidden
			});
		};

		if (typeof window.addEventListener === 'function') {
			window.addEventListener('resize', () => {
				for (const [slot, bridge] of this.slotBridges.entries()) {
					sendResize(slot, bridge?.root || document.body);
				}
			});
		}
		if (typeof document.addEventListener === 'function') {
			document.addEventListener('visibilitychange', () => {
				for (const [slot, bridge] of this.slotBridges.entries()) {
					sendResize(slot, bridge?.root || document.body);
				}
			});
		}
	}

	_transferSlotCanvases(slot) {
		if (!this.worker || typeof document === 'undefined') return;
		const bridge = this.slotBridges.get(slot);
		const root = bridge?.root || document.body;
		if (!root || typeof root.querySelectorAll !== 'function') return;

		const canvases = root.querySelectorAll('canvas[data-bud-canvas], canvas');
		for (const c of canvases) {
			if (c.__stele_registered) continue;
			c.__stele_registered = true;
			const id = c.getAttribute('id') || '';
			const budId = c.getAttribute('data-bud-id') || '';
			const fit = c.hasAttribute('data-bud-canvas-fit') || id === 'seven-stage-canvas';
			const webgl = c.hasAttribute('data-bud-webgl') || fit;
			const rect = typeof c.getBoundingClientRect === 'function' ? c.getBoundingClientRect() : null;
			const width = Math.round(
				(fit ? (rect?.width || c.clientWidth) : (parseInt(c.getAttribute('width') || '0', 10) || rect?.width || c.clientWidth)) ||
				window.innerWidth || 800
			);
			const height = Math.round(
				(fit ? (rect?.height || c.clientHeight) : (parseInt(c.getAttribute('height') || '0', 10) || rect?.height || c.clientHeight)) ||
				window.innerHeight || 600
			);
			this.worker.postMessage({
				type: 'stele:canvas_attach',
				slot,
				id,
				budId,
				fit,
				webgl,
				width,
				height
			});
		}
	}

	_configureBridgeForWorker(slot, bridge) {
		if (!bridge || !this.worker) return;
		if (typeof bridge.setDispatcher === 'function') {
			bridge.setDispatcher((nodeId, eventName, bubbles, valueStr) => {
				this.worker.postMessage({
					type: 'stele:dispatch',
					slot,
					nodeId,
					eventName,
					bubbles: !!bubbles,
					valueStr: valueStr || ''
				});
			});
		}
		if (bridge.root) {
			const rootEl = bridge.root;
			const w = rootEl.clientWidth || (typeof window !== 'undefined' ? window.innerWidth : 800) || 800;
			const h = rootEl.clientHeight || (typeof window !== 'undefined' ? window.innerHeight : 600) || 600;
			this.worker.postMessage({
				type: 'stele:resize',
				slot,
				width: w,
				height: h,
				hidden: typeof document !== 'undefined' ? !!document.hidden : false
			});
			if (typeof ResizeObserver !== 'undefined') {
				try {
					const ro = new ResizeObserver(() => {
						this.worker?.postMessage({
							type: 'stele:resize',
							slot,
							width: rootEl.clientWidth || window.innerWidth || 800,
							height: rootEl.clientHeight || window.innerHeight || 600,
							hidden: typeof document !== 'undefined' ? !!document.hidden : false
						});
					});
					ro.observe(rootEl);
				} catch (_) {}
			}
		}
	}

	async boot(wasmUrl = null) {
		this._initWorkerCoordinator();
		await this.loadCapabilities();

		if (!this.worker && wasmUrl) {
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
		if (typeof document === 'undefined') return false;
		const modulesAttr = document.body?.getAttribute?.('data-modules') || '';
		const tokens = modulesAttr.trim().split(/\s+/).filter(Boolean);
		let mountedCount = 0;

		const chromeRoot = document.querySelector?.('[data-slot="0"]') || document.getElementById?.('chrome-root');
		const chromeModAttr = chromeRoot?.getAttribute?.('data-module');
		const chromeModule = chromeModAttr || (chromeRoot ? (tokens[0] || null) : null);
		if (chromeRoot && chromeModule) {
			const bridge = createSlotBridge(chromeRoot);
			if (bridge) this.slotBridges.set(0, bridge);
			const stateEl = document.querySelector?.('[data-state="0"]') || document.getElementById?.('chrome-state');
			let state = null;
			if (stateEl && stateEl.hasAttribute?.('data-title')) {
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
			if (await this.mountSlot(0, chromeModule, state)) mountedCount++;
		}

		const pageRoot = document.querySelector?.('[data-slot="1"], [data-slot]:not([data-slot="0"]), [data-module]:not([data-slot="0"])') ||
		                 document.getElementById?.('page-root');
		if (pageRoot) {
			const pageModule = pageRoot.getAttribute?.('data-module') || (chromeModule ? tokens.find(t => t !== chromeModule) : tokens[0]) || tokens[1] || '';
			if (pageModule) {
				const bridge = createSlotBridge(pageRoot);
				if (bridge) this.slotBridges.set(1, bridge);

				let state = null;
				const binSrcMeta = document.querySelector?.('meta[name="state-src"]') ||
				                   document.querySelector?.('meta[name="page-state-src"]');
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
					const stateEl = document.querySelector?.('[data-state="1"], [data-state]:not([data-state="0"]), #page-state') ||
					                document.getElementById?.('page-state');
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

	async mountSlot(slot, moduleName, json, extraCaps = []) {
		// If running on Main Thread with a Worker coordinator, delegate slot mount to the Worker
		if (this.worker && !this.isWorkerHost) {
			const bridge = this.slotBridges.get(slot);
			if (bridge) {
				this._configureBridgeForWorker(slot, bridge);
				if (typeof bridge.rescan === 'function') bridge.rescan();
				if (typeof bridge.mount === 'function') bridge.mount();
			}
			this._transferSlotCanvases(slot);

			const hasDoc = typeof document !== 'undefined';
			const slotRoot = bridge?.root || (hasDoc
				? (slot === 0 ? (document.querySelector?.('[data-slot="0"]') || document.getElementById?.('chrome-root')) : document.querySelector?.('[data-slot="1"], [data-slot]'))
				: null);
			const capStr = [
				slotRoot?.getAttribute?.('data-capabilities') || '',
				(slot === 1 && hasDoc) ? (document.body?.getAttribute?.('data-capabilities') || '') : ''
			].join(' ');
			const declaredCaps = [...new Set(capStr.trim().split(/\s+/).filter(Boolean))];

			if (declaredCaps.includes('axil-tty') && typeof globalScope.__wasm_capability_registry['axil-tty'] === 'function') {
				const ttyBridge = globalScope.__wasm_capability_registry['axil-tty'](slotRoot || document.body, () => null);
				if (ttyBridge && !this.activeBridges.includes(ttyBridge)) {
					this.activeBridges.push(ttyBridge);
					window.__active_bridges = this.activeBridges;
				}
			}

			const mountPromise = new Promise((resolve) => {
				this._pendingMountPromises.set(slot, resolve);
			});

			this.worker.postMessage({
				type: 'stele:mount_slot',
				slot,
				moduleName,
				state: json,
				declaredCaps
			});

			const ok = await mountPromise;
			if (ok) {
				this.slotInstances.set(slot, { workerProxy: true, moduleName });
				if (typeof window !== 'undefined') {
					window.__slot_bridges = window.__slot_bridges || {};
					window.__slot_bridges[slot] = bridge || { workerProxy: true };
				}
				this._transferSlotCanvases(slot);
				this._remountActiveBridges();
			}
			return ok;
		}

		// 1. Try on-demand dynamic component WASM instantiation with shared linear memory substrate
		if (typeof fetch === 'function') {
			try {
				const configuredBundle = (typeof document !== 'undefined' && document.body?.getAttribute?.('data-wasm-bundle')) || this.fallbackWasmUrl;
				const candidateUrls = [...new Set([
					`/${moduleName}.wasm`,
					configuredBundle
				].filter(Boolean))];
				for (const compUrl of candidateUrls) {
					const fullUrl = resolveFullUrl(compUrl);
					if (!fullUrl) continue;

					let wasmModule = await compileWasmModuleCached(compUrl);
					let wasmBuffer = null;

					if (!wasmModule) {
						try {
							const res = await fetch(fullUrl);
							if (res.ok) {
								wasmBuffer = await res.arrayBuffer();
								if (typeof WebAssembly.compile === 'function') {
									wasmModule = await WebAssembly.compile(wasmBuffer);
								}
							}
						} catch (_) {}
					}

					if (Array.isArray(extraCaps) && extraCaps.length > 0) {
						for (const cap of extraCaps) {
							if (!globalScope.__wasm_capability_registry[cap]) {
								await this.resolveCapability(cap);
							}
						}
					}

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

					const compImports = this.buildSlotImports(slot, wasmModule, extraCaps);
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
						this.slotInstances.set(slot, { instance, memory: compMemory, moduleName, hasTick: false });

						if (compImports.__candidateBridges) {
							this.activeBridges.push(...compImports.__candidateBridges);
							if (typeof window !== 'undefined') {
								window.__active_bridges = this.activeBridges;
							}
						}

						if (instance.exports._initialize) instance.exports._initialize();

						const isBin = (json instanceof Uint8Array) || (typeof json === 'object' && json?.type === 'bin');
						const binBytes = isBin ? (json instanceof Uint8Array ? json : json.data) : null;
						const jsonStr = isBin ? '{}' : (typeof json === 'string' ? json : (json?.data || JSON.stringify(json || {})));

						this.beginTurn();
						try {
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
						} finally {
							this.flushTurn();
						}

						const hasTickFn = instance.exports.stl_kernel_has_tick || instance.exports.stele_kernel_has_tick;
						const hasTick = typeof hasTickFn === 'function'
							? (hasTickFn() !== 0)
							: (typeof instance.exports.wasm_tick === 'function');
						const slotRec = this.slotInstances.get(slot);
						if (slotRec) slotRec.hasTick = hasTick;

						const bridge = this.slotBridges.get(slot);
						if (bridge) {
							bridge.wasm = instance.exports;
							bridge.memory = compMemory;
							if (typeof bridge.setWasm === 'function') {
								bridge.setWasm(instance.exports, compMemory);
							}
							if (typeof bridge.mount === 'function') {
								this.beginTurn();
								try {
									bridge.mount();
								} finally {
									this.flushTurn();
								}
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

						if (!this.isWorkerHost && hasTick && typeof requestAnimationFrame === 'function') {
							let lastTime = 0;
							const tick = (now) => {
								if (typeof document !== 'undefined' && document.hidden) {
									requestAnimationFrame(tick);
									return;
								}
								const dt = lastTime ? (now - lastTime) / 1000 : 0.016;
								lastTime = now;
								const rootEl = this.slotBridges.get(slot)?.root || (typeof document !== 'undefined' ? document.body : null);
								const w = rootEl?.clientWidth || (typeof window !== 'undefined' ? window.innerWidth : 800) || 800;
								const h = rootEl?.clientHeight || (typeof window !== 'undefined' ? window.innerHeight : 600) || 600;
								this.beginTurn();
								try {
									if (typeof instance.exports.wasm_tick === 'function') {
										instance.exports.wasm_tick(dt, w, h);
									} else if (typeof instance.exports.stl_kernel_tick === 'function') {
										instance.exports.stl_kernel_tick(dt, w, h);
									}
								} catch (e) {
									console.error('wasm_tick error:', e);
								} finally {
									this.flushTurn();
								}
								requestAnimationFrame(tick);
							};
							requestAnimationFrame(tick);
						}
						return true;
					}
				}
			} catch (err) {
				console.warn(`Dynamic component WASM load failed for ${moduleName}:`, err);
			}
		}

		// 2. Fall back to shared substrate kernel slot mount if available
		const isBin = (json instanceof Uint8Array) || (typeof json === 'object' && json?.type === 'bin');
		const binBytes = isBin ? (json instanceof Uint8Array ? json : json.data) : null;
		const jsonStr = isBin ? '{}' : (typeof json === 'string' ? json : (json?.data || JSON.stringify(json || {})));

		if (isBin && binBytes && (this.instance?.exports.stl_kernel_mount_bin || this.instance?.exports.stele_kernel_mount_bin)) {
			const mountBinFn = this.instance.exports.stl_kernel_mount_bin || this.instance.exports.stele_kernel_mount_bin;
			const [namePtr] = this.encodeString(this.instance, this.memory, moduleName);
			const [binPtr, binLen] = this.writeBytes(this.instance, this.memory, binBytes);
			this.beginTurn();
			let rc = -1;
			try {
				rc = mountBinFn(slot, namePtr, binPtr, binLen);
			} finally {
				this.flushTurn();
			}
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
			this.beginTurn();
			let rc = -1;
			try {
				rc = mountFn(slot, namePtr, jsonPtr, jsonLen);
			} finally {
				this.flushTurn();
			}
			if (this.instance.exports.free) {
				this.instance.exports.free(namePtr);
				this.instance.exports.free(jsonPtr);
			}
			return rc === 0;
		}
		return false;
	}

	invokeSlotAction(slot, actionId, actionData = '') {
		const comp = this.slotInstances.get(slot);
		if (comp?.instance?.exports?.wasm_action) {
			const [dataPtr] = this.encodeString(comp.instance, comp.memory, actionData || '');
			this.beginTurn();
			try {
				comp.instance.exports.wasm_action(actionId, dataPtr);
				if (comp.instance.exports.free && dataPtr) comp.instance.exports.free(dataPtr);
				this.updateSlot(slot);
			} finally {
				this.flushTurn();
			}
			return true;
		}
		const compActionFn = comp?.instance?.exports?.stl_kernel_action || comp?.instance?.exports?.stele_kernel_action;
		if (compActionFn) {
			const [dataPtr] = this.encodeString(comp.instance, comp.memory, actionData || '');
			this.beginTurn();
			try {
				compActionFn(slot, actionId, dataPtr);
			} finally {
				this.flushTurn();
			}
			if (comp.instance.exports.free && dataPtr) comp.instance.exports.free(dataPtr);
			this._scheduleBatchRemount();
			return true;
		}
		const instActionFn = this.instance?.exports?.stl_kernel_action || this.instance?.exports?.stele_kernel_action;
		if (instActionFn) {
			const [dataPtr] = this.encodeString(this.instance, this.memory, actionData || '');
			this.beginTurn();
			try {
				instActionFn(slot, actionId, dataPtr);
			} finally {
				this.flushTurn();
			}
			if (this.instance.exports.free && dataPtr) this.instance.exports.free(dataPtr);
			this._scheduleBatchRemount();
			return true;
		}
		return false;
	}

	dispatchToSlot(slot, nodeId, eventName, bubbles, valueStr) {
		const comp = this.slotInstances.get(slot);
		const inst = comp?.instance || this.instance;
		const mem = comp?.memory || this.memory;
		if (!inst || !mem) return 0;
		const fn = inst.exports.bud_app_dispatch;
		if (typeof fn !== 'function') return 0;

		const [namePtr] = this.encodeString(inst, mem, eventName || '');
		const [valPtr] = valueStr ? this.encodeString(inst, mem, String(valueStr)) : [0, 0];
		this.beginTurn();
		let ret = 0;
		try {
			ret = fn(nodeId, namePtr || 0, bubbles ? 1 : 0, valPtr || 0);
		} finally {
			this.flushTurn();
		}
		if (inst.exports.free) {
			if (valPtr) inst.exports.free(valPtr);
			if (namePtr) inst.exports.free(namePtr);
		}
		return ret;
	}

	updateSlot(slot) {
		if (this.worker && !this.isWorkerHost) {
			this.worker.postMessage({ type: 'stele:update_slot', slot });
			return;
		}
		const comp = this.slotInstances.get(slot);
		if (comp?.instance) {
			if (comp.instance.exports.wasm_update) {
				this.beginTurn();
				try {
					comp.instance.exports.wasm_update(slot);
				} finally {
					this.flushTurn();
				}
				this._scheduleBatchRemount();
				return;
			}
			const updateFn = comp.instance.exports.stl_kernel_update || comp.instance.exports.stele_kernel_update;
			if (updateFn) {
				this.beginTurn();
				try {
					updateFn(slot);
				} finally {
					this.flushTurn();
				}
				this._scheduleBatchRemount();
				return;
			}
		}
		const instUpdateFn = this.instance?.exports.stl_kernel_update || this.instance?.exports.stele_kernel_update;
		if (instUpdateFn) {
			this.beginTurn();
			try {
				instUpdateFn(slot);
			} finally {
				this.flushTurn();
			}
			this._scheduleBatchRemount();
			return;
		}
		const bridge = this.slotBridges.get(slot);
		if (bridge && typeof bridge.update === 'function') {
			bridge.update(slot);
		}
		this._scheduleBatchRemount();
	}

	_scheduleBatchRemount() {
		if (this._remountScheduled) return;
		this._remountScheduled = true;
		const scheduleFn = (typeof queueMicrotask === 'function')
			? queueMicrotask
			: (fn) => setTimeout(fn, 0);
		scheduleFn(() => {
			this._remountScheduled = false;
			this._remountActiveBridges();
		});
	}

	_remountActiveBridges() {
		if (typeof window !== 'undefined') {
			window.__active_bridges = this.activeBridges;
		}
		for (const cap of this.activeBridges) {
			if (typeof cap.mount === 'function') {
				try { cap.mount(); } catch (err) { console.error('cap.mount error:', err); }
			}
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

		if (this.worker && !this.isWorkerHost) {
			this.worker.postMessage({
				type: 'stele:emit',
				topic,
				payload: payloadBytes
			});
		}

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
					this.beginTurn();
					try {
						onEventFn(tBuf, topic.length, pBuf, payloadBytes.length);
					} catch (err) {
						console.error(`[stele-client] Event delivery to slot ${targetSlot} failed:`, err);
					} finally {
						this.flushTurn();
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
		if (typeof document === 'undefined' || typeof document.addEventListener !== 'function') return;

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

				if (this.worker && !this.isWorkerHost) {
					if (!this.slotInstances.has(slot) && this.slotInstances.has(1)) slot = 1;
					this.worker.postMessage({
						type: 'stele:action',
						slot,
						actionId,
						actionData
					});
					return;
				}

				let comp = this.slotInstances.get(slot);
				if (!comp && this.slotInstances.has(1)) {
					slot = 1;
					comp = this.slotInstances.get(1);
				}

				this.invokeSlotAction(slot, actionId, actionData);
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
				if (this.worker && !this.isWorkerHost && comp) {
					evt.preventDefault();
					const formData = new FormData(form);
					if (evt.submitter && evt.submitter.name && evt.submitter.value) {
						formData.set(evt.submitter.name, evt.submitter.value);
					}
					const params = new URLSearchParams();
					for (const [k, v] of formData.entries()) {
						if (v !== '') params.append(k, v);
					}
					this.worker.postMessage({
						type: 'stele:action',
						slot,
						actionId: 3,
						actionData: params.toString()
					});
					return;
				}
				if (comp?.instance?.exports?.wasm_action || comp?.instance?.exports?.stl_kernel_action || comp?.instance?.exports?.stele_kernel_action || this.instance?.exports?.stl_kernel_action || this.instance?.exports?.stele_kernel_action) {
					evt.preventDefault();
					const formData = new FormData(form);
					if (evt.submitter && evt.submitter.name && evt.submitter.value) {
						formData.set(evt.submitter.name, evt.submitter.value);
					}
					const params = new URLSearchParams();
					for (const [k, v] of formData.entries()) {
						if (v !== '') params.append(k, v);
					}
					this.invokeSlotAction(slot, 3, params.toString());
					return;
				}
				return;
			}

			// Handle POST forms with optimistic mutations & rollback
			if (method === 'POST') {
				const isOptimistic = form.hasAttribute('data-optimistic') || form.hasAttribute('data-dataset') || form.hasAttribute('data-table');
				const actionUrl = form.getAttribute('action') || (typeof window !== 'undefined' ? window.location.href : '');
				const dataset = form.getAttribute('data-dataset') || form.getAttribute('data-table') || '';
				const itemId = form.getAttribute('data-item') || '';

				if (!isOptimistic && !comp?.instance?.exports?.wasm_action && !(this.worker && !this.isWorkerHost && comp)) return;

				evt.preventDefault();
				const formData = new FormData(form);
				if (evt.submitter && evt.submitter.name && evt.submitter.value) {
					formData.set(evt.submitter.name, evt.submitter.value);
				}

				// 1. Optimistic mutation in WASM
				const patchObj = Object.fromEntries(formData.entries());
				const patchJson = JSON.stringify(patchObj);

				if (this.worker && !this.isWorkerHost) {
					this.worker.postMessage({
						type: 'stele:optimistic_update',
						slot,
						dataset,
						itemId,
						patchJson
					});
					this.emit('stl:mutate', { dataset, itemId, patch: patchObj, slot });
				} else {
					const optUpdateFn = comp?.instance?.exports?.stl_optimistic_update || comp?.instance?.exports?.stele_optimistic_update;
					if (optUpdateFn && dataset && itemId) {
						const [dPtr] = this.encodeString(comp.instance, comp.memory, dataset);
						const [iPtr] = this.encodeString(comp.instance, comp.memory, itemId);
						const [pPtr] = this.encodeString(comp.instance, comp.memory, patchJson);
						this.beginTurn();
						try {
							optUpdateFn(dPtr, iPtr, pPtr);
							this.updateSlot(slot);
						} finally {
							this.flushTurn();
						}
						if (comp.instance.exports.free) {
							comp.instance.exports.free(dPtr);
							comp.instance.exports.free(iPtr);
							comp.instance.exports.free(pPtr);
						}
						this.emit('stl:mutate', { dataset, itemId, patch: patchObj, slot });
					} else if (this.invokeSlotAction(slot, 4, patchJson)) {
						this.emit('stl:mutate', { dataset, itemId, patch: patchObj, slot });
					}
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
				} catch (err) {
					console.warn('[stele-client] Form mutation failed, executing rollback:', err);
					// 3. Rollback on failure
					if (this.worker && !this.isWorkerHost) {
						this.worker.postMessage({
							type: 'stele:optimistic_rollback',
							slot,
							dataset,
							itemId
						});
						this.emit('stl:rollback', { dataset, itemId, error: err.message, slot });
					} else {
						const optRollbackFn = comp?.instance?.exports?.stl_optimistic_rollback || comp?.instance?.exports?.stele_optimistic_rollback;
						if (optRollbackFn && dataset && itemId) {
							const [dPtr] = this.encodeString(comp.instance, comp.memory, dataset);
							const [iPtr] = this.encodeString(comp.instance, comp.memory, itemId);
							this.beginTurn();
							try {
								optRollbackFn(dPtr, iPtr);
							} finally {
								this.flushTurn();
							}
							if (comp.instance.exports.free) {
								comp.instance.exports.free(dPtr);
								comp.instance.exports.free(iPtr);
							}
						} else {
							this.invokeSlotAction(slot, 5, itemId);
						}
						this.updateSlot(slot);
						this.emit('stl:rollback', { dataset, itemId, error: err.message, slot });
					}
				}
			}
		});
	}
}

/* ── Self-Hosted Web Worker Execution Engine ── */
if (isWorkerContext) {
	const workerClient = new SteleWasmClient({ useWorker: false, isWorkerHost: true });
	globalScope.__stele_worker_canvases = new Map();
	globalScope.__stele_worker_post = (msg, transfer) => {
		self.postMessage(msg, transfer || []);
	};
	globalScope.__stele_begin_turn = () => workerClient.beginTurn();
	globalScope.__stele_flush_turn = () => workerClient.flushTurn();

	let workerRafRunning = false;
	let workerTickScheduled = false;
	let workerLastTickTime = 0;
	let workerTickBusy = false;

	function flushWorkerCanvases(defaultSlot = 1) {
		for (const entry of globalScope.__stele_worker_canvases.values()) {
			const c = entry?.canvas;
			if (c && c.__stele_dirty && typeof c.transferToImageBitmap === 'function') {
				c.__stele_dirty = false;
				try {
					const bmp = c.transferToImageBitmap();
					self.postMessage({
						type: 'stele:canvas_bitmap',
						slot: entry.slot ?? defaultSlot,
						id: entry.id || '',
						budId: entry.budId || '',
						fit: !!entry.fit,
						width: c.width,
						height: c.height,
						bmp
					}, [bmp]);
				} catch (_) {}
			}
		}
	}

	function runWorkerTick(now) {
		if (workerClient._hidden || workerTickBusy) return;
		let anyActive = false;
		for (const comp of workerClient.slotInstances.values()) {
			if (comp.hasTick) {
				anyActive = true;
				break;
			}
		}
		if (!anyActive) return;
		workerTickBusy = true;
		try {
			const dt = workerLastTickTime ? Math.min(0.1, Math.max(0.001, (now - workerLastTickTime) / 1000)) : 0.016;
			workerLastTickTime = now;
			for (const [slot, comp] of workerClient.slotInstances.entries()) {
				if (!comp.hasTick || !comp.instance) continue;
				const dims = workerClient._slotDimensions.get(slot) || { w: 1400, h: 836 };
				workerClient.beginTurn();
				try {
					if (typeof comp.instance.exports.wasm_tick === 'function') {
						comp.instance.exports.wasm_tick(dt, dims.w, dims.h);
					} else if (typeof comp.instance.exports.stl_kernel_tick === 'function') {
						comp.instance.exports.stl_kernel_tick(dt, dims.w, dims.h);
					}
				} catch (e) {
					console.error('[stele-worker] wasm_tick error:', e);
				} finally {
					workerClient.flushTurn();
				}
				const bridges = comp.activeBridges || workerClient.activeBridges || [];
				for (const cap of bridges) {
					if (cap && typeof cap.afterTick === 'function') {
						try { cap.afterTick(slot); } catch (_) {}
					}
				}
				flushWorkerCanvases(slot);
			}
		} finally {
			workerTickBusy = false;
		}
	}

	function scheduleWorkerTick() {
		if (!workerRafRunning || workerClient._hidden || workerTickScheduled) return;
		workerTickScheduled = true;
		const tickCallback = (now) => {
			workerTickScheduled = false;
			if (!workerRafRunning || workerClient._hidden) return;
			const ts = typeof now === 'number' ? now : (typeof performance !== 'undefined' ? performance.now() : Date.now());
			runWorkerTick(ts);
			if (workerRafRunning && !workerClient._hidden) {
				scheduleWorkerTick();
			}
		};
		if (typeof self.requestAnimationFrame === 'function') {
			self.requestAnimationFrame(tickCallback);
		} else {
			setTimeout(() => tickCallback(typeof performance !== 'undefined' ? performance.now() : Date.now()), 16);
		}
	}

	function ensureWorkerTickLoop() {
		let anyHasTick = false;
		for (const comp of workerClient.slotInstances.values()) {
			if (comp.hasTick) {
				anyHasTick = true;
				break;
			}
		}
		workerRafRunning = anyHasTick;
		self.postMessage({ type: 'stele:tick_state', active: anyHasTick });
		if (workerRafRunning && !workerClient._hidden) {
			scheduleWorkerTick();
		}
	}

	let initChain = Promise.resolve();

	self.addEventListener('message', (evt) => {
		const msg = evt.data;
		if (!msg || typeof msg.type !== 'string') return;

		switch (msg.type) {
		case 'stele:init': {
			if (msg.origin) globalScope.__stele_origin = msg.origin;
			if (msg.capabilityUrls) Object.assign(workerClient.capabilityUrls, msg.capabilityUrls);
			if (msg.fallbackWasmUrl) workerClient.fallbackWasmUrl = msg.fallbackWasmUrl;
			break;
		}
		case 'stele:canvas_attach': {
			const w = msg.width || 800;
			const h = msg.height || 600;
			let c = msg.canvas || null;
			if (!c && typeof OffscreenCanvas !== 'undefined') {
				const key = msg.id || msg.budId || `slot-${msg.slot}`;
				const existing = globalScope.__stele_worker_canvases.get(key);
				if (existing && existing.canvas) {
					c = existing.canvas;
				} else {
					c = new OffscreenCanvas(w, h);
				}
			}
			if (c) {
				c.__stele_fit = !!msg.fit;
				c.__stele_webgl = !!msg.webgl;
				c.__stele_id = msg.id || '';
				const key = msg.id || msg.budId || `slot-${msg.slot}`;
				globalScope.__stele_worker_canvases.set(key, {
					canvas: c,
					id: msg.id || '',
					budId: msg.budId || '',
					fit: !!msg.fit,
					webgl: !!msg.webgl,
					width: w,
					height: h,
					slot: msg.slot
				});
			}
			break;
		}
		case 'stele:resize': {
			if (typeof msg.slot === 'number') {
				workerClient._slotDimensions.set(msg.slot, {
					w: msg.width || 1400,
					h: msg.height || 900
				});
			}
			if (typeof msg.hidden === 'boolean') {
				const wasHidden = workerClient._hidden;
				workerClient._hidden = msg.hidden;
				if (wasHidden && !workerClient._hidden && workerRafRunning) {
					workerLastTickTime = 0;
					scheduleWorkerTick();
				}
			}
			break;
		}
		case 'stele:tick': {
			runWorkerTick(msg.now || (typeof performance !== 'undefined' ? performance.now() : Date.now()));
			break;
		}
		case 'stele:mount_slot': {
			initChain = initChain.then(async () => {
				let ok = false;
				try {
					ok = await workerClient.mountSlot(msg.slot, msg.moduleName, msg.state, msg.declaredCaps || []);
					if (ok) {
						ensureWorkerTickLoop();
						flushWorkerCanvases(msg.slot);
					}
				} catch (err) {
					console.error('[stele-worker] mountSlot failed:', err);
				}
				self.postMessage({ type: 'stele:mounted', slot: msg.slot, ok });
			});
			break;
		}
		case 'stele:dispatch': {
			initChain = initChain.then(() => {
				workerClient.dispatchToSlot(msg.slot, msg.nodeId, msg.eventName, msg.bubbles, msg.valueStr);
				flushWorkerCanvases(msg.slot);
			});
			break;
		}
		case 'stele:action': {
			initChain = initChain.then(() => {
				workerClient.invokeSlotAction(msg.slot, msg.actionId, msg.actionData || '');
				flushWorkerCanvases(msg.slot);
			});
			break;
		}
		case 'stele:update_slot': {
			initChain = initChain.then(() => {
				workerClient.updateSlot(msg.slot);
				flushWorkerCanvases(msg.slot);
			});
			break;
		}
		case 'stele:optimistic_update': {
			initChain = initChain.then(() => {
				const comp = workerClient.slotInstances.get(msg.slot);
				const optUpdateFn = comp?.instance?.exports?.stl_optimistic_update || comp?.instance?.exports?.stele_optimistic_update;
				if (optUpdateFn && msg.dataset && msg.itemId) {
					const [dPtr] = workerClient.encodeString(comp.instance, comp.memory, msg.dataset);
					const [iPtr] = workerClient.encodeString(comp.instance, comp.memory, msg.itemId);
					const [pPtr] = workerClient.encodeString(comp.instance, comp.memory, msg.patchJson || '{}');
					workerClient.beginTurn();
					try {
						optUpdateFn(dPtr, iPtr, pPtr);
						workerClient.updateSlot(msg.slot);
					} finally {
						workerClient.flushTurn();
					}
					if (comp.instance.exports.free) {
						comp.instance.exports.free(dPtr);
						comp.instance.exports.free(iPtr);
						comp.instance.exports.free(pPtr);
					}
				} else {
					workerClient.invokeSlotAction(msg.slot, 4, msg.patchJson || '{}');
				}
			});
			break;
		}
		case 'stele:optimistic_rollback': {
			initChain = initChain.then(() => {
				const comp = workerClient.slotInstances.get(msg.slot);
				const optRollbackFn = comp?.instance?.exports?.stl_optimistic_rollback || comp?.instance?.exports?.stele_optimistic_rollback;
				if (optRollbackFn && msg.dataset && msg.itemId) {
					const [dPtr] = workerClient.encodeString(comp.instance, comp.memory, msg.dataset);
					const [iPtr] = workerClient.encodeString(comp.instance, comp.memory, msg.itemId);
					workerClient.beginTurn();
					try {
						optRollbackFn(dPtr, iPtr);
						workerClient.updateSlot(msg.slot);
					} finally {
						workerClient.flushTurn();
					}
					if (comp.instance.exports.free) {
						comp.instance.exports.free(dPtr);
						comp.instance.exports.free(iPtr);
					}
				} else {
					workerClient.invokeSlotAction(msg.slot, 5, msg.itemId || '');
				}
			});
			break;
		}
		case 'stele:emit': {
			initChain = initChain.then(() => {
				workerClient.emit(msg.topic, msg.payload);
			});
			break;
		}
		case 'stele:ws_send': {
			if (globalScope.__axil_ws && typeof globalScope.__axil_ws.send === 'function') {
				globalScope.__axil_ws.send(msg.data);
			}
			break;
		}
		case 'stele:tty_input': {
			initChain = initChain.then(() => {
				for (const comp of workerClient.slotInstances.values()) {
					const fn = comp?.instance?.exports?.wasm_term_on_input || comp?.instance?.exports?.wasm_tty_on_input;
					const malloc = comp?.instance?.exports?.malloc;
					const free = comp?.instance?.exports?.free;
					if (fn && malloc && comp.memory) {
						const bytes = new TextEncoder().encode(msg.data || '');
						const ptr = malloc(bytes.length);
						if (ptr) {
							new Uint8Array(comp.memory.buffer, ptr, bytes.length).set(bytes);
							workerClient.beginTurn();
							try {
								fn(ptr, bytes.length);
							} finally {
								workerClient.flushTurn();
							}
							if (free) free(ptr);
						}
					}
				}
			});
			break;
		}
		default:
			break;
		}
	});
}

export const steleClient = !isWorkerContext ? new SteleWasmClient() : null;
export const stlClient = steleClient;

if (typeof window !== 'undefined' && !isWorkerContext) {
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
