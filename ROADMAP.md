# SurView DIC - roadmap

Where the work goes next. This is the one place open items live, so that
nothing has to be carried around as a caveat: a gap found anywhere gets written
down here and then fixed. Nothing in this file belongs in outward-facing text.

Ordered within each section by what unblocks the most.

## Working today

The whole 2D loop, end to end: import and record images with provenance, define
a region by hand or by auto-detection, correlate a sequence with optional
reference updating, fit strain, report per-point reliability, read any point off
the field, and export the result as `.vtu` or `.csv` with full provenance.
Six synthetic example sets with an exactly known answer and three real ones.

## Gaps against other DIC GUIs (reviewed 2026-09-01)

Measured against pyALDIC, iCorrVision-2D, Ncorr, and the commercial tools
(VIC-2D, MatchID, GOM Correlate, LaVision DaVis). Our three-column shape is
the idiom of the field, and these are what we do not have yet:

- **Plots over the sequence.** Nothing charts a quantity against frame, and
  no virtual extensometer or line probe. Every commercial tool has this; it is
  how a loading curve gets read.
- **Binaries.** Everyone else ships installers. We ship a build. `install()`
  rules exist now, so this is packaging rather than plumbing.
- **Stereo and 3D.** VIC-3D, GOM and MatchID measure out-of-plane. The engine
  can; the application has no path to it.
- **Image acquisition.** iCorrVision has a frame grabber and drives the
  camera. Out of scope for now, noted because it is a real difference.
- **Export styling.** pyALDIC has a preview tab for colour map, fonts and
  colour bar before writing a figure.

Where we already differ, and should keep differing: per-point reliability
qualified in words on screen (VIC-2D reports a sigma, but no tool found
states what its number cannot see); provenance recorded per image with a
SHA-256 and carried into the exported file; nothing unmeasured written as
zero anywhere; and native VTK export, which one of eleven tools reviewed had.

## Next

- **A second pass at the points that failed.** Every run reports points the
  solver rejected: 439 of 8700 on the OHT-CFRP pair, and the synthetic tension
  sequence falls to 47 per cent solved before re-anchoring rescues it. Most of
  those failures are a poor initial guess rather than an unmeasurable place on
  the specimen, and the engine now has the remedy: `RegionFit2D` fits an affine
  displacement field to the reliable POIs around a failed one, which is a far
  better starting point than the one that failed.

  The design decision this hangs on, and it has to be made before any code:
  **a fitted point is not a measured point.** Regional fitting produces an
  estimate borrowed from the neighbours, and the engine says so by resetting
  that point's correlation to zero. The honest use is therefore as an initial
  guess that is then RE-SOLVED by ICGN, so what is reported is a real
  correlation at that point, and a point that still fails stays failed. Using
  the fitted value directly as a displacement would put an interpolation into
  the field wearing the same colour as a measurement, which is the trap already
  ruled out for unmeasured points at export and for strain fitted onto rejected
  points.

  Two properties of the module to design around, both established by
  `region_fit_smoke_test.cpp` in the engine fork:

  - **It declines silently.** Given too few reliable neighbours it returns
    having touched nothing, so a declined point and an untried one are the
    same state. SurView must track the attempt itself rather than reading it
    back off the point.
  - **It has never been through our own audit.** It landed upstream on
    2026-08-14, after both full-branch audits, and upstream's own history
    around it is unsettled: a `knn_search` fix beside it was reverted within
    two commits. The smoke test is its first coverage in this fork.

  Worth doing because it attacks the number a user actually looks at, and
  because it composes with reference updating rather than competing with it:
  one recovers points lost to a bad guess, the other points lost to a stale
  reference.

## Then

- **Calibration UX.** Engine-side detection and quality metrics are done
  (checkerboard, dot target, stereo epipolar). The screen is not designed:
  live numeric pose coaching, a coverage heat map, and drag-to-exclude on a
  live reprojection-error chart.
- **Stereo and 3D.** The engine has the capability; the application has no
  path to it.
- **Packaging.** No installer, no `install()` rules, no release artifacts.
  This is the same item as shipping the examples, one layer out.

## Later

- **GPU acceleration** through VTK's WebGPU compute pipeline, cross-vendor.
  Sequenced after a working CPU-path GUI, per tenet 10. Needs a prototype
  spike before it is a commitment.
- **Global/regularized DIC** as a third solver family, for specimens where
  local subset solving diverges. Scoped, not planned; revisit on concrete need.
- **Topology-aware ROI** beyond the multiply-connected regions already
  supported. Value is concentrated in near-discontinuity precision.

## Test and tooling debt

- **Mutation testing** exists (`tools/mutants.py`) and is not routine.
- **Coverage reporting** is present but not tracked over time.
- The **walkthrough suite races with the X server's own pointer motion**;
  synthetic and real mouse moves arrive in an order that is not deterministic.
  Worked around by repeating the gesture and by taking readings from clicks.

## Fixed, kept here because the reason is worth remembering

- **Choosing a subset radius and a region blind.** Both largely decide how
  reliable a run will be, and neither said anything until a correlation had
  been sat through. The Analysis panel now estimates what the speckle inside
  the region can resolve, live, at the radius currently chosen - as a
  displacement in pixels rather than a score, because a score would need a
  threshold and any threshold for "good speckle" has to be invented.

- **A menu that promised what it did not do.** New, Open and Save Project sat
  on the File menu from the first window and reported themselves unimplemented.
  A project now saves where the images are, the region, every solver and strain
  setting and the reference-update policy. It stores paths rather than pictures,
  with each file's SHA-256, so a session that opens against images that have
  moved or changed says so instead of measuring them quietly.

- **Accuracy that nothing checked.** The synthetic sets stated the exact answer
  and the suite only ever counted how many points converged, never what they
  converged on. `test_measured_accuracy` now measures three of them against the
  file that ships beside them: a 1 px translation to within 0.01 px, a uniaxial
  tension to within a tenth of its own strain, and a one-degree rotation, where
  the correct strain is zero and a fit that mistook rotation for strain would
  read a thousand times the bound.

- **Examples that shipped with nothing to open them.** The example data was in
  the repository, installed nowhere, and unreachable from the application.
  `File > Open Example` and `install()` rules fixed it. Two findings came out
  of building it: grouping by folder alone would have joined the six synthetic
  sets into one bogus eight-frame sequence, and the menu showed "Rotation"
  twice until it was headed by family. The second was found by installing it
  and looking, not by a test.

- **A headline number set by one bad point.** The noise-floor-against-movement
  statistic quoted the worst point in the run, and on a real photograph the
  grid covers unspeckled background where a subset has no gradient energy and
  still correlates above 0.9. An excellent measurement reported itself as "at
  worst one part in 3". Now a 95th percentile: one part in 161 on the same
  data. Found only because real examples were brought in.
