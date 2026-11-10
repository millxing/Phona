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
	cd $(RUST_DIR) && MACOSX_DEPLOYMENT_TARGET=11.0 cargo build --release

include $(RACK_DIR)/plugin.mk
