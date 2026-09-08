#!/usr/bin/env bash
#
# Render the application icon's PNG from its SVG.
#
# packaging/surview.svg is the source of the mark and the file INSTALLED for the
# desktop entry. packaging/surview-256.png is what the running window uses, and
# is committed rather than generated at build time: rendering it needs inkscape,
# which is a strange thing to demand of anyone building a DIC application, and
# an icon changes about once a year.
#
# Run this after changing the SVG, and commit both.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v inkscape >/dev/null 2>&1; then
    echo "make-icon: inkscape is not installed, and it is what renders the SVG." >&2
    echo "apt install inkscape, or render packaging/surview.svg to" >&2
    echo "packaging/surview-256.png at 256 px square by any other means." >&2
    exit 3
fi

inkscape --export-type=png --export-width=256 --export-height=256 \
         --export-filename="$repo/packaging/surview-256.png" \
         "$repo/packaging/surview.svg"

echo "make-icon: wrote packaging/surview-256.png"
