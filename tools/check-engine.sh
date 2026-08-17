#!/bin/sh
# check-engine.sh -- the three-way check on the OpenCorr engine.
#
#   upstream/main  ->  katalystnord/OpenCorr@surview-dev  ->  SurView's pin
#
# SurView recorded no engine version at all, and that missing third link is
# what let two of our own correctness fixes (PRs #25, #26) sit missing from the
# branch SurView consumes for two weeks while every commit graph looked clean.
# This checks all three links at once, because checking any one of them alone
# is what hid the problem.
#
#   1. upstream -> fork   is everything upstream merged down into the fork?
#   2. by content         are the fixes we sent upstream actually present in
#                         the fork's source, whatever the commit graph says?
#   3. fork -> pin        is SurView's pinned commit what the fork HEAD is?
#
# Exits 0 if all three pass, 1 otherwise, so it is usable from CI. Fetches from
# the network (check 1 cannot be answered offline). The configure-time check in
# cmake/OpenCorrPin.cmake covers only check 3, offline, and is advisory.
#
# Usage: tools/check-engine.sh [path-to-OpenCorr-checkout]

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

# shellcheck source=../cmake/opencorr.pin
. "$here/cmake/opencorr.pin"

engine=${1:-${SURVIEW_OPENCORR_DIR:-$here/../OpenCorr}}

if [ ! -d "$engine/.git" ]; then
	echo "FAIL: no OpenCorr checkout at $engine"
	echo "      pass one as an argument, or set SURVIEW_OPENCORR_DIR"
	exit 1
fi

engine=$(CDPATH= cd -- "$engine" && pwd)
git="git -C $engine"

echo "engine:  $engine"
echo "pin:     $SURVIEW_OPENCORR_PIN ($SURVIEW_OPENCORR_PIN_NOTE)"
echo "branch:  $SURVIEW_OPENCORR_BRANCH"
echo

failures=0
note() { echo "  $1"; }
fail() { echo "  FAIL: $1"; failures=$((failures + 1)); }

# --- check 1: is everything upstream merged down into the fork? -------------
echo "=== 1. upstream -> fork ==="

if ! $git remote get-url upstream >/dev/null 2>&1; then
	fail "no 'upstream' remote in $engine (expected vincentjzy/OpenCorr)"
else
	$git fetch --quiet upstream 2>/dev/null || note "warning: could not fetch upstream, using last-fetched state"
	$git fetch --quiet origin 2>/dev/null || true

	behind=$($git rev-list --count "$SURVIEW_OPENCORR_BRANCH..upstream/main")
	ahead=$($git rev-list --count "upstream/main..$SURVIEW_OPENCORR_BRANCH")

	if [ "$behind" -eq 0 ]; then
		note "OK: upstream/main fully merged down ($ahead fork commit(s) on top)"
	else
		fail "$behind upstream commit(s) not merged into $SURVIEW_OPENCORR_BRANCH"
		note "     merge them with:  git -C $engine merge upstream/main -X renormalize"
		note "     (-X renormalize is required -- the fork is LF, upstream is CRLF,"
		note "      so without it every file conflicts end-to-end on line endings)"
		$git log --oneline "$SURVIEW_OPENCORR_BRANCH..upstream/main" | sed 's/^/       /'
	fi
fi
echo

# --- check 2: are our upstreamed fixes present, by content? -----------------
# The commit graph cannot answer this. #25 and #26 were authored on standalone
# branches off upstream and merged THERE, so their commits are not ancestors of
# the fork branch even when the code is present -- and were absent even though
# nothing looked wrong. Each probe counts a pattern in the fork and in upstream:
# the fork must have at least as many. Comparing against upstream rather than a
# hardcoded number means these do not need updating as either side grows.
echo "=== 2. our upstreamed fixes, by content ==="

probe() {
	label=$1
	pattern=$2
	shift 2

	fork_n=$($git grep -c -e "$pattern" "$SURVIEW_OPENCORR_BRANCH" -- "$@" 2>/dev/null |
		awk -F: '{s += $NF} END {print s + 0}')
	up_n=$($git grep -c -e "$pattern" upstream/main -- "$@" 2>/dev/null |
		awk -F: '{s += $NF} END {print s + 0}')

	if [ "$up_n" -eq 0 ]; then
		note "?? $label: pattern not found upstream either -- probe may be stale"
	elif [ "$fork_n" -ge "$up_n" ]; then
		note "OK: $label (fork $fork_n / upstream $up_n)"
	else
		fail "$label MISSING from fork (fork $fork_n / upstream $up_n)"
	fi
}

probe "#24 <random> include"        '#include <random>'                    'src/oc_feature_affine.cpp'
probe "#25 partial-OOB subset guard" 'eg_mat.array() < 0.f'                'src/oc_icgn.cpp'
probe "#25 partial-OOB guard (3D)"   'tar_subset_out_of_range'             'src/oc_icgn.cpp'
probe "#26 num_threads pinning"      'num_threads(thread_number)'          'src/*.cpp'
probe "#27 FeatureAffine3D wz"       'deformation.wz = affine_matrix(2, 2)' 'src/oc_feature_affine.cpp'
echo

# --- check 3: is the fork HEAD what SurView pinned? -------------------------
echo "=== 3. fork -> SurView's pin ==="

head=$($git rev-parse "$SURVIEW_OPENCORR_BRANCH")
if [ "$head" = "$SURVIEW_OPENCORR_PIN" ]; then
	note "OK: $SURVIEW_OPENCORR_BRANCH is at the pinned commit"
elif ! $git cat-file -e "$SURVIEW_OPENCORR_PIN^{commit}" 2>/dev/null; then
	fail "pinned commit $SURVIEW_OPENCORR_PIN is not in this checkout at all"
	note "     wrong repository, or it needs a fetch"
else
	pin_behind=$($git rev-list --count "$SURVIEW_OPENCORR_PIN..$head")
	pin_ahead=$($git rev-list --count "$head..$SURVIEW_OPENCORR_PIN")
	fail "$SURVIEW_OPENCORR_BRANCH is at $(echo "$head" | cut -c1-7), pin is $(echo "$SURVIEW_OPENCORR_PIN" | cut -c1-7)"
	note "     branch is $pin_behind commit(s) ahead of the pin, missing $pin_ahead of its commit(s)"
	note "     if the engine moved on purpose, bump cmake/opencorr.pin as its own commit"
fi
echo

if [ "$failures" -eq 0 ]; then
	echo "ALL THREE LINKS OK"
	exit 0
fi

echo "$failures CHECK(S) FAILED"
exit 1
