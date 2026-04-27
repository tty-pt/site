RUST_RENDERER_DIR = rust-renderer
RUST_RENDERER_LIB = ${RUST_RENDERER_DIR}/target/release/libndc_ssr_render.so

EXTRA_LDLIBS += -L${RUST_RENDERER_DIR}/target/release -lndc_ssr_render -lqsys
LDFLAGS += -shared
LDFLAGS += -Wl,-rpath,\$$ORIGIN/rust-renderer/target/release

include ../../build.mk

rust-renderer:
	cargo build --manifest-path ${RUST_RENDERER_DIR}/Cargo.toml --release

${TARGET}: rust-renderer

clean:
	rm -f ${TARGET}
	cargo clean --manifest-path ${RUST_RENDERER_DIR}/Cargo.toml

.PHONY: clean rust-renderer
