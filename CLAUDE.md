# SurView DIC - Context File

---

# ⚑⚑⚑ THE TEN TENETS - the premise, above everything else in this file

**Stated by David, 2026-07-21.** Everything else in this file is subordinate
to these. Where the rest of the file disagrees with a tenet, **the tenet wins
and the rest is wrong.**

1. **Speckle images and video in → trustworthy displacement/strain fields
   out.** This is the whole product. It stands on two co-equal pillars:
   1) faithfully record the pixel evidence, and 2) correlate them into
   fields. Everything else may only *augment or add to* it, without getting
   in the way of it.

2. **There was no technically all-encompassing, cross-platform,
   ecosystem-native DIC GUI a working scientist could adopt.** There are
   several good engines, but only rudimentary GUIs. This is why SurView
   exists.

3. **We chose the best open-source engine available as the starting point for
   our own development. A starting point. Not a parent.**

4. **We will incorporate vetted good solutions into our own DIC stack** -
   from wherever they come, bound to no single line of heritage.

5. **We hold no allegiance to any upstream at the code level** - licensing
   and attribution only.

6. **All interoperability with other tools happens at the file level** -
   never at the model or code level.

7. **The GUI is also the product: UX has the same parity as measurement
   capability.** Not a hierarchy. A UX defect is a defect.

8. **We will introduce alternative designs and break with inherited
   approaches** whenever that is required to achieve the solution we want.

9. **We quantify and report the reliability of both what we record and what
   we interpret from it** - with a clear account of the known accuracy. We
   never present a number as more trustworthy than we can substantiate.

10. **We keep the record pristine with retained provenance** - no silent
    loss, conversion, or resampling; the record is the foundation everything
    stands on. We seek the simplest, most robust solution for reliable and
    trustworthy results, never over-interpreting where a simpler approach
    will do.

### Mechanism that lives outside the tenets

The tenets stay pure principle; three operating mechanisms that came up while
sharpening them live in their natural sections below, not in a tenet:

- **Upstreaming policy** (from tenet 5) and **fork-idiom conformance** (from
  tenet 8) - see *Engine capability roadmap (OpenCorr fork)*.
- **CPU-first, heavier-machinery-on-need sequencing** (from tenet 10) - see
  *Roadmap: cross-vendor GPU acceleration*.

---

## What this is

The open-source DIC (Digital Image Correlation) world has good engines and
poor GUIs. The missing piece is not another correlation kernel - it is a
well-engineered, cross-platform, license-clean, ecosystem-native GUI that a
working scientist can adopt and hand to a colleague. SurView DIC fills that
space.

## Engine: OpenCorr

SurView DIC wraps **OpenCorr** (Jiang, `vincentjzy/OpenCorr`, MPL-2.0) as its
correlation engine, chosen for its maturity and technical capability:

- 2D DIC, stereo/3D DIC, and volumetric DVC in one library.
- CPU-path solvers (ICGN, NR, IC-LM), self-adaptive subsets,
  SIFT-feature-guided and epipolar-constrained stereo matching - open,
  buildable, MPL-2.0.
- MPL-2.0 license (file-level copyleft, linkable into a larger work) does
  not constrain SurView DIC's own license - see License below.
- Actively maintained and citable (Jiang, *Optics and Lasers in
  Engineering*, 2023).

OpenCorr also ships a GPU-accelerated ICGN path, but it is closed-source
(no CUDA source in the repo), Windows/NVIDIA-only, has an open unresolved
crash bug, and its binaries carry no verifiable license. SurView DIC treats
it as unavailable - not something we can build, fix, or ship. See GPU
acceleration under Roadmap for the actual plan.

The upstream engine stays a dependency; this repo is the GUI/packaging
layer. Do not merge OpenCorr into this repo.

## Why Qt + VTK

SurView DIC is built in Qt and VTK to be a native complement to the
open-source technical-computing ecosystem it sits alongside (FreeCAD,
ParaView, and similar Kitware/VTK-stack tools), not just for the GUI toolkit
itself.

VTK specifically is a design decision, not an incidental library choice:

- DIC/DVC output is a displacement/strain field on a mesh - exactly what VTK
  represents and renders.
- Native `.vtu`/`.vtk` output means ParaView opens it directly and FreeCAD's
  FEM workbench consumes it, with no conversion step.
- This closes a real loop: DIC-measured fields validating FEA predictions,
  both in the same pipeline.
- It makes SurView DIC a citizen of the VTK/Kitware ecosystem (ParaView,
  3D Slicer), not merely a standalone Qt app.

**Build constraint:** Qt's GPL-3.0-only add-on modules (Qt Charts, Qt Quick
3D) are off-limits - using either would force SurView DIC's own license to
GPL. All charting and 3D rendering goes through VTK (`vtkChart`, `vtkPlot`,
etc.), which is BSD-3-Clause and imposes no such constraint. Qt itself stays
scoped to the application shell (windows, menus, dialogs, docking) under its
LGPLv3 essential modules.

### One coordinate frame: world = image = engine (2026-08-18)

