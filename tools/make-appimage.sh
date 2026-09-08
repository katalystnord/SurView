#!/usr/bin/env bash
#
# Build SurView as a single-file AppImage.
#
# WHY THIS EXISTS. Every other DIC tool a working scientist might reach for
# ships something they can download and run. We shipped a build: clone two
# repositories, install Qt, VTK, OpenCV, FFTW and Eigen, and configure CMake.
# That is a reasonable thing to ask of a contributor and an unreasonable thing
# to ask of the colleague they want to hand the tool to, which is the person
# this project exists for.
#
# ⚑ AN APPIMAGE BUNDLES ITS LIBRARIES, WHICH IS THE WHOLE POINT AND ALSO THE
# WHOLE COST. SurView links VTK 9.5 and OpenCV 4.10; a .deb could name them as
# dependencies and would then install only on distributions carrying those exact
# major versions, which is a small and shrinking set. The AppImage carries them,
# so it runs on a machine that has never heard of VTK. The price is a large
# file and a bundle that has to be rebuilt to pick up a security fix in any
# library inside it - both accepted deliberately, and stated in the release
# notes rather than left for someone to discover.
#
# ⚑ PROVENANCE TRAVELS WITH THE BINARY. SURVIEW_OPENCORR_PIN is compiled in and
# every exported .vtu states it, so a package built against a checkout that is
# not the pin would produce files claiming an engine that never measured them.
# This script reports the pin and the engine's actual HEAD, and refuses to
# package when they differ unless it is told to go ahead anyway.
#
# Usage:
#   tools/make-appimage.sh [--opencorr <path>] [--allow-engine-drift]
#   tools/make-appimage.sh --stage-only   # build and stage AppDir, package nothing
#
# Output: dist/SurView-<version>-x86_64.AppImage
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$repo/build-appimage"
appdir="$build/AppDir"
dist="$repo/dist"

opencorr="$repo/../OpenCorr"
allow_drift=0
# Everything except the final packaging step. It exists so the half of this
# script that can go wrong quietly -- what ends up in the staged tree, and
# whether the application can find its own examples from there -- can be run and
# checked on a machine without the AppImage tooling installed.
stage_only=0
while [ $# -gt 0 ]; do
    case "$1" in
        --opencorr) opencorr="$2"; shift 2 ;;
        --allow-engine-drift) allow_drift=1; shift ;;
        --stage-only) stage_only=1; shift ;;
        -h|--help) sed -n '3,30p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

# --- the tools this needs, named outright ----------------------------------
# Named rather than worked around: an AppImage built by some other route is not
# the artefact this script claims to produce, and a silent substitution is how a
# release ends up differing from what its instructions describe.
missing=()
[ "$stage_only" -eq 1 ] || command -v linuxdeploy >/dev/null 2>&1 || missing+=("linuxdeploy (https://github.com/linuxdeploy/linuxdeploy/releases)")
[ "$stage_only" -eq 1 ] || command -v linuxdeploy-plugin-qt >/dev/null 2>&1 || missing+=("linuxdeploy-plugin-qt (https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases)")
[ "$stage_only" -eq 1 ] || command -v patchelf >/dev/null 2>&1 || missing+=("patchelf (apt install patchelf)")
if [ ${#missing[@]} -gt 0 ]; then
    echo "make-appimage: the following are needed and were not found:" >&2
    for tool in "${missing[@]}"; do echo "  - $tool" >&2; done
    echo >&2
    echo "linuxdeploy and its Qt plugin are themselves AppImages: download them," >&2
    echo "chmod +x, and put them on PATH under those names." >&2
    exit 3
fi

# --- the engine the package will claim -------------------------------------
pin="$(sed -n 's|^SURVIEW_OPENCORR_PIN=||p' "$repo/cmake/opencorr.pin")"
head="$(git -C "$opencorr" rev-parse HEAD 2>/dev/null || echo unknown)"
echo "make-appimage: engine pin  $pin"
echo "make-appimage: engine HEAD $head  ($opencorr)"
if [ "$pin" != "$head" ]; then
    if [ "$allow_drift" -eq 0 ]; then
        echo >&2
        echo "make-appimage: the engine checkout is not the commit this build is" >&2
        echo "pinned to. Every field this package exports would state a commit it" >&2
        echo "was not measured by. Check the engine out at the pin, update the pin," >&2
        echo "or pass --allow-engine-drift if you know what you are doing." >&2
        exit 4
    fi
    echo "make-appimage: WARNING packaging against an engine that is not the pin"
fi

# --- build, Release ---------------------------------------------------------
# Release rather than the default: the debug build is around forty times slower
# on a real correlation, which is the difference between a tool and a demo.
cmake -S "$repo" -B "$build" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DSURVIEW_OPENCORR_DIR="$opencorr" \
      -DSURVIEW_BUILD_TESTS=OFF
cmake --build "$build" -j"$(nproc)"

rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$build" --prefix /usr

# --- what the package must contain, checked before it is sealed -------------
# ⚑ A missing example folder is invisible until a first-time reader opens the
# menu and finds nothing there, which is exactly the person this package is for.
for required in \
    "$appdir/usr/bin/SurView" \
    "$appdir/usr/share/applications/surview.desktop" \
    "$appdir/usr/share/icons/hicolor/scalable/apps/surview.svg" \
    "$appdir/usr/share/surview/examples/synthetic/ground_truth.json" \
    "$appdir/usr/share/surview/examples/real"
do
    if [ ! -e "$required" ]; then
        echo "make-appimage: the staged tree is missing $required" >&2
        exit 5
    fi
done

if [ "$stage_only" -eq 1 ]; then
    echo
    echo "make-appimage: staged $appdir"
    echo "make-appimage: run it with $appdir/usr/bin/SurView"
    exit 0
fi

version="$(sed -n 's/.*project(SurView VERSION \([0-9.]*\).*/\1/p' "$repo/CMakeLists.txt")"
mkdir -p "$dist"

# The Qt plugin brings the platform, image-format and style plugins the binary
# loads at run time rather than links against -- including the SVG image format,
# without which the window icon silently draws nothing.
export QMAKE="${QMAKE:-/usr/lib/qt6/bin/qmake6}"
export EXTRA_QT_PLUGINS="${EXTRA_QT_PLUGINS:-svg;imageformats}"
export LDAI_OUTPUT="SurView-$version-x86_64.AppImage"
export LDAI_UPDATE_INFORMATION="${LDAI_UPDATE_INFORMATION:-}"

cd "$dist"
linuxdeploy --appdir "$appdir" \
            --desktop-file "$appdir/usr/share/applications/surview.desktop" \
            --icon-file "$appdir/usr/share/icons/hicolor/scalable/apps/surview.svg" \
            --plugin qt \
            --output appimage

echo
echo "make-appimage: wrote $dist/$LDAI_OUTPUT"
echo "make-appimage: engine $pin"
