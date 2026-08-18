#!/bin/sh
# coverage.sh -- what the tests actually reach, and what they do not.
#
# Coverage is a map of the UNTESTED, not a score to raise. A line the suite
# never executes cannot be protected by it, however green the run looks; that
# is the question this answers. It does NOT answer whether the lines it did
# reach are meaningfully checked -- for that, see tools/mutants.py, which breaks
# them on purpose and asks whether anything notices.
#
# Builds into its own directory: --coverage changes the object code, and sharing
# a build tree with the binaries people actually run means never being sure
# which of the two you have.
#
# Usage: tools/coverage.sh [--html]
#
# Reports on SurView's own sources only. The engine has its own suite and its
# own coverage run -- see the matching script in the OpenCorr fork.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$here/build-coverage"
out="$here/build-coverage/report"

want_html=0
[ "${1:-}" = "--html" ] && want_html=1

if ! command -v gcovr >/dev/null 2>&1; then
    echo "coverage.sh: gcovr not found. Install it with: sudo apt install gcovr" >&2
    exit 1
fi

echo "=== Building instrumented ==="
cmake -S "$here" -B "$build" -G Ninja -DSURVIEW_COVERAGE=ON >/dev/null
cmake --build "$build"

echo ""
echo "=== Running the suite ==="
# The walkthrough tests bring their own display, as everywhere else.
( cd "$build" && ctest --output-on-failure )

mkdir -p "$out"

# What is measured, and what is deliberately not:
#   --filter src/         SurView's own code, the thing under test.
#   engine excluded       It is a pinned dependency with its own suite; folding
#                         its lines in here would dilute this number into
#                         meaninglessness and hide our own gaps.
#   tests/ excluded       Measuring how thoroughly the tests run themselves
#                         flatters the total and says nothing.
echo ""
echo "=== Coverage: SurView sources ==="
gcovr \
    --root "$here" \
    --filter "$here/src/" \
    --exclude '.*/tests/.*' \
    --exclude '.*_autogen/.*' \
    --print-summary \
    --sort=uncovered-percent \
    --txt "$out/coverage.txt" \
    --json-summary "$out/coverage.json" \
    "$build"

if [ "$want_html" -eq 1 ]; then
    gcovr --root "$here" --filter "$here/src/" \
        --exclude '.*/tests/.*' --exclude '.*_autogen/.*' \
        --html-details "$out/index.html" "$build" >/dev/null
    echo ""
    echo "HTML report: $out/index.html"
fi

echo ""
echo "Per-file detail: $out/coverage.txt"
