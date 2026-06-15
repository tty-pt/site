function parseListenerList(value) {
	if (!value) {
		return [];
	}

	return value.split(',').map((entry) => {
		const parts = entry.split(':');
		return {
			event: parts[0] || '',
			bubbles: parts[1] === '1'
		};
	}).filter((entry) => entry.event.length > 0);
}

function parseNodeId(value) {
	if (!value) {
		return null;
	}

	const id = Number.parseInt(value, 10);
	return Number.isFinite(id) ? id : null;
}

function nodeIdFromComment(text) {
	if (!text) {
		return null;
	}

	const match = text.match(/^\/?bud-(?:text|fragment):(\d+)$/);
	return match ? Number.parseInt(match[1], 10) : null;
}

function walkNodes(root, visitor) {
	const stack = [root];

	while (stack.length > 0) {
		const node = stack.pop();
		visitor(node);
		if (node && node.childNodes && node.childNodes.length) {
			for (let i = node.childNodes.length - 1; i >= 0; i -= 1) {
				stack.push(node.childNodes[i]);
			}
		}
	}
}

function findTextNode(startComment) {
	let node = startComment ? startComment.nextSibling : null;

	while (node && node.nodeType === Node.COMMENT_NODE) {
		node = node.nextSibling;
	}

	return node && node.nodeType === Node.TEXT_NODE ? node : null;
}

function buildHydrationMap(root) {
	const nodes = new Map();

	walkNodes(root, (node) => {
		if (!node) {
			return;
		}

		if (node.nodeType === Node.ELEMENT_NODE) {
			const id = parseNodeId(node.getAttribute('data-bud-id'));
			if (id !== null) {
				nodes.set(id, node);
			}
			return;
		}

		if (node.nodeType !== Node.COMMENT_NODE) {
			return;
		}

		const id = nodeIdFromComment(node.textContent || '');
		if (id === null) {
			return;
		}

		const textNode = findTextNode(node);
		if (textNode) {
			nodes.set(id, textNode);
		}
	});

	return nodes;
}

function normalizeOp(op) {
	if (Array.isArray(op)) {
		return {
			op: op[0],
			a: op[1],
			b: op[2],
			c: op[3]
		};
	}

	return op || {};
}

function parseIntOrNull(value) {
	if (value === null || value === undefined || value === '') {
		return null;
	}

	const parsed = Number.parseInt(value, 10);
	return Number.isFinite(parsed) ? parsed : null;
}

function getListenerSpec(node, event) {
	if (!node || node.nodeType !== Node.ELEMENT_NODE) {
		return null;
	}

	const specs = parseListenerList(node.getAttribute('data-bud-on'));
	for (const spec of specs) {
		if (spec.event === event) {
			return spec;
		}
	}

	return null;
}

function getDocumentForNode(node) {
	if (!node) {
		if (typeof document !== 'undefined') {
			return document;
		}
		return null;
	}

	if (node.ownerDocument) {
		return node.ownerDocument;
	}

	if (node.nodeType === Node.DOCUMENT_NODE) {
		return node;
	}

	if (typeof document !== 'undefined') {
		return document;
	}

	return null;
}

function clearNodeChildren(node) {
	while (node.firstChild) {
		node.removeChild(node.firstChild);
	}
}

function removeTextWrapper(node, id) {
	const start = node.previousSibling;
	const end = node.nextSibling;
	const startId = start && start.nodeType === Node.COMMENT_NODE ? nodeIdFromComment(start.textContent || '') : null;
	const endId = end && end.nodeType === Node.COMMENT_NODE ? nodeIdFromComment(end.textContent || '') : null;

	if (start && startId === id) {
		start.remove();
	}
	if (end && endId === id) {
		end.remove();
	}
	if (node.parentNode) {
		node.parentNode.removeChild(node);
	}
}

class BudHostBase {
	constructor(root, listenerResolver = null) {
		this.root = root;
		this.listenerResolver = listenerResolver;
		this.rootListeners = new Map();
		this.boundHandlers = new Map();
		this.nodes = buildHydrationMap(root);
		this._parentStack = [];
		this._currentParent = root;
		this.autoBindListeners();
	}

	autoBindListeners() {
		for (const [id, node] of this.nodes.entries()) {
			if (node.nodeType === Node.ELEMENT_NODE) {
				const attr = node.getAttribute('data-bud-on');
				if (attr) {
					const list = parseListenerList(attr);
					for (const spec of list) {
						const key = `${id}:${spec.event}`;
						if (this.boundHandlers.has(key)) continue;
						const handler = this.resolveListener({
							id,
							event: spec.event,
							bubbles: spec.bubbles,
							node,
							root: this.root
						});
						if (typeof handler === 'function') {
							this.bind(id, spec.event, handler, spec.bubbles);
						}
					}
				}
			}
		}
	}

