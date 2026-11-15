# Platform settings for the headless tests. Source it after RACK_DIR is set.
# RACK_RUN: folder with the libRack that the test programs run against. The Mac and Linux SDKs
# have a complete libRack; the Windows SDK has only an import library, so on Windows set
# RACK_RUN to a Rack installation (libRack.dll and the DLLs next to it).
RACK_RUN=${RACK_RUN:-$RACK_DIR}
TESTS_DIR=${TESTS_DIR:-tests}
TEST_FLAGS=""
# GCC (Linux, Windows): see rack_test.hpp
GCC_TEST_FLAGS="-include $TESTS_DIR/rack_test.hpp"
case "$(uname -s)" in
Darwin)
	TEST_LIBS="-framework CoreFoundation -framework Security -lpthread -ldl"
	RUST_LIB=rust/target/release/libchimera_rack.a
	export DYLD_LIBRARY_PATH="$RACK_RUN${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
	;;
Linux)
	TEST_FLAGS="$GCC_TEST_FLAGS"
	TEST_LIBS="-lpthread -ldl -lm"
	RUST_LIB=rust/target/release/libchimera_rack.a
	export LD_LIBRARY_PATH="$RACK_RUN${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
	;;
MINGW* | MSYS*)
	TEST_FLAGS="-D_USE_MATH_DEFINES $GCC_TEST_FLAGS"
	TEST_LIBS="-lkernel32 -lntdll -luserenv -lws2_32 -ldbghelp -lsynchronization -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic"
	RUST_LIB=rust/target/x86_64-pc-windows-gnu/release/libchimera_rack.a
	PATH="$(cygpath -u "$RACK_RUN"):$PATH" # a Windows path (D:/...) has a colon that breaks PATH
	export PATH
	;;
*)
	echo "Unknown platform: $(uname -s)" >&2
	exit 1
	;;
esac
