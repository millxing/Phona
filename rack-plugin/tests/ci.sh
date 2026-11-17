#!/usr/bin/env bash
# All headless tests for one platform, as GitHub Actions runs them. Run from anywhere after `make dist`.
# Needs: RACK_PLUGINS (a folder with Fundamental), RACK_RUN on Windows (see platform.sh),
# RUST_TARGET (Rust target triple of this platform).
# Writes a short CPU report to $GITHUB_STEP_SUMMARY when it is set.
set -euo pipefail # a failing test fails the pipe, not only the last grep
cd "$(dirname "$0")/.."
: "${RACK_PLUGINS:?Set RACK_PLUGINS to a folder that holds the Fundamental plugin}"
: "${RUST_TARGET:?Set RUST_TARGET to the Rust target of this platform}"
RACK_DIR=${RACK_DIR:-../sdk/Rack-SDK}
LOG=$(mktemp "${TMPDIR:-/tmp}/phona-ci.XXXXXX")
trap 'rm -f "$LOG"' EXIT HUP INT TERM
# On a failure, show the end of the full output (the filters below hide most of it).
trap 'echo "--- Failed. Last lines of the full output:"; tail -60 "$LOG"' ERR
# Show the useful lines only. Always exits 0, so that the exit status comes from the test.
quiet() { grep -vE "warning:|note:|^ *[0-9]* \\||^ *\\||In file included|^ *\\^|^ *~" || true; }

echo "== Same sound as macOS (bit for bit), and the plugin's Rust unit tests"
# Same CPU setting as the plugin build (Makefile: x64 only). The allocation test is a separate CI step.
case "$RUST_TARGET" in x86_64*) export RUSTFLAGS="-C target-cpu=nehalem" ;; esac
(cd rust && cargo test --release --lib --tests --target "$RUST_TARGET" -- \
	--skip alloc_test::audio_path_does_not_allocate --nocapture) 2>&1 | tee -a "$LOG" | { grep -E "^(test |sung|lyrics|beatbox|output differs|.*expected 0x)|panicked" || true; }

for t in beatbox poly view; do
	echo "== Module test: $t"
	sh "tests/$t.sh" 2>&1 | tee -a "$LOG" | quiet
done

echo "== The built plugin file in Rack's engine with Fundamental modules (patch tests)"
RACK_SDK=$(cd "$RACK_DIR" && pwd) PHONA_DIR=$(pwd)/dist/Spunoza sh tests/patches/check.sh 2>&1 | tee -a "$LOG" | quiet

if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
	{
		echo "### CPU, $RUST_TARGET"
		echo
		echo "Processor: ${CPU_NAME:-unknown}"
		echo
		echo "Phona with 3 voices, gates open (poly test; 100% = one full core):"
		echo
		awk '/^Sample rate/ { rate = $0 } /3 voices:/ { print "    " rate ": " $0 }' "$LOG"
	} >> "$GITHUB_STEP_SUMMARY"
fi
echo "All tests passed"
