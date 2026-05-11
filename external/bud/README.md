# bud

`bud` is a small C DOM/runtime scaffold for this repo.

It currently provides:

- a tree model for fragments, elements, and text nodes
- attribute storage and child linking
- child/attr/listener accessors for host-side hydration
- a structured tree walk API for host-side inspection without HTML parsing
- a walk op stream for browser/WASM bridges
- HTML SSR serialization
- hydrated HTML markers for DOM reattachment
- a hydration callback stream with stable node IDs
- a mutation patch stream for browser-side DOM apply
- a validation hook that lets a WASM host confirm an existing DOM matches the rendered tree
- a runtime wrapper with dirty/flush state management
- host-driven event dispatch with bubbling and stop-propagation
- mount, update, and unmount lifecycle hooks
- a raw WASM host bridge shape for patch emission and event forwarding

The initial goal is not to replace the existing Rust Dioxus path. This package
exists so C-side rendering logic can grow behind a stable API that works for
both server rendering and browser-side patch emission.
