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

## Next

- **Ship the examples.** They are in the repository and reachable from nothing:
  there are no `install()` rules anywhere in the project, the README does not
  mention them, and the GUI has no way to open one. A `File > Open Example`
  entry and install rules, so a fresh install can measure something within a
  minute of starting.
- **Verify measured strain against `examples/synthetic/ground_truth.json`.**
  The synthetic sets state the exact answer and nothing yet checks a run
  against it. Solve counts have been checked; accuracy has not. The rotation
  set is the sharpest test, since the correct strain there is zero everywhere.
- **Editing a committed region.** Today a region is redrawn, not adjusted.
- **Live speckle-quality indicator**, from `SpeckleQualityMap`, while the
  region is being drawn rather than after a run.

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

- **A headline number set by one bad point.** The noise-floor-against-movement
  statistic quoted the worst point in the run, and on a real photograph the
  grid covers unspeckled background where a subset has no gradient energy and
  still correlates above 0.9. An excellent measurement reported itself as "at
  worst one part in 3". Now a 95th percentile: one part in 161 on the same
  data. Found only because real examples were brought in.
