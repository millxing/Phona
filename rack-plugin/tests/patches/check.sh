#!/bin/sh
# Requires existing Rack SDK and installed modules. Builds only small test executables.
set -eu
: "${RACK_SDK:?Set RACK_SDK to the existing Rack-SDK directory}"
: "${RACK_PLUGINS:?Set RACK_PLUGINS to the installed plugins directory}"
PHONA_DIR=${PHONA_DIR:-"$RACK_PLUGINS/Phona"}
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TEST_TMP=$(mktemp -d "${TMPDIR:-/tmp}/phona-patch-test.XXXXXX")
trap 'rm -rf "$TEST_TMP"' EXIT HUP INT TERM
export DYLD_LIBRARY_PATH="$RACK_SDK${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
for name in host_probe host_smoke; do
  clang++ -std=c++11 -Wno-deprecated-declarations -I"$RACK_SDK/include" -I"$RACK_SDK/dep/include" \
    "$HERE/$name.cpp" -L"$RACK_SDK" -lRack -o "$TEST_TMP/$name"
done
"$TEST_TMP/host_probe" "$RACK_PLUGINS/Fundamental" SEQ3 LFO VCO VCA Notes > "$TEST_TMP/fundamental.json"
"$TEST_TMP/host_probe" "$PHONA_DIR" Phona > "$TEST_TMP/phona.json"
python3 - "$HERE/schema.json" "$TEST_TMP/fundamental.json" "$TEST_TMP/phona.json" <<'PY'
import json,sys
expected=json.load(open(sys.argv[1])); actual=json.load(open(sys.argv[2]));actual.update(json.load(open(sys.argv[3])))
assert expected==actual, 'Live schema differs: inspect IDs/ranges/state before updating schema.json'
print('PASS live installed schema matches fixture schema')
PY
python3 "$HERE/generate.py" --check
status=0
for patch in ${PHONA_PATCHES:-singing-triggers singing-held beatbox-triggers}; do
  "$TEST_TMP/host_smoke" "$RACK_PLUGINS/Fundamental" "$PHONA_DIR" "$HERE/$patch.vcv" || status=1
done
exit "$status"
