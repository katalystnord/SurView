#!/bin/sh
# run-tests.sh -- the whole suite, in one command.
#
# This is SurView's `npm test`. It exists so that "run the tests" is a single
# thing a person or a hook can do without knowing which build directory is
# current, which tests need a display, or that the engine keeps its own suite
# somewhere else entirely.
#
# It runs BOTH halves:
#   1. SurView's own tests (unit + walkthrough), via CTest.
#   2. The pinned OpenCorr fork's smoke tests.
#
# The engine half is not optional. SurView builds the engine from a pinned
# checkout, so a green SurView suite against a broken engine is a green suite
# that tells you nothing -- and the engine is where the measurement actually
# happens.
#
# Usage: tools/run-tests.sh [-q]
#   -q  quieter: only failures and the summary.
#
# Exits 0 only if every test in both halves passes.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$here/build-ninja"
engine_build="$here/build-ninja/engine-tests"

quiet=0
[ "${1:-}" = "-q" ] && quiet=1

say() { [ "$quiet" -eq 1 ] || printf '%s\n' "$*"; }

# shellcheck source=../cmake/opencorr.pin
. "$here/cmake/opencorr.pin"
engine=${SURVIEW_OPENCORR_DIR:-$here/../OpenCorr}

# No display handling here on purpose. The walkthrough tests carry their own
# private X server, registered that way in tests/CMakeLists.txt, so `ctest` is
# headless whether it is run from here, from a hook, from CI, or by hand in the
# middle of a working session. A runner that supplied the display would make
# "headless" a property of HOW the suite was started, which is how windows end
# up on someone's desktop.

# --- 1. SurView -----------------------------------------------------------
say "=== Building SurView and its tests ==="
cmake -S "$here" -B "$build" -G Ninja >/dev/null
cmake --build "$build"

say ""
say "=== SurView test suite ==="
surview_status=0
( cd "$build" && ctest --output-on-failure ) || surview_status=$?

# --- 2. the pinned engine -------------------------------------------------
say ""
say "=== Engine (OpenCorr fork) smoke tests ==="
engine_status=0
if [ ! -d "$engine/.git" ]; then
    echo "run-tests.sh: no OpenCorr checkout at $engine -- engine suite NOT run." >&2
    engine_status=1
else
    cmake -S "$engine" -B "$engine_build" -G Ninja \
        -DOPENCORR_BUILD_SMOKE_TEST=ON >/dev/null
    cmake --build "$engine_build" >/dev/null
    # The smoke tests read example images by relative path, so they run from
    # the engine's own root.
    ( cd "$engine" && ctest --test-dir "$engine_build" --output-on-failure ) \
        || engine_status=$?
fi

# --- verdict --------------------------------------------------------------
echo ""
if [ "$surview_status" -eq 0 ] && [ "$engine_status" -eq 0 ]; then
    echo "ALL TESTS PASSED (SurView + engine)"
    exit 0
fi
[ "$surview_status" -ne 0 ] && echo "FAILED: SurView test suite"
[ "$engine_status" -ne 0 ] && echo "FAILED: engine smoke tests"
exit 1
