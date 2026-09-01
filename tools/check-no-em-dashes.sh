#!/bin/sh
# check-no-em-dashes.sh -- refuse non-ASCII dashes anywhere in the tree.
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

# The whole family of non-ASCII dashes, not only the em-dash. Written as escapes
# so this script contains none of what it forbids and cannot fail against itself.
#
#   U+2013 en-dash      U+2014 em-dash      U+2212 minus sign
#   U+2010 hyphen       U+2011 non-breaking hyphen
#
# Widened after the em-dash sweep, because "Newton<en-dash>Raphson" survived it
# untouched in a user-facing string: forbidding one character of a family that
# all look identical in a code font leaves the rest to arrive in its place.
dashes=$(printf '\342\200\220\|\342\200\221\|\342\200\223\|\342\200\224\|\342\210\222')

if [ "$#" -gt 0 ]; then
    files=$*
else
    # Tracked files, plus files that are new but not ignored.
    #
    # ⚑ Tracked-only silently skipped anything not yet added, so running this by
    # hand on a file just written reported "clean" about a file it never opened.
    # The pre-commit hook was never fooled, because staging makes a file
    # tracked - but a check whose manual run does not mean what a reader thinks
    # it means is worse than no manual run at all. Found when a new page for the
    # website passed with an em-dash in its title.
    files=$(git -C "$here" ls-files --cached --others --exclude-standard)
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
    if grep -n "$dashes" "$path" >/dev/null 2>&1; then
        grep -n "$dashes" "$path" | sed "s|^|$f:|"
        found=$((found + 1))
    fi
done

if [ "$found" -ne 0 ]; then
    echo ""
    echo "Non-ASCII dashes found in $found file(s). Replace them:"
    echo "  C++ comment          ->  --"
    echo "  user-facing string   ->  -"
    echo "  Markdown             ->  -"
    exit 1
fi

exit 0
