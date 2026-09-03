# -*- coding: utf-8 -*-
"""Builds the SurView DIC manual, in both of the forms it is published in.
 
One content source, two outputs. The web version is a set of linked pages
under docs/manual/; the PDF is a single continuous document with a title page
and a printed table of contents. They are generated from the same chapter
bodies on purpose: a manual that is edited in two places drifts in two
directions, and the whole subject of Part III is not claiming more than can be
substantiated.
 
Usage:
    python3 tools/manual/build.py          # web pages, and the print document
    python3 tools/manual/build.py --pdf    # ...and run Chrome to make the PDF
 
The PDF step needs a Chrome or Chromium on PATH. Without one it writes the
print-ready HTML and says so, rather than failing: the web manual is the part
that must never be blocked on a browser being installed.
"""
import os
import re
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, "docs", "manual") + os.sep

# The manual carries SurView's own version, not a number of its own: it
# documents one state of one application, and two independent version numbers
# would only invite the question of which one a reader is holding.
VERSION = "2026.09.0"
VERSION_DATE = "2026-09-03"

STYLE = """
:root {
  --bg: #fbfcfd;
  --surface: #ffffff;
  --text: #131a21;
  --text-dim: #55616d;
  --border: #e2e8ee;
  --accent: #1a6fb5;
  --accent-strong: #14568c;
  --accent-soft: #e8f1f9;
  --hot: #d9531e;
  --dark: #0f1720;
  --code-bg: #f2f5f8;
  --shadow: 0 1px 2px rgba(15,23,32,0.05), 0 8px 24px rgba(15,23,32,0.09);
}
:root[data-theme="dark"] {
  --bg: #10161c; --surface: #182028; --text: #eef3f7; --text-dim: #a7b4c0;
  --border: #26313b; --accent: #4aa3e0; --accent-strong: #8ccbf2;
  --accent-soft: rgba(74,163,224,0.14); --hot: #f0743c; --dark: #0b1116;
  --code-bg: #1d262f; --shadow: 0 1px 2px rgba(0,0,0,0.3), 0 8px 30px rgba(0,0,0,0.45);
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #10161c; --surface: #182028; --text: #eef3f7; --text-dim: #a7b4c0;
    --border: #26313b; --accent: #4aa3e0; --accent-strong: #8ccbf2;
    --accent-soft: rgba(74,163,224,0.14); --hot: #f0743c; --dark: #0b1116;
    --code-bg: #1d262f; --shadow: 0 1px 2px rgba(0,0,0,0.3), 0 8px 30px rgba(0,0,0,0.45);
  }
}
* { box-sizing: border-box; }
html { scroll-behavior: smooth; }
body {
  margin: 0; background: var(--bg); color: var(--text);
  font-family: Inter, -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  line-height: 1.62; -webkit-font-smoothing: antialiased;
}
a { color: var(--accent-strong); }
img { max-width: 100%; }
code, .mono { font-family: "SF Mono", Menlo, Consolas, monospace; font-size: 0.92em; }
code { background: var(--code-bg); padding: 0.1em 0.4em; border-radius: 4px; }

header.nav {
  position: sticky; top: 0; z-index: 20; background: var(--bg);
  border-bottom: 1px solid var(--border); display: flex; align-items: center;
  gap: 22px; padding: 12px 28px; flex-wrap: wrap;
}
header.nav .brand {
  display: flex; align-items: center; gap: 8px; font-weight: 800; font-size: 1.02rem;
  color: var(--text); text-decoration: none; margin-right: auto;
}
header.nav .brand svg { display: block; }
.nav-links { display: flex; gap: 18px; flex-wrap: wrap; font-size: 0.92rem; }
.nav-links a { color: var(--text-dim); text-decoration: none; font-weight: 600; }
.nav-links a:hover, .nav-links a.current { color: var(--accent-strong); }

.book-shell { display: grid; grid-template-columns: 1fr; max-width: 1180px; margin: 0 auto; }
@media (min-width: 980px) {
  .book-shell { grid-template-columns: 250px minmax(0,1fr); gap: 48px; padding: 0 28px; }
}
.book-toc {
  padding: 32px 20px 40px; font-size: 0.9rem;
}
@media (min-width: 980px) {
  .book-toc { position: sticky; top: 64px; align-self: start; max-height: calc(100vh - 90px); overflow-y: auto; padding: 32px 0 40px; }
}
.book-toc .toc-label { text-transform: uppercase; letter-spacing: 0.06em; font-size: 0.72rem; font-weight: 800; color: var(--text-dim); margin: 18px 0 8px; }
.book-toc .toc-label:first-child { margin-top: 0; }
.book-toc ol { list-style: none; margin: 0; padding: 0; }
.book-toc li { margin: 0; }
.book-toc a { display: block; padding: 5px 10px; border-radius: 6px; color: var(--text-dim); text-decoration: none; font-weight: 600; }
.book-toc a:hover { background: var(--accent-soft); color: var(--accent-strong); }
.book-toc a.current { background: var(--accent-soft); color: var(--accent-strong); }
.book-toc .part-link { color: var(--text); font-weight: 800; }
.book-toc .toc-label.current-part { color: var(--accent-strong); }

main.chapter { padding: 40px 20px 90px; max-width: 760px; }
main.chapter .kicker { color: var(--accent-strong); font-weight: 800; text-transform: uppercase; letter-spacing: 0.06em; font-size: 0.78rem; margin: 0 0 10px; }
main.chapter h1 { font-size: 2.1rem; letter-spacing: -0.01em; margin: 0 0 6px; }
main.chapter .lede { color: var(--text-dim); font-size: 1.12rem; margin: 0 0 36px; max-width: 60ch; }
main.chapter h2 { font-size: 1.42rem; margin: 46px 0 14px; letter-spacing: -0.005em; }
main.chapter h2:first-of-type { margin-top: 8px; }
main.chapter h3 { font-size: 1.1rem; margin: 30px 0 10px; }
main.chapter p { margin: 0 0 16px; max-width: 68ch; }
main.chapter ul, main.chapter ol { margin: 0 0 16px; padding-left: 22px; max-width: 66ch; }
main.chapter li { margin: 0 0 8px; }
main.chapter strong { color: var(--text); }

.callout {
  border-left: 3px solid var(--accent); background: var(--accent-soft);
  border-radius: 0 10px 10px 0; padding: 16px 20px; margin: 24px 0; max-width: 66ch;
}
.callout p:last-child { margin-bottom: 0; }
.callout .callout-label { font-weight: 800; font-size: 0.78rem; text-transform: uppercase; letter-spacing: 0.05em; color: var(--accent-strong); margin: 0 0 6px; }
.callout.warn { border-left-color: var(--hot); background: rgba(217,83,30,0.08); }
.callout.warn .callout-label { color: var(--hot); }

table.data { border-collapse: collapse; margin: 18px 0 28px; font-size: 0.92rem; width: 100%; max-width: 66ch; }
table.data th, table.data td { border: 1px solid var(--border); padding: 8px 12px; text-align: left; }
table.data th { background: var(--code-bg); font-weight: 700; }
table.data td.num, table.data th.num { text-align: right; font-family: "SF Mono", Menlo, Consolas, monospace; }

.chapter-nav { display: flex; justify-content: space-between; gap: 16px; margin-top: 56px; padding-top: 24px; border-top: 1px solid var(--border); max-width: 66ch; }
.chapter-nav a { text-decoration: none; color: var(--text); font-weight: 700; padding: 12px 16px; border: 1px solid var(--border); border-radius: 10px; background: var(--surface); box-shadow: var(--shadow); flex: 1; }
.chapter-nav a.next { text-align: right; }
.chapter-nav a small { display: block; color: var(--text-dim); font-weight: 600; font-size: 0.76rem; text-transform: uppercase; letter-spacing: 0.04em; margin-bottom: 3px; }
.chapter-nav a:hover { border-color: var(--accent); }

dl.glossary dt { font-weight: 800; margin-top: 18px; }
dl.glossary dd { margin: 4px 0 0; color: var(--text-dim); max-width: 62ch; }

.status-note { font-size: 0.9rem; color: var(--text-dim); border: 1px dashed var(--border); border-radius: 10px; padding: 14px 18px; margin: 24px 0; max-width: 66ch; }

figure.plate { margin: 26px 0 30px; max-width: 66ch; }
figure.plate img { display: block; width: 100%; border: 1px solid var(--border); border-radius: 10px; box-shadow: var(--shadow); background: var(--surface); }
figure.plate figcaption { font-size: 0.87rem; color: var(--text-dim); margin-top: 10px; line-height: 1.5; }
figure.plate figcaption b { color: var(--text); font-weight: 700; }
figure.plate.tight img { max-width: 420px; }

.version-chip { display: inline-block; font-size: 0.78rem; font-weight: 700; letter-spacing: 0.03em; color: var(--accent-strong); background: var(--accent-soft); border-radius: 999px; padding: 4px 12px; margin: 0 0 18px; }
.cite-box { border: 1px solid var(--border); background: var(--surface); border-radius: 10px; padding: 18px 20px; margin: 22px 0; max-width: 66ch; box-shadow: var(--shadow); }
.cite-box p { margin: 0 0 10px; }
.cite-box .cite-text { font-family: "SF Mono", Menlo, Consolas, monospace; font-size: 0.84rem; background: var(--code-bg); padding: 12px 14px; border-radius: 8px; line-height: 1.5; }
.pdf-link { display: inline-flex; align-items: center; gap: 8px; text-decoration: none; font-weight: 700; border: 1px solid var(--border); background: var(--surface); border-radius: 10px; padding: 11px 18px; box-shadow: var(--shadow); color: var(--text); }
.pdf-link:hover { border-color: var(--accent); }

footer { border-top: 1px solid var(--border); padding: 34px 28px 44px; text-align: center; color: var(--text-dim); font-size: 0.86rem; }
footer .footer-links { display: flex; gap: 18px; justify-content: center; flex-wrap: wrap; margin-bottom: 12px; }
footer a { color: var(--text-dim); text-decoration: none; font-weight: 600; }
footer a:hover { color: var(--accent-strong); }
"""

LOGO_SVG = """<svg width="24" height="24" viewBox="0 0 32 32" aria-hidden="true">
      <rect x="1" y="1" width="30" height="30" rx="7" fill="var(--dark)"/>
      <circle cx="9" cy="9" r="2" fill="#4aa3e0"/>
      <circle cx="16" cy="10" r="1.4" fill="#8fd3c8"/>
      <circle cx="23" cy="8" r="1.8" fill="#d9d94a"/>
      <circle cx="10" cy="17" r="1.5" fill="#2bb8a6"/>
      <circle cx="17" cy="18" r="2.2" fill="#e8b13c"/>
      <circle cx="24" cy="16" r="1.5" fill="#e8743c"/>
      <circle cx="8" cy="24" r="1.7" fill="#4aa3e0"/>
      <circle cx="16" cy="24" r="1.4" fill="#8fd3c8"/>
      <circle cx="24" cy="23" r="2" fill="#d9531e"/>
    </svg>"""

# (slug, short title for TOC, part label or None for standalone)
PARTS = [
    ("index", "Front matter", None),
    ("part-1-what-dic-measures", "Part I. What DIC measures", [
        ("the-idea", "1. Two photographs and a question"),
        ("displacement-and-strain", "2. Displacement is measured, strain is fitted"),
        ("absence-not-zero", "3. A rejected point is not a zero"),
        ("coordinate-frame", "4. One coordinate frame"),
    ]),
    ("part-2-running-a-measurement", "Part II. Running a measurement", [
        ("region-and-holes", "5. The region, and what a hole means"),
        ("sequences", "6. Sequences: order matters, reference matters"),
        ("reference-updating", "7. When the reference goes stale"),
        ("recovery", "8. Points that fail, and the pass that repairs them"),
    ]),
    ("part-3-how-much-to-trust-it", "Part III. How much to trust it", [
        ("two-questions", "9. Two questions, never one score"),
        ("reading-critically", "10. Reading a field critically"),
    ]),
    ("part-4-beyond-one-field", "Part IV. Beyond one field", [
        ("sequence-as-curve", "11. Reading a sequence as a curve"),
        ("leaving-the-application", "12. The field leaving the application"),
    ]),
    ("part-5-practice", "Part V. Practice", [
        ("speckling", "13. Speckling a specimen"),
        ("subset-and-step", "14. Choosing subset radius and grid step"),
        ("lighting-and-imaging", "15. Lighting and imaging"),
    ]),
    ("appendix", "Appendix", None),
]

