RACK_DIR ?= ../sdk/Rack-SDK

RUST_DIR := rust
RUST_LIB := $(RUST_DIR)/target/release/libchimera_rack.a

FLAGS += -I$(RUST_DIR)
SOURCES += $(wildcard src/*.cpp)
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

# Rust engine, built before linking (local prototype: host target only)
OBJECTS += $(RUST_LIB)
# phony: always ask cargo, which tracks the engine sources and calibration data itself
.PHONY: rustlib
$(RUST_LIB): rustlib
rustlib:
	cd $(RUST_DIR) && MACOSX_DEPLOYMENT_TARGET=11.0 cargo build --release

include $(RACK_DIR)/plugin.mk
