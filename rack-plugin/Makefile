RACK_DIR ?= ../sdk/Rack-SDK

RUST_DIR := rust
RUST_LIB := $(RUST_DIR)/target/release/libchimera_rack.a

FLAGS += -I$(RUST_DIR)
SOURCES += $(wildcard src/*.cpp)
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

# Rust engine, built before linking (local prototype: host target only)
OBJECTS += $(RUST_LIB)
# Always ask cargo (it tracks the engine sources and calibration data itself). The recipe must be on
# the library target itself: with a separate phony prerequisite, GNU Make 3.81 (macOS) compares the
# library's old timestamp and does not relink the plugin after a Rust-only change.
.PHONY: FORCE
FORCE:
$(RUST_LIB): FORCE
# The path remap keeps the build machine's folder names (user name) out of the shared binary.
	cd $(RUST_DIR) && MACOSX_DEPLOYMENT_TARGET=11.0 \
		RUSTFLAGS="--remap-path-prefix=$(abspath ..)=. --remap-path-prefix=$(HOME)=~" \
		cargo build --release

include $(RACK_DIR)/plugin.mk

# Package to give to other people: the same as `dist`, but without the build machine's user name
# in the archive headers. Output: share/<slug>-<version>-<arch>.vcvplugin
.PHONY: share
share: dist
	mkdir -p share
	cd dist && tar --no-xattrs --uid 0 --gid 0 --uname "" --gname "" -c $(SLUG) | zstd -$(ZSTD_COMPRESSION_LEVEL) -f -o ../share/"$(SLUG)"-"$(VERSION)"-$(ARCH_NAME).vcvplugin