def toc_html(current_slug):
    # current_slug is a bare page slug ("index", "part-1-...", "appendix").
    # There is no per-chapter scrollspy here (no JS on these pages, and no
    # other page on the site uses one) -- so "current" marks the whole PART
    # a reader is on, every chapter link in it, which is the coarsest true
    # statement the sidebar can make without one.
    out = ['<div class="book-toc"><nav aria-label="Table of contents">']
    label_cur = ' class="toc-label current-part"' if current_slug == "index" else ' class="toc-label"'
    out.append(f'<div{label_cur}>Manual</div><ol>')
    cur = ' class="current"' if current_slug == "index" else ''
    out.append(f'<li><a href="index.html"{cur}>Front matter</a></li>')
    out.append('</ol>')
    for slug, label, chapters in PARTS:
        if chapters is None:
            continue
        on_this_part = (current_slug == slug)
        label_cur = ' class="toc-label current-part"' if on_this_part else ' class="toc-label"'
        out.append(f'<div{label_cur}>{label}</div><ol>')
        for csl, ctitle in chapters:
            cur = ' class="current"' if on_this_part else ''
            out.append(f'<li><a href="{slug}.html#{csl}"{cur}>{ctitle}</a></li>')
        out.append('</ol>')
    label_cur = ' class="toc-label current-part"' if current_slug == "appendix" else ' class="toc-label"'
    out.append(f'<div{label_cur}>Reference</div><ol>')
    cur = ' class="current"' if current_slug == "appendix" else ''
    out.append(f'<li><a href="appendix.html"{cur}>Appendix</a></li>')
    out.append('</ol>')
    out.append('</nav></div>')
    return "\n".join(out)

def page(title, description, current_slug, body_html, first_title=True):
    toc = toc_html(current_slug)
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<meta name="description" content="{description}">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800&display=swap" rel="stylesheet">
<style>{STYLE}</style>
</head>
<body>

<header class="nav">
  <a class="brand" href="../index.html">
    {LOGO_SVG}
    SurView DIC
  </a>
  <nav class="nav-links">
    <a href="index.html">Manual</a>
    <a href="../index.html#get">Download</a>
    <a href="https://github.com/katalystnord/SurView">GitHub</a>
  </nav>
</header>

<div class="book-shell">
{toc}
{body_html}
</div>

<footer>
  <div class="footer-links">
    <a href="../index.html">SurView DIC</a>
    <a href="index.html">Manual contents</a>
    <a href="https://github.com/katalystnord/SurView/blob/main/ROADMAP.md">Roadmap</a>
    <a href="https://github.com/katalystnord/SurView">GitHub</a>
  </div>
  <p>The SurView DIC Manual, version {VERSION} ({VERSION_DATE}).<br>
  A Katalyst Nord project. Free and open source, LGPL-2.1-or-later.</p>
</footer>

</body>
</html>
"""

def chapter_nav(prev, nxt):
    parts = ['<div class="chapter-nav">']
    if prev:
        href, label = prev
        parts.append(f'<a class="prev" href="{href}"><small>Previous</small>{label}</a>')
    else:
        parts.append('<span></span>')
    if nxt:
        href, label = nxt
        parts.append(f'<a class="next" href="{href}"><small>Next</small>{label}</a>')
    else:
        parts.append('<span></span>')
    parts.append('</div>')
    return "\n".join(parts)

print("template ready")

# ============================================================ index.html ===
index_body = """
<main class="chapter">
  <p class="kicker">The SurView DIC Manual</p>
  <h1>Digital image correlation, measured carefully</h1>
  <p class="lede">
    What a DIC measurement actually is, what it can and cannot tell you, and
    how to run one that holds up. Written alongside SurView DIC, and grounded
    in it: every number and example here came from a real run, not an
    illustration invented for the page.
  </p>

  <p class="version-chip">Version VERSION_TOKEN &middot; VERSION_DATE_TOKEN</p>

  <p>
    <a class="pdf-link" href="surview-dic-manual.pdf">Download as PDF</a>
  </p>

  <div class="callout warn">
    <p class="callout-label">Scope: two-dimensional DIC</p>
    <p>
      This manual covers <strong>2D DIC</strong>: one camera, a flat or
      nearly flat surface, and displacement measured in the plane of the
      image. That is what SurView measures today. <strong>Stereo
      ("3D") DIC</strong>, which adds out-of-plane movement using two
      calibrated cameras, and <strong>volumetric DVC</strong>, which
      correlates through the inside of a solid from tomography data, are
      both tracked on
      <a href="https://github.com/katalystnord/SurView/blob/main/ROADMAP.md">the roadmap</a>
      and neither has a workflow in the application yet.
      <a href="part-1-what-dic-measures.html#two-dimensional-and-three">Chapter 1
      explains the difference</a>, because it decides which questions a
      measurement can answer at all.
    </p>
  </div>

  <h2>Who this is for</h2>
  <p>
    Someone technical who has never used digital image correlation, and
    someone who has used it for years and wants to know exactly what a
    number on screen is claiming. Neither is talked down to. DIC has a
    handful of ideas that are easy to state and easy to get quietly wrong,
    and this manual exists to make the wrong ways visible rather than to
    simplify the right ones away.
  </p>

  <h2>How it is organised</h2>
  <p>
    Five parts, in the order a measurement actually happens. <strong>Part
    I</strong> is the physics: what a correlation measures, why strain is a
    different kind of quantity from displacement, and the coordinate frame
    everything else sits in. <strong>Part II</strong> is running a real
    measurement: the region, the sequence, the reference, and what happens
    when a point cannot be measured. <strong>Part III</strong> is trust: the
    two numbers that say how far a result can be relied on, and how to read
    a coloured field without being misled by it. <strong>Part IV</strong> is
    what comes after one field: reading a whole test as a curve, and taking
    a result somewhere else. <strong>Part V</strong> is practice: speckling
    a specimen, choosing settings, lighting a rig.
  </p>
  <p>
    Each chapter stands alone reasonably well, but the ideas build: Part I
    is worth reading in order even by someone who already knows DIC,
    because SurView's specific commitments (never write zero for something
    unmeasured, report two numbers where one would be dishonest) run
    through everything after it.
  </p>

  <h2>What is not here yet</h2>
  <p>
    This is a manual for the 2D planar case, which is what SurView measures
    today. Stereo and out-of-plane measurement, a calibration workflow, and
    reading a quantity along a line are real, working parts of the roadmap
    but not of the application yet, so they are not written up here either
    - a chapter describing a screen that does not exist would be worse
    than no chapter. See
    <a href="https://github.com/katalystnord/SurView/blob/main/ROADMAP.md">ROADMAP.md</a>
    for where those stand. This manual will grow as they land.
  </p>

  <h2>How to cite this</h2>
  <p>
    The manual is documentation for the application rather than a separate
    work, so cite SurView itself, at the version you actually used. The
    repository carries a
    <a href="https://github.com/katalystnord/SurView/blob/main/CITATION.cff">CITATION.cff</a>
    that GitHub and reference managers read directly.
  </p>
  <div class="cite-box">
    <p class="cite-text">Sandquist, D. (2026). SurView DIC (version VERSION_TOKEN)<br>
    [Computer software]. Katalyst Nord.<br>
    https://github.com/katalystnord/SurView</p>
    <p style="margin-bottom:0; font-size:0.88rem; color:var(--text-dim);">
      State the version, because what a result means depends on it: the
      recovery pass, reference updating and the reliability figures each
      arrived in a specific version, and a reader reproducing your
      measurement needs to know which behaviour they are matching.
    </p>
  </div>

  <div class="callout">
    <p class="callout-label">Where to start</p>
    <p>
      New to DIC: begin at Chapter 1 and read Part I straight through.
      Already know DIC and want SurView's specific behaviour: start at
      Part II. Only want to know whether to trust a result you already
      have: go straight to Part III.
    </p>
  </div>

  {nav}
