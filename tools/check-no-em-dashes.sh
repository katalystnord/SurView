#!/bin/sh
# check-no-em-dashes.sh -- refuse em-dashes anywhere in the tree.
#
# David, 2026-08-18: no em-dashes in this code base, ever.
#
# This is a check rather than a note because a rule that depends on remembering
# is a rule that lapses, and an em-dash is invisible in review: it looks almost
# exactly like the hyphen it should have been, in a diff, in a terminal, and in
# a code font. The 179 that had to be swept out of this repository all arrived
# one at a time without anybody noticing.
#
# The substitute depends on where it sits:
#   in a C++ comment      --   (matches the engine fork's existing convention)
#   in a user-facing string  -   ("--" in text a user reads looks like a typo)
#   in Markdown           -
#
# Usage: tools/check-no-em-dashes.sh [paths...]
#        with no arguments, checks the whole working tree.
#
# Exits 0 if clean, 1 if any are found.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

# U+2014, written as an escape so this script contains none of what it forbids
# and cannot fail against itself.
em=$(printf '\342\200\224')

if [ "$#" -gt 0 ]; then
    files=$*
else
    # Tracked files only: build trees and third-party checkouts are not ours.
    files=$(git -C "$here" ls-files)
fi

found=0
for f in $files; do
    [ -f "$here/$f" ] || [ -f "$f" ] || continue
    path="$f"
    [ -f "$path" ] || path="$here/$f"
    # Skip anything that is not text.
    case "$(file -b --mime-type "$path" 2>/dev/null)" in
        text/*|application/json|application/javascript) ;;
        *) continue ;;
    esac
    if grep -n "$em" "$path" >/dev/null 2>&1; then
        grep -n "$em" "$path" | sed "s|^|$f:|"
        found=$((found + 1))
    fi
done

if [ "$found" -ne 0 ]; then
    echo ""
    echo "Em-dashes found in $found file(s). Replace them:"
    echo "  C++ comment          ->  --"
    echo "  user-facing string   ->  -"
    echo "  Markdown             ->  -"
    exit 1
fi

exit 0
