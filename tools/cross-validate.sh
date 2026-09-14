#!/usr/bin/env bash
#
# Measure SurView against a benchmark somebody else made.
#
# WHY THIS EXISTS. Every other accuracy check in this project measures our own
# examples against the answer our own generator states. That verifies the
# implementation and says nothing about whether what we intended agrees with the
# field. This runs the same solver over the iDICs/SEM DIC Challenge 1.0 rigid
# shift set, whose images, generator and prescribed answer are all external.
#
# ⚑ THE DATA IS NOT IN THIS REPOSITORY AND MUST NOT BE. The challenge sets state
# no licence or terms of use: they may be used, and whether they may be
# redistributed is unestablished. So this unpacks them from wherever they were
# downloaded into a scratch directory, and the test skips when they are absent.
# A fresh clone and CI run the reading cases and skip the measurement, which is
# why the run says "skipped" rather than quietly saying nothing at all.
#
# Usage:
#   tools/cross-validate.sh                 # ~/code/dic-datasets, build-release
#   SURVIEW_DIC_DATASETS=/path tools/cross-validate.sh
#
# Get the data with dic-datasets/fetch.sh, which pulls it from idics.org.

set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
datasets="${SURVIEW_DIC_DATASETS:-$HOME/code/dic-datasets}"
zips="$datasets/2d-dic-challenge-1.0"
build="${SURVIEW_BUILD_DIR:-$here/build-release}"
work="${TMPDIR:-/tmp}/surview-cross-validate"

if [ ! -d "$zips" ]; then
    echo "cross-validate: no challenge data at $zips" >&2
    echo "Fetch it with dic-datasets/fetch.sh, or point SURVIEW_DIC_DATASETS at it." >&2
    exit 2
fi

# ⚑ A RELEASE BUILD, ALWAYS. The debug build is around forty times slower on a
# real correlation, and this runs eleven of them.
if [ ! -x "$build/tests/test_cross_validation" ]; then
    echo "cross-validate: no test binary at $build/tests/test_cross_validation" >&2
    echo "Configure a Release build there first, then: ninja -C $build test_cross_validation" >&2
    exit 2
fi

mkdir -p "$work"
if [ ! -f "$work/s3/Sample3 Reference.tif" ]; then
    echo "Unpacking Sample3 into $work/s3"
    unzip -o -q "$zips/Sample3.zip" -d "$work/s3"
fi

echo "Measuring against Sample 3 (rigid shifts of 0.00 to 1.00 px, stated per file)."
SURVIEW_DIC_CHALLENGE_SAMPLE3="$work/s3" \
QT_QPA_PLATFORM=offscreen \
    "$build/tests/test_cross_validation"