</main>
"""

# ================================================ part-1-what-dic-measures ===
part1_body = """
<main class="chapter">
  <p class="kicker">Part I &middot; What DIC measures</p>
  <h1 id="the-idea">1. Two photographs and a question</h1>
  <p class="lede">
    Digital image correlation answers one question: how did the surface in
    this photograph move, compared with this other one? Everything else in
    this manual is about what that question does and does not let you ask.
  </p>

  <h2>The reference and the target</h2>
  <p>
    Every measurement starts with two photographs of the same surface: a
    <strong>reference</strong>, taken before whatever you are studying
    happens, and a <strong>target</strong>, taken after. A test with several
    load steps has several targets, each compared against the reference in
    turn - Part II covers what "compared" means once there is more
    than one.
  </p>
  <p>
    The surface has to carry a random, high-contrast texture: a
    <strong>speckle pattern</strong>. This is not decoration. It is what
    makes the measurement possible at all. A patch of featureless grey has
    nothing distinguishing it from the patch next to it, so there is no way
    to say which pixel in the target corresponds to which pixel in the
    reference - the correspondence is genuinely ambiguous, not merely
    hard to compute. A patch of speckle is different every few pixels in
    every direction, so its match in the target is (with enough contrast
    and enough texture) unique. Part V returns to what makes a pattern good.
  </p>

  <h2>The subset, and the grid</h2>
  <p>
    DIC does not try to match single pixels. It lays a grid of points over
    the reference image, and around each point takes a small square patch
    of pixels called a <strong>subset</strong>. For each subset, it searches
    the target image for the patch of pixels whose pattern of light and
    dark most closely matches it - allowing that patch to have moved,
    stretched, rotated, or sheared slightly, since the surface genuinely did
    those things between the two photographs. The centre of the best match
    is where that point of the surface now is. The distance between where
    it started and where it was found is the <strong>displacement</strong>
    at that point, in pixels.
  </p>
  <p>
    Do this at every point of the grid and the result is a
    <strong>displacement field</strong>: not one number, but a value at
    every measured point, which can be drawn as a picture - usually a
    colour map, warm where the movement was large, cool where it was small.
  </p>

  <h2>How sure the match is</h2>
  <p>
    The search does not just report a best match; it reports how good that
    match was, as a <strong>correlation coefficient</strong>. A value close
    to its best possible score means the patch in the target looks almost
    identical to the patch in the reference, once the found deformation is
    accounted for. A poor score means no candidate patch looked convincingly
    like the original - the surface may have torn, gone out of focus,
    left the frame, or simply have too little texture in that subset to
    pin down at all.
  </p>
  <p>
    This number is a statement about pattern-matching confidence, not a
    statement about physical truth. A subset can match with high confidence
    while telling you nothing you actually want to know - a smooth,
    empty patch of background can correlate excellently against itself
    while genuinely reporting no useful information. Confidence in the
    match and meaningfulness of the answer are related but not the same
    question, and Part III is built entirely around keeping them separate.
  </p>

  <div class="callout">
    <p class="callout-label">In one sentence</p>
    <p>
      DIC follows small, uniquely textured patches of a photographed
      surface from a reference image to a target image, and reports how
      far each one moved and how confident it is in that answer.
    </p>
  </div>

  <h2 id="two-dimensional-and-three">Two-dimensional, stereo, and volumetric</h2>
  <p>
    "DIC" names a family of techniques rather than one, and they differ in
    what dimensions of movement they can see at all. The distinction
    matters before anything else in this manual, because it decides which
    questions a given measurement is even capable of answering.
  </p>

  <h3>Two-dimensional DIC: one camera, movement in the image plane</h3>
  <p>
    One camera looks squarely at a flat, or nearly flat, surface, and
    measures how that surface moves <em>parallel to the image plane</em>:
    two components of displacement, in-plane, at every point. This is what
    everything else in this manual describes, and what SurView measures
    today.
  </p>
  <p>
    Its limitation is exactly its definition: it cannot see movement
    <em>toward or away from</em> the camera. Worse, it cannot tell such
    movement apart from real in-plane strain. A specimen that bows
    slightly out of plane appears, to a single camera, to have stretched
    - because the part of it that moved closer genuinely does image
    larger. That is not an implementation flaw to be fixed; it is
    information a single viewpoint does not contain. It is why a 2D
    measurement wants a specimen that stays flat, and why out-of-plane
    motion belongs on the list of things a noise floor cannot warn you
    about (Chapter 9).
  </p>

  <h3>Stereo DIC: two cameras, movement in three directions</h3>
  <p>
    Two calibrated cameras viewing the same speckled surface from
    different angles can triangulate each point, and so measure all three
    components of its displacement, out-of-plane included. This is
    universally called "3D DIC," and the name slightly overstates it:
    what it measures is a <strong>surface</strong>, one depth per point of
    the master camera's view, carrying a fully three-dimensional
    displacement. The geometry is two-and-a-half dimensional; the
    displacement on it is genuinely 3D. It cannot see inside the
    material, only the face turned toward the cameras.
  </p>

  <h3>Volumetric DVC: correlation through the inside of a solid</h3>
  <p>
    Digital <em>volume</em> correlation applies the same idea to a
    three-dimensional image of a specimen's interior, usually from X-ray
    computed tomography: instead of subsets of pixels on a surface, it
    follows subsets of <em>voxels</em> through the volume, and reports
    displacement and strain inside the material rather than on its skin.
    Natural texture in the material's own microstructure - porosity,
    inclusions, grain contrast - takes the place of an applied speckle
    pattern, since nothing can be painted onto an interior.
  </p>

  <div class="callout warn">
    <p class="callout-label">Which one this manual covers</p>
    <p>
      <strong>Two-dimensional DIC only.</strong> That is what SurView
      measures today, and the rest of this manual is written for it
      throughout. Stereo and volumetric correlation are both real,
      tracked work on
      <a href="https://github.com/katalystnord/SurView/blob/main/ROADMAP.md">the roadmap</a>
      - the correlation engine underneath SurView already carries much of
      what each needs - but neither has a workflow in the application yet,
      and a chapter describing a screen that does not exist would be worse
      than an honest gap. Read anything here about displacement, strain,
      or reliability as in-plane unless it says otherwise.
    </p>
  </div>

  <figure class="plate">
    <img src="../assets/speckle-closeup.png" alt="A close view of a random speckle pattern: irregular black blobs of varying size scattered on a white background, with no repeating structure.">
    <figcaption>
      <b>A speckle pattern, closely cropped.</b> Random and non-periodic,
      with strong local contrast at every scale a subset might span. Every
      small window onto this pattern is different from every other one,
      which is what lets a correlation search find exactly one convincing
      match rather than several equally good ones.
    </figcaption>
  </figure>

  <h1 id="displacement-and-strain">2. Displacement is measured, strain is fitted</h1>
  <p class="lede">
    Displacement and strain are reported side by side and look like the
    same kind of number. They are not measured the same way, and treating
    them as though they were is the single most common way to misread a
    DIC result.
  </p>

  <h2>Displacement exists at a point</h2>
  <p>
    A correlation finds where one subset went. That answer belongs entirely
    to that one point: ask "how far did this point move" and the field has
    a direct answer, measured by comparing that point's own neighbourhood
    in the reference against the target.
  </p>

  <h2>Strain does not</h2>
  <p>
    Strain is a measure of local stretching or shearing - how much a
    small patch of material deformed relative to its own neighbours, not
    how far it travelled. That is a <em>gradient</em> of displacement, and a
    gradient has no meaning at a single point on its own: it is a
    comparison between a point and the points around it. So strain is not
    measured directly at all. It is <strong>fitted</strong>: a plane (or a
    more general surface) is fitted through the displacements of every
    measured point inside a small neighbourhood, and the slope of that fit
    is the strain reported at the centre.
  </p>
  <p>
    This has real consequences, and each of them is easy to miss unless a
    tool goes out of its way to surface it.
  </p>

  <h3>A declined fit is not a strain of zero</h3>
  <p>
    A fit can fail to find enough well-correlated neighbours to fit through
    at all. When that happens, the honest answer is "not established here",
    and a tool that quietly reports zero in that case is lying in the most
    dangerous possible direction: zero looks like a real, specific,
    reassuring answer - "this region did not strain" - rather
    than "this region was never evaluated". SurView carries the distinction
    explicitly, as a flag alongside every strain value, and a strain map
    with holes in it means exactly what it looks like: places nothing was
    established, not places nothing happened.
  </p>

  <h3>Neighbourhoods can be silently substituted</h3>
  <p>
    A fit needs a minimum number of neighbouring points inside its
    subregion. If a subregion does not contain enough of them - near
    an edge of the measured area, or near a hole, or wherever the
    displacement field itself is sparse - some engines will quietly
    reach further out and borrow the nearest points available instead of
    refusing, and the result looks exactly as complete as a properly
    supported fit. The honest response is to say, live, how many points a
    given subregion setting will actually gather before a run is even
    started, so a setting that would trigger this silently is visible
    before it is used rather than discovered afterwards.
  </p>

  <h3>Only well-correlated points get to vote</h3>
  <p>
    A point that correlated poorly is not trustworthy evidence about its
    own displacement, and a strain fit that used it anyway would be
    building a gradient on top of noise. DIC engines typically exclude
    points below a correlation threshold (0.9 is a common default) from
    every fit they could otherwise contribute to. A consequence worth
    expecting rather than being alarmed by: a strain map is often visibly
    sparser than the displacement map sitting right next to it. That is
    the fit being conservative, not a fault.
  </p>

  <h3>The trap that is easiest to fall into: extrapolation dressed as measurement</h3>
  <p>
    Here is the one worth reading twice. A strain fit is centred on a
    point and built from that point's <em>neighbours</em>. Nothing stops
    the fit from succeeding at a centre point whose own displacement was
    <em>rejected</em>: the centre simply excludes itself from its own
    regression and still receives a strain value extrapolated entirely from
    points around it. That value describes a place the instrument never
    actually got a reading from, and on the screen it is completely
    indistinguishable from a value measured where the instrument really
    did succeed.
  </p>
  <p>
    This was found, not anticipated: a run reported <strong>"strain fitted
    at 1092 of the 1025 solved points"</strong> - more strain values
    than there were successful displacement measurements to fit them from,
    which is only possible if some of those strain values were sitting on
    points the instrument had already given up on. The fix is a rule that
    is nowhere in the correlation engine itself and has to be added on
    top: <strong>strain is reported only at a point whose own displacement
    was measured.</strong> A rejected centre gets no strain value, however
    good its neighbours look, because a value there would be an
    extrapolation wearing the same colour as a measurement.
  </p>

  <div class="callout warn">
    <p class="callout-label">What to check, every time</p>
    <p>
      Before reading a strain map as an answer, check: are unmeasured
      regions shown as gaps, or could they be reading as zero? Does the
      strain map extend anywhere the displacement map itself has holes? If
      a tool cannot answer either question, treat every value near a
      boundary or a hole with real suspicion.
    </p>
  </div>

  <h2>Two display choices that follow from this</h2>
  <p>
    Because strain and displacement are different kinds of quantity, they
    should not share a colour convention. A strain field's scale is
    centred on zero, because zero strain is a real physical state (nothing
    stretched) and the sign either side of it is meaningful (tension
    against compression). A displacement field's scale is not centred,
    because a displacement of zero is only "wherever the reference frame
    happens to sit" - a specimen that simply translated across the
    frame would spend half its colour range on values that cannot occur if
    the scale were forced to straddle zero.
  </p>

  <figure class="plate">
    <img src="../assets/strain-rotation.png" alt="A strain field over a speckled specimen that was rotated rather than deformed. The map is near-uniform on a diverging scale centred on zero, running from -0.000457 to 0.000116.">
    <figcaption>
      <b>A rotation is not a strain.</b> This specimen was turned, not
      stretched, and the correct answer is zero strain everywhere. The
      fitted field reads -0.000457 to 0.000116 across the whole specimen:
      zero, to within the method's own noise. A shape function that
      mistook rotation for strain would read around 0.017 here, the sine
      of the angle, and it would look entirely plausible on a colour map.
    </figcaption>
  </figure>

  <h1 id="absence-not-zero">3. A rejected point is not a zero</h1>
  <p class="lede">
    One principle, showing up in every part of a DIC result: a place the
    instrument did not measure, and a place the instrument measured a
    value of zero, must never look the same. This chapter states the
    principle once so the rest of the manual does not have to keep
    re-arguing it.
  </p>

  <h2>Why zero is the dangerous default</h2>
  <p>
    Software defaults numeric fields to zero constantly, and it is usually
    harmless. In a measurement field it is not, because zero is never a
    neutral placeholder here - it is always <em>also</em> a
    legitimate, meaningful answer. Zero displacement means "this point did
    not move." Zero strain means "this material did not deform." A rejected
    point defaulting to zero is therefore not a blank; it is a false and
    entirely plausible-looking claim, and it looks exactly as confident as
    a real one.
  </p>
  <p>
    The fix is the same everywhere it applies: use a value that cannot be
    mistaken for a measurement - not-a-number, a missing entry, a
    hole in a colour map - and carry a separate flag saying whether a
    value was actually established. "Nothing here" and "zero here" have to
    stay two different, unconfusable states, all the way from the engine
    to the screen to whatever file the result is exported into.
  </p>

  <h2>Where this shows up</h2>
  <ul>
    <li>
      <strong>A rejected point's displacement.</strong> The correlation
      failed, or the subset moved out of frame, or the match was too poor
      to trust. The point is marked as not converged, and reports no
      displacement value at all - not a displacement of zero.
    </li>
    <li>
      <strong>A declined strain fit</strong> (Chapter 2): the same rule,
      one derived quantity further out.
    </li>
    <li>
      <strong>An unestablished reliability figure</strong> (Part III):
      a noise floor or match-conditioning value that could not be
      computed is absent, never zero - zero would be the single
      most flattering, and therefore most misleading, reading either
      metric could give.
    </li>
    <li>
      <strong>A frame a sequence could not read</strong> (Part IV): missing
      from a curve as a visible break, never plotted as though the
      quantity had returned to zero.
    </li>
    <li>
      <strong>An exported field</strong> (Chapter 12): the same absence has
      to survive leaving the application. A viewer that assumes every cell
      holds a number, and a format that cannot represent "not established",
      quietly turn every one of the cases above back into a lie the moment
      the file is opened somewhere else.
    </li>
  </ul>

  <div class="callout">
    <p class="callout-label">A reading habit worth having</p>
    <p>
      When a field looks unusually smooth or unusually complete, ask what
      would have happened to a point the instrument could not measure. If
      the honest answer is "it would look identical to a real zero," the
      field is not to be trusted at face value, whichever tool produced it.
    </p>
  </div>

  <h1 id="coordinate-frame">4. One coordinate frame</h1>
  <p class="lede">
    Every number in a DIC result is a position or a displacement, and every
    position or displacement is meaningless without knowing which way the
    axes point. This is duller than the rest of Part I and just as capable
    of quietly ruining a result.
  </p>

  <h2>Image coordinates, not maths coordinates</h2>
  <p>
    A photograph is stored as rows of pixels, and the row order is a
    convention, not a law: some formats store the top row of the image
    first, others store the bottom row first. Screen and image coordinate
    systems follow the file: x increases to the right, and
    <strong>y increases downward</strong>, with the origin at the top-left
    pixel. That is the opposite of the y-axis convention used in ordinary
    graphing, where y increases upward - and a rendering system built
    for graphs (as most 3D and plotting toolkits are) will get this
    backwards unless it is deliberately told not to.
  </p>

  <h2>Why this has to be nailed down explicitly, once</h2>
  <p>
    Different image formats do not even agree with each other. Some readers
    honour a file's own stated orientation; others always deliver rows in
    one fixed order regardless of what the file says. Left alone, this
    produces a genuinely alarming failure mode: some image formats display
    correctly and others display <strong>vertically mirrored</strong>,
    silently, depending only on which file format happened to be used for
    that particular photograph - with nothing about the mirrored
    image looking obviously wrong at a glance, since a flipped photograph
    of a speckle pattern still looks like a perfectly plausible speckle
    pattern.
  </p>
  <p>
    The only reliable fix is to normalise every image, on the way in, to
    one stated row order, and to make every other coordinate in the system
    (a click on screen, a region's corners, a measured point, a rendered
    field) agree with that same order and with nothing else. Once that is
    true, a click on the picture, a point in the result, and a pixel in
    the exported file are all talking about the same place without any
    conversion between them anywhere in the pipeline - which is also
    what makes it safe to overlay a result directly on top of its own
    reference photograph and trust that they line up.
  </p>

  <h2>Why an exported file has to say so explicitly</h2>
  <p>
    A result rarely stays inside the tool that produced it. It gets opened
    in a general-purpose viewer that has its own, entirely reasonable,
    default assumption about which way is up - usually the graphing
    convention, not the image convention. If the file does not state its
    own coordinate frame, a viewer that assumes the wrong one draws the
    field <strong>mirrored</strong>, and there is no way to tell from
    looking, because a mirrored field of a real specimen still looks like
    a plausible field of some specimen. The one thing that catches it is
    laying the exported field directly back over the original photograph:
    a field that lines up is right, and a field that lines up perfectly
    except flipped is a coordinate-frame bug wearing the appearance of a
    correct result.
  </p>

  {nav}
