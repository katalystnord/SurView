#!/usr/bin/env python3
"""mutants.py -- break the code on purpose and see whether anything notices.

Coverage says which lines the suite EXECUTES. That is a much weaker claim than
it looks: a test can run a line, and assert nothing about what it did. This
asks the stronger question -- change the meaning of a line, and does a test go
red? A mutant that survives marks behaviour the suite watches run without
checking.

This is the C++ stand-in for PlotTracer's Stryker. It works by editing the
source, rebuilding, and running the suite, rather than by instrumenting LLVM
bitcode the way mull does. That is slower per mutant and enormously simpler:
no third-party apt repository, no coupling to a particular LLVM version, and
it reads like what it is.

Three outcomes, kept apart because collapsing them flatters the score:
  KILLED     a test failed. The suite noticed.
  SURVIVED   everything still passed. The suite did not notice. <- the finding
  NOT VIABLE the mutant would not compile. It was never a real change in
             behaviour, so it is excluded from the score rather than counted
             as a kill, which is what claiming it would do.

Usage:
  tools/mutants.py                      # SurView's core, all operators
  tools/mutants.py --limit 40 --seed 1  # a quick sample
  tools/mutants.py --engine             # the pinned OpenCorr fork instead
  tools/mutants.py --files src/core/PoiGrid.cpp
  tools/mutants.py --include-slow       # do not skip the slow tests
"""

import argparse
import json
import os
import random
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


# --- what counts as a mutation -------------------------------------------
#
# Each operator is (name, pattern, replacement). Patterns run against a MASKED
# copy of the source in which comments and string literals have been blanked
# out -- without that, a codebase commented as heavily as this one would spend
# its whole run mutating prose, and every one of those mutants would survive
# while meaning nothing at all.
OPERATORS = [
    # Relational boundaries: the classic off-by-one, and the single richest
    # source of real defects in grid and bounds arithmetic.
    ("relational", r"(?<![<>=!])<=(?!=)", "<"),
    ("relational", r"(?<![<>=!])>=(?!=)", ">"),
    ("relational", r"(?<![<>=!ei])<(?![<=])", "<="),
    ("relational", r"(?<![<>=!-])>(?![>=])", ">="),
    ("relational", r"(?<![<>=!])<(?![<=])", ">"),
    ("equality",   r"(?<![<>=!])==(?!=)", "!="),
    ("equality",   r"(?<![<>=!])!=(?!=)", "=="),

    # Arithmetic. Division is left alone as a replacement: swapping * for /
    # mostly produces divide-by-zero crashes rather than wrong answers, and a
    # crash is a kill for uninteresting reasons.
    ("arithmetic", r"(?<![+\-*/=<>!&|])\+(?![+=])", "-"),
    ("arithmetic", r"(?<![+\-*/=<>!&|])-(?![-=>])", "+"),

    # Boundary: the +1/-1 that turns an inclusive bound exclusive.
    ("boundary", r"\+ 1\b", "+ 2"),
    ("boundary", r"\+ 1\b", "+ 0"),
    ("boundary", r"- 1\b", "- 2"),
    ("boundary", r"- 1\b", "- 0"),

    # Logic.
    ("logical", r"&&", "||"),
    ("logical", r"\|\|", "&&"),

    # Constants and returns.
    ("constant", r"\btrue\b", "false"),
    ("constant", r"\bfalse\b", "true"),
]


def mask_source(text):
    """Blank comments and string literals, preserving every offset.

    Offsets must survive exactly, because mutation sites are found in the mask
    and applied to the original.
    """
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            out[i] = out[i + 1] = " "
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = out[i + 1] = " "
                i += 2
        elif c in "\"'":
            quote = c
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\":
                    out[i] = " "
                    i += 1
                if i < n and text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                i += 1
        elif c == "#":
            # Preprocessor lines: mutating an #include or a guard produces
            # noise, not a behaviour change.
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        else:
            i += 1
    return "".join(out)


