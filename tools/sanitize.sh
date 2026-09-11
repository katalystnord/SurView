#!/usr/bin/env bash
# Run SurView's suite under the address and undefined-behaviour sanitizers.
#
# ⚑ WHAT THIS CATCHES THAT NOTHING ELSE DOES. A read or write one element past
# the end of a vector returns whatever sits there and changes nothing
# downstream: no assertion can see it, and a mutation sweep reports it as a
# survivor with nothing to be done about it. That is the largest single class of
# survivor left after the sweeps of 2026-09-10/11, and this is what closes it.
#
# ⚑ Leak detection is OFF. Qt and VTK keep singletons alive to exit by design,
# and LeakSanitizer reports their housekeeping at a volume that buries ours. The
# class being hunted here is memory ERRORS, which ASan and UBSan report exactly.
# See the comment on SURVIEW_SANITIZE in CMakeLists.txt.
set -euo pipefail

cd "$(dirname "$0")/.."

cmake -S . -B build-asan -G Ninja -DSURVIEW_SANITIZE=ON
cmake --build build-asan

# halt_on_error keeps the first report as the failure rather than letting a test
# run on in an already-broken state.
export ASAN_OPTIONS="detect_leaks=0:halt_on_error=1"
export UBSAN_OPTIONS="print_stacktrace=1"

ctest --test-dir build-asan --output-on-failure "$@"
