all: client-wasm songbook.so

include ../../build.mk

client-wasm:
	@if command -v wasm-bindgen >/dev/null 2>&1 && rustc --print target-list | grep wasm32-unknown-unknown; then \
		${MAKE} -C client; \
	else \
		echo "Skipping songbook wasm client build (need wasm-bindgen and target wasm32-unknown-unknown)"; \
	fi