The viewport's VTK world coordinates **are** image pixel coordinates - x right,
y **down**, origin at the top-left pixel - which is also the file's own row
order and the engine's `Point2D` convention. A click, a region's corners, a
measured point and a rendered field are all in that one frame, and nothing
converts between conventions.

This had to be established before the ROI work, because it was not true before:

- VTK's image readers disagree about row order. `vtkTIFFReader` honours the
  file's orientation tag (top-down by default); the PNG, JPEG and BMP readers
  all emit bottom-up. Verified by reading a known image through each.
- VTK renders y up, so TIFFs - the usual speckle format - were displayed
  **vertically mirrored**, and PNG/JPEG/BMP were not.
- `showField()` placed the field using engine coordinates straight into world y,
  so the field agreed with the photograph only for TIFF, by accident.

Fixed in two places: `decodeImage()` normalises every decoded image to the
file's own row order (TIFF pinned to `ORIENTATION_TOPLEFT`, everything else
flipped once with `vtkImageFlip`, recorded in `ImageRecord::rowsReversedByDecoder`
and reported in the Record panel), and the camera views the image plane from
**-z** with view-up **-y**. Turning the camera around rather than only inverting
the up vector matters: inverting up alone rotates the view 180° and mirrors x
too. Consequence to remember: overlays drawn in front of the photograph sit at
**negative** z.

### Strain is fitted, not measured (2026-08-19)

Correlation measures displacement at a point. Strain is its gradient, so it has
no value at a single point at all: the engine fits a plane through the
displacements of the neighbours inside a subregion. Three consequences that are
invisible unless deliberately surfaced, and that this codebase now handles:

- **A strain the fit declined is not a strain of zero.** `POI2D::clear()` zeroes
  the strain members, and `Strain::compute()` leaves them untouched when it
  gives up, so "no fit here" and "unstrained here" are the same three zeros.
  `Correlation.cpp` writes a not-a-number sentinel into all three before the
  fit and reads back `strainFitted` from whether the engine overwrote it. The
  same rule as the existing "a rejected point is not a displacement of zero",
  and equally load-bearing: the negative check in `tests/test_strain_measure.cpp`
  records the sentinel being removed and the test going red.
- **The engine does not refuse a subregion that is too small.** When the radius
  search returns fewer than the minimum, `Strain::compute()` silently falls back
  to a KNN search for the nearest N points, however far outside the subregion
  they lie, and returns a field that looks exactly as complete as a good one.
  `core/StrainFit.h` counts the lattice points a subregion actually holds and
  the Analysis panel says so live, while the numbers are being chosen.
- **Points correlating below 0.9 are excluded from every fit** (the engine's own
  default). Stated in the panel and counted in the run report, because a sparse
  strain map over a dense displacement map otherwise reads as a fault.
- **Strain is reported only where the displacement it describes was measured.**
  This one is ours, not the engine's: `Strain::compute()` fits at every point it
  is given, and a point whose own displacement was rejected merely excludes
  itself from its own regression and still receives a value from its neighbours.
  That value is an extrapolation into a place the instrument measured nothing,
  and it is indistinguishable downstream from a measured one. Found by exporting
  and reading the file back: a run reported "strain fitted at 1092 of the 1025
  solved points" and the file carried confident strain at 67 points whose
  displacement was not-a-number.

Two display rules follow from the same place, both in `core/FieldLayout.h`:
a strain scale is centred on zero and a displacement scale is not (strain's zero
is a physical state; displacement's is wherever the reference frame happens to
sit, so a specimen that merely translated would spend half the colours on values
that cannot occur), and scale labels are sized in SIGNIFICANT digits rather than
decimal places, since the two quantities sit at opposite ends of the number line
and any fixed decimal count fails at one end or the other.

### Sequences: one reference, many frames (2026-08-19)

A DIC test is a loading series, not a pair of photographs. The interface had
accepted several targets since the first window and measured exactly one, which
is the one place it over-promised outright.

⚑ **Frame order is a correctness problem, not a presentation one.** Sorted the
way a computer sorts strings, `frame_10.tif` comes before `frame_2.tif`, so a
twelve-frame test is measured 1, 10, 11, 12, 2, 3 ... and every individual field
is perfect while the series they form is nonsense. Nothing downstream can detect
it: each frame solves, the displacements are real, and the specimen simply
appears to jump about. `core/Sequence.h` compares digit runs as numbers, and the
project tree is sorted into that order ON IMPORT, so what is listed is what will
be measured and the two cannot disagree unnoticed.

**Every frame is measured against the original reference**, never against the
frame before it. That is the ordinary meaning of a DIC sequence and it keeps the
displacements directly comparable, but correlation degrades as the specimen
moves away from where it started, and past some deformation the engine's
reference-update tracking is what a run needs instead. Not built. Said in the
run log rather than left implicit, because a sequence that stops correlating
halfway through looks exactly like a specimen that stopped deforming.

`SequenceRunner` owns only the loop; `CorrelationRunner` stays the single-pair
engine it already was, with its tests intact. The cost of that separation is
re-reading the reference and rebuilding the solver's preparation per frame,
which is a fraction of a frame's solve and buys leaving the engine containment
boundary alone.