def find_mutants(path):
    text = path.read_text()
    masked = mask_source(text)
    found = []
    for name, pattern, replacement in OPERATORS:
        for m in re.finditer(pattern, masked):
            start, end = m.start(), m.end()
            original = text[start:end]
            new_text = text[:start] + replacement + text[end:]
            if new_text == text:
                continue
            line = text.count("\n", 0, start) + 1
            found.append({
                "file": str(path.relative_to(REPO)) if REPO in path.parents or path.parent == REPO else str(path),
                "path": path,
                "line": line,
                "operator": name,
                "from": original,
                "to": replacement,
                "start": start,
                "end": end,
                "source": text,
                "mutated": new_text,
                "context": text[text.rfind("\n", 0, start) + 1:text.find("\n", start)].strip()[:100],
            })
    return found


def run(cmd, cwd, timeout):
    try:
        p = subprocess.run(cmd, cwd=cwd, timeout=timeout,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return p.returncode
    except subprocess.TimeoutExpired:
        # A mutant that makes the suite hang is caught, not ignored: an
        # infinite loop is a behaviour change a user would certainly notice.
        return 124


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--files", nargs="*", help="source files to mutate")
    ap.add_argument("--engine", action="store_true",
                    help="mutate the pinned OpenCorr fork instead of SurView")
    ap.add_argument("--limit", type=int, default=0, help="sample this many mutants")
    ap.add_argument("--seed", type=int, default=20260818)
    ap.add_argument("--include-slow", action="store_true",
                    help="do not exclude the slow tests")
    ap.add_argument("--timeout", type=int, default=600)
    ap.add_argument("--json", help="write the full result to this file")
    args = ap.parse_args()

    if args.engine:
        root = Path(os.environ.get("SURVIEW_OPENCORR_DIR", REPO.parent / "OpenCorr")).resolve()
        # Its own tree, beside the engine rather than inside SurView's build
        # directory: mutating the engine has nothing to do with SurView's build,
        # and burying it there made the engine's own instruments look like they
        # belonged to its consumer.
        build = root / "build-mutants"
        configure = ["cmake", "-S", str(root), "-B", str(build), "-G", "Ninja",
                     "-DOPENCORR_BUILD_SMOKE_TEST=ON"]
        # Only the sources whose behaviour the FAST smoke tests actually cover.
        # Mutating a file whose only test takes four minutes turns a run into an
        # overnight job, and mutating one with no test at all measures nothing.
        default_files = [root / "src" / n for n in
                         ("oc_shape.cpp", "oc_strain.cpp", "oc_io.cpp", "oc_dic.cpp")]
        # Correspondingly, the fast tests that cover them.
        test_filter = ["-R", "shape|strain|io_roundtrip|status_flag|region_with_holes"]
        ctest_dir = build
        ctest_cwd = root
    else:
        root = REPO
        # ⚑ Its own build directory. Sharing build-ninja means a mutant is being
        # compiled into the tree that the pre-commit hook, an editor build and
        # anything else are also using -- and during the first real run a commit
        # happened mid-mutation, with a deliberately broken source file sitting
        # in the working tree.
        build = REPO / "build-mutants"
        configure = ["cmake", "-S", str(REPO), "-B", str(build), "-G", "Ninja"]
        default_files = sorted((REPO / "src" / "core").glob("*.cpp"))
        # The walkthrough is ~3 s against ~0.03 s for the rest; excluded by
        # default so a full run finishes in a sitting. Anything it alone would
        # catch is reported as a survivor, which is honest but incomplete --
        # --include-slow is the complete answer.
        test_filter = [] if args.include_slow else ["-E", "workspace_walkthrough"]
        ctest_dir = build
        ctest_cwd = build

    files = [Path(f).resolve() for f in args.files] if args.files else default_files
    files = [f for f in files if f.exists()]
    if not files:
        print("mutants.py: no source files to mutate", file=sys.stderr)
        return 2

    # ⚑ A dirty tree is refused. This harness restores each file from a backup
    # it took itself, so an edit made while it runs is silently reverted -- and
    # if it dies between writing a mutant and restoring, an uncommitted change
    # would be indistinguishable from the damage.
    #
    # Checked against the repository being MUTATED, which is the engine in
    # --engine mode. Checking SurView's tree while editing the fork's files was
    # a guard pointed at the wrong repository.
    dirty = subprocess.run(["git", "status", "--porcelain", "--", "src"],
                           cwd=root, capture_output=True, text=True).stdout.strip()
    if dirty and not os.environ.get("MUTANTS_ALLOW_DIRTY"):
        print(f"mutants.py: {root} has uncommitted changes under src/. Commit "
              "or stash them first -- this harness edits those files and "
              "restores them, and cannot tell your changes from its own.\n",
              file=sys.stderr)
        print(dirty, file=sys.stderr)
        return 2

    print("=== Baseline: the suite must be green before anything is broken ===")
    subprocess.run(configure, check=True, stdout=subprocess.DEVNULL)
    if subprocess.run(["cmake", "--build", str(build)],
                      stdout=subprocess.DEVNULL).returncode != 0:
        print("mutants.py: the tree does not build. Fix that first.", file=sys.stderr)
        return 2
    baseline = run(["ctest", "--test-dir", str(ctest_dir)] + test_filter,
                   ctest_cwd, args.timeout)
    if baseline != 0:
        print("mutants.py: the suite is RED before mutation. A run now would "
              "score every mutant as killed by a failure that was already "
              "there.", file=sys.stderr)
        return 2
    print("Baseline green.\n")

    mutants = []
    for f in files:
        mutants.extend(find_mutants(f))
    total_found = len(mutants)

    if args.limit and args.limit < len(mutants):
        random.Random(args.seed).shuffle(mutants)
        mutants = mutants[:args.limit]

    print(f"=== {len(mutants)} mutants "
          f"({total_found} found across {len(files)} files) ===\n")

    killed, survived, not_viable = [], [], []
    started = time.time()

    for i, m in enumerate(mutants, 1):
        path = m["path"]
        backup = path.read_text()
        try:
            path.write_text(m["mutated"])
            built = subprocess.run(["cmake", "--build", str(build)],
                                   stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL).returncode
            if built != 0:
                not_viable.append(m)
                verdict = "not viable"
            else:
                rc = run(["ctest", "--test-dir", str(ctest_dir)] + test_filter,
                         ctest_cwd, args.timeout)
                if rc == 0:
                    survived.append(m)
                    verdict = "SURVIVED"
                else:
                    killed.append(m)
                    verdict = "killed"
        finally:
            path.write_text(backup)

        rate = (time.time() - started) / i
        print(f"[{i}/{len(mutants)}] {verdict:10} "
              f"{Path(m['file']).name}:{m['line']} "
              f"{m['operator']}: {m['from']!r} -> {m['to']!r}"
              f"   (~{rate:.1f}s/mutant)")

    # Rebuild clean, so the tree is left as it was found.
    subprocess.run(["cmake", "--build", str(build)], stdout=subprocess.DEVNULL)

    scored = len(killed) + len(survived)
    score = (100.0 * len(killed) / scored) if scored else 0.0

    print("\n" + "=" * 70)
    print(f"Mutation score: {score:.1f}%  ({len(killed)} killed / {scored} viable)")
    print(f"  killed     {len(killed)}")
    print(f"  SURVIVED   {len(survived)}")
    print(f"  not viable {len(not_viable)}  (did not compile; excluded from the score)")

    if survived:
        print("\n--- Survivors: behaviour the suite runs but does not check ---")
        for m in survived:
            print(f"  {m['file']}:{m['line']}  "
                  f"{m['operator']}: {m['from']!r} -> {m['to']!r}")
            print(f"      {m['context']}")

    if args.json:
        Path(args.json).write_text(json.dumps({
            "score": score,
            "killed": len(killed),
            "survived": [{k: v for k, v in m.items()
                          if k not in ("path", "source", "mutated")}
                         for m in survived],
            "not_viable": len(not_viable),
        }, indent=2))
        print(f"\nFull result: {args.json}")

    return 0


if __name__ == "__main__":
    # Line-buffered, so a run redirected to a file shows progress as it goes.
    # The first full run printed nothing for twenty minutes and looked hung.
    sys.stdout.reconfigure(line_buffering=True)
    sys.exit(main())
