#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
RACK_DIR=${RACK_DIR:-../sdk/Rack-SDK}
TEST_TMP=$(mktemp -d "${TMPDIR:-/tmp}/phona-beatbox.XXXXXX")
trap 'rm -rf "$TEST_TMP"' EXIT HUP INT TERM
c++ -std=c++11 -O2 -I"$RACK_DIR/include" -I"$RACK_DIR/dep/include" -Irust \
  tests/beatbox.cpp rust/target/release/libchimera_rack.a -L"$RACK_DIR" -lRack \
  -framework CoreFoundation -framework Security -lpthread -ldl -o "$TEST_TMP/beatbox-test"
for rate in 44100 48000 96000; do
  printf "Sample rate: %s Hz\n" "$rate"
  DYLD_LIBRARY_PATH="$RACK_DIR${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" "$TEST_TMP/beatbox-test" "$rate"
done