	getNode(id) {
		return this.nodes.get(id) || null;
	}

	setNode(id, node) {
		if (id === null || id === undefined || !node) {
			return;
		}

		this.nodes.set(id, node);
	}

	clearTrackedNodes() {
		this.nodes.clear();
		this.boundHandlers.clear();
		this.rootListeners.clear();
	}

	removeTrackedNode(id) {
		this.nodes.delete(id);
		this.boundHandlers.delete(`${id}:`);
	}

	resolveListener(info) {
		if (typeof this.listenerResolver !== 'function') {
			return null;
		}

		return this.listenerResolver(info);
	}

	bind(id, event, handler, isBubbling = false) {
		const node = this.getNode(id);
		if (!node || !event || typeof handler !== 'function') {
			return false;
		}

		const key = `${id}:${event}`;
		const listenerMap = this.boundHandlers.get(key) || new Map();
		listenerMap.set(handler, true);
		this.boundHandlers.set(key, listenerMap);

		if (!isBubbling) {
			node.addEventListener(event, handler);
			return true;
		}

		if (!this.rootListeners.has(event)) {
			const rootHandler = (evt) => {
				const path = typeof evt.composedPath === 'function'
					? evt.composedPath()
					: [evt.target];

				for (const candidate of path) {
					if (!candidate || candidate.nodeType !== Node.ELEMENT_NODE) {
						continue;
					}

					const candidateId = parseNodeId(candidate.getAttribute('data-bud-id'));
					if (candidateId === null) {
						continue;
					}

					const handlers = this.boundHandlers.get(`${candidateId}:${event}`);
					if (!handlers) {
						continue;
					}

					for (const fn of handlers.keys()) {
						fn(evt, candidate);
					}
					break;
				}
			};

			this.root.addEventListener(event, rootHandler);
			this.rootListeners.set(event, rootHandler);
		}

		return true;
	}

	clearRoot() {
		if (!this.root) {
			return;
		}

		clearNodeChildren(this.root);
		this.clearTrackedNodes();
	}

	createWrappedText(id, text, parent) {
		const doc = getDocumentForNode(parent || this.root);
		let start;
		let textNode;
		let end;

		if (!doc) {
			return null;
		}

		start = doc.createComment(`bud-text:${id}`);
		textNode = doc.createTextNode(text);
		end = doc.createComment(`/bud-text:${id}`);
		parent.appendChild(start);
		parent.appendChild(textNode);
		parent.appendChild(end);
		this.setNode(id, textNode);
		return textNode;
	}

	removeNode(id) {
		const node = this.getNode(id);
		let textId;

		if (!node) {
			return false;
		}

		if (node.nodeType === Node.TEXT_NODE) {
			removeTextWrapper(node, id);
		} else {
			walkNodes(node, (child) => {
				if (!child) {
					return;
				}
				if (child.nodeType === Node.ELEMENT_NODE) {
					textId = parseNodeId(child.getAttribute('data-bud-id'));
					if (textId !== null) {
						this.nodes.delete(textId);
					}
					return;
				}
				if (child.nodeType === Node.TEXT_NODE) {
					textId = nodeIdFromComment(child.previousSibling && child.previousSibling.textContent ? child.previousSibling.textContent : '');
					if (textId !== null) {
						this.nodes.delete(textId);
					}
				}
			});
			node.remove();
		}

		this.nodes.delete(id);
		return true;
	}