Two defects found while building it, both by tests that were sharpened after
first passing for the wrong reason:

- ⚑ **`CorrelationResult::cancelled` was never set when the cancel arrived
  during the last chunk of a stage** -- the outer loop's own `!m_cancelled`
  condition ended the run without passing through the break that recorded it.
  On any grid small enough to be a single chunk that was EVERY cancel, so a
  stopped run reported a clean finish over a solve it had abandoned. Now asked
  of the flag once, after the loops.
- **Closing the window mid-run aborted the process** with "QThread: Destroyed
  while thread is still running". MainWindow had no destructor stopping its
  worker.

Both were found because a test that passed was not believed: the Stop case
passed with the reach-into-the-frame removed, so it was rewritten to ask the
interrupted frame whether it knew it had been interrupted, and that is what
exposed the sentinel bug underneath.

Export writes one file per frame, numbered and zero-padded, which is how
ParaView groups a series into a time series -- unpadded, `frame_10` sorts before
`frame_2` and the animation plays out of order, which is the same trap as the
frame ordering itself, one layer out.

### Reliability: two questions, never one score (2026-08-19)

Every run now reports how far each point can be trusted, always, with no
setting to forget: tenet 9 makes this the measurement's own account of itself,
and it costs about a second per 30,000 points against a solve costing far more.
The engine's `Uncertainty2D` provides both metrics; what matters is that they
answer DIFFERENT questions and must never be collapsed into one "quality" score.

