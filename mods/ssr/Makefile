RUST_RENDERER_DIR = rust-renderer
RUST_TARGET_DIR   = ../../target
TARGET            = ssr.so
DEV              ?= 0

ifeq ($(DEV),1)
CARGO_FLAGS   =
CARGO_OUT_DIR = $(RUST_TARGET_DIR)/debug
else
CARGO_FLAGS   = --release
CARGO_OUT_DIR = $(RUST_TARGET_DIR)/release
endif

rust-renderer:
	cargo build --manifest-path $(RUST_RENDERER_DIR)/Cargo.toml \
		--target-dir $(RUST_TARGET_DIR) $(CARGO_FLAGS)

$(TARGET): rust-renderer
	cp $(CARGO_OUT_DIR)/libndc_dioxus.so $@

all: ${TARGET}

clean:
	rm -f ${TARGET}

distclean: clean
	cargo clean --manifest-path ${RUST_RENDERER_DIR}/Cargo.toml

.PHONY: all clean distclean rust-renderer