	applyPatchRecord(op) {
		const current = this.getNode(parseIntOrNull(op.b));
		let node;
		let parent;
		let id;
		let tag;
		let bubbles;
		let handler;

		switch (op.op) {
		case 'patch-clear':
			{
				const rootParent = this.root ? this.root.parentNode : null;
				if (rootParent) {
					rootParent.removeChild(this.root);
				}
				this.clearRoot();
				this._parentStack.length = 0;
				this._currentParent = rootParent || this.root;
				this._rebuilding = true;
			}
			return true;
		case 'patch-open':
			tag = op.a || '';
			id = parseIntOrNull(op.b);
			if (id === null) {
				throw new Error('bud: missing patch node id');
			}
			parent = this._currentParent || this.root;
			node = current;
			if (!node || node.nodeType !== Node.ELEMENT_NODE || node.tagName.toLowerCase() !== tag) {
				node = getDocumentForNode(parent).createElement(tag);
				node.setAttribute('data-bud-id', String(id));
				parent.appendChild(node);
				this.setNode(id, node);
			} else {
				if (node.getAttribute('data-bud-id') !== String(id)) {
					node.setAttribute('data-bud-id', String(id));
				}
				if (node.parentNode !== parent) {
					parent.appendChild(node);
				}
				this.setNode(id, node);
			}
			if (this._rebuilding) {
				this.root = node;
				this._rebuilding = false;
			}
			this._parentStack.push(this._currentParent || this.root);
			this._currentParent = node;
			return true;
		case 'patch-attr':
			id = parseIntOrNull(op.a);
			node = this.getNode(id);
			if (node && node.nodeType === Node.ELEMENT_NODE) {
				if (node.getAttribute(op.b) !== op.c) {
					node.setAttribute(op.b, op.c || '');
				}
			}
			return true;
		case 'patch-listener':
			id = parseIntOrNull(op.a);
			node = this.getNode(id);
			if (!node) {
				throw new Error('bud: missing patch listener node');
			}
			bubbles = op.c === '1';
			handler = this.resolveListener({
				id,
				event: op.b,
				bubbles,
				node,
				root: this.root
			});
			if (typeof handler === 'function') {
				this.bind(id, op.b, handler, bubbles);
			}
			return true;
			case 'patch-text':
				id = parseIntOrNull(op.b);
				if (id === null) {
					throw new Error('bud: missing patch text id');
				}
				parent = this._currentParent || this.root;
				node = this.getNode(id);
				if (!node || node.nodeType !== Node.TEXT_NODE) {
					this.createWrappedText(id, op.a || '', parent);
				} else if (node.textContent !== op.a) {
					node.textContent = op.a || '';
				}
			return true;
		case 'patch-raw':
			id = parseIntOrNull(op.b);
			parent = this._currentParent || this.root;
			if (parent && parent.nodeType === Node.ELEMENT_NODE) {
				const doc = getDocumentForNode(parent);
				const marker = doc.createComment(`bud-raw:${id}`);
				parent.appendChild(marker);
				if (op.a) {
					const tpl = doc.createElement('template');
					tpl.innerHTML = op.a;
					parent.insertBefore(tpl.content, marker.nextSibling);
				}
			}
			return true;
		case 'patch-remove':
			id = parseIntOrNull(op.a);
			if (id !== null) {
				this.removeNode(id);
			}
			return true;
		case 'patch-replace':
			id = parseIntOrNull(op.a);
			if (id !== null) {
				this.removeNode(id);
			}
			return true;
		case 'patch-close':
			this._currentParent = this._parentStack.pop() || this.root;
			return true;
		case 'patch-innerhtml':
			id = parseIntOrNull(op.a);
			if (id !== null) {
				node = this.getNode(id);
				if (node && node.nodeType === Node.ELEMENT_NODE) {
					node.innerHTML = op.b || '';
				}
			}
			return true;
		default:
			return false;
		}
	}

	replay(ops) {
		const stack = [];
		const walkStack = [];
		let current = null;

		for (const rawOp of ops) {
			const op = normalizeOp(rawOp);
			if (op.op && op.op.startsWith('patch-')) {
				this.applyPatchRecord(op);
				continue;
			}

			switch (op.op) {
			case 'fragment-open':
				current = this.getNode(Number.parseInt(op.a, 10));
				stack.push(current);
				break;
			case 'fragment-close':
				stack.pop();
				current = stack[stack.length - 1] || null;
				break;
			case 'element-open':
				current = this.getNode(Number.parseInt(op.b, 10)) || this.getNode(Number.parseInt(op.c, 10));
				if (!current || current.nodeType !== Node.ELEMENT_NODE) {
					throw new Error(`bud: missing element node for ${op.a}`);
				}
				stack.push(current);
				break;
			case 'attr':
				if (current && current.nodeType === Node.ELEMENT_NODE) {
					if (current.getAttribute(op.a) !== op.b) {
						current.setAttribute(op.a, op.b);
					}
				}
				break;
			case 'listener': {
				const id = Number.parseInt(op.c, 10);
				const node = this.getNode(id);
				if (!node) {
					throw new Error('bud: missing listener node');
				}
				const bubbles = typeof op.b === 'string' && op.b === '1';
				const handler = this.resolveListener({
					id,
					event: op.a,
					bubbles,
					node,
					root: this.root
				});
				if (typeof handler === 'function') {
					this.bind(id, op.a, handler);
				}
				break;
			}
			case 'text': {
				const node = this.getNode(Number.parseInt(op.b, 10)) || this.getNode(Number.parseInt(op.c, 10));
				if (!node) {
					throw new Error('bud: missing text node');
				}
				if (node.textContent !== op.a) {
					node.textContent = op.a;
				}
				break;
			}
			case 'element-close':
				stack.pop();
				current = stack[stack.length - 1] || null;
				break;
			case 'walk-enter': {
				const id = parseIntOrNull(op.c);
				if (id === null) {
					throw new Error('bud: missing walk node id');
				}
				if (!this.getNode(id)) {
					throw new Error('bud: missing walk node');
				}
				walkStack.push(id);
				break;
			}
			case 'walk-attr': {
				const node = this.getNode(parseIntOrNull(op.a));
				if (node && node.nodeType === Node.ELEMENT_NODE && node.getAttribute(op.b) !== op.c) {
					node.setAttribute(op.b, op.c);
				}
				break;
			}
			case 'walk-listener': {
				const id = parseIntOrNull(op.a);
				const node = this.getNode(id);
				if (!node) {
					throw new Error('bud: missing walk listener node');
				}
				const bubbles = op.c === '1';
				const handler = this.resolveListener({
					id,
					event: op.b,
					bubbles,
					node,
					root: this.root
				});
				if (typeof handler === 'function') {
					this.bind(id, op.b, handler);
				}
				break;
			}
			case 'walk-text': {
				const node = this.getNode(parseIntOrNull(op.a));
				if (node && node.nodeType === Node.TEXT_NODE && node.textContent !== op.c) {
					node.textContent = op.c;
				}
				break;
			}
			case 'walk-leave':
				walkStack.pop();
				break;
			default:
				break;
			}
		}

		return this;
	}
}