- **Noise floor (the engine's sigma, px)** = `sqrt(2*noise^2 / min(sum gx^2, sum
  gy^2))`. Image noise, estimated once globally by Immerkaer, divided by this
  subset's gradient energy. ⚑ **It never examines the target image**, so it says
  how well the measurement could ever have gone here, not how well it did: it
  cannot see decorrelation, out-of-plane motion, interpolation bias or a shape
  function too poor for the deformation. A lower bound, never a total error bar.
  In practice the map is a speckle-quality map in displacement units. Measured:
  0.0031 to 0.0040 px on the shift fixture, 0.0095 px mean on OHT-CFRP.
- **Match conditioning (the engine's beta)** probes the ZNSSD cost around the
  solution actually found, so it does judge this particular match. Dimensionless,
  with an arbitrary scale (it carries DICe's per-axis factors and depends on the
  perturbation size): comparable between points in one run, meaningless across
  runs. Measured 0.18 to 0.41 here.

Both are larger-is-worse, which is the opposite reading from every other
channel, so the screen says so.

⚑ **Strictly positive, not merely non-negative.** The engine writes -1 when it
cannot produce a value, but `POI2D::clear()` leaves both at 0, and zero is the
FLATTERING reading for each: a zero noise floor claims a perfect measurement, a
zero conditioning a perfectly sharp cost. Neither is reachable as a real result,
so anything not above zero was never written. Found by negative check, where a
`>= 0` test happily reported a noise floor of exactly zero at every point.

⚑ **A `-1` conditioning at a converged point is a WARNING, not an absence.** The
engine uses the same sentinel for "not computed" and "the cost was too flat to
probe", which looks like a conflation, but "not computed" fires only for a
failed or out-of-bounds point and neither is ever reported. So among converged
points it can only mean the probe found the cost unusable. Counted and stated,
not left blank. No engine change needed.

Naming is deliberate, to reach all three audiences at once: **"Noise floor,
sigma"** and **"Match conditioning, beta"**. The plain term says what it is, the
symbol is what a DIC-fluent reader recognises, and the word "uncertainty"
appears only in the caption beside it, where it can be qualified rather than
asserted -- a bare "uncertainty" on a colour scale is read as a total error bar
by everyone. Every channel now carries a one-sentence `fieldChannelNote()`
saying what it is NOT, shown next to the number rather than in documentation
nobody has open, and a walkthrough test fails if the noise floor reaches the
screen without it.

The noise floor is also put against the movement it qualifies
(`noiseFloorAgainstMovement()`): "at worst one part in 756 of the largest
displacement measured". A floor of 0.004 px is unreadable alone -- whether it is
excellent or useless depends on how much movement it qualifies -- and the
alternative, colouring the map against some absolute idea of a good noise floor,
would mean inventing a threshold that depends on the measurement being
attempted. This invents nothing: it is the run's own two numbers.

Noise is reported with the RESULT rather than in the image Record panel, even
though it is a property of the reference image, because estimating it needs the
engine and `core/Correlation.cpp` is one of only two files allowed to know the
engine exists.

### The field leaving the application (2026-08-19)

`Export Results (.vtu)` writes the measured field as a VTK unstructured grid,
which ParaView and FreeCAD's FEM workbench open directly. Three decisions worth
not re-deriving, plus one trap:

- **A cell exists where all four of its corners were ATTEMPTED**, not where they
  succeeded (`core/FieldMesh.h`). Geometry then says where the instrument was
  pointed and the data arrays say what came back, so a difficult specimen does
  not quietly export as a smaller one. Cell corners walk the perimeter; listed
  diagonally they make a cell every reader opens, as a bow tie.
- **Nothing unmeasured is written as zero.** In any viewer a field of zeros is a
  perfectly good blue region, indistinguishable from a real measurement of no
  movement. Rejected points and unfitted strains are not-a-number, and a
  `solved` array states the rejection outright rather than leaving it to be
  inferred from the holes. Strain arrays are ABSENT, not present-and-empty, when
  strain was never asked for: an all-not-a-number strain array reads as a
  measurement that failed everywhere.
- **The file states its own coordinate frame**, because no mesh viewer assumes
  ours. Coordinates are reference-image pixels with y DOWN, unaltered, so the
  field lays straight back over the photograph; a reader that assumes y up draws
  it mirrored and cannot tell, since the picture is plausible either way. The
  same trap as the viewport's, one file format further out. Provenance travels
  with it (tenet 10): both images with their SHA-256, the solver and its
  settings, the region, the strain fit, and the engine commit the build is
  pinned to, compiled in as `SURVIEW_OPENCORR_PIN`. Captured when the run
  STARTS, not read back from the panel at export time: the panel keeps taking
  input after a run finishes, and reading it later writes a file stating, with
  a SHA-256 beside it, a configuration that never measured anything.

Provenance is not greppable, and that is a property of the format rather than
an oversight: the file is written in binary mode, so field data is base64 and
compressed on disk. Read it with ParaView's Information panel, or any VTK
reader. A walkthrough test that searched the raw bytes failed against correct
code before this was understood.

⚑ **`vtkXMLUnstructuredGridWriter::Write()` returns 1 on failure.** Verified
against VTK 9.5 in a standalone program, not inferred: given a path in a
directory that does not exist it returns 1 (success) while `GetErrorCode()`
reports `CannotOpenFileError` and no file appears. A writer trusted on its
return value tells a user their measurement is saved when it is not. The error
code is the signal, with the file's existence checked behind it.

## License

SurView DIC is licensed **LGPL-2.1-or-later**, matching FreeCAD's choice in
the same ecosystem. Fully open-source, no proprietary/open-core tier.

Reasons:

- Protects against the scenario this project actually cares about - someone
  taking the whole application, rebranding it, and reselling it closed-source
  - about as effectively as GPL would, since there's no "larger work" for a
  whole-app fork to hide behind: the copyleft obligation to share modified
  source applies to the rebranded fork itself.
- Stays welcoming to outside contributors, who already trust and understand
  LGPL from FreeCAD, VTK, and the rest of this ecosystem.
- Leaves a narrow, accepted gap: someone could extract a specific SurView
  DIC module (e.g. the calibration workflow) into their own separate closed
  product, keeping only that module's source open. Accepted trade-off for
  the contributor-friendliness and ecosystem fit above.
- Neither OpenCorr's MPL-2.0 nor Qt's LGPLv3 essential modules constrain this
  choice - both permit combination with a differently-licensed larger work.

## ⚑⚑ How this project is run - tests, and the order things happen in

**Stated by David, 2026-08-18**, after a piece of work was delivered, verified by
driving the GUI, and reported complete while the repository contained no test
suite at all. The regime below is PlotTracer's, carried over deliberately: same
author, same standards, same expectations. Where anything else in this file
conflicts with it, this section wins.

### The one command

```bash
tools/run-tests.sh      # SurView (unit + walkthrough) AND the pinned engine
```

That is this project's `npm test`. It runs **both halves** - SurView's own CTest
suite and the OpenCorr fork's smoke tests - because SurView builds the engine
from a pinned checkout, and a green SurView suite against a broken engine is a
green suite that tells you nothing. The engine is where the measurement happens.

```bash
cd build-ninja && ctest --output-on-failure    # SurView's half alone, ~3 s
```

⚑ **The suite is headless by construction, not by invocation.** The walkthrough
tests call `show()` on a real main window; registered plainly they inherit
whatever `DISPLAY` the developer has and throw windows onto their desktop
mid-session - which is what happened the first time this suite was run.
`tests/CMakeLists.txt` wraps them in `xvfb-run -a`, so `ctest`, the hook and CI
all behave identically. They cannot merely run offscreen:
`QT_QPA_PLATFORM=offscreen` crashes the VTK OpenGL widget outright, and mapping a
click to an image pixel goes through the renderer's real projection - the thing
those tests exist to check.

### The order things happen in

1. **A failing test first.** The cases become named failing tests **before the
   first line of implementation**. Named for the CASE, not the function -
   `a region touching the image edge keeps its subsets inside`, not
   `buildPoiGrid returns cells`. A design doc reads as satisfied; a red test
   does not.
2. **Then the code**, written so it CAN be covered: pure functions taking values
   and returning values, separated from widgets, threads, files and the engine.
   Where logic cannot be reached by a test, that is a structural defect to fix,
   not a fact to accept. `core/PoiGrid.h` and `core/FieldLayout.h` exist for
   exactly this reason - both were carved out of code that was untestable where
   it sat.
3. **Then the full suite** - `tools/run-tests.sh`, engine included - after every
   major piece of work.
4. **Then commit.** Not before.

### What a green test is worth

- ⚑ **A green test proves nothing until it has been shown to fail WITHOUT the
  fix.** Break the code, watch the test go red, put it back. Record what the
  negative check showed in the test file, including what it did *not* catch -
  see the header of `tests/test_image_decode.cpp`, which says plainly that one
  of its cases passes either way, and why.
- ⚑ **A comment may say WHY a mechanism is what it is. It may NOT assert what
  the design requires unless a test of that name enforces it.** A comment
  restating a design is false evidence of compliance: every later reader,
  including its author, checks the header, sees the agreement, and stops looking.
- ⚑ **A walkthrough test may only do what something on screen tells it to do.**
  If a step needs a coordinate, an order or a precondition that no visible text
  describes, that is a **UI defect found at the moment the test is written** -
  not a detail of the test. This is what turns "could Parallel Universe David do
  it?" from a judgement call into a test-authoring constraint.
- **A test must not re-derive the arithmetic it is testing.** The first
  walkthrough helper recomputed the camera's framing instead of asking the
  viewport, assumed `ResetCamera` fits exactly (it leaves a margin), and failed
  by 16 px while the application was correct. Where a test must aim through the
  code under test, prove the property that matters independently - as
  `moving_down_and_right_on_screen_moves_down_and_right_in_the_image` does for
  the coordinate frame, asserting only the direction of travel.
- **Add coverage as part of the same change, never as an afterthought.**

### Layout

```
tests/                        SurView's suite. One executable per subject.
  test_*.cpp                  Qt Test; unit tests run offscreen.
  test_workspace_walkthrough  The e2e half: the real MainWindow, real clicks.
  fixtures/                   Committed inputs with independently verified
                              properties (row-order markers; a pair whose
                              target IS the reference shifted +3 px in x).
