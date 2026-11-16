RACK_DIR ?= ../sdk/Rack-SDK

RUST_DIR := rust

# Platform (ARCH_MAC / ARCH_WIN / ARCH_LIN, ARCH_X64 / ARCH_ARM64). plugin.mk includes it again; that is harmless.
include $(RACK_DIR)/arch.mk

# Rack for Windows is built with MinGW, so the Rust library must use the GNU target, not the default MSVC one.
ifdef ARCH_WIN
	RUST_TARGET ?= x86_64-pc-windows-gnu
endif
ifdef RUST_TARGET
	RUST_LIB := $(RUST_DIR)/target/$(RUST_TARGET)/release/libchimera_rack.a
	CARGO_TARGET := --target $(RUST_TARGET)
else
	RUST_LIB := $(RUST_DIR)/target/release/libchimera_rack.a
endif
# Oldest macOS for the Rust code: 11 on Apple Silicon. On Intel, 10.12, the oldest that Rust supports
# (Rack's own flags say 10.9, so Phona needs a newer Mac than Rack does).
ifdef ARCH_MAC
ifdef ARCH_X64
	RUST_MACOS_MIN := 10.12
else
	RUST_MACOS_MIN := 11.0
endif
endif
# Same minimum x64 CPU as Rack itself (compile.mk: -march=nehalem). Nehalem has no FMA, so results do not change.
ifdef ARCH_X64
	RUST_CPU := -C target-cpu=nehalem
endif
# System libraries the Rust standard library needs (`cargo rustc -- --print native-static-libs`)
ifdef ARCH_LIN
	LDFLAGS += -lpthread -ldl -lm
endif
ifdef ARCH_WIN
	LDFLAGS += -lkernel32 -lntdll -luserenv -lws2_32 -ldbghelp -lsynchronization
	# Put libgcc and winpthread inside plugin.dll. Otherwise it needs MinGW DLLs from Rack's folder, which can be
	# older than the compiler's (for example, without nanosleep64), and then the plugin does not load.
	LDFLAGS += -static-libgcc -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic
endif

FLAGS += -I$(RUST_DIR)
SOURCES += $(wildcard src/*.cpp)
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

# Rust engine, built before linking
OBJECTS += $(RUST_LIB)
# Always ask cargo (it tracks the engine sources and calibration data itself). The recipe must be on
# the library target itself: with a separate phony prerequisite, GNU Make 3.81 (macOS) compares the
# library's old timestamp and does not relink the plugin after a Rust-only change.
.PHONY: FORCE
FORCE:
$(RUST_LIB): FORCE
# The path remap keeps the build machine's folder names (user name) out of the shared binary.
	cd $(RUST_DIR) && MACOSX_DEPLOYMENT_TARGET=$(RUST_MACOS_MIN) \
		RUSTFLAGS="--remap-path-prefix=$(abspath ..)=. --remap-path-prefix=$(HOME)=~ $(RUST_CPU)" \
		cargo build --release $(CARGO_TARGET)

include $(RACK_DIR)/plugin.mk

# Package to give to other people: the same as `dist`, but without the build machine's user name
# in the archive headers. Output: share/<slug>-<version>-<arch>.vcvplugin
.PHONY: share
share: dist
	mkdir -p share
	cd dist && tar --no-xattrs --uid 0 --gid 0 --uname "" --gname "" -c $(SLUG) | zstd -$(ZSTD_COMPRESSION_LEVEL) -f -o ../share/"$(SLUG)"-"$(VERSION)"-$(ARCH_NAME).vcvplugin

# Signed and notarized package (Apple Developer ID). Needs the certificate in the keychain and a notarytool
# keychain profile. Override with SIGN_IDENTITY=... NOTARY_PROFILE=... . A bare dylib cannot be stapled, so
# Gatekeeper checks the ticket online. Output: share/<slug>-<version>-<arch>.vcvplugin (replaces the unsigned one)
SIGN_IDENTITY ?= Developer ID Application: Rob Schoen
NOTARY_PROFILE ?= framedl-notary
.PHONY: notarize
notarize: dist
	codesign --force --options runtime --timestamp --sign "$(SIGN_IDENTITY)" dist/$(SLUG)/$(TARGET)
	codesign --verify --strict --verbose=2 dist/$(SLUG)/$(TARGET)
	rm -f dist/notarize.zip
	cd dist && ditto -c -k --keepParent $(SLUG) notarize.zip
	xcrun notarytool submit dist/notarize.zip --keychain-profile "$(NOTARY_PROFILE)" --wait | tee dist/notarize.txt
	grep -q "status: Accepted" dist/notarize.txt
	rm -f dist/notarize.zip
	mkdir -p share
	cd dist && tar --no-xattrs --uid 0 --gid 0 --uname "" --gname "" -c $(SLUG) | zstd -$(ZSTD_COMPRESSION_LEVEL) -f -o ../share/"$(SLUG)"-"$(VERSION)"-$(ARCH_NAME).vcvplugin