</main>
"""

# ======================================== part-2-running-a-measurement ===
part2_body = """
<main class="chapter">
  <p class="kicker">Part II &middot; Running a measurement</p>
  <h1 id="region-and-holes">5. The region, and what a hole means</h1>
  <p class="lede">
    Most of the time a correlation does not need to measure the whole
    photograph - the specimen fills only part of the frame, and the
    background around it correlates against itself perfectly well while
    telling you nothing you want to know. Restricting where points are
    placed is not just an optimisation; it changes what the result can
    honestly claim.
  </p>

  <h2>Drawn or detected, the region is honest about which</h2>
  <p>
    A region of interest can be drawn by hand, corner by corner, or
    proposed automatically by segmenting the image for wherever the
    speckle is strong enough to correlate well. The two are not the same
    kind of claim: a hand-drawn boundary is a person's judgement about
    where the specimen is, and a detected one is an algorithm's judgement
    about where the pattern is measurable, which are related but different
    questions. A result should carry which kind of boundary produced it,
    because a reader deciding how much to trust the edge of a field needs
    to know which question was actually asked there.
  </p>

  <h2>What a region actually restricts</h2>
  <p>
    A region decides where the <em>centres</em> of measurement points are
    allowed to sit. It does not shrink the subset each point correlates
    over: a point near the edge of a region still looks at a full subset
    of pixels around it, some of which lie outside the region. That is
    unavoidable - a correlation needs real texture to work with, and
    clipping the subset at the boundary would only starve it - but
    it means a boundary is a little softer than it looks: points close to
    the edge are influenced by whatever sits just outside it.
  </p>

  <h2>A hole is not decoration</h2>
  <p>
    Many real specimens are not solid: a dogbone tension coupon drilled
    for a pin, a plate with a hole for a fastener, a bracket with slots cut
    into it. Where the material is genuinely absent, the camera sees
    whatever is behind the specimen instead - background, or a
    fixture, or empty space - and that background does
    <strong>not</strong> move the way the specimen does. A measurement
    point placed there correlates the background against itself
    perfectly well and reports, with complete confidence, that nothing
    moved.
  </p>
  <p>
    That is the worst possible place for a false reading to appear. A
    hole through a loaded part is exactly where stress concentrates, which
    is exactly where a strain map is most worth looking at closely -
    and a cold, confident, entirely wrong "no strain here" sitting right
    at that concentration is precisely the kind of error nobody thinks to
    double-check, because it looks like an answer rather than like a
    mistake.
  </p>
  <p>
    A region that can describe a hole - an outer boundary with a
    place cut out of it, excluded from measurement - fixes this by
    simply not placing points there at all. What is drawn is then a
    positive statement about the specimen's actual shape, not merely a
    rough outer boundary hoping nobody looks too closely at what is
    inside it.
  </p>

  <figure class="plate">
    <img src="../assets/region.png" alt="Region drawing mode: a bounded quadrilateral drawn over a speckled specimen, with an on-screen bar reading the number of corners placed, the pointer position in image pixels, and buttons to undo, close, or cancel.">
    <figcaption>
      <b>Drawing a region.</b> Corners are placed by clicking the image,
      and the mode states what it is doing, how many corners exist so far,
      and every way to finish or abandon it. A hole is added the same way,
      as a second ring cut out of the region already in force.
    </figcaption>
  </figure>

  <h2>A subset can still reach into a hole it does not sit inside</h2>
  <p>
    The same softness that applies at the outer boundary applies at a
    hole's edge too, and for the same reason: a subset extends beyond the
    single point it is centred on. A point just outside a hole can still
    have a subset that reaches partway into it, picking up some pixels
    that are genuinely background and do not move with the specimen. That
    pulls its answer very slightly toward "did not move," in the same
    quiet, plausible-looking direction as a point placed fully inside the
    hole would be, just to a lesser degree.
  </p>
  <p>
    The honest response is not to pretend this cannot happen by excluding
    every point anywhere near a hole - that would apply a stricter
    rule at a hole's edge than the same result already accepts at the
    outer boundary, for no principled reason. It is to <strong>count and
    report</strong> how many measured points have a subset reaching into a
    hole, so a reader can widen the exclusion or simply read those points
    knowing what they are.
  </p>

  <h1 id="sequences">6. Sequences: order matters, reference matters</h1>
  <p class="lede">
    A DIC test is almost never one photograph compared against another. It
    is a loading series: a reference, and a run of targets taken as
    whatever is being studied happens over time. Two decisions that look
    like bookkeeping turn out to be about correctness.
  </p>

  <h2>Frame order is a correctness problem, not a presentation one</h2>
  <p>
    Sort a list of filenames the way a computer sorts text, and
    <code>frame_10</code> comes before <code>frame_2</code>, because
    <code>"1"</code> is a smaller character than <code>"2"</code> and the
    rest of the comparison never looks at the full number. A twelve-frame
    test sorted that way gets measured in the order 1, 10, 11, 12, 2, 3,
    and so on - and every individual frame still solves correctly,
    because the correlation itself has no idea what order it was asked to
    run in.
  </p>
  <p>
    The result is a field that is internally perfect and, taken as a
    series, complete nonsense: a specimen that appears to jump backward
    and forward instead of loading smoothly. Nothing about any one frame
    reveals the problem, because nothing is wrong with any one frame. The
    only fix is to sort frames the way a person reads them - as
    numbers, not as text - before anything is measured, so what is
    listed as the sequence is guaranteed to be what gets measured as the
    sequence.
  </p>
  <p>
    The same trap appears again on the way out. A results file numbered
    without leading zeros sorts the same wrong way in most viewers, so an
    exported frame 10 is grouped before frame 2 when a series is played
    back as an animation. Files need to be numbered so their names sort
    the same way their content is meant to play.
  </p>

  <h2>Every frame against the original reference, by default</h2>
  <p>
    The straightforward, and usual, meaning of a DIC sequence is that
    every target is compared against the <em>same</em> reference frame,
    not against the frame immediately before it. That keeps every
    reported displacement directly comparable across the whole test: frame
    5's displacement and frame 9's displacement both mean "how far has
    this point moved from where it started," and can be laid over one
    another or subtracted from one another meaningfully.
  </p>
  <p>
    The cost is that correlation quality degrades as the specimen deforms
    further from how it looked in that original photograph - the
    later the frame, the less it resembles the reference, and eventually
    subsets that correlated beautifully at the start start failing. The
    next chapter is about what to do when that happens.
  </p>

  <h1 id="reference-updating">7. When the reference goes stale</h1>
  <p class="lede">
    A reference that no longer resembles the current state of the
    specimen makes correlation fail, not through any flaw in the method,
    but because the two images genuinely no longer look alike enough to
    match. Re-anchoring is the standard answer, and it trades one kind of
    correctness for another in a way worth understanding before switching
    it on.
  </p>

  <h2>What re-anchoring does</h2>
  <p>
    Once too little of the field still correlates well against the
    original reference, later frames are measured against the
    <em>current</em> frame instead of the original one. Each point's
    displacement since the original reference is banked - the new
    increment is added to whatever was already accumulated - so what
    is <strong>reported</strong> stays relative to the original reference
    throughout, even though what was actually <strong>measured</strong>
    on any individual frame after the switch was relative to a more
    recent one.
  </p>
  <p>
    This recovers correlation on a specimen that has deformed too far to
    compare directly against its starting photograph. It is not free: it
    abandons any point that could not be measured on the exact frame the
    re-anchor happens on, because there is no other way to know where
    that point went. That trade is explained wherever the setting is
    switched on, not buried in a default.
  </p>

  <h2>The rule that decides when to re-anchor has to count losses, not just successes</h2>
  <p>
    The obvious way to decide "has the reference gone stale" is to ask
    what share of points still correlate well. The obvious way to build
    that is to count the points that succeeded. That is backwards, and
    the reason is not obvious until you watch it fail: if only the points
    that are <em>still solving well</em> get a vote, then a field that has
    lost half its points to decorrelation but still correlates beautifully
    on the half that remains never triggers a re-anchor at all - the
    surviving half looks great and is the only half being asked.
  </p>
  <p>
    On a real synthetic tension sequence this was not a hypothetical: solved
    points fell from 97% of the field to 47% across five load steps, and a
    rule that counted only successes never re-anchored once. <strong>A
    point lost to decorrelation is the strongest evidence available that
    the reference has gone stale</strong>, and it has to count against the
    reference exactly because it dropped out, not despite it.
  </p>
  <p>
    There is one case where that rule has to bend the other way: a frame
    where <em>nothing</em> correlated must never trigger a re-anchor,
    because re-anchoring works by banking the increment just measured, and
    a frame with no increment to bank marks every point lost and ends the
    sequence's usefulness outright. A reference that is merely stale can
    recover on a later frame. A field that has lost every tracked point
    cannot.
  </p>

  <h2>The threshold is set by measurement, and moving it changes the outcome directly</h2>
  <p>
    A convenient-sounding default (borrowed from other tools' 75% share)
    turned out to be too permissive when tried against real data: at 75%,
    a tension sequence sat at 75.3% on its fourth load step - a hair
    above the line - held the reference one step too long, and
    arrived at the fifth step with less than half the field still solved.
    At 90%, the same sequence re-anchors one step earlier, and its final
    frame keeps far more of the field intact. The number is not a
    convention; it is a knob that visibly changes how much of a hard test
    survives to the end.
  </p>

  <h2>Two rules the bookkeeping has to get right, always</h2>
  <ul>
    <li>
      <strong>A frame is always reported on the original grid, relative
      to the original reference</strong> - whatever it was actually
      measured against. Otherwise the reported field drifts across the
      picture as the specimen itself does, and no two frames of the
      series, and no frame and the original photograph, can ever be laid
      over one another again.
    </li>
    <li>
      <strong>A point with no measurement on a re-anchor frame is lost,
      not frozen.</strong> Its position in the new reference is unknown
      the moment the anchor moves without it, so continuing to report its
      last known displacement forever would present a region that has
      genuinely stopped being tracked as though it had simply stopped
      moving - a measurement claim, sitting where there is none
      left to make.
    </li>
  </ul>

  <h1 id="recovery">8. Points that fail, and the pass that repairs them</h1>
  <p class="lede">
    Most points a correlation loses are not places where the surface is
    genuinely unmeasurable. They are places where the search started from
    a poor guess. A second pass that gives failed points a better starting
    guess, and re-solves them properly, recovers a substantial share of a
    field - if it is built so that it can never make anything worse.
  </p>

  <h2>Borrowing from the neighbours, then re-solving in full</h2>
  <p>
    A subset's correlation search has to start somewhere, and if that
    starting guess is far from the real answer, the search can fail to
    converge even though a perfectly good match exists nearby. A recovery
    pass fits a displacement field to the points around a failed one that
    <em>did</em> correlate well, uses that fit as a better starting guess
    at the failed point, and runs the full correlation search again from
    there.
  </p>
  <p>
    The result of the fit is never reported as a measurement on its own.
    It is only ever an initial guess, and the point is only accepted once
    it has been correlated properly from that guess - with its own,
    independently earned correlation score. Reporting the fitted value
    directly, without re-solving, would put an interpolation borrowed from
    a point's neighbours into the field wearing the same appearance as a
    real measurement, which is exactly the trap Chapter 2 describes for
    strain, one step further along the same idea.
  </p>

  <h2>The rule that makes this safe to leave switched on: it may never make anything worse</h2>
  <p>
    A recovery pass that only ever touches points that already failed can,
    at worst, do nothing. That is the entire argument for running it by
    default: an answer is accepted only if it converges <em>and</em>
    correlates better than whatever was there before - nothing, in
    the case of a point that had failed outright - so the pass can
    only add points to a field, never spoil ones that were already there.
  </p>
  <p>
    Recovery spreads in rounds: points recovered in one round become
    trustworthy neighbours for the next, so repair can work its way into
    the middle of a large failed patch that had no good neighbour at all
    on the very first attempt.
  </p>

  <h2>A recovered point is marked, everywhere it is reported</h2>
  <p>
    A recovered point is a genuine measurement - it converged, and
    it carries its own correlation score - and needs no apology on
    that account. But it reached its answer by a different route than the
    rest of the field, and that is exactly the kind of thing this manual
    has already argued should never be hidden: the mark travels with the
    point everywhere it is reported, so a reader can always ask where the
    repairs are concentrated. That question has a real answer worth
    asking - repairs clustered along one edge of a specimen usually
    mean something about the specimen; repairs scattered evenly usually
    mean something about the correlation settings.
  </p>

  <figure class="plate">
    <img src="../assets/repair-map.png" alt="A map showing which points a second pass repaired on a real dogbone specimen. Most of the field is one flat colour for points measured on the first solve, with patches of a second colour around the two holes, along the edges, and in a band across the gauge length.">
    <figcaption>
      <b>Where the repairs fell.</b> 2531 of 9044 measured points on this
      real specimen were recovered by the second pass. The map answers what
      the count cannot: they cluster around the holes and along the edges,
      which is the specimen talking, rather than scattering evenly, which
      would point at the correlation settings instead. Two flat colours and
      no gradient between them, because the channel has exactly two states
      and a ramp would imply readings that cannot occur.
    </figcaption>
  </figure>

  <h2>What it actually buys, measured</h2>
  <p>
    On real specimens the effect is large. A hard frame of a real tension
    test with two holes through the gauge section went from
    <strong>5401 points measured to 8455</strong> once the pass was
    switched on. On a run that already measured most of its field, the
    same pass costs a modest amount of extra time and changes almost
    nothing - the effort scales to how much of the field was
    actually broken.
  </p>

  <table class="data">
    <thead><tr><th>pair</th><th class="num">before</th><th class="num">after</th></tr></thead>
    <tbody>
      <tr><td>synthetic large strain, first to last frame</td><td class="num">167</td><td class="num">8099</td></tr>
      <tr><td>synthetic rotation, first to a later frame</td><td class="num">2574</td><td class="num">10170</td></tr>
      <tr><td>real tension coupon without holes</td><td class="num">5401</td><td class="num">8455</td></tr>
      <tr><td>real tension coupon with holes</td><td class="num">6513</td><td class="num">9044</td></tr>
    </tbody>
  </table>

  {nav}
</main>
"""

# =================================== part-3-how-much-to-trust-it ===
part3_body = """
<main class="chapter">
  <p class="kicker">Part III &middot; How much to trust it</p>
  <h1 id="two-questions">9. Two questions, never one score</h1>
  <p class="lede">
    "How reliable is this measurement" sounds like one question. It is
    genuinely two, they have different answers, and collapsing them into a
    single quality score throws away exactly the information that would
    let you tell which kind of problem you are looking at.
  </p>

  <h2>Question one: how well could this ever have gone?</h2>
  <p>
    A speckle pattern has more or less texture, more or less contrast,
    in any given small patch. Some patches are naturally easier to lock
    onto than others, independent of anything that happens to them
    afterward - a patch with strong, varied local contrast can be
    located far more precisely than a smoother, weaker one, purely as a
    matter of how much information is in the pattern itself.
  </p>
  <p>
    A <strong>noise floor</strong> answers exactly this: given the
    pattern's own local contrast and the camera sensor's own noise level,
    what is the finest displacement this particular subset could ever
    resolve? It is computed from the reference image alone, comparing the
    subset's gradient strength against an estimate of the sensor's noise.
  </p>
  <div class="callout warn">
    <p class="callout-label">What it cannot see</p>
    <p>
      Because it never looks at the target image at all, a noise floor
      cannot see anything that goes wrong <em>during</em> the measurement:
      decorrelation, out-of-plane motion, focus drift, or a shape function
      too poor to represent the real deformation. It is a lower bound on
      how good a result could be, never a total error bar on how good the
      result actually was.
    </p>
  </div>
  <p>
    In practice it behaves like a map of speckle quality, expressed
    directly in the units that matter: pixels of displacement. On real
    measured data it typically sits in the range of a few thousandths of
    a pixel - small enough that reading the bare number in isolation
    is nearly meaningless. Chapter 10 covers how to read it usefully.
  </p>

  <h2>Question two: how confident is this particular answer?</h2>
  <p>
    <strong>Match conditioning</strong> asks a different question: around
    the specific solution this search actually converged to, how sharply
    does the matching cost rise as the candidate position moves away from
    it? A sharp, well-defined minimum means the search landed somewhere
    unambiguous. A shallow, flat-bottomed minimum means many nearby
    positions matched almost as well, so the reported position, while a
    genuine best answer, carries more uncertainty about exactly where the
    true match sits.
  </p>
  <p>
    Unlike the noise floor, this probes the actual match that was found,
    so it does respond to problems during the measurement. It is
    dimensionless and has no absolute scale of its own: it is only
    meaningful <em>within</em> one run, comparing one point against
    another in the same field, never across two different runs or two
    different specimens.
  </p>

  <h2>Both read the opposite way from everything else</h2>
  <p>
    Every other quantity in a DIC result - displacement, strain
    - is read the ordinary way: a value simply is what it is,
    neither good nor bad on its own. Both reliability figures are
    different: for both of them, <strong>a larger number is worse</strong>,
    the way a margin of error is worse the larger it gets. Reading them
    with the wrong habit - treating a large value as an interesting
    hotspot rather than a warning - is an easy mistake to carry over
    from every other channel, and it needs to be stated plainly wherever
    these numbers appear, not left to be inferred.
  </p>

  <h2>Zero is the most dangerous value either can show</h2>
  <p>
    Neither figure can honestly reach zero. A zero noise floor would claim
    a perfectly resolvable measurement; a zero match conditioning would
    claim a perfectly sharp, unambiguous cost minimum. Neither is
    physically reachable. So a value that is not strictly positive was
    never actually established - it is the same "absence, not zero"
    principle from Chapter 3, and it applies with particular force here,
    because zero looks like the <em>best</em> possible reading a reader
    could hope for, rather than an obviously suspicious one.
  </p>

  <h2>A bare number needs something to be measured against</h2>
  <p>
    A noise floor of a few thousandths of a pixel means nothing on its
    own - whether that is excellent or barely adequate depends
    entirely on how large the displacements being measured actually are.
    Putting it against the largest displacement the run actually measured
    turns it into a sentence that means something on its own: something
    like "at worst, one part in several hundred of the largest movement
    measured." That comparison uses only numbers the run already produced;
    it invents no external idea of what counts as a "good" noise floor,
    which would only be a threshold pulled from nowhere in particular.
  </p>

  <figure class="plate tight">
    <img src="../assets/point-readout.png" alt="A point readout panel listing the reference pixel, displacement, magnitude, correlation, noise floor sigma, match conditioning beta, and fitted strain for one measured point, each with a sentence beneath it saying what it cannot tell you.">
    <figcaption>
      <b>One point, and what each of its numbers is not.</b> Every channel
      carries a sentence stating its own limits, next to the number rather
      than in documentation nobody has open. The noise floor says it is a
      lower bound and never examines the target image; the conditioning
      says it is comparable within this run and meaningless across runs.
    </figcaption>
  </figure>

  <h1 id="reading-critically">10. Reading a field critically</h1>
  <p class="lede">
    A colour map is persuasive by nature - smooth gradients look
    authoritative whether or not they deserve to. This chapter is a
    checklist for looking at any DIC field, from any tool, without being
    talked into more confidence than the data actually earns.
  </p>

  <h2>Read the scale before reading the colours</h2>
  <p>
    What is the range, and what are the units? A field that runs from
    3.000000 to 3.000002 pixels is, for all practical purposes, uniform
    - and drawn with the same rainbow as a field that genuinely
    spans a wide range, it can look just as dramatic. The colours alone
    cannot distinguish real variation from noise sitting at the sixth
    decimal place; only the number on the scale can.
  </p>

  <h2>Is zero centred, or is it not?</h2>
  <p>
    A strain scale should be centred on zero, because zero strain is a
    real physical state and the sign either side of it means something
    (tension against compression). A displacement scale should not be,
    because a specimen that merely translated across the frame has no
    reason for its displacement to straddle zero at all. If a scale's
    centring does not match which kind of quantity is on screen, treat
    every colour on it with suspicion until that is understood.
  </p>

  <h2>Which direction is "worse"?</h2>
  <p>
    For an ordinary measurement, a large value is just a large value. For
    a reliability channel (Chapter 9), a large value is a warning. The
    same warm-to-cool colour ramp can be used for both, and nothing about
    the colours themselves tells you which reading applies - only
    the label does. Check what is actually being shown before deciding
    whether the red patch is the interesting result or the part to
    distrust.
  </p>

  <h2>Are the holes really holes?</h2>
  <p>
    Ask what an unmeasured point would look like on this particular map.
    If the honest answer is "identical to a genuine zero," as Chapter 3
    warns, then a smooth-looking field may be smooth because the tool
    filled in the gaps with the most reassuring possible value rather
    than because the specimen actually behaved smoothly. A field that is
    willing to show visible holes is, perhaps counterintuitively, more
    trustworthy in the parts that remain than one that never does.
  </p>

  <h2>Does the field extend further than the measurement that feeds it?</h2>
  <p>
    A strain map should never reach further than the displacement map it
    was fitted from (Chapter 2), and a repaired point (Chapter 8) should
    be visibly marked as one if that information is available at all. A
    field that looks suspiciously complete, with no trace of where the
    underlying measurement actually struggled, is a field worth asking
    harder questions about.
  </p>

  <div class="callout">
    <p class="callout-label">The five-question pass</p>
    <p>
      Scale and units. Centred on zero, or not. Which direction is worse.
      Are the gaps real. Does this field outrun what it was built from.
      Running through these five before reading any conclusion off a DIC
      field takes seconds, and it is where most confident misreadings
      actually get caught.
    </p>
  </div>

  {nav}
</main>
"""

# =========================================== part-4-beyond-one-field ===
part4_body = """
<main class="chapter">
  <p class="kicker">Part IV &middot; Beyond one field</p>
  <h1 id="sequence-as-curve">11. Reading a sequence as a curve</h1>
  <p class="lede">
    One field is a snapshot. What a loading test is actually for is a
    curve - strain against load step, elongation against time
    - and getting from a stack of fields to a trustworthy curve has
    its own ways of going wrong.
  </p>

  <h2>The virtual extensometer</h2>
  <p>
    A physical extensometer is a clip gauge: two arms attached to a
    specimen a fixed distance apart, reporting how that distance changes
    as the specimen deforms. A <strong>virtual extensometer</strong> is
    the same idea done in software: place two points anywhere on the
    measured field, and read the change in the distance between them
    across every frame of the sequence.
  </p>

  <h2>It measures the gap between the anchors, never the movement of either one</h2>
  <p>
    This is the rule that matters most, and it is easy to get backwards
    without noticing, because a common way to test the idea does not
    expose the mistake. A gauge has to compute the change in
    <strong>distance between</strong> its two anchor points, not the
    displacement of either anchor on its own. A specimen carried bodily
    across the frame - a pure rigid translation, no stretching at
    all - has genuinely strained by nothing whatsoever. An
    implementation that (by mistake) read one anchor's own displacement
    instead of the gap between the two would turn that rigid motion into
    a large, smooth-looking, entirely fictitious strain reading.
  </p>
  <p>
    The trap in verifying this: testing only with a pure stretch centred
    on the origin will not catch the bug, because under that specific kind
    of deformation the two calculations happen to agree by coincidence. It
    takes a genuinely rigid translation, tested on its own, to tell the
    two apart.
  </p>

  <h2>An anchor needs real measurement on all sides to mean anything</h2>
  <p>
    An anchor placed between grid points does not sit exactly on a
    measured point; its reading is interpolated from the four points
    around it. If any one of those four is a gap (Chapter 3), every
    obvious way to paper over it is dishonest in its own way: using the
    three that remain quietly changes what is being averaged; reaching
    further out for a substitute borrows a reading from somewhere the
    anchor is not; filling the gap with zero drags the whole reading
    toward "did not move," which is the most plausible-looking wrong
    answer available. The honest response is to refuse the reading
    outright and let the anchor be moved somewhere the field can actually
    support it.
  </p>

  <h2>A gap in the curve is drawn as a gap</h2>
  <p>
    Some frames will not be readable - an anchor sat over a hole
    (Chapter 5), or that frame's field simply did not measure enough of
    the specimen. Dropping that frame from the curve and drawing a
    straight line from the frame before it to the frame after would fill
    territory nobody actually measured with a segment that is, on the
    screen, indistinguishable from real data. The honest curve breaks
    visibly where a frame could not be read, and says so in words, rather
    than smoothing over the hole.
  </p>

  <h2>Verified, not merely argued</h2>
  <p>
    A synthetic tension sequence ships with an exactly known answer for
    every frame, and a virtual extensometer placed on it reads 0.005,
    0.010, 0.020 and 0.036 against stated strains of 0.005, 0.010, 0.020
    and 0.035 across four frames - close enough, over displacements
    reaching tens of pixels, to trust the method rather than merely the
    argument for it.
  </p>

  <figure class="plate">
    <img src="../assets/loading-curve.png" alt="A five-frame tension sequence: the displacement field over a speckled specimen with a virtual extensometer drawn across it, and beneath it a plot of engineering strain rising smoothly against frame number from near zero to about 0.055.">
    <figcaption>
      <b>A loading curve from two clicks.</b> The extensometer is the pale
      line drawn across the specimen; the curve beneath reads the change in
      the distance between its two ends across every frame. The caption
      above the chart states how many frames were readable, so a break in
      the line can never be mistaken for smoothing.
    </figcaption>
  </figure>

  <h1 id="leaving-the-application">12. The field leaving the application</h1>
  <p class="lede">
    A result rarely stays where it was produced. It gets opened in
    another tool entirely - a general-purpose scientific viewer, a
    spreadsheet, a finite-element package - and every principle this
    manual has argued for has to survive that trip, or it was never really
    upheld at all.
  </p>

  <h2>Geometry says where the instrument looked; the data says what it found</h2>
  <p>
    An exported field's underlying mesh should cover every place a
    measurement was <em>attempted</em>, not merely every place it
    <em>succeeded</em>. If the geometry itself shrank to fit only the
    successful points, a difficult specimen would quietly export as a
    smaller, easier-looking one than it really is - the very
    difficulty that makes it worth studying would disappear from the
    file before anyone else ever saw it. The data arrays sitting on that
    geometry are where "attempted but not measured" actually gets stated,
    following the same absence-not-zero rule as everything else.
  </p>

  <h2>The same "not zero" rule, one file format further out</h2>
  <p>
    In almost any viewer, a field of zeros renders as a perfectly
    reasonable, uniform blue region - entirely indistinguishable
    from a genuine measurement of no movement. An export has to carry
    unmeasured values as not-a-number, exactly as the application's own
    screen does, and state outright, in an array of its own, which points
    were actually measured - rather than leaving a reader to infer
    it from wherever the holes happen to be. A derived quantity that was
    never asked for at all (strain, on a run where it was switched off)
    should be <em>absent</em> from the file entirely, not present and
    filled with not-a-number everywhere: an array that is entirely
    not-a-number reads as "we tried and failed everywhere," which is a
    much more alarming and much less accurate claim than "this was never
    attempted."
  </p>

  <h2>A file has to state its own coordinate frame</h2>
  <p>
    Chapter 4 explained why one consistent coordinate frame matters
    inside the application. The moment a field leaves it, that guarantee
    is gone: whatever opens the file next has its own default assumption
    about which way is up, and there is no reason to expect it matches.
    The file has to say, explicitly, which convention its coordinates
    use, so a reader - human or software - does not have to
    guess and does not draw the field mirrored without any way to notice.
  </p>

  <h2>Provenance travels with the file, captured at the moment that matters</h2>
  <p>
    A trustworthy result carries an account of how it was made: both
    source images identified precisely enough that a substitution would
    be caught, the settings the correlation actually ran with, the region
    that was used, and the exact version of the software and its engine
    that produced it. That account has to be captured <strong>when the
    run starts</strong>, not read back from an on-screen panel afterward
    - a panel that keeps accepting input after a run finishes will,
    if read late, describe a configuration that never actually produced
    anything, attached with complete confidence to a result that was
    really made under different settings entirely.
  </p>

  <figure class="plate">
    <img src="../assets/paraview.png" alt="ParaView with an exported .vtu file open: the pipeline browser lists the file, the properties panel lists displacement, strain, correlation, solved and both reliability arrays, and the strain field is drawn on the specimen with unmeasured points left as visible holes.">
    <figcaption>
      <b>The same field, opened somewhere else.</b> No conversion step: the
      format is VTK's own, so ParaView and FreeCAD's FEM workbench read it
      directly. Every channel the run measured is in the array list, and
      the gaps are still gaps - unmeasured points arrive as not-a-number
      rather than as a flat blue region no reader could tell from a real
      measurement of no movement.
    </figcaption>
  </figure>

  <div class="callout warn">
    <p class="callout-label">A trap worth knowing about, generally</p>
    <p>
      A file-writing call that reports success is not automatically proof
      a file was written. It is worth checking, once, that the specific
      writer a pipeline depends on actually fails loudly - some
      report success even when the destination directory does not exist
      and nothing was written at all. A confirmed write, not merely an
      unrejected one, is the only thing that should tell a user their
      result is saved.
    </p>
  </div>

  {nav}
</main>
"""

# ================================================== part-5-practice ===
part5_body = """
<main class="chapter">
  <p class="kicker">Part V &middot; Practice</p>
  <h1 id="speckling">13. Speckling a specimen</h1>
  <p class="lede">
    Chapter 1 explained why a speckle pattern is necessary at all: a
    featureless surface has no unique texture to lock onto. This chapter
    is about what makes one pattern actually work well and another one
    fail quietly.
  </p>

  <h2>What a good pattern needs</h2>
  <ul>
    <li>
      <strong>Random, not periodic.</strong> A repeating pattern -
      an evenly spaced grid of dots, a woven fabric texture - gives
      the correlation search more than one place that looks like an
      equally good match. The search can lock onto the wrong repeat of
      the pattern with high apparent confidence, which is a far worse
      failure than an honest low correlation score: it reports a
      plausible, wrong answer instead of admitting doubt.
    </li>
    <li>
      <strong>High local contrast.</strong> The correlation is only as
      good as the intensity gradient inside each subset. A washed-out,
      low-contrast pattern gives the search little to grip, which is
      exactly what a noise floor (Chapter 9) measures directly: weak
      local contrast against a given camera's own noise level produces a
      correspondingly coarse noise floor, however good the pattern looks
      to the eye.
    </li>
    <li>
      <strong>Feature size matched to the subset, not to the whole
      image.</strong> What matters is not how the pattern looks across
      the whole specimen, but how much distinct texture falls inside one
      subset. A pattern with speckles far larger than the subset gives
      each subset too little variation to be unique; a pattern with
      speckles far smaller can blur together at the imaging resolution
      actually being used. Chapter 14 covers choosing the subset itself;
      the two decisions have to be made together, not in either order
      alone.
    </li>
    <li>
      <strong>Genuine full-range contrast, without saturating.</strong> A
      pattern that is mostly pure black and pure white loses gradient
      information exactly where the camera clips - a saturated
      pixel carries no information about how much brighter or darker the
      true value was, so a pattern that clips throws away contrast it
      could otherwise have used.
    </li>
  </ul>

  <h2>How a pattern gets onto a specimen</h2>
  <p>
    Some specimens already carry enough natural texture to correlate
    well and need nothing applied at all - worth checking, with a
    real photograph and a real quality estimate, before assuming a
    pattern is necessary. Where one is needed, common methods include
    spray or airbrush application of a fine random speckle, and printing
    a computer-generated, non-periodic pattern onto a decal or stencil
    and transferring or applying it to the specimen.
  </p>
  <h2>Checking a pattern before committing to a whole test</h2>
  <p>
    The cheapest check is the one done before any load is applied at
    all: photograph the specimen as speckled, and look at the reliability
    figures a correlation of that reference image against itself, or
    against a first small step, actually produces. A live estimate of
    what a chosen subset radius can resolve against the pattern actually
    present is worth having on screen while those settings are still
    being chosen, precisely because it is far cheaper to re-speckle a
    specimen before a test than to discover a pattern was too weak only
    after the test is finished and cannot be repeated.
  </p>

  <h1 id="subset-and-step">14. Choosing subset radius and grid step</h1>
  <p class="lede">
    Two numbers control almost every correlation: how large a patch of
    pixels each measurement point looks at, and how far apart the
    measurement points are spaced. Neither has a single correct value;
    both are trade-offs that depend on the pattern and on what is being
    measured.
  </p>

  <h2>Subset radius: robustness against locality</h2>
  <p>
    A larger subset contains more texture, which generally makes its
    correlation more robust and its noise floor finer (Chapter 9) -
    more pixels means more gradient energy to work with, and a steadier
    average against sensor noise. The cost is spatial: a large subset
    blurs the field, because a single reported point is really an average
    over a fairly wide patch of the specimen. Where the deformation
    changes sharply over a short distance - near a hole, at a crack
    tip, across a narrow gauge section - too large a subset
    averages the interesting detail away before it is ever reported.
  </p>
  <p>
    A smaller subset can follow sharper local detail, but has less
    texture to work with, is more sensitive to noise, and needs a
    correspondingly finer speckle pattern to still contain enough unique
    contrast to correlate reliably at all.
  </p>

  <h2>Grid step: sampling density against cost</h2>
  <p>
    The grid step is simply how far apart the measurement points are
    placed. A finer step gives a denser field and a smoother-looking
    result, at a roughly proportional cost in computation. It does not,
    on its own, improve the quality of any individual point's
    measurement - that is entirely the subset's job - so
    a very fine grid step over an unreliable subset produces a dense
    field of unreliable answers, not a genuinely more precise one.
  </p>

  <h2>Working from the reliability figures, rather than guessing</h2>
  <p>
    The two settings are not really independent, and neither is right or
    wrong in isolation: the sensible way to choose them is to look at
    what the actual pattern, at the actual imaging resolution being used,
    can resolve at a candidate subset radius - the noise floor from
    Chapter 9, computed live against the settings under consideration
    - rather than picking a subset radius by habit or by copying a
    value from an unrelated test.
  </p>

  <h1 id="lighting-and-imaging">15. Lighting and imaging</h1>
  <p class="lede">
    Everything in Parts I through IV assumes the two photographs differ
    only by the deformation being studied. Lighting, focus, and camera
    settings that drift between reference and target introduce
    differences that have nothing to do with the specimen, and DIC cannot
    tell those apart from real movement.
  </p>

  <h2>Even, diffuse, and stable</h2>
  <p>
    Direct, hard lighting produces specular highlights - small,
    very bright patches that shift position as the specimen itself
    moves, even though the surface underneath is speckled normally.
    A highlight sliding across a subset between reference and target
    looks, to a correlation search, like a change in the pattern itself,
    and can corrupt an otherwise perfectly good measurement. Even,
    diffuse lighting, without a strong single source, avoids this.
  </p>
  <p>
    Lighting also has to stay <strong>stable</strong> for the whole
    duration of a test, not merely at the moment each photograph is
    taken. A light that dims, flickers, or casts a moving shadow across
    the specimen over the course of a long test introduces intensity
    changes a correlation search has no way to separate from real
    deformation.
  </p>

  <h2>Fixed focus, fixed aperture, fixed everything that can auto-adjust</h2>
  <p>
    Any camera setting that can change automatically between shots
    - autofocus, auto-exposure, auto white balance - is a
    setting that can quietly change the image in a way indistinguishable,
    to the correlation, from real deformation. All of them should be
    fixed manually before a test begins and left untouched for its
    entire duration, including through however many load steps and
    however much time the test takes.
  </p>

  <h2>The camera itself has to hold still</h2>
  <p>
    Everything this manual has said about a stable coordinate frame
    (Chapter 4) assumes the camera did not move between the reference
    photograph and any target. A tripod or rigid mount that cannot shift,
    even slightly, over the course of a test is not a minor
    convenience - a camera that moves introduces a rigid
    displacement across the entire field that is real, in the sense that
    it genuinely happened, and completely uninteresting, in the sense
    that it has nothing to do with the specimen being studied, and no
    way to tell the two apart after the fact.
  </p>

  <div class="status-note">
    This chapter covers the fundamentals that apply regardless of
    equipment. It does not yet cover camera and lens selection, or the
    calibration procedure a stereo rig needs - that workflow is on
    SurView's own roadmap and will get a chapter here once it exists in
    the application. See the Appendix for what else is still to come.
  </div>

  {nav}
</main>
"""

# ======================================================== appendix ===
appendix_body = """
<main class="chapter">
  <p class="kicker">Appendix</p>
  <h1 id="glossary">Glossary</h1>
  <p class="lede">
    Terms as this manual uses them. Where SurView's own screens use a
    different word for the same idea, that word is given alongside it.
  </p>

  <dl class="glossary">
    <dt>Reference image</dt>
    <dd>The photograph taken before deformation. Every displacement in a
    run is measured relative to it.</dd>

    <dt>Target image</dt>
    <dd>A photograph taken after some deformation has occurred, compared
    against the reference (or, after reference updating, against a
    previous target) to measure movement.</dd>

    <dt>Subset</dt>
    <dd>The small patch of pixels around one measurement point that is
    matched between the reference and the target. Its size is the subset
    radius.</dd>

    <dt>Grid step</dt>
    <dd>The spacing, in pixels, between neighbouring measurement points.</dd>

    <dt>Correlation coefficient (ZNCC)</dt>
    <dd>A number stating how closely a candidate match in the target
    resembles the original subset in the reference. A measure of matching
    confidence, not of physical correctness.</dd>

    <dt>Displacement field</dt>
    <dd>The measured movement, in pixels, at every point of the grid.</dd>

    <dt>Strain</dt>
    <dd>Local stretching or shearing, fitted from the displacements of
    neighbouring points rather than measured directly at any one point.
    Reported in three common forms: Cauchy (small-strain), Green-Lagrange,
    and Euler-Almansi, which agree closely at small deformations and
    diverge at large ones.</dd>

    <dt>Region of interest (ROI)</dt>
    <dd>The boundary restricting where measurement points are placed,
    drawn by hand or proposed by automatic segmentation of the speckle
    quality. May exclude one or more holes: places inside the outer
    boundary that are not to be measured.</dd>

    <dt>Sequence</dt>
    <dd>A reference image plus an ordered series of target images, making
    up one loading test.</dd>

    <dt>Reference updating (re-anchoring)</dt>
    <dd>Switching a sequence's comparison from the original reference to
    a more recent frame, once too little of the field still correlates
    against the original. Off by default.</dd>

    <dt>Recovery pass ("Points that failed" in the Analysis panel)</dt>
    <dd>A second attempt at points the first correlation could not measure
    well, seeded with a displacement fitted from nearby reliable points
    and then fully re-solved. On by default; never accepts an answer worse
    than the one it would replace.</dd>

    <dt>Noise floor (sigma)</dt>
    <dd>The finest displacement a given subset's own speckle quality could
    resolve against the camera's sensor noise. A lower bound, computed
    from the reference image alone.</dd>

    <dt>Match conditioning (beta)</dt>
    <dd>How sharply the matching cost rises around the solution actually
    found for one point. Meaningful only within a single run.</dd>

    <dt>Virtual extensometer</dt>
    <dd>Two points placed on the measured field whose changing distance
    apart is plotted against frame, standing in for a physical clip
    gauge.</dd>
  </dl>

  <h1 id="on-screen">Where to find it on screen</h1>
  <table class="data">
    <thead><tr><th>To do this</th><th>Look here</th></tr></thead>
    <tbody>
      <tr><td>Import the reference or target images</td><td>Toolbar: <strong>Reference</strong>, <strong>Target</strong></td></tr>
      <tr><td>Draw or auto-detect a region</td><td>Toolbar: <strong>Define ROI</strong>, <strong>Auto-detect ROI</strong></td></tr>
      <tr><td>Exclude a hole from a region</td><td>Toolbar: <strong>Add Hole</strong> (needs a region first)</td></tr>
      <tr><td>Choose the solver, subset, grid step, strain settings</td><td><strong>Analysis</strong> panel</td></tr>
      <tr><td>Turn reference updating on and see its cost</td><td><strong>Analysis</strong> panel, "Reference" group</td></tr>
      <tr><td>Turn the recovery pass on and adjust it</td><td><strong>Analysis</strong> panel, "Points that failed" group</td></tr>
      <tr><td>Start or stop measuring</td><td>Toolbar: <strong>Run Correlation</strong>, <strong>Stop</strong></td></tr>
      <tr><td>Read a value at one point of the field</td><td><strong>Point</strong> panel; hover the field, click to pin</td></tr>
      <tr><td>Switch between displacement, strain, and reliability channels</td><td>The "Showing" selector above the field</td></tr>
      <tr><td>Place a virtual extensometer</td><td>Toolbar: <strong>Extensometer</strong>, then two clicks on the specimen</td></tr>
      <tr><td>Plot a quantity against frame</td><td><strong>Plot</strong> panel</td></tr>
      <tr><td>See provenance for an imported image</td><td><strong>Record</strong> panel</td></tr>
      <tr><td>Export a field</td><td>File menu: <code>.vtu</code> for ParaView and FreeCAD, or <code>.csv</code></td></tr>
    </tbody>
  </table>

  <h1 id="not-yet">What is not in this manual yet</h1>
  <p>
    Written up only where the screen it describes actually exists, this
    manual is necessarily behind SurView's own roadmap. As of this
    writing, the following are real, tracked work that simply has not
    landed:
  </p>
  <ul>
    <li>Stereo and out-of-plane (2.5D) measurement, and the camera
    calibration workflow it depends on.</li>
    <li>Reading a quantity along a line, rather than only at one point or
    between two extensometer anchors.</li>
    <li>Multi-view measurement from a single moved camera, for slow or
    static tests such as creep.</li>
    <li>Sparse point-marker tracking, alongside the dense speckle field.</li>
  </ul>
  <p>
    See <a href="https://github.com/katalystnord/SurView/blob/main/ROADMAP.md">ROADMAP.md</a>
    for the current state of each. This manual will gain a chapter for
    each one as it ships.
  </p>

  {nav}
</main>
"""

print("all bodies staged")

# ============================================================== assembly ===
pages = [
    ("index",                          "Front matter",
     "Digital image correlation, measured carefully",
     "Who this manual is for, how it is organised, and what is not written yet.",
     index_body, None),
    ("part-1-what-dic-measures",       "Part I: What DIC measures",
     "Part I. What DIC measures - The SurView DIC Manual",
     "Two photographs, a speckle pattern, and what a correlation actually measures. Why strain is fitted, not measured, and why an unmeasured point must never look like a zero.",
     part1_body, "index"),
    ("part-2-running-a-measurement",   "Part II: Running a measurement",
     "Part II. Running a measurement - The SurView DIC Manual",
     "Regions and holes, frame order, reference updating, and the second pass that recovers points the first solve could not measure.",
     part2_body, "part-1-what-dic-measures"),
    ("part-3-how-much-to-trust-it",    "Part III: How much to trust it",
     "Part III. How much to trust it - The SurView DIC Manual",
     "Noise floor and match conditioning: two different questions about reliability, and a five-question checklist for reading any DIC field critically.",
     part3_body, "part-2-running-a-measurement"),
    ("part-4-beyond-one-field",        "Part IV: Beyond one field",
     "Part IV. Beyond one field - The SurView DIC Manual",
     "Reading a whole sequence as a curve with a virtual extensometer, and what a result has to carry with it once it leaves the application.",
     part4_body, "part-3-how-much-to-trust-it"),
    ("part-5-practice",                "Part V: Practice",
     "Part V. Practice - The SurView DIC Manual",
     "Speckling a specimen, choosing subset radius and grid step, and lighting a rig for digital image correlation.",
     part5_body, "part-4-beyond-one-field"),
    ("appendix",                       "Appendix",
     "Appendix - The SurView DIC Manual",
     "Glossary, where to find each task on screen, and what this manual does not cover yet.",
     appendix_body, "part-5-practice"),
]

for i, (slug, navlabel, title, desc, body, current_key) in enumerate(pages):
    prev = None
    nxt = None
    if i > 0:
        pslug, plabel = pages[i-1][0], pages[i-1][1]
        prev = (f"{pslug}.html", plabel)
    if i < len(pages) - 1:
        nslug, nlabel = pages[i+1][0], pages[i+1][1]
        nxt = (f"{nslug}.html", nlabel)
    nav_html = chapter_nav(prev, nxt)
    filled = body.replace("{nav}", nav_html)
    filled = filled.replace("VERSION_TOKEN", VERSION).replace("VERSION_DATE_TOKEN", VERSION_DATE)
    full = page(title, desc, slug, filled)
    with open(OUT + slug + ".html", "w", encoding="utf-8") as f:
        f.write(full)
    print("wrote", slug, len(full), "bytes")


# ============================================== print / PDF rendering ===
# A single continuous document: title page, printed contents, then every
# chapter in reading order. Deliberately NOT the web pages with a print
# stylesheet bolted on - the web layout is a sticky header and a sticky
# sidebar, and neither paginates. This shares the chapter BODIES and nothing
# else, which is the only part that must not drift.

PRINT_STYLE = """
@page {
  size: A4;
  margin: 22mm 20mm 20mm 20mm;
  @bottom-center { content: counter(page); font-family: Inter, sans-serif; font-size: 9pt; color: #55616d; }
}
@page :first { margin: 0; @bottom-center { content: ""; } }

:root {
  --text: #131a21; --text-dim: #55616d; --border: #d8e0e8;
  --accent: #1a6fb5; --accent-strong: #14568c; --accent-soft: #eef4fa;
  --hot: #b8461a; --code-bg: #f3f6f9; --dark: #0f1720;
}
* { box-sizing: border-box; }
body {
  margin: 0; color: var(--text); background: #fff;
  font-family: Inter, -apple-system, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  font-size: 10.5pt; line-height: 1.55;
  -webkit-print-color-adjust: exact; print-color-adjust: exact;
}
a { color: var(--accent-strong); text-decoration: none; }
code { font-family: "SF Mono", Menlo, Consolas, monospace; background: var(--code-bg); padding: 0.05em 0.3em; border-radius: 3px; font-size: 0.92em; }

/* ---- title page ---- */
.cover {
  height: 297mm; padding: 45mm 22mm 20mm; background: var(--dark); color: #eef3f7;
  break-after: page; page-break-after: always; position: relative;
}
.cover h1 { font-size: 30pt; line-height: 1.12; margin: 0 0 10mm; letter-spacing: -0.01em; font-weight: 800; }
.cover .sub { font-size: 13pt; color: #a7b4c0; margin: 0 0 22mm; max-width: 130mm; line-height: 1.5; }
.cover .meta { position: absolute; bottom: 24mm; left: 22mm; right: 22mm; font-size: 10pt; color: #a7b4c0; }
.cover .meta strong { color: #eef3f7; }
.cover .ver { display: inline-block; border: 1px solid #4aa3e0; color: #8ccbf2; border-radius: 999px; padding: 3mm 7mm; font-size: 10pt; font-weight: 700; margin-bottom: 10mm; }

/* ---- printed contents ---- */
.contents { break-after: page; page-break-after: always; }
.contents h2 { font-size: 17pt; margin: 0 0 8mm; }
.contents .part { font-size: 8.5pt; text-transform: uppercase; letter-spacing: 0.07em; font-weight: 800; color: var(--accent-strong); margin: 7mm 0 2.5mm; }
.contents ol { list-style: none; margin: 0; padding: 0; }
.contents li { margin: 0 0 1.8mm; font-size: 10.5pt; }
.contents li a { color: var(--text); }
.contents li a::after { content: " . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . ." ; color: var(--border); }
.contents li .pg { float: right; color: var(--text-dim); font-size: 9.5pt; }

/* ---- chapters ---- */
.part-body { break-before: page; page-break-before: always; }
.part-body:first-of-type { break-before: auto; page-break-before: auto; }
.kicker { color: var(--accent-strong); font-weight: 800; text-transform: uppercase; letter-spacing: 0.06em; font-size: 8pt; margin: 0 0 3mm; }
h1 { font-size: 19pt; margin: 0 0 3mm; letter-spacing: -0.005em; break-after: avoid; page-break-after: avoid; }
h1:not(:first-of-type) { break-before: page; page-break-before: always; margin-top: 0; }
.lede { color: var(--text-dim); font-size: 11.5pt; margin: 0 0 7mm; max-width: 150mm; }
h2 { font-size: 13pt; margin: 8mm 0 2.5mm; break-after: avoid; page-break-after: avoid; }
h3 { font-size: 11pt; margin: 6mm 0 2mm; break-after: avoid; page-break-after: avoid; }
p { margin: 0 0 3.2mm; orphans: 3; widows: 3; }
ul, ol { margin: 0 0 3.5mm; padding-left: 6mm; }
li { margin: 0 0 1.8mm; orphans: 2; widows: 2; }

.callout { border-left: 2.5pt solid var(--accent); background: var(--accent-soft); padding: 4mm 6mm; margin: 5mm 0; break-inside: avoid; page-break-inside: avoid; }
.callout p:last-child { margin-bottom: 0; }
.callout .callout-label { font-weight: 800; font-size: 8pt; text-transform: uppercase; letter-spacing: 0.05em; color: var(--accent-strong); margin: 0 0 1.5mm; }
.callout.warn { border-left-color: var(--hot); background: #fdf1ec; }
.callout.warn .callout-label { color: var(--hot); }

figure.plate { margin: 6mm 0; break-inside: avoid; page-break-inside: avoid; }
figure.plate img { display: block; width: 100%; border: 0.5pt solid var(--border); border-radius: 2mm; }
figure.plate.tight img { max-width: 90mm; }
figure.plate figcaption { font-size: 9pt; color: var(--text-dim); margin-top: 2mm; line-height: 1.45; }
figure.plate figcaption b { color: var(--text); }

table.data { border-collapse: collapse; margin: 4mm 0 6mm; font-size: 9.5pt; width: 100%; break-inside: avoid; page-break-inside: avoid; }
table.data th, table.data td { border: 0.5pt solid var(--border); padding: 2mm 3mm; text-align: left; }
table.data th { background: var(--code-bg); font-weight: 700; }
table.data td.num, table.data th.num { text-align: right; font-family: "SF Mono", Menlo, Consolas, monospace; }

dl.glossary dt { font-weight: 800; margin-top: 4mm; break-after: avoid; page-break-after: avoid; }
dl.glossary dd { margin: 1mm 0 0; color: var(--text-dim); }
.status-note { font-size: 9.5pt; color: var(--text-dim); border: 0.5pt dashed var(--border); border-radius: 2mm; padding: 3.5mm 5mm; margin: 5mm 0; break-inside: avoid; }
.cite-box { border: 0.5pt solid var(--border); border-radius: 2mm; padding: 4mm 5mm; margin: 5mm 0; break-inside: avoid; }
.cite-box .cite-text { font-family: "SF Mono", Menlo, Consolas, monospace; font-size: 9pt; background: var(--code-bg); padding: 3mm 4mm; border-radius: 1.5mm; }
.version-chip, .pdf-link, .chapter-nav, .book-toc { display: none !important; }
"""


REFERENCE_ENTRIES = [
    ("glossary", "Glossary"),
    ("on-screen", "Where to find it on screen"),
    ("not-yet", "What is not in this manual yet"),
]


def print_document(page_map=None):
    """One continuous HTML document, print-styled, ready for Chrome to render.

    `page_map` maps a chapter title to the printed page it starts on. It is
    absent on the first pass, because the page a chapter lands on is not
    knowable until the document has been laid out once: Chrome does not
    implement target-counter(), so the numbers are read back off the rendered
    PDF and the document is built a second time. A reference manual wants real
    page numbers - a passage cited as "p. 12" has to be on page 12.
    """
    def pg(title):
        if not page_map or title not in page_map:
            return ''
        return f'<span class="pg">{page_map[title]}</span>'

    toc = ['<div class="contents"><h2>Contents</h2>']
    for slug, label, chapters in PARTS:
        if chapters is None:
            continue
        toc.append(f'<div class="part">{label}</div><ol>')
        for csl, ctitle in chapters:
            toc.append(f'<li>{pg(ctitle)}<a href="#{csl}">{ctitle}</a></li>')
        toc.append('</ol>')
    toc.append('<div class="part">Reference</div><ol>')
    for anchor, ctitle in REFERENCE_ENTRIES:
        toc.append(f'<li>{pg(ctitle)}<a href="#{anchor}">{ctitle}</a></li>')
    toc.append('</ol>')
    toc.append('</div>')

    cover = f"""<div class="cover">
  <div class="ver">Version {VERSION}</div>
  <h1>Digital image correlation,<br>measured carefully</h1>
  <p class="sub">
    What a DIC measurement actually is, what it can and cannot tell you,
    and how to run one that holds up. The manual for SurView DIC.
  </p>
  <div class="meta">
    <strong>SurView DIC</strong> &middot; version {VERSION} &middot; {VERSION_DATE}<br>
    A Katalyst Nord project. Free and open source, LGPL-2.1-or-later.<br>
    github.com/katalystnord/SurView
  </div>
</div>"""

    # Chapter bodies, stripped of the things that only mean something on the web.
    bodies = []
    for slug, navlabel, title, desc, body, _prev in pages:
        if slug == "index":
            continue          # the cover and contents replace the front matter
        filled = body.replace("{nav}", "")
        filled = filled.replace("VERSION_TOKEN", VERSION).replace("VERSION_DATE_TOKEN", VERSION_DATE)
        # relative asset paths differ: the print doc sits in the same folder
        filled = filled.replace('src="../assets/', 'src="../assets/')
        filled = filled.replace('<main class="chapter">', '<div class="part-body">')
        filled = filled.replace('</main>', '</div>')
        bodies.append(filled)

    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>The SurView DIC Manual {VERSION}</title>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800&display=swap" rel="stylesheet">
<style>{PRINT_STYLE}</style>
</head>
<body>
{cover}
{"".join(toc)}
{"".join(bodies)}
</body>
</html>
"""


def build_pdf(print_path, pdf_path):
    """Render the print document to PDF with whatever Chrome is on PATH."""
    for exe in ("google-chrome", "chromium", "chromium-browser", "google-chrome-stable"):
        chrome = shutil.which(exe)
        if chrome:
            break
    else:
        print("no Chrome or Chromium on PATH: wrote print HTML only, PDF not built")
        return False

    cmd = [chrome, "--headless", "--disable-gpu", "--no-sandbox",
           "--no-pdf-header-footer",
           f"--print-to-pdf={pdf_path}",
           "file://" + print_path]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    if not os.path.exists(pdf_path):
        print("PDF not produced:", result.stderr[-400:])
        return False
    print("wrote", os.path.basename(pdf_path), os.path.getsize(pdf_path), "bytes")
    return True


def read_back_page_numbers(pdf_path):
    """Which printed page each chapter and reference section starts on.

    Read out of the rendered PDF with pdftotext, one page at a time, by
    looking for the chapter's own heading text. Matched on a normalised,
    space-collapsed form because the PDF's extracted text breaks lines
    wherever the layout did, and only the FIRST page a heading appears on
    counts - a chapter mentioned again later must not move its own entry.
    """
    if not shutil.which("pdftotext"):
        print("no pdftotext: contents will have no page numbers")
        return None

    wanted = []
    for slug, label, chapters in PARTS:
        if chapters:
            wanted.extend(t for _, t in chapters)
    wanted.extend(t for _, t in REFERENCE_ENTRIES)

    def norm(text):
        return re.sub(r"\s+", " ", text).strip().lower()

    found = {}
    pages = int(subprocess.run(["pdfinfo", pdf_path], capture_output=True, text=True)
                .stdout.split("Pages:")[1].split()[0])
    for page in range(1, pages + 1):
        out = subprocess.run(["pdftotext", "-f", str(page), "-l", str(page),
                              pdf_path, "-"], capture_output=True, text=True).stdout
        flat = norm(out)

        # ⚑ SKIP THE CONTENTS PAGES. They list every chapter title, so
        # without this the first page each title appears on is the contents
        # itself and every entry reads the same number. Found by rendering it
        # and reading the page: all eighteen said "2".
        if flat.startswith("contents"):
            continue

        for title in wanted:
            if title in found:
                continue
            # A chapter starts a page of its own, so its heading is at the
            # top. Requiring that, rather than anywhere on the page, keeps a
            # passing mention of one chapter inside another from claiming it.
            if norm(title) in flat[:300]:
                found[title] = page
    missing = [t for t in wanted if t not in found]
    if missing:
        print("page numbers not found for:", ", ".join(missing))
    return found


print_path = OUT + "surview-dic-manual.print.html"
pdf_path = OUT + "surview-dic-manual.pdf"

with open(print_path, "w", encoding="utf-8") as f:
    f.write(print_document())
print("wrote surview-dic-manual.print.html")

if "--pdf" in sys.argv:
    # Pass one lays the document out so the page numbers exist to be read.
    if build_pdf(print_path, pdf_path):
        page_map = read_back_page_numbers(pdf_path)
        if page_map:
            with open(print_path, "w", encoding="utf-8") as f:
                f.write(print_document(page_map))
            # Pass two carries the numbers. Contents length is unchanged by
            # adding them, so the numbers it states stay the true ones.
            build_pdf(print_path, pdf_path)
            print("contents carries page numbers for", len(page_map), "sections")