export class BudHydrator extends BudHostBase {
	constructor(root, listenerResolver = null) {
		super(root, listenerResolver);
	}
}

export class BudPatchApplier extends BudHostBase {
	constructor(root, listenerResolver = null) {
		super(root, listenerResolver);
	}

	apply(ops) {
		return this.replay(ops);
	}
}

function getWasmExport(wasm, names) {
	for (const name of names) {
		if (wasm && typeof wasm[name] === 'function') {
			return wasm[name];
		}
	}

	return null;
}

function readWasmString(memory, ptr, len) {
	if (!memory || !ptr || len < 0) {
		return '';
	}

	const view = new Uint8Array(memory.buffer, ptr, len);
	return new TextDecoder('utf-8').decode(view);
}


export class BudWasmBridge {
	constructor(root, wasm = null, listenerResolver = null) {
		this.wasm = wasm || null;
		this.memory = this.wasm && this.wasm.memory ? this.wasm.memory : null;
		this.listenerResolver = listenerResolver;
		this.host = new BudPatchApplier(root, (info) => this.resolveListener(info));
		this._debugged = false;
	}

	setWasm(wasm) {
		this.wasm = wasm || null;
		this.memory = this.wasm && this.wasm.memory ? this.wasm.memory : null;
		if (this.wasm) {
			this.rescan();
		}
		return this;
	}

	rescan() {
		if (this.host) {
			this.host.autoBindListeners();
		}
		return this;
	}

	setMemory(memory) {
		this.memory = memory || null;
		return this;
	}

	mount() {
		const fn = getWasmExport(this.wasm, ['bud_app_mount', 'bud_demo_mount']);
		if (fn) {
			fn();
		}
		return this;
	}

	resolveListener(info) {
		if (this.wasm && typeof this.wasm.bud_app_dispatch === 'function') {
			return (evt, candidate) => {
				const target = evt.target;
				let eventData = null;
				const malloc = getWasmExport(this.wasm, ['malloc']);
				const free = getWasmExport(this.wasm, ['free']);
				let eventDataPtr = 0;
				if (target && target.nodeType === Node.ELEMENT_NODE) {
					let value = '';
					const tag = target.tagName.toLowerCase();
					if (tag === 'select') {
						value = target.value;
					} else if (tag === 'input' && target.type === 'checkbox') {
						value = target.checked ? '1' : '0';
					} else if (tag === 'input' || tag === 'textarea') {
						value = target.value;
					}
					if (value && malloc) {
						const bytes = new TextEncoder().encode(value + '\0');
						eventDataPtr = malloc(bytes.length);
						if (eventDataPtr) {
							new Uint8Array(this.memory.buffer, eventDataPtr, bytes.length).set(bytes);
							eventData = eventDataPtr;
						}
					}
				}
				this.dispatchEvent(info.id, info.event, info.bubbles, eventData);
				if (free && eventDataPtr) {
					free(eventDataPtr);
				}
			};
		}
		if (typeof this.listenerResolver === 'function') {
			const resolved = this.listenerResolver(info);
			if (typeof resolved === 'function') {
				return resolved;
			}
		}

		return (evt) => {
			const target = evt && evt.target;
			let eventData = null;
			if (target && target.nodeType === Node.ELEMENT_NODE) {
				let value = '';
				const tag = target.tagName.toLowerCase();
				if (tag === 'select') {
					value = target.value;
				} else if (tag === 'input' && target.type === 'checkbox') {
					value = target.checked ? '1' : '0';
				} else if (tag === 'input' || tag === 'textarea') {
					value = target.value;
				}
				if (value) {
					const malloc = getWasmExport(this.wasm, ['malloc']);
					if (malloc) {
						const bytes = new TextEncoder().encode(value + '\0');
						const ptr = malloc(bytes.length);
						if (ptr) {
							new Uint8Array(this.memory.buffer, ptr, bytes.length).set(bytes);
							eventData = ptr;
						}
					}
				}
			}
			this.dispatchEvent(info.id, info.event, info.bubbles, eventData);
			if (eventData) {
				const free = getWasmExport(this.wasm, ['free']);
				if (free) free(eventData);
			}
		};
	}