tools/run-tests.sh            The one command.
tools/git-hooks/pre-commit    Refuses a commit that does not build and pass.
```

Fixtures are **committed, not generated**: generating them needs a writer whose
own convention would then be the thing under test. Each carries a stated,
independently checked property, and the tests assert against that property rather
than against whatever the code currently happens to produce.

### No em-dashes, ever

**David, 2026-08-18.** There are to be no em-dashes in this code base, in code,
comments, documentation or user-facing text.

Enforced, not remembered: `tools/check-no-em-dashes.sh` runs in the pre-commit
hook and as the first step in CI. It is a check rather than a note because an
em-dash is invisible in review - it is near-identical to the hyphen it should
have been, in a diff, in a terminal and in a code font - so it is exactly the
kind of rule that erodes one character at a time. The 179 swept out of this
repository all arrived that way.

What to use instead depends on where it sits:

| where | substitute | why |
|---|---|---|
| C++ comment | `--` | matches the engine fork's existing convention |
| user-facing string | `-` | `--` in text a user reads looks like a typo |
| Markdown | `-` | |

### The hook

```bash
git config core.hooksPath tools/git-hooks      # once per clone
```

Refuses a commit touching C++ or CMake that does not build and pass SurView's own
tests. It deliberately does **not** run the engine suite (~8.5 min) - too slow to
sit in front of every commit, and it would train everyone to use `--no-verify`.
The full run stays the author's job, per step 3 above.

### Deliberately not done yet

- **Mutation testing.** PlotTracer has Stryker; the C++ equivalent (mull) needs
  LLVM instrumentation and is a real piece of work. Noted, not silently omitted.
- **Coverage reporting.** `gcov` is present; `lcov`/`gcovr` are not.
- **CI.** No workflow yet, so the hook and the author are the only gates - which
  is the arrangement that survives a fresh clone and `--no-verify` least well,
  and should be fixed.

## Roadmap: cross-vendor GPU acceleration

SurView DIC will target GPU-accelerated solvers via VTK's own WebGPU compute
pipeline (built on Dawn) instead of OpenCorr's existing GPU path. Dawn
targets Vulkan (Linux/Android), Metal (macOS), and DX12 (Windows) under one
API, covering NVIDIA/AMD/Intel and the existing Android target from a single
implementation, and reuses the VTK dependency instead of adding a second
graphics/compute API.

- This is new engineering, not a port: OpenCorr's own CUDA kernels aren't
  open-sourced, so there's nothing to adapt - the published ICGN algorithm
  would be reimplemented as compute shaders from scratch.
- Architecturally a sibling solver module alongside OpenCorr's own classes
  (mirroring their `ICGN2D1`/`ICGN2D1GPU` pattern), not a modification of
  OpenCorr - consistent with "do not merge OpenCorr into this repo."
- Open question, not yet decided: does this module live in this repo or as
  a separate library, mirroring the OpenCorr-stays-a-dependency pattern.
- VTK's WebGPU compute path is new (still landing per VTK's own 2026
  roadmap) and unproven for a workload this numerically heavy - needs its
  own prototype spike before it's a real commitment.
- Sequencing: after a working CPU-path GUI exists, not before. This is
  tenet 10's relocated mechanism - prefer the validated CPU path; add heavier
  machinery (GPU, global/regularized DIC) only on concrete need, never
  speculatively.

## Engine capability roadmap (OpenCorr fork)

OpenCorr remains the correlation engine - re-validated 2026-07-18 against
the broader headless C++ DIC/DVC landscape (DICe, ncorr_2D_cpp; both read
down to actual source, not just docs). Neither is a viable alternative
*engine*: DICe is Trilinos-coupled (an HPC framework built for clusters,
wrong shape for a single-workstation desktop app); ncorr_2D_cpp is
unmaintained since 2018, with unfixed compiler-breaking issues and a
heavier dependency footprint than OpenCorr. Both are, however, sources of
small, self-contained, BSD-3-Clause capabilities worth porting.

We maintain **katalystnord/OpenCorr**, a fork for SurView-driven capability
work (new classes mirroring OpenCorr's own patterns, or changes touching
private internals) - distinct from small, generically-useful fixes, which
go upstream directly (e.g. PR #24, the missing `<random>` include on
Linux/GCC builds).

This paragraph is where two mechanisms relocated from the Ten Tenets live:
the **upstreaming policy** (tenet 5 - small generic fixes upstream, capability
work in the fork) and **fork-idiom conformance** (tenet 8 - diverge from
upstream on *direction*, but mirror OpenCorr's own patterns inside the fork so
fixes stay upstreamable and the code stays legible).

The upstreaming policy has a failure mode we actually hit: a fix authored on a
standalone branch off upstream and merged *there* does not thereby exist on
`surview-dev` - #25 and #26 were missing from the branch SurView consumes for
two weeks. **Upstreaming a fix is not finished until it is merged back down
into the fork**, and that is checked by content, never by the commit graph
(their commits are not ancestors of `surview-dev`, so `git log` looks fine
while the code is absent). Merge upstream with `-X renormalize`: the fork
normalizes to LF via `.gitattributes` and upstream is CRLF, so without it every
file conflicts end-to-end on line endings alone.

Punch list tracked in [fork issue #1](https://github.com/katalystnord/OpenCorr/issues/1)
is complete (9/9): uncertainty quantification (sigma/beta, from DICe),
calibration - checkerboard + dot-target/donut-marker detection + stereo
epipolar quality metric (from DICe), `.cine` high-speed-camera file I/O
(from DICe's `hypercine`), gradient-free simplex matching (from DICe),
phase-correlation initializer (from DICe), RG-DIC seed-propagation
flood-fill (from ncorr_2D_cpp), sequence/reference-update tracking (from
both), conformal subset shapes - data model only (from DICe). Two small,
generically-useful fixes found along the way went upstream directly instead
of staying fork-only (PRs [#25](https://github.com/vincentjzy/OpenCorr/pull/25),
[#26](https://github.com/vincentjzy/OpenCorr/pull/26)).

Explicitly rejected as not worth porting: DICe's global/regularized
(mesh/FE) DIC as literal code (Trilinos-saturated - a future clean-room
reimplementation against Eigen, not a port, and a large lift); MPI
parallelism (wrong parallelism model for a desktop app - OpenCorr's
existing per-POI OpenMP threading already covers it); DICe's Exodus/HDF5
export (Trilinos/SEACAS-locked - the existing VTK `.vtu` decision already
covers this niche).

### Build integration (settled)

Upstream OpenCorr still ships no CMake library target, so the fork provides its
own (`5d88cea`, written with SurView as the named consumer), and SurView builds
it from a pinned checkout via `add_subdirectory`. Dependencies as built today:
Eigen 3.4.0, OpenCV 4.10.0, FFTW 3.3.10, nanoflann 1.7.0, OpenMP. Both
header-include patches Linux once needed are now upstream and carry-free -
`oc_feature_affine.cpp`'s missing `<random>` (our PR #24, merged 2026-08-01) and
the older `world.hpp` removal (upstream since 2024).

Two gotchas worth keeping:

- FFTW's *headers* (`libfftw3-dev`) are a hard build requirement, not just the
  runtime libs - without them `oc_fftcc.cpp`/`oc_phase_correlation.cpp` fail to
  compile, and every smoke test with them, since all of them include the
  `opencorr.h` umbrella that pulls in `oc_fftcc.h`.
- `add_subdirectory(OpenCorr)` makes `find_package(VTK)` fail on
  `JsonCpp::JsonCpp`. CMake validates imported link interfaces at *generate*
  time, so it surfaces after "Configuring done" and blames the `find_package(VTK)`
  line that already succeeded. Fixed with a `find_package(jsoncpp QUIET)` up
  front, like the existing MPI workaround.

## Second-pass capability candidates (DICe/ncorr deep dive, 2026-07-19)

With the original 9/9 punch list closed, a deeper pass over DICe and
ncorr_2D_cpp source (not just docs) surfaced further candidates, this time
including higher-effort ones previously passed over as "too big." Not yet
turned into fork issues - listed here for scoping, ordered by suggested
priority.

**Done:**

- Per-point failure-reason taxonomy - done, fork issue
  [#13](https://github.com/katalystnord/OpenCorr/issues/13). Named
  `StatusFlag` enum + `statusDescription()` (was DICe's `Status_Flag`),
  wired through every solver's existing bail-out points. Also added
  Hessian-invertibility rejection to ICGN (not ICLM, whose
  Levenberg-Marquardt damping already handles ill-conditioning).
- Dynamic obstruction/occlusion masking - done, fork issue
  [#14](https://github.com/katalystnord/OpenCorr/issues/14), but re-scoped
  to phase 1 (data model only) on `Subset2D`: making it actually affect
  correlation results turned out to need the same cross-cutting
  correlation-kernel integration as conformal shapes' own deferred phase 2
  below, not a small self-contained addition.

**Large, scoped and deferred - not currently planned, revisit on concrete need:**

- Topology-aware ROI - scoped, then split. Phase 1a (multiply-connected
  regions: an outer shape minus holes, point-membership only) shipped as
  `RegionWithHoles2D`, fork issue
  [#15](https://github.com/katalystnord/OpenCorr/issues/15). The rest
  (ncorr's connectivity-aware `contig_subregion_generator`, clipping a
  subset's own interior to the ROI's real connected-component topology
  right up to a hole/crack edge, plus discontinuity-aware strain fitting
  as a direct follow-on) is **not currently planned**: its value is real
  but concentrated in near-discontinuity precision (fracture-mechanics
  crack-tip fields), not the common case phase 1a already covers, and its
  cost (~6-9 weeks) is risk-concentrated in modifying OpenCorr's
  already-validated ICGN/ICLM hot loops. Revisit if a concrete need for
  precise near-discontinuity measurement comes up.
- Global/regularized (mesh/FE) DIC - scoped in depth (~6-9 weeks, same
  order as topology-aware ROI above), and **not currently planned**, but
  for a different reason than topology-aware ROI: this one is purely
  additive (a new `GlobalDIC2D` solver alongside ICGN/ICLM, touching none
  of their already-validated code) with real, if narrower-than-universal,
  value - specimens with patchy/low-texture speckle or thin membranes,
  where local subset solving simply diverges and a whole-ROI regularized
  solve borrows information from well-textured neighbors. Third solver
  family alongside local (current) and topology-aware, selected per
  correlation run, not simultaneously. Meshing doesn't need
  `artem-ogre/CDT`/Delaunay at all for a v1: OpenCorr's ROI model has no
  hole concept today, so a simple regular-grid mesh (DICe's own fallback
  for hole-free rectangular ROIs) covers the realistic case - CDT only
  becomes relevant if/when ROI holes reach meshing. Revisit if a specimen
  class needing this shows up.

**Scoped and not worth building - revisit only on concrete need:**

- Harmonic/Laplacian inpainting for ROI-edge extrapolation (ncorr's
  `Data2D::inpaint_nlinfo`). Reality-checked, not just license-checked:
  the problem it solves in ncorr (a global FFTW-deconvolution biquintic
  interpolator that's fragile to garbage pixels near a cropped ROI edge)
  doesn't exist in OpenCorr - `BicubicBspline` computes coefficients from
  a small local stencil over the whole dense image, never a
  cropped/masked region. The one real present-day use case (failed POIs
  leaking unfiltered into exported raster maps) is cosmetic/export-only,
  already mostly covered by `Strain`'s existing neighbor-regression
  output, and SurView has no GUI export consumer yet to even show the
  artifact. (License note for if this ever gets revisited: ncorr's own
  implementation links SuiteSparseQR/CHOLMOD, GPL-2.0-or-later -
  disqualifying under SurView's LGPL posture; would need a clean-room
  Eigen-sparse reimplementation, never a code lift.)

**Done:**

- Crack/discontinuity full-field diagnostic - done, fork issue
  [#16](https://github.com/katalystnord/OpenCorr/issues/16), as
  `CrackResidual2D`. Backward-warps the reference image through a
  densely re-evaluated local displacement fit and diffs against the
  real target image. Verified empirically, not just argued: the smoke
  test's synthetic two-rigid-piece image gives every placed POI
  zncc>0.9 (56/56, individually perfect correlations) while the
  residual is ~26x higher right at the known discontinuity - confirming
  it catches exactly what pointwise metrics (`StatusFlag`, sigma/beta,
  `SpeckleQualityMap`) can't. Bonus fix landed alongside: `oc_strain.cpp`
  had no degenerate-fit guard and redundantly decomposed the same
  matrix twice - fixed, plus `Strain` got its first test coverage in
  this fork.
- Virtual-extensometer/line-probe UX (DICe's `Live_Plot_Post_Processor`):
  not a port target itself (trivial/file-driven in DICe), but a roadmap
  idea - point/line probes interpolated from the same strain-fit
  machinery the item above already needs.

**Confirmed no gap, nothing to port:** DICe has no DVC/volumetric support,
no true >2-camera triangulation (hard-coded to a stereo pair), and no
adaptive subset sizing beyond OpenCorr's own; its VSG strain is already
matched by `oc_strain.cpp`. Ncorr's `std::thread` domain-decomposition
parallelism is superseded by OpenCorr's existing per-POI OpenMP threading
- confirmed by reading the code, not assumed.

## Full-branch code audit (2026-07-19)

A 3-chunk, 12-agent code review across all 8 established angles, covering
~11K lines of fork content (calibration/cine I/O, matching strategies/
diagnostics, core taxonomy/hot-path) - not scoped to a single feature's
diff, looking for cross-module issues a per-feature review wouldn't catch.

Five confirmed, unambiguous bugs (mechanical fixes, no design judgment
needed) are fixed and committed to `surview-dev`: `FeatureAffine3D`'s
wy/wz copy-paste bug, `CameraCalibrator`'s missing `Calibration::prepare()`
call and an epipolar-residual distortion-domain mismatch, `CrackResidual2D`
missing its `zncc<0` gate, `SimplexMatch2D` discarding its own convergence
flag, and an `oc_io.cpp` cluster (sentinel values leaking into exported
heat-maps, a binary/text file-mode mismatch, missing CSV bounds-checking,
a save function with no load counterpart).

Remaining findings - items needing real design judgment before fixing,
plus a longer tail of smaller/structural findings - are tracked in
[fork issue #17](https://github.com/katalystnord/OpenCorr/issues/17)
(closed 2026-07-19: all of Wave 2 and the Tier 2/3 backlog fixed or
explicitly resolved as "not pursued, with reasoning recorded"), not
itemized here.

A second full-branch audit, same methodology, run against the fork state
after #17 closed (to check whether that round's own fixes introduced
anything new and catch what the first pass missed), is tracked and closed
in [fork issue #18](https://github.com/katalystnord/OpenCorr/issues/18):
three hypercine `read_header()` crash bugs (div-by-zero, an out-of-bounds
read, a 32-bit-overflowing truncation check), a missing `num_threads()`
guard on `CrackResidual2D`, a `stereoCalibrate()` call missing
`CALIB_USE_INTRINSIC_GUESS` (silently discarding the previous round's own
per-camera intrinsics fix - confirmed empirically against OpenCV
directly), a genuine coordinate-frame mismatch in `SequenceTracker2D`'s
reference-update bank step, a `Uncertainty2D` beta-sentinel inconsistency
plus a matching out-of-bounds probe gap, a `Polygon2D::contains()`
degenerate-edge bug, missing validation in `loadTable2D`'s legacy-format
inference, a real (if modest) perf regression in `ransacAffineFit`'s
per-trial hot loop, and one more upstream PR
([vincentjzy/OpenCorr#27](https://github.com/vincentjzy/OpenCorr/pull/27)),
merged 2026-08-01 along with #24-#26.

## Not yet decided - pending from 2026-07-17 research

Two research passes (competitive review of 11 open/commercial DIC GUIs;
deep OpenCorr source review) surfaced findings not yet turned into scope
decisions:

- **Calibration module**: adopt live numeric pose-coaching UX (angle/height
  variance thresholds, color-coded pass/fail, per GOM/ZEISS), a visual
  coverage heat-map (per MatchID), and drag-to-exclude-outlier on a live
  reprojection-error chart (per MATLAB Stereo Camera Calibrator/DuoDIC -
  the strongest single calibration screen found across the competitive
  review). Engine-side detection/quality-metric is now fully implemented
  (checkerboard and dot-target, both mono and stereo - see Engine capability
  roadmap above); the GUI/UX itself is still to be designed.
- **Uncertainty quantification module**: **done 2026-08-19** (see *Reliability:
  two questions, never one score* above). Both metrics run on every
  correlation, are selectable as field channels, are qualified on screen, and
  travel in the `.vtu`. Still to be designed: a per-POI readout on hover, and
  whether to flag points whose displacement is smaller than their own noise
  floor.
- **ROI tooling**: auto-detect/threshold-based segmentation was unclaimed by
  every tool reviewed (open or commercial) - engine-side implementation done
  (`AutoROI`/`SpeckleQualityMap`, fork issues #11/#12 - an MIG quality
  map feeding an Otsu-threshold segmentation into the existing `Polygon2D`
  ROI model; SSSIG and SIFT-density/evenness are separate whole-image
  quality scalars `SpeckleQualityMap` also computes, not inputs to
  `AutoROI`'s own segmentation). Known limitation: single-region-only, no
  hole support. **The GUI is now built too** (2026-08-18): click-to-place
  polygon drawing in the viewport with an on-screen mode bar carrying its own
  Undo/Close/Cancel, an Auto-detect button over the same `AutoROI`, and the
  region restricting the POI grid via the engine's own `Polygon2D::contains()`.
  Still to be designed: the live speckle-quality indicator, and editing a
  boundary after it is committed (today it is redrawn, not adjusted).
- **VTK `.vtu` export**: confirmed a real differentiator empirically - only
  1 of 11 tools reviewed has any VTK-family export. **Built 2026-08-19** (see
  *The field leaving the application* above): points and quad cells, every
  channel the run measured, and full provenance in the file's own field data.
  Still to be designed: exporting a sequence rather than one field, and whether
  a plain CSV alongside it is worth having for spreadsheet users.
