#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
RACK_DIR=${RACK_DIR:-../sdk/Rack-SDK}
c++ -std=c++11 -O2 -I"$RACK_DIR/include" -I"$RACK_DIR/dep/include" -Irust \
  tests/beatbox.cpp rust/target/release/libchimera_rack.a -L"$RACK_DIR" -lRack \
  -framework CoreFoundation -framework Security -lpthread -ldl -o /tmp/phona-beatbox-test
DYLD_LIBRARY_PATH="$RACK_DIR${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" /tmp/phona-beatbox-test