	makeImports() {
		const bridge = this;

		return {
			__proto__: null,
			bud_host_emit_patch(op_ptr, op_len, a_ptr, a_len, b_ptr, b_len, c_ptr, c_len) {
				const op = readWasmString(bridge.memory, op_ptr, op_len);
				const a = readWasmString(bridge.memory, a_ptr, a_len);
				const b = readWasmString(bridge.memory, b_ptr, b_len);
				const c = readWasmString(bridge.memory, c_ptr, c_len);
				bridge.host.replay([{ op, a, b, c }]);
			},
			bud_host_mark_dirty() {
				bridge.markDirty();
			},
			bud_host_flush() {
				bridge.flush();
			},
			bud_host_log(msg_ptr, msg_len) {
				const msg = readWasmString(bridge.memory, msg_ptr, msg_len);
				console.debug('[wasm]', msg);
			},
			bud_host_fetch(url_ptr, url_len, request_id) {
				const url = readWasmString(bridge.memory, url_ptr, url_len);
				fetch(url)
					.then(res => res.text())
					.then(text => {
						const malloc = getWasmExport(bridge.wasm, ['malloc']);
						if (!malloc) return;
						const bytes = new TextEncoder().encode(text + '\0');
						const data_ptr = malloc(bytes.length);
						if (!data_ptr) return;
						new Uint8Array(bridge.memory.buffer, data_ptr, bytes.length).set(bytes);
						const fn = getWasmExport(bridge.wasm, ['wasm_fetch_callback']);
						if (fn) {
							fn(request_id, data_ptr, bytes.length - 1);
						}
						const freeFn = getWasmExport(bridge.wasm, ['free']);
						if (freeFn) freeFn(data_ptr);
					})
					.catch(err => console.error('bud_host_fetch error:', err));
			},
			bud_host_set_location(url_ptr, url_len) {
				const url = readWasmString(bridge.memory, url_ptr, url_len);
				history.replaceState(null, '', url);
			},
		};
	}

	applyPatchOps(ops) {
		this.host.replay(ops);
		return this;
	}

	hydrate(ops) {
		this.host.replay(ops);
		return this;
	}

	update() {
		const fn = getWasmExport(this.wasm, ['bud_app_update']);
		if (fn) {
			fn();
		}
		return this;
	}

	unmount() {
		const fn = getWasmExport(this.wasm, ['bud_app_unmount']);
		if (fn) {
			fn();
		}
		return this;
	}

	markDirty() {
		const fn = getWasmExport(this.wasm, ['bud_app_mark_dirty']);
		if (fn) {
			fn();
		}
		return this;
	}

	flush() {
		const fn = getWasmExport(this.wasm, ['bud_app_flush']);
		if (fn) {
			fn();
		}
		return this;
	}

	dispatchEvent(nodeId, eventName, bubbles = true, eventInit = null) {
		const fn = getWasmExport(this.wasm, ['bud_app_dispatch']);
		if (fn) {
			const malloc = getWasmExport(this.wasm, ['malloc']);
			const free = getWasmExport(this.wasm, ['free']);
			let namePtr = 0;
			
			if (malloc) {
				const bytes = new TextEncoder().encode(eventName + '\0');
				namePtr = malloc(bytes.length);
				if (namePtr) {
					const view = new Uint8Array(this.memory.buffer, namePtr, bytes.length);
					view.set(bytes);
				}
			}

			const ret = fn(nodeId, namePtr || 0, bubbles ? 1 : 0, eventInit || 0);

			if (free && namePtr) {
				free(namePtr);
			}

			return ret;
		}

		const node = this.host.getNode(nodeId);
		if (!node) {
			return false;
		}

		const event = new Event(eventName, {
			bubbles: !!bubbles,
			cancelable: true
		});
		if (eventInit && typeof eventInit === 'object') {
			const safe = {};
			for (const k of Object.keys(eventInit)) {
				if (k !== 'isTrusted') safe[k] = eventInit[k];
			}
			Object.assign(event, safe);
		}
		return node.dispatchEvent(event);
	}

