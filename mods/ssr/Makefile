RUST_RENDERER_DIR = rust-renderer
RUST_RENDERER_LIB = ${RUST_RENDERER_DIR}/target/release/libndc_ssr_render.a

EXTRA_LDLIBS += ${RUST_RENDERER_LIB} -lqsys
EXTRA_LDLIBS-OpenBSD += -lexecinfo -lpthread -lc++abi -lcompiler_rt
LDFLAGS += -shared

include ../../build.mk

rust-renderer:
	cargo build --manifest-path ${RUST_RENDERER_DIR}/Cargo.toml --release

${TARGET}: rust-renderer

clean:
	rm -f ${TARGET}
	cargo clean --manifest-path ${RUST_RENDERER_DIR}/Cargo.toml

.PHONY: clean rust-renderer
