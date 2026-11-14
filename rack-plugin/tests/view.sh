#!/bin/sh
# Phona View headless test. Optional: VIEW_SVG_DIR=<dir> also writes SVG pictures of the display.
set -eu
cd "$(dirname "$0")/.."
RACK_DIR=${RACK_DIR:-../sdk/Rack-SDK}
TEST_TMP=$(mktemp -d "${TMPDIR:-/tmp}/phona-view.XXXXXX")
trap 'rm -rf "$TEST_TMP"' EXIT HUP INT TERM
c++ -std=c++11 -O2 -I"$RACK_DIR/include" -I"$RACK_DIR/dep/include" -Irust -Wno-deprecated-declarations -Wno-\#warnings \
  tests/view.cpp rust/target/release/libchimera_rack.a -L"$RACK_DIR" -lRack \
  -framework CoreFoundation -framework Security -lpthread -ldl -o "$TEST_TMP/view-test"
for rate in 44100 48000 96000; do
  printf "Sample rate: %s Hz\n" "$rate"
  DYLD_LIBRARY_PATH="$RACK_DIR${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" "$TEST_TMP/view-test" "$rate" ${VIEW_SVG_DIR:-}
done