	/* ── Tree debugging ── */

	getTreeText() {
		const fn = getWasmExport(this.wasm, ['wasm_get_tree_text']);
		if (fn) {
			const ptr = fn();
			return readWasmCString(this.memory, ptr);
		}
		return '';
	}

	getSource(nodeId) {
		const fn = getWasmExport(this.wasm, ['wasm_get_src']);
		if (fn) {
			const ptr = fn(nodeId);
			return readWasmCString(this.memory, ptr);
		}
		return `(no wasm_get_src export for node ${nodeId})`;
	}

	_parseTreeLine(rawLine) {
		const m = rawLine.match(/^(\s*)id=(\d+) kind=(\w+)(.*)$/);
		if (!m) {
			return null;
		}
		const depth = m[1].length;
		const id = Number.parseInt(m[2], 10);
		const kind = m[3];
		const rest = m[4].trim();
		let tag = '';
		let src = '';
		let text = '';
		const attrs = {};
		const srcMatch = rest.match(/\[(.+?:\d+)\]/);
		if (srcMatch) {
			src = srcMatch[1];
		}
		const tagMatch = rest.match(/^tag=(\S+)/);
		if (tagMatch) {
			tag = tagMatch[1];
		}
		const idMatch = rest.match(/\bid="([^"]+)"/);
		if (idMatch) {
			attrs.id = idMatch[1];
		}
		const classMatch = rest.match(/\bclass="([^"]+)"/);
		if (classMatch) {
			attrs.class = classMatch[1];
		}
		if (kind === 'TEXT') {
			const tmatch = rest.match(/^"([^"]*)"/);
			if (tmatch) {
				text = tmatch[1];
			}
		}
		return { id, kind, tag, attrs, src, text, depth };
	}

	_parseTree(text) {
		const entries = [];
		for (const rawLine of text.split('\n')) {
			if (!rawLine.trim()) {
				continue;
			}
			const parsed = this._parseTreeLine(rawLine);
			if (parsed) {
				entries.push(parsed);
			}
		}
		return entries;
	}

	_buildDomTree() {
		const seen = new Set();
		const entries = [];
		const root = this.host && this.host.root;
		if (!root) {
			return entries;
		}

		const stack = [{ node: root, depth: 0 }];
		while (stack.length > 0) {
			const { node, depth } = stack.pop();
			if (!node) {
				continue;
			}

			if (node.nodeType === Node.ELEMENT_NODE) {
				const id = parseNodeId(
				        node.getAttribute('data-bud-id'));
				if (id !== null && !seen.has(id)) {
					seen.add(id);
					const src = node.getAttribute('data-bud-src') || '(DOM)';
					const tag = node.tagName.toLowerCase();
					const attrs = {};
					const elId = node.getAttribute('id');
					if (elId) {
						attrs.id = elId;
					}
					const elClass = node.getAttribute('class');
					if (elClass) {
						attrs.class = elClass;
					}
					entries.push(
					        { id, kind: 'ELEMENT', tag, attrs, src, depth });
				}
			} else if (node.nodeType === Node.COMMENT_NODE) {
				const commentText = node.textContent || '';
				if (commentText.startsWith('/bud-')) {
					/* skip closing comments */
				} else {
					const id = nodeIdFromComment(commentText);
					if (id !== null && !seen.has(id)) {
						seen.add(id);
						if (commentText.startsWith('bud-text:')) {
							const textNode = findTextNode(node);
							const textContent = textNode
							        ? textNode.textContent.substring(0, 40)
							        : '';
							entries.push(
							        { id, kind: 'TEXT', tag: '', text: textContent, src: '(DOM)', depth });
						} else if (commentText.startsWith('bud-fragment:')) {
							entries.push(
							        { id, kind: 'FRAGMENT', tag: '', src: '(DOM)', depth });
						}
					}
				}
			}

			/* push children (reverse for document order) */
			if (node.childNodes && node.childNodes.length) {
				for (let i = node.childNodes.length - 1; i >= 0; i--) {
					stack.push(
					        { node: node.childNodes[i], depth: depth + 1 });
				}
			}
		}

		return entries;
	}

	dumpTrees() {
		console.log('═══════════════════════════════════════');
		console.log('WASM tree (call wasm_dump_tree via export):');
		const dumpFn = getWasmExport(this.wasm, ['wasm_dump_tree']);
		if (dumpFn) {
			dumpFn();
		} else {
			console.log('(wasm_dump_tree not available)');
		}
		console.log('───────────────────────────────────────');
		console.log('DOM tree (from server-rendered HTML):');
		const domEntries = this._buildDomTree();
		for (const e of domEntries) {
			console.log(`  id=${e.id} ${e.kind}${e.tag ? ' tag=' + e.tag : ''}`);
		}
		console.log('═══════════════════════════════════════');
	}

	_findNextMatchingEntry(entries, startIdx, target, matchAttrs = false) {
		for (let k = startIdx; k < entries.length; k++) {
			const e = entries[k];
			if (e.kind !== target.kind || e.tag !== target.tag) {
				continue;
			}
			if (matchAttrs) {
				if (e.kind === 'ELEMENT') {
					const ea = e.attrs || {};
					const ta = target.attrs || {};
					if (ea.id !== ta.id || ea.class !== ta.class) {
						continue;
					}
				} else if (e.kind === 'TEXT') {
					if (e.text !== target.text) {
						continue;
					}
				}
			}
			return k;
		}
		return -1;
	}

	compareTrees() {
		const wasmText = this.getTreeText();
		const wasmEntries = this._parseTree(wasmText);
		const domEntries = this._buildDomTree();

		const LOOKAHEAD_LIMIT = Math.min(
		        Math.max(wasmEntries.length, domEntries.length), 500);
		let first = null;
		const rawHtmlGaps = [];
		const diff = [];
		let matchRun = 0;
		let matchCount = 0;
		let i = 0, j = 0;

		while (i < wasmEntries.length || j < domEntries.length) {
			const w = wasmEntries[i];
			const d = domEntries[j];

			if (w && d && w.kind === d.kind && w.tag === d.tag) {
				matchRun++;
				matchCount++;
				i++;
				j++;
				continue;
			}

			if (matchRun > 0) {
				diff.push({ type: 'match', count: matchRun });
				matchRun = 0;
			}

			if (w && w.kind === 'RAW_HTML') {
				rawHtmlGaps.push(w.id);
				i++;
				continue;
			}
			if (d && d.kind === 'RAW_HTML') {
				rawHtmlGaps.push(d.id);
				j++;
				continue;
			}

			if (!d) {
				if (!first) first = { id: w.id, wasm: w, dom: null,
				        reason: 'missing_in_dom' };
				diff.push({ type: 'added', entry: w });
				i++;
				continue;
			}

			if (!w) {
				if (!first) first = { id: d.id, wasm: null, dom: d,
				        reason: 'missing_in_wasm' };
				diff.push({ type: 'removed', entry: d });
				j++;
				continue;
			}

			console.debug('WASM lookahead for d:', JSON.stringify(d));
			const wAhead = this._findNextMatchingEntry(
			        wasmEntries, i + 1, d, true);
			console.debug('  wAhead='+wAhead, wAhead>=0 ? JSON.stringify(wasmEntries[wAhead]) : null);
			if (wAhead >= 0 && wAhead - i <= LOOKAHEAD_LIMIT) {
				while (i < wAhead) {
					const ins = wasmEntries[i];
					if (ins.kind === 'RAW_HTML') {
						rawHtmlGaps.push(ins.id);
					} else {
						if (!first) first = { id: ins.id,
						        wasm: ins, dom: null,
						        reason: 'missing_in_dom' };
						diff.push({ type: 'added', entry: ins });
					}
					i++;
				}
				continue;
			}

			console.debug('DOM lookahead for w:', JSON.stringify(w));
			const dAhead = this._findNextMatchingEntry(
			        domEntries, j + 1, w, true);
			console.debug('  dAhead='+dAhead, dAhead>=0 ? JSON.stringify(domEntries[dAhead]) : null);
			if (dAhead >= 0 && dAhead - j <= LOOKAHEAD_LIMIT) {
				while (j < dAhead) {
					const rem = domEntries[j];
					if (!first) first = { id: rem.id,
					        wasm: null, dom: rem,
					        reason: 'missing_in_wasm' };
					diff.push({ type: 'removed', entry: rem });
					j++;
				}
				continue;
			}

			console.debug('MISMATCH FALLTHROUGH: w:', JSON.stringify(w), 'd:', JSON.stringify(d),
			        'wAhead:', wAhead, 'dAhead:', dAhead);
			if (!first) first = { id: w.id, wasm: w, dom: d,
			        reason: 'kind_or_tag_mismatch' };
			diff.push({ type: 'changed', wasm: w, dom: d });
			i++;
			j++;
		}

		if (matchRun > 0) {
			diff.push({ type: 'match', count: matchRun });
		}

		const result = {
		        matched: first === null,
		        wasmCount: wasmEntries.length,
		        domCount: domEntries.length,
		        matchCount,
		        diff,
		        first,
		        rawHtmlGaps,
		        wasmTree: wasmEntries,
		        domTree: domEntries
		};
		return result;
	}

	/* ── Auto-debug banner ── */

		autoDebug() {
		if (this._debugged) return;
		this._debugged = true;
		const result = this.compareTrees();
		if (result.matched) {
			console.info(
			        `WASM/SSR trees match — ${result.wasmCount} nodes`);
		} else {
			let msg =
			        `WASM/SSR mismatch: ${result.wasmCount} WASM vs ${result.domCount} DOM nodes`;
			if (result.rawHtmlGaps.length) {
				msg += ` (+${result.rawHtmlGaps.length} RAW_HTML gaps)`;
			}
			const diffEntryIds = new Set();
			for (const de of result.diff) {
				if (de.type !== 'match') {
					const e = de.entry || de.wasm;
					if (e) diffEntryIds.add(e.id);
				}
			}
			const shownAncestors = new Set();
			let inChunk = false;
			for (const de of result.diff) {
				if (de.type === 'match') {
					inChunk = false;
					continue;
				}
				const entry = de.entry || de.wasm;
				if (!entry) continue;
				const tree = de.type === 'removed' ? result.domTree : result.wasmTree;
				const ancestors = this._getAncestorLevels(entry, tree, 3)
					.filter(a => !diffEntryIds.has(a.id));
				for (const a of ancestors) {
					const key = `${a.depth}:${a.id}`;
					if (!shownAncestors.has(key)) {
						shownAncestors.add(key);
						msg += `\n  ${this._renderEntry(a)}`;
					}
				}
				if (!inChunk) {
					inChunk = true;
					msg += `\n${this._renderEntrySrc(entry)}`;
				}
				msg += `\n  ${this._renderDiffText(de)}`;
			}
			console.error(msg);
		}
		return result;
	}

	_renderEntry(e, { noDepth = false } = {}) {
		const indent = noDepth ? '' : '  '.repeat(e.depth || 0);
		switch (e.kind) {
		case 'ELEMENT':
			{
				let s = `${indent}<${e.tag}`;
				const attrs = e.attrs || {};
				if (attrs.id) {
					s += ` id="${attrs.id}"`;
				}
				if (attrs.class) {
					s += ` class="${attrs.class}"`;
				}
				return s + '>';
			}
		case 'TEXT':
			return `${indent}"${e.text || ''}"`;
		case 'FRAGMENT':
			return `${indent}<!-- fragment -->`;
		case 'RAW_HTML':
			return `${indent}<!-- raw html -->`;
		default:
			return `${indent}${e.kind}`;
		}
	}

	_renderEntrySrc(e) {
		return e && e.src ? `  ${e.src}` : '';
	}

	_renderDiffText(de) {
		switch (de.type) {
		case 'added':
			return `+ ${this._renderEntry(de.entry)}`;
		case 'removed':
			return `- ${this._renderEntry(de.entry)}`;
		case 'changed':
			return `~ ${this._renderEntry(de.wasm)}\n  ~ ${this._renderEntry(de.dom)}`;
		default:
			return '';
		}
	}

	_getAncestorLevels(entry, tree, maxLevels = 3) {
		if (!entry || entry.depth === undefined || entry.depth <= 0) {
			return [];
		}
		const targetId = entry.id;
		const lastAtDepth = {};
		for (const e of tree) {
			if (e.id === targetId) break;
			if (e.depth !== undefined) {
				lastAtDepth[e.depth] = e;
			}
		}
		const startDepth = Math.max(0, entry.depth - maxLevels);
		const chain = [];
		for (let d = startDepth; d < entry.depth; d++) {
			if (lastAtDepth[d]) {
				chain.push(lastAtDepth[d]);
			}
		}
		return chain;
	}

}


/* ── Tree debug helpers ── */

function readWasmCString(memory, ptr) {
	if (!ptr || !memory) {
		return '';
	}
	const view = new Uint8Array(memory.buffer);
	let end = ptr;
	while (end < view.length && view[end] !== 0) {
		end++;
	}
	return new TextDecoder('utf-8').decode(view.slice(ptr, end));
}

export function hydrateBud(root, ops, listenerResolver = null) {
	return new BudHydrator(root, listenerResolver).replay(ops);
}

export function hydrateBudFromWalk(root, walkOps, listenerResolver = null) {
	return hydrateBud(root, walkOps, listenerResolver);
}

export function applyBudPatch(root, patchOps, listenerResolver = null) {
	return new BudPatchApplier(root, listenerResolver).apply(patchOps);
}

export function createBudWasmBridge(root, wasm = null, listenerResolver = null) {
	return new BudWasmBridge(root, wasm, listenerResolver);
}

export { buildHydrationMap, parseListenerList };
