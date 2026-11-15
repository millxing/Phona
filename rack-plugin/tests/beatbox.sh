#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
RACK_DIR=${RACK_DIR:-../sdk/Rack-SDK}
. tests/platform.sh
TEST_TMP=$(mktemp -d "${TMPDIR:-/tmp}/phona-beatbox.XXXXXX")
trap 'rm -rf "$TEST_TMP"' EXIT HUP INT TERM
c++ -std=c++11 -O2 $TEST_FLAGS -I"$RACK_DIR/include" -I"$RACK_DIR/dep/include" -Irust \
  tests/beatbox.cpp "$RUST_LIB" -L"$RACK_DIR" -lRack \
  $TEST_LIBS -o "$TEST_TMP/beatbox-test"
for rate in 44100 48000 96000; do
  printf "Sample rate: %s Hz\n" "$rate"
  "$TEST_TMP/beatbox-test" "$rate"
done
