# SurView DIC - roadmap

Where the work goes next. This is the one place open items live, so that
nothing has to be carried around as a caveat: a gap found anywhere gets written
down here and then fixed. Nothing in this file belongs in outward-facing text.

Ordered within each section by what unblocks the most.

## Working today

The whole 2D loop, end to end: import and record images with provenance, define
a region by hand or by auto-detection, correlate a sequence with optional
reference updating, repair the points the first solve could not measure, fit
strain, report per-point reliability, read any point off the field, plot a
quantity against frame with virtual extensometers, and export the result as
`.vtu` or `.csv` with full provenance. Six synthetic example sets
with an exactly known answer and three real ones, and on those, a screen putting
the measured field, the stated answer and the difference between them side by
side on one shared colour scale.


## What we lack, against the rest of the field

Measured against pyALDIC, iCorrVision-2D, Ncorr, and the commercial tools
(VIC-2D, MatchID, GOM Correlate, LaVision DaVis). Our three-column shape is
the idiom of the field, and these are what we do not have yet:


- **Binaries.** Everyone else ships installers. `tools/make-appimage.sh` builds
  a single-file AppImage, `.github/workflows/release.yml` builds and checks one
  on every tag and attaches it to a draft release, and both refuse to package
  against an engine that is not the pin.

  ⚑ **What is left is REACH, not packaging.** The AppImage is built on Ubuntu
  26.04, because that is the first runner image whose VTK is a Qt6 build, and an
  AppImage needs a glibc at least as new as the machine that built it. So it
  runs on distributions of about that vintage and newer, and refuses to start on
  anything older. Fixing that means building Qt and VTK from source in CI on an
  old image, which is a real piece of work and not yet worth it. Also still
  open: a `.deb`, which needs a decision about which VTK and OpenCV versions the
  target distributions actually carry, and Windows and macOS builds, each of
  which is its own dependency problem (VTK has no MSVC binaries; a usable macOS
  .dmg needs signing and notarisation).
- **Stereo and 3D.** VIC-3D, GOM and MatchID measure out-of-plane. The engine
  can; the application has no path to it.
- ~~**A line probe.**~~ Built 2026-10-10: Line Probe on the toolbar.
- **Image acquisition.** iCorrVision has a frame grabber and drives the
  camera. Out of scope for now, noted because it is a real difference.
- **Export styling.** pyALDIC has a preview tab for colour map, fonts and
  colour bar before writing a figure.


Where we already differ, and should keep differing: per-point reliability
qualified in words on screen (VIC-2D reports a sigma, but no tool found
states what its number cannot see); provenance recorded per image with a
SHA-256 and carried into the exported file; nothing unmeasured written as
zero anywhere; and native VTK export, which one of eleven tools reviewed had.


## What others do better, and what to take from it

Reviewed 2026-09-01, then properly on 2026-09-03. Split from the list
above because the two point in opposite directions and were confusing to
read interleaved: that one is what we do not have, this one is what other
people do better than we do it. Findings live here; the work they imply is
tracked under Now, Next and Big.

⚑ **The most directly comparable tool belongs in this list and was not in it
until 2026-09-03: OpenCorr's OWN GUI**, by the engine's authors (LI Rui, REN
Haoqiang and Dr JIANG Zhenyu), now at GUI 3.0. Worth recording not because it
unsettles anything - it does not, see below - but because a competitive review
that omits the tool built on our own engine is an incomplete review. It is documented in the
library we fork, in `7_Software_with_GUI.md`, which we had never read as
competitive material. It is not rudimentary: 2D DIC single and multi-view,
3D/stereo DIC with calibration file loading, DVC on multi-page TIFF and
binary volumes, stereo reconstruction, strain, an ROI built from rectangles,
ellipses and polygons with "+" and "-" for inclusion and exclusion, POI lists
that round-trip through CSV, and subset and strain-subregion overlays for
checking settings before a run. On stereo and volumetric it is AHEAD of us,
which is worth saying plainly since those are two directions we want to go.

What it is not, and this is where our own niche actually sits:

- **Shareware, not open source.** Free for non-commercial use; the full-
  function version requires emailing the authors from an institutional
  address with your name, institution, research project, and your
  supervisor's details if you are a graduate student. GUI 2.0 is the freely
  downloadable one and has limited functions.

  ⚑ **THE RELEASE PAGE APPEARS TO OFFER SOURCE, AND DOES NOT.** The
  GUI_3.0 release lists "Source code (zip)" and "Source code (tar.gz)"
  beside the download, which GitHub adds automatically to every release as
  a snapshot of the repository at the tag, whether or not the author
  uploaded anything. It is the LIBRARY, not the GUI. Verified rather than
  assumed, 2026-09-03: that archive was downloaded and unpacked, and holds
  302 files - the documentation, LICENSE, README, `examples/`, `gpu_lib/`,
  `img/`, and a `src/` of 42 files every one of which is an `oc_*`
  correlation source. Searching the whole archive for ImGui, ImPlot, a
  `main.cpp`, window or render code, or file dialogs returns nothing. The
  single genuinely uploaded asset is `OpenCorr.GUI_3.0.7z`, 13 MB, the
  compiled Windows package. There is no separate GUI repository on the
  account either; what is there are forks of `implot`, `implot3d` and
  `portable-file-dialogs`, which are the dependencies such an application
  needs, so it is possible to see what the GUI is built FROM and not what
  it is built OF.

  Recorded at this length because "GitHub says Source code" is exactly the
  confusion the next person to look will hit, and the answer took a
  download to establish beyond doubt.
- **Windows only**, an .exe plus DLLs in one folder, and no mention of Linux
  or macOS anywhere in its documentation.

  ⚑ The REASON matters more than the fact. Dear ImGui, ImPlot and
  portable-file-dialogs are all cross-platform: nothing in that stack
  prevents a Linux build. The obstacle is only that the source is
  unpublished, so a Linux build is something only their team can produce,
  and they do not. Nobody else can port it, package it for a distribution,
  patch it, or audit it. That, rather than the feature list, is where our
  niche actually is: a gated Windows binary with no source is not adoptable
  by a Linux-based lab or by anyone who has to show how a published number
  was produced.
- **CSV out.** No VTK-family export, so no path into ParaView or FreeCAD
  without a conversion step of the user's own.
- **No account of its own reliability** that we can find in its
  documentation: no per-point noise floor or match conditioning, and nothing
  stating what a given number cannot tell you.

⚑ **THIS IS THE SAME PATTERN WE ALREADY DOCUMENTED, NOT A NEW FINDING**, and
David's point on first reading it: it changes nothing about where SurView came
from. `CLAUDE.md` has recorded the identical shape since the beginning, about
the engine's GPU path - "closed-source (no CUDA source in the repo),
Windows/NVIDIA-only ... binaries carry no verifiable license. SurView DIC
treats it as unavailable - not something we can build, fix, or ship." The GUI
is the second instance of that, not a surprise: closed source, Windows only,
binary only, licence restrictive. Where the GPU path was one component
distributed that way, the GUI is the whole application distributed that way.

So this **justifies tenet 2 again rather than challenging it.** The tenet asks
for a *cross-platform, ecosystem-native* GUI a working scientist *could
adopt*, and "adopt" is the operative word: a gated Windows shareware binary
with no source is not adoptable by a Linux lab, by anyone who has to show how
a published number was produced, or by anyone who needs to fix it. On its own
terms the tenet is straightforwardly correct.

An earlier draft of this entry claimed the tenet's supporting sentence -
"there are several good engines, but only rudimentary GUIs" - had been made
wrong by this. That was an overstatement and is withdrawn. It reads as a
statement about what is *usable*, which is what the rest of the tenet is
about, and the GUI's own authors write that "its function and usage
experience are far from perfect at the moment." Nothing here needs changing
on David's side.

⚑ **AND THERE ARE SPECIFIC THINGS IN IT WE SHOULD TAKE.** David's read on
seeing its DVC screen, and he is right. Approved in full on 2026-09-03 and
now tracked as work under Next, in "Borrowed from upstream's GUI"; what
follows is the reasoning, kept here with the rest of the competitive
assessment: the licence and the platform are one
question, and whether the interface does good things is a separate one. It
does. In rough order of what we would gain:

1. **A permanent coordinate-system legend on screen.** Their DVC view carries
   a small panel drawing O, X, Y and Z with each of the three planes colour
   coded and an "Eye" arrow showing the viewing direction. We have an entire
   section of `CLAUDE.md` about the y-down trap and a chapter of the manual
   about the same thing, and our answer has been to get it right internally
   and then explain it in prose. Theirs is better: a picture of the axes,
   always visible, removes the ambiguity instead of describing it. Cheap, and
   it would serve the 2D viewport today, not only a future 3D one.

2. **Three linked orthogonal slices with a shared crosshair**, plus a "Show
   intersections" switch. Generalised into its own entry under Next, because
   their own tabs call this "Multi-view" against "Main view" and it is a
   viewport mode rather than anything to do with volumes. X-Y at a chosen Z, Z-Y at a chosen X, X-Z at a
   chosen Y, each with a slider, and green crosshairs marking the common
   point in all three. This is the answer to "a volume renderer" in the
   volumetric entry above, and a better first answer than a rendered volume:
   slices are how people actually navigate tomography data, and it is far
   less work than isosurfaces.

3. **An ROI built from additive and subtractive primitives.** Six buttons:
   +Rect, +Ellipse, +Polygon, -Rect, -Ellipse, -Polygon, with Clear and
   Export. That is strictly more expressive than what we shipped on
   2026-09-03 (one outer polygon plus polygon holes), and it makes a hole
   the natural special case of subtraction rather than its own concept. Our
   `RegionWithHoles2D` already models outer-minus-holes, so the gap is the
   drawing UI and the shape variety, not the engine. Worth revisiting the
   region model against this before building the 3D version.

4. **A 3D region by drawing on one plane and bounding the third axis.**
   "Plane to draw ROI: X-Y / Z-Y / X-Z" together with "Range in z-axis: 0 to
   705 [voxel]". The volumetric entry above lists a 3D region as an open
   problem; this is a concrete, buildable answer to it.

5. **Draw the subset and the strain subregion on the image**, at the chosen
   size, behind "Show subset" and "Show subregion" checkboxes. We do the
   harder and arguably better thing already - we COUNT the neighbours a
   subregion will actually hold and say so in words while the numbers are
   being chosen - but "is this box big enough to contain distinct pattern"
   is a visual question, and a drawn box answers it faster than any number.
   Doing both would beat either.

6. **A live readout under the cursor at all times**: "X = 489, Y = 382,
   Z = 705; Grayvalue = 65". Ours reads out a measured point after a run;
   theirs reads the raw image value always, which is what you want while
   judging exposure, contrast and clipping before running anything.

7. **Units on every numeric field**, [voxel], [pixel], [step], consistently.
   We do this in places and not others.

8. **A log with Copy and Filter**, not only auto-scroll. Both cheap, both
   things a user of a long run actually reaches for.

What we should NOT take: everything sits at one visual weight with no
progressive disclosure, so there is no answer to "where do I start"; and
there is no account of reliability anywhere in it. Their "ZNCC of reliable
POIs >= 0.900" is a filter, not an uncertainty. Nothing states what a number
cannot tell you. That remains ours.

⚑ **It is also the best available reference for our own stereo and DVC work.**
Written by the engine's authors, its feature set shows how the engine is
meant to be driven for the modes we have not built: `7_Software_with_GUI.md`
walks through the stereo workflow step by step, including the calibration
file it loads and the primary-view convention, which is exactly the shape
the stereo entry below has to match.


### Screenshot pass, 2026-09-03

David asked for the same treatment on the others, on the evidence available.
What follows came from actually looking at pyALDIC's published screenshots and
reading MatchID's own documentation. The commercial vendors gate their manuals
and screenshots behind customer logins, so for those this is feature-level
description rather than anything seen running, and it is marked as such.

**pyALDIC** (open, and the strongest single find here):

- ⚑ **It CLIPS a subset at a region boundary and correlates the valid pixels
  only**, which it calls window splitting. Its screenshot draws the subsets
  near a boundary, the boundary as a real curve, and a zoomed subset with its
  pixels labelled "Valid" and "Masked". **This corrects something we had
  written down as impossible**: the manual said a boundary's softness "is
  unavoidable" and that "clipping the subset at the boundary would only
  starve it." That was wrong and is now corrected. It is a genuine technique
  with a genuine cost, not an impossibility.

  Directly buildable: fork issue #14 already carries subset masking as a data
  model on `Subset2D`, with phase 2 - making it actually affect the
  correlation - deferred. This is the reason to finish it, and it is the real
  third option beside the two the region work considered (exclude the point,
  or count it and report). It matters most at a hole, where the pixels
  outside are background that does not move with the specimen.
- **Displacement drawn as vector arrows over the magnitude field.** We have no
  vector view at all: displacement is offered as magnitude, u, or v, each a
  separate scalar map. Direction is immediately legible from arrows and is
  not legible from any scalar map, and a rotation or a shear reads instantly
  in a quiver plot.
- **Four channels shown at once**, in one figure, rather than switched between
  with a selector. Another instance of the multi-panel viewport entry above,
  with a third kind of content: not two views and not three slices, but the
  channels of one field side by side.
- **A ground-truth comparison presented in-app**: measured against stated,
  side by side, on a SHARED colour scale, with the RMSE in each panel's own
  title. We ship six synthetic sets with an exactly known answer and a
  `ground_truth.json` beside them, and we check them in the test suite - but
  a user who opens one of those examples is shown none of it. The data is
  already there.

**MatchID** (documentation only, not seen running):

- ⚑ **It reports SPATIAL resolution as a quantity, not only displacement
  resolution.** These are two different things that trade directly against
  each other - a larger subset resolves displacement more finely and resolves
  spatial detail more coarsely - and we report only the first, as the noise
  floor. This is exactly the shape of the "two questions, never one score"
  argument we already make about the noise floor and the conditioning, one
  level up, and we are currently making the mistake we warn about. Chapter 14
  of the manual discusses the trade qualitatively without naming spatial
  resolution as a reportable number.
- A **performance module** doing a "convergence study of signal versus noise
  performance according to user settings": a systematic sweep of what the
  settings do to the result, where our live speckle estimate answers a single
  point of that space.
- An **image quality assessment module** as a step of its own, before
  correlation.
- **Temperature import**, so a thermal field can be brought in alongside the
  measurement. Independent commercial confirmation that the second-modality
  registration entry above is a real need and not only ours.
- An **FEA validation module** comparing measurement against simulation. Our
  entire `.vtu` rationale is that loop, and we do the export half only.
- **Batch mode** across 2D, stereo and calibration.

⚑ **AND A CORRECTION TO WHAT WE CLAIMED ABOUT OURSELVES.** The upstream-GUI
notes above end by saying an account of reliability "remains ours." Against
upstream's GUI that holds. Against the commercial field it does not: MatchID
sells specifically on metrology, listing confidence margins, resolution
quantification, error evaluation and noise assessment. What is genuinely
ours is narrower and should be stated narrowly - being open, cross-platform
and licence-clean, and the specific practice of printing beside every number
the sentence saying what it cannot tell you. Not caring about metrology.
Nobody has a monopoly on that.


⚑ **THIS REVIEW IS A FEATURE COMPARISON, NOT AN INTERACTION ONE, AND THAT IS
A GAP IN OUR OWN METHOD.** David asked on 2026-09-03 whether the commercial
tools had been looked at the way upstream's GUI just was. They have not. The
asymmetry is worth stating plainly, because it is about how we reviewed and
not about what we found:

- **Upstream's OpenCorr GUI** got a full interaction pass, and six adoptable
  items came out of it. That happened because a screenshot of it running was
  put in front of us, not because it was reviewed more diligently.
- **Calibration** is the one area where a real interaction pass WAS done, in
  the 2026-07-17 review of eleven GUIs, and it produced exactly this kind of
  finding: live numeric pose coaching with variance thresholds and
  colour-coded pass or fail (GOM/ZEISS), a visual coverage heat map
  (MatchID), and drag-to-exclude an outlier on a live reprojection-error
  chart (MATLAB Stereo Camera Calibrator and DuoDIC, described then as the
  strongest single calibration screen found anywhere in the review). Those
  are recorded in `CLAUDE.md` and still unbuilt.
- **Everything else** across VIC-2D, MatchID, GOM Correlate and LaVision
  DaVis was compared on CAPABILITY only: what they have that we lack. How
  their screens actually work, what they show beside a number, what they do
  while a run is in progress, how they get a novice to a first result - none
  of that was studied.

Two honest constraints on fixing it. We do not own any of the four commercial
tools and cannot run them, so the evidence would be vendor manuals, published
screenshots, training and webinar recordings, and screenshots inside papers:
real, but weaker than a running application. The open tools are the opposite
case and the better first target precisely because we CAN drive them - Ncorr
in MATLAB, pyALDIC and iCorrVision in Python, and upstream's own GUI 2.0,
which is freely downloadable with limited functions.

Worth doing, and worth doing on the runnable ones first. The calibration
findings above are the proof that this kind of pass yields concrete work
rather than impressions.


## Waiting on a decision

Open questions that are David's rather than the author's, each one already
researched to the point where the choice is the only thing left. The detail
lives where the finding does; this is the list, so none of them has to be
rediscovered. Raised 2026-09-14, out of the first cross-validation run.

**1. ~~The grid row and column that can never be measured.~~** Decided
2026-10-09 (option 1, inset pinned by a test, with our own wording for the
second kind) and done the same day. The verified mechanism is under *Test and
tooling debt*.

- The grid now starts at subset radius + 2 and ends at width - 3 - radius
  (`kGridMarginBefore`, `kGridMarginAfter` in `core/PoiGrid.h`).
  ⚑ **Plus 2, not the plus 1 written down here as option 1**, found by the
  case written for it: at radius + 1 a point's outermost sample sits exactly
  on the interpolator's limit with no room at all, and the top point failed on
  a frame that does not move in y, because the solver's own iterate wandered a
  few thousandths of a pixel outward. `tests/test_grid_margin.cpp` asks the
  engine on an exact +0.5 and -0.5 px translation that every inset point
  solves both ways, so the copied margin cannot drift from it unseen.
- Where the specimen's movement carries a subset past the edge anyway, the
  readout and the run report say "the specimen carried this subset past the
  edge of the image" instead of the engine's "subset out of image bounds, or
  invalid initial guess" (`subsetCarriedPastTheEdge()`), judged by the room on
  each side against the estimated movement, so a refusal in the middle of the
  image still keeps the engine's words.
- ⚑ **Four engine-backed cases had been leaning on the defect.** Each needs
  some points to fail and checks that it got them, and every failure they ever
  saw was in the unmeasurable edge row and column; two of their comments
  credited "one iteration against a strict threshold", which once the grid was
  fixed failed nothing at all. They run on
  `tests/fixtures/shift_target_occluded.tif` now: the shift target with a
  48 x 48 block painted flat, a failure that is real and stated.

**2. ~~The engine pin is two commits behind the checkout.~~** Decided and done
2026-09-28 (`989e00c`): the pin moved to `90c4ea2`, which also brings in
upstream's RegionFit example fix, and `tools/run-tests.sh` passed in full
against it. Kept here so the numbering of the other two does not change.

**3. ~~The other half of the cross-validation is still not done.~~** Done
2026-10-01, as an internal check. What is reported here is SurView against the
published reference answers: on the 24 synthetic frames and the 11 DIC
Challenge Sample 3 frames, 0.0016 to 0.0050 px RMS per component on every frame
and under 0.01 px at the 95th percentile, recovered points included.
`tools/measure/` writes the field as a table for checks of this kind. The one
finding that is ours to act on is the next item.

**4. ~~A subset in clipped background is reported as solved.~~** Decided
2026-10-09: the remedy is the region of interest, not a rule about the
answers. Found by the same check: on the real tension specimen without holes,
five points in glare off the background, their subsets 99.6 per cent at 255,
converge with a correlation of 0.96 to 1.00 and a displacement of zero.

- ⚑ **No rule about the answers catches this reliably, because the answers
  look like measurements.** An outlier test against the neighbours has none to
  compare with; one against the whole field sees a reading of zero, which a
  tension specimen also produces at its fixed grip; a sparseness rule removes
  isolated points whatever isolates them, and the same image holds two genuine
  stationary measurements on the dark background (sigma 0.16 px) that any such
  rule would take with the glare. Background, grips and glare each fool the
  correlation in their own way, and leaving them out covers all of them at
  once.
- **So a field measured with no region says so** (done 2026-10-09), beside the
  field, in the run report and on the Run button: "No region was drawn, so the
  whole image was measured, including anything in it that is not the
  specimen. Use Define ROI to measure only the specimen."
  (`wholeImageMeasuredNote()`, pinned by
  `a_field_measured_without_a_region_says_it_covers_the_whole_image`).
- What a region cannot settle, glare ON the specimen, is under *Next*.

## Now

Small, well understood, and each one changes what a person sees the next
time they open the application. Days rather than weeks. Nothing here needs
a design pass first.

- **Borrowed from upstream's GUI, smallest first.** David approved the whole
  list on 2026-09-03. The competitive section above says why each one is
  better than what we do; this is the commitment to do them. Five of the six
  are small. The first three are DONE (2026-09-03 and 2026-09-08); units on
  every numeric field and Copy and Filter on the log are DONE too (2026-10-07).

  1. ~~**A permanent coordinate-frame legend in the viewport.**~~ DONE
     2026-09-03. A small panel
     drawing the axes with their directions, always visible. We have a
     section of `CLAUDE.md` and a chapter of the manual devoted to the y-down
     trap, and our answer so far has been to get it right internally and
     explain it in prose. A picture of the axes removes the ambiguity instead
     of describing it, and it serves the 2D viewport today. Small, and the
     best ratio of value to work on this list.
  2. ~~**Draw the subset and the strain subregion at their chosen size**,
     behind their own switches, over the reference image.~~ DONE 2026-09-08.
     ⚑ IN ADDITION to
     counting the neighbours a subregion will hold, which we already do and
     they do not: the count is the rigorous answer and the drawn box is the
     fast one, and "is this big enough to contain distinct pattern" is a
     visual question. Neither replaces the other. The panel now states the
     count at all times rather than only when it is too small.
  3. ~~**A live readout of the raw image value under the cursor**, before and
     independent of any run.~~ DONE 2026-09-08. Ours reports a measured point after a
     correlation; this reports what the camera recorded, which is what a
     person wants while judging exposure, contrast and clipping. Pairs with
     the clipping figures the Record panel already computes.
  4. ~~**Units on every numeric field**, consistently.~~ DONE 2026-10-07.
     Every numeric control on the Analysis panel carries a unit, a symbol or
     a counted noun inside the box ("zncc < 0.70", "20 rounds", "1
     iteration"), and `every_number_on_the_analysis_panel_says_what_it_
     counts_or_measures` walks them all, so a new control cannot arrive bare.
  5. ~~**Copy and Filter on the log**~~ DONE 2026-10-07. A filter field
     above the log (case-insensitive, stated as "N of M lines" while it
     narrows) and a Copy button that copies what is shown. ⚑ Found on the
     way: the log silently kept only its last 1000 lines, and the lines a long
     sequence loses first are the settings it was measured under. It keeps
     every line now.


- **The plot's axis titles are clipped in a short Plot panel.** Seen
  2026-10-10 at a 1300 x 900 window: the y title reads "placement magnitu"
  and the x title is cut through its middle. The chart's borders are fixed
  pixels (`SetBorders(66, 44, ...)`); they want sizing from the fonts, or the
  titles shortening when the panel is short.
- ~~**The axes legend covers the lowest labels of the colour scale**~~ DONE
  2026-10-10. The scale stood at fixed fractions of the viewport, the corner
  the legend owns, and the image fit knew about neither. `placeScaleBar()` in
  `core/ViewFit.h` puts it in the right-hand column between the field bar and
  the legend, or beside the legend when that column is too short, and the
  picture is fitted to its left. In a viewport narrower than the scale it is
  hidden, and the field bar still states the range in words.
  ⚑ Found on the way: a legend wider than the viewport has a NEGATIVE left
  edge, which the first version took for "no legend" and put the scale under.
  Also in narrow viewports: the selector now gives way (its text elided, the
  full name on its tooltip) instead of clipping the row to "Showir", "D" and
  "Arrc", and the legend's caption wraps narrower instead of losing the first
  letter of every line. Pinned down to a 320 px viewport; below about 250 px
  the row still collides, which no usable field view reaches.

  What is deliberately NOT on this list, from the same review: their flat
  single-weight control layout, which answers nothing about where to start,
  and the absence of any reliability account. Those stay ours.

## Next

Things that change a measurement, or that everything after them depends on.
Understood well enough to start, large enough to need their own care.

- **Glare on the specimen itself.** Left over from decision 4. The first step
  is DONE (2026-10-10): every point carries the share of its reference subset
  at the darkest or brightest value the image holds (`core/Clipping.h`),
  stated in the point readout (a warning above one half), counted among the
  solved in the run report and on its own map, "Clipped share of subset",
  drawn on a fixed 0 to 100 per cent scale so a clean specimen does not look
  like glare everywhere, and written to the `.vtu` and the CSV.
  ⚑ Judged against the image's OWN extremes, not the type's: the test
  fixtures paint their glare at 250, so a rule judging against 255 finds none.
  **Decided 2026-10-10: flagged, not refused.** David asked instead for a
  number for the view and the problem areas highlighted, which became the
  Cautions switch (`core/Cautions.h`, done the same day): every caution this
  application states about a measured point, marked over whichever map is
  shown and counted over the whole field or what is in view. ⚑ A COUNT, never
  a confidence: nothing here converts to a probability that a point is right.
  The question it replaced was whether a mostly clipped subset should be
  refused, with a reason of its own. On the glare fixtures five or six edge
  points (depending on the iteration limit) are mostly glare and still solve; on the tension example the glare subsets
  sit at 87 per cent or more and the specimen's at about 10 per cent or less.

- **Learned from the rest of the field, 2026-09-03.** The reasoning and
  sources are in the screenshot pass above; this is the commitment to act on
  it. Ordered by what it changes about the measurement, not by size. The
  fourth thing from that pass, showing measured against known for the shipped
  examples, was the smallest and is DONE (2026-09-08): Analysis > Compare with
  the Known Answer, three panels on one shared colour scale.

  1. ⚑ **Masked subsets at a boundary or a hole.** Correlate a straddling
     subset on its valid pixels only, instead of on a full subset that
     reaches into territory the region deliberately excluded. pyALDIC does
     this and calls it window splitting; we had written it down in the manual
     as impossible, which was wrong and is now corrected there.

     This is the third option the region work never considered. It had two:
     exclude a point whose subset reaches a hole, or count it and report it,
     and we chose to report because applying a stricter rule at a hole than
     at the outer boundary would be an unexplainable difference. Masking
     dissolves that dilemma - the subset simply stops using pixels that are
     not specimen, at both boundaries equally, and the count we currently
     report becomes a count of points that were CORRECTED rather than a
     caveat about points that were not.

     Buildable rather than speculative: fork issue #14 already carries subset
     masking as a data model on `Subset2D`, with phase 2, making it affect
     the correlation, deferred at the time because it needed cross-cutting
     work in the correlation kernel. This is the reason to finish it. It
     matters most at a hole, where the excluded pixels are background that
     does not move with the specimen at all and therefore drag the answer
     toward zero.

     Needs its own honest accounting: a masked subset has fewer pixels, so
     its noise floor is genuinely worse than a full one's, and that has to
     show up in the reliability figures rather than being hidden by the
     repair. A point measured on half a subset is not as good as a point
     measured on a whole one, and it should not claim to be.

  2. ~~⚑ **Report SPATIAL resolution, not only displacement resolution.**~~
     DONE 2026-10-07, `core/SpatialResolution.h`. The Analysis panel and the
     run report state the length a displacement is averaged over (the subset,
     2r + 1) and the virtual strain gauge (the lattice span the strain fit
     uses, plus one subset), each beside the controls that decide it, and
     Chapter 14 of the manual names both. ⚑ Stated as lengths AVERAGED OVER,
     never as a resolved feature size: the Star 1 measurement under *Test and
     tooling debt* puts the 90 per cent amplitude point near four subsets, so
     "33 px resolution" would claim what the instrument does not deliver.
     Written into the `.vtu` and `.csv` provenance as well (2026-10-08), so a
     file read outside the application states both halves of the trade.
     MatchID reports both. They are different quantities that trade directly
     against each other - a larger subset resolves displacement more finely
     and spatial detail more coarsely - and we report only the noise floor,
     which is the displacement half. Reporting one of a trading pair is
     exactly the failure our own "two questions, never one score" argument
     warns about, one level up, and we are currently committing it.

     Wants: a stated spatial resolution derived from the subset size and
     step, shown beside the noise floor and in the run report, with the same
     treatment every other number here gets - a sentence saying what it is
     not. And Chapter 14 of the manual, which today discusses the trade
     qualitatively without naming spatial resolution as a number a reader
     could be told, should name it.

  3. ~~**Displacement as vector arrows over the field.**~~ DONE 2026-10-08,
     `core/DisplacementArrows.h`: an Arrows switch beside the channel
     selector, arrows only at measured points, thinned by whole grid steps
     as the grid closes on screen, drawn to a scale the bar states. Found by
     screenshot on the way: the arrow note grew the bar back over the field,
     and "0.83 times their true length" did not read; both are cases now. We offer magnitude, u
     and v as three separate scalar maps, and direction is legible from none
     of them. A quiver overlay makes a rotation or a shear obvious at a
     glance. Needs the usual care: arrows only where a point was measured,
     never a zero-length arrow standing in for a rejected point, and a
     density that thins with zoom rather than turning the field black.


  Considered from the same pass and NOT taken now, recorded so the decision
  is visible rather than forgotten: a settings sweep in the manner of
  MatchID's convergence study, where our live speckle estimate answers one
  point of that space; an image-quality assessment as a step of its own
  before correlation, most of which the Record panel already computes and
  does not present as a verdict; batch mode; and comparison against an FEA
  result, which is the other half of the loop our `.vtu` export exists to
  serve and which is probably better done in ParaView than reimplemented
  here.

- **An ROI of additive and subtractive primitives.** Rectangle, ellipse and
  polygon, each in a plus and a minus form, as upstream's GUI offers them.
  Strictly more expressive than the outer polygon plus polygon holes shipped
  on 2026-09-03, and it makes a hole the natural special case of subtraction
  rather than a concept of its own.

  Sized out of Now deliberately: it revisits a region model that is days old
  and already carries tests, serialisation, a drawing mode and an engine
  boundary built against it. Worth doing before the 3D region in the
  volumetric entry, so that one generalises a good model rather than a narrow
  one, and before masked subsets above, which will want to ask the same
  boundary a more detailed question.

- **An optional multi-panel viewport, with linked panning and a shared
  crosshair.** David's, 2026-09-03, on seeing that upstream's own GUI labels
  its three-orthogonal-slice screen "Multi-view" against "Main view": those
  are tabs on one viewport, not a feature of volumetric data. The same
  abstraction serves every case we have or want:

  | case | the panels are |
  |---|---|
  | 2D DIC, today | reference against target |
  | Stereo, roadmap | view 1 against view 2, and N views after that |
  | Volumetric DVC, roadmap | the X-Y, Z-Y and X-Z slices |

  ⚑ **It earns its place on the 2D tool we already have, before either of
  those lands.** A reference and a target side by side, panning and zooming
  together, is how a person actually sees what went wrong on a hard frame:
  lighting that shifted, a specimen that moved out of plane, a region that
  decorrelated, a highlight that crossed the pattern. Today those can only be
  inferred from a field full of holes, or found by flicking between two
  images in the project tree and trying to remember what the last one looked
  like.

  **Optional, and a tab rather than a replacement.** A single large panel is
  the right default for placing a region or reading a point, and the multi
  panel view is for comparing. Upstream gets this right by making it a
  toggle, and it is worth copying as a toggle rather than as a layout.

  ⚑ **One thing we could do in it that they cannot, and it falls out of
  having measured the field.** In a reference-against-target pair, a crosshair
  at the same PIXEL in both panels is not the same material point: the point
  moved, which is the entire subject of the measurement. Because the
  displacement at that point is known, the crosshair can instead follow the
  MATERIAL point - reference position in the left panel, reference plus
  measured displacement in the right - so the two crosshairs sit on the same
  piece of specimen rather than the same coordinate. Where the point was
  rejected there is no displacement to follow, and the honest behaviour is to
  say so rather than to leave the right-hand crosshair at the unmoved
  coordinate, which would quietly assert a displacement of zero (Chapter 3 of
  the manual, one interaction further out).

  Shares its whole shape with the N-view project model recorded under stereo
  below: panels are a list, not a left and a right.

- **A reference and target photographed in SEPARATE sessions.** Today the run
  refuses a target whose pixel dimensions differ from the reference: "They must
  describe the same pixel grid." That is right for a fixed rig, where a size
  change means somebody altered the setup mid-test, and wrong for anything
  photographed twice by hand.

  The size check is the lesser half. Two sessions differ in scale, rotation and
  perspective, and the first pass is an integer-pixel translation estimator that
  cannot bridge any of it. What is needed is a coarse GLOBAL pre-alignment
  before correlation: estimate a similarity or homography between the two
  photographs, warp, then solve as usual. The fork already has the pieces --
  `SIFT2D`, `FeatureAffine2D` and `ransacAffineFit` do exactly that step for
  another purpose -- and printed fiducials would make it robust rather than
  merely likely.

  Unblocks the recovered-strip direction below, and hand-held or phone capture
  generally.

- **One visual language for the kinds of picture we will be drawing.** By the time the entries
  above land there will be four views that all look three-dimensional and mean
  quite different things: today's flat 2D field; the pseudo-3D surface, where
  height is not a measurement at all; stereo, a 2.5D surface carrying 3D
  displacement; and multi-view, the same but sometimes more. David's constraint
  on the pseudo-3D surface was the first instance of a problem that needs one
  scheme across all four rather than a rule invented per view.

  What the scheme has to settle, once, before the second of them is built: which
  views are shaded and textured and which are not, which carry a metric axis
  triad and which carry a unitless one, what an exaggeration factor looks like
  on screen, and what each view is called in words. The rule underneath is the
  one this project already keeps everywhere else -- a picture must not be able
  to be mistaken for a more trustworthy picture than it is.

- **A pseudo-3D strain surface**, drawing the field as a height map with strain
  as the height. VTK does the work with `vtkWarpScalar`, which we already link.
  Worth having because the eye reads HEIGHT far better than colour for ordering
  and for local gradient: a colour map flattens a plateau and a slow ramp into
  nearly the same picture, and a surface separates them at a glance, which is
  exactly what matters at a strain concentration or a crack tip. A standard
  idiom elsewhere (ParaView's Warp By Scalar, MATLAB's `surf`), and absent from
  the open DIC GUIs.

  ⚑ **THE CONSTRAINT THIS HANGS ON, and it is David's: it must look VERY
  DIFFERENT from a real stereo/3D view.** The height is strain drawn in pixels,
  a quantity with no spatial meaning, and the picture it makes is a convincing
  likeness of out-of-plane displacement, which is the one thing 2D DIC is blind
  to and precisely what the stereo work above will really measure. Two views
  that look alike and mean different things is the worst outcome available here,
  and it gets worse once both exist in the same application.

  Concrete means, to be settled when it is built rather than left to taste:

  - The real stereo view is a SHADED SOLID with the photograph mapped onto it,
    under a perspective camera, with a metric axis triad. The pseudo-3D surface
    is none of those: an unshaded lattice or wireframe, orthographic, with no
    photographic texture at all.
  - Its vertical axis is labelled in the STRAIN's own units and never in
    millimetres or pixels, and the exaggeration factor is stated on screen
    beside it. Without that, two pictures of the same data look like two
    different specimens, and the factor is a number nobody can infer.
  - The words "out-of-plane" and "height" do not appear in its labels.

  Two things to design around. Unmeasured points are not-a-number, and a
  not-a-number height throws geometry to infinity, so the existing rule that a
  cell exists where all four corners were ATTEMPTED needs a companion rule for
  heights. And a peak occludes what is behind it, including the holes we are
  careful to keep visible everywhere else, so the flat map has to stay one
  gesture away rather than being replaced.

- ~~**A line probe.**~~ DONE 2026-10-10. Line Probe on the toolbar: two clicks
  draw a line, and the Plot panel shows the map on screen along it, for the
  frame on screen, following either when it changes (`probeProfile()` in
  `core/Series.h`). Sampled one grid step apart or closer, both ends included,
  each sample interpolated from the four grid points around it; the line carries
  a tick at every sample, from the same function the profile uses. ⚑ Any of
  the four not carrying the CHANNEL -- not merely unconverged: strain the fit
  declined, a noise floor never established -- makes a gap, not a value, and a
  two-state map has nothing between its states to read. Saved with the session;
  "Save plot data" writes it against `distance_px`. One probe at a time:
  placing another replaces it.

- **Registering a second image of the same specimen through its own speckle.**
  A general capability, arrived at from a specific case: reading a colour
  indicator layer that a monochrome DIC camera cannot resolve. Photograph or
  scan the specimen separately, in colour and at leisure, and register that
  image to the measurement by correlating the SPECKLE ITSELF. The pattern is
  already a near-perfect registration target; that is what it was designed to
  be. Any second modality then lands in the same coordinate frame as the strain
  field with no fiducial hunting.

  ⚑ **It registers to the FINAL frame, not the reference.** A scan taken after
  the test shows the sticker deformed, so it corresponds to the last measured
  state, and reaching reference coordinates means mapping back through the
  displacement field we measured. That chain is only available because we
  measured the deformation, and getting it backwards would put the second
  modality in the wrong place by exactly the specimen's own displacement.

  Two further things it is not: the registration is a WARP rather than a shift,
  since a flatbed scan is orthographic and flat while a camera image carries
  perspective, lens distortion and any curvature of the specimen. And a
  registered field from another instrument does not deserve the same visual
  authority as a measured one -- a pressure film good to about ten per cent
  must not look like a strain field with a stated noise floor of 0.004 px. That
  is the visual language entry below, extended to a fifth kind of picture.

- **Packaging, the remaining half.** The AppImage exists
  (`tools/make-appimage.sh`, since 2026-09-08) and runs on a machine that has
  never heard of VTK. What is still missing is PUBLISHING it: the one
  release so far, v2026.09.0, carries the manual and no binary, so the next
  one should carry the AppImage and let somebody download the tool rather
  than build it. Then the `.deb`, which first wants a survey of what
  the target distributions ship, and Windows and macOS builds.

## Big

Weeks each, and each wants a design pass before any code. They are grouped
here so that the two lists above stay readable as lists of work rather than
as a backlog with everything in it.

- **Stereo and 3D.** Now the largest gap in the tool, and the engine side of it
  is already done. Confirmed by reading the fork rather than our own notes:
  `Calibration` (intrinsics, extrinsics, projection matrix, undistortion maps),
  `CameraCalibrator` (our port from DICe: checkerboard AND dot-target detection,
  mono and stereo), `Stereovision` (fundamental matrix, `reconstruct()` from a
  matched pair to a `Point3D`), `EpipolarSearch`, and `POI2DS`, which carries a
  3D deformation vector, 3D reference and target coordinates and a 3D strain
  vector. Five worked demos ship with real stereo data, one of them 3D strain.

  What is missing is all ours, and it is three pieces rather than one:

  1. **Calibration UX**, which is the first half of this item rather than a
     separate one: nothing else can start until a pair of cameras can be
     calibrated from the screen. The engine-side detection and quality metrics
     are done (checkerboard, dot target, stereo epipolar); the screen is not
     designed. From the 2026-07-17 competitive review, the parts worth having
     are live numeric pose coaching, a coverage heat map, and drag-to-exclude
     on a live reprojection-error chart.
  2. **A project model of N VIEWS, not "left and right".** Today a project is
     one reference and a list of targets. A stereo project is several
     synchronised sequences plus a calibration, and every place that assumes a
     single image list has to learn the difference. Pairing is a correctness
     matter of the same kind as frame order: two views silently misaligned by
     one frame produce a plausible, entirely wrong shape.

     ⚑ **Model it as N views from the start, and calibration as a camera
     NETWORK rather than a pair.** Two cameras is then simply N = 2. This costs
     nothing now and is a rewrite later, because the multi-view entry below
     needs exactly that shape and would otherwise arrive as a parallel universe
     beside the stereo one.

  3. **What of the six strain components is actually MEASURED.** `POI2DS`
     carries a `StrainVector3D` with exx, eyy, ezz, exy, eyz and ezx, but a
     surface method cannot see through the thickness. Whether ezz is measured,
     inferred from an incompressibility assumption, or simply left zero has to
     be read out of the engine's own 3D strain code before any of it reaches a
     screen. A slot in a struct is not a measurement, and under tenet 9 a
     component we cannot substantiate must be absent rather than zero -- the
     same rule already keeping unfitted strain out of the 2D field.
  4. **A 3D view**, for a real out-of-plane measurement. Bound by the visual
     language entry below, which governs this one and the pseudo-3D surface
     together.

  ⚑ **What this actually measures is 2.5D geometry with 3D displacement**, and
  the engine says so in its own type name: `POI2DS` derives from `Point2D` and
  carries a `Point3D` alongside. A stereo point is indexed by a 2D position in
  the master view, so z is a FUNCTION of (x, y) and the surface is single-valued
  by construction. The displacement on that surface is fully three-dimensional,
  which is what lets it see out-of-plane motion at all. The field calls this
  "3D DIC" and users will search for that term, so we should use it -- and say
  plainly what it measures, the same way the noise floor states what it is not.
  Genuinely volumetric measurement is DVC.

  Volumetric DVC is in the engine as well and is NOT part of this item; it wants
  volume data we have no way to load and a renderer of its own.

- **Volumetric DVC, from tomography.** David's, 2026-09-03. Digital volume
  correlation follows subsets of VOXELS through a three-dimensional image of a
  specimen's interior, usually an X-ray CT reconstruction, and reports
  displacement and strain inside the material rather than on its surface. The
  material's own microstructure supplies the texture, since nothing can be
  painted on an interior.

  Genuinely different from everything above, and worth being clear about why:
  stereo and multi-view both measure a SURFACE (2.5D geometry, 3D
  displacement). This is the only entry that measures the inside, and it is
  the only one where "3D" is the whole truth rather than a convention.

  The engine carries real building blocks, checked rather than assumed:
  `Image3D` loads a volume from a binary file or a multi-page TIFF, `POI3D`
  carries a 3D deformation vector and a six-component 3D strain vector, there
  is a `DVC` base class beside `DIC`, and `examples/dvc/` ships working
  demos with real volume data (`al_foam4_0.bin`, a torus set) driven through
  FFTCC plus ICGN, and a strain example. Upstream's own GUI already exposes
  DVC, which tells us the library APIs are sufficient for a real workflow.

  What is missing is ours, and it is more than the other entries need:

  1. **Volume data in, with provenance.** Today the record pillar describes
     2D images. A CT reconstruction is a stack or a volume file with its own
     voxel spacing, bit depth and orientation conventions, and getting that
     wrong is the volumetric version of the row-order trap: a volume read
     with the wrong axis order gives a field that looks plausible and is
     transposed. Formats worth supporting: multi-page TIFF and raw binary
     (what the engine already reads), and probably DICOM, which is what a
     scanner actually emits.
  2. **A 3D region of interest.** A polygon with holes does not generalise to
     a volume. This wants a box, or a mask volume, and the honest version of
     "the subset must lie wholly inside the data" in three dimensions.
  3. **A volume renderer.** VTK does this well and is already a dependency,
     but showing a displacement field INSIDE a solid is a genuinely different
     visualisation problem from drawing one on a photograph: slices, clipping
     planes, isosurfaces, and the same care about never letting an unmeasured
     voxel render as a measured zero.
  4. **Cost.** A volume is not an image. A 1000-cubed volume is a billion
     voxels, and a correlation over it is a different order of computation
     from a 2D field. The chunked-progress-and-cancel trick still applies,
     but memory does not: the engine's DVC API takes resident volumes.

  Sequenced after stereo, for the same reason multi-view is: it reuses the
  N-view project model, the calibration screen has nothing to do with it, and
  the 3D view it needs is a superset of the one stereo builds. Worth writing
  down now because it changes one decision early, exactly as multi-view did:
  the record pillar should not assume an image is two-dimensional.

- **Multi-view 3D from ONE camera, for slow or static tests.** David's, and the
  qualifier is the whole idea: stereo needs two synchronised cameras only
  because the specimen is moving. Take the motion away and synchronisation stops
  mattering, so one camera moved to several positions and fired sequentially
  gives the same thing. That is photogrammetry, and creep is close to an ideal
  case -- the deformation timescale is hours and the capture takes minutes.

  Not a lesser class than stereo. It is the SAME class -- 2.5D surface, 3D
  displacement -- acquired differently, and in two respects it is better:

  - **Redundancy.** A static specimen can be photographed from eight or ten
    positions, and a bundle adjustment over ten views is better conditioned
    than a single pair. This is a real accuracy advantage, not a compromise.
  - **It can exceed 2.5D where stereo cannot.** A stereo pair sees one aspect.
    A camera network can walk around a CURVED specimen -- a cylinder, a pressure
    vessel, a wrapped panel -- and recover a surface that is not single-valued
    from any direction. Stereo cannot do that at all, and it is exactly the
    specimen geometry where creep testing is common.

  What DIC adds over plain photogrammetry, and it is easy to lose: reconstructing
  a surface before and after and differencing them measures surface-to-surface
  DISTANCE, which cannot see in-plane sliding and cannot say which material point
  went where. Speckle correlation gives dense material-point correspondence, so
  the pipeline is bundle-adjust each state, reconstruct PER MATERIAL POINT by
  correlating across views, and difference those.

  ⚑ **THE DATUM IS WHAT WILL BITE, not the bundle adjustment.** A
  structure-from-motion reconstruction is determined only up to an arbitrary
  similarity transform. Comparing state A with state B means registering them,
  and registering on the specimen surface ABSORBS THE DEFORMATION INTO THE
  REGISTRATION and measures nothing at all -- while producing a clean, plausible
  field. It needs reference points known not to move, fixed to the frame and off
  the specimen. That is a rig requirement rather than a software one, and it is
  the most likely way for this to yield beautiful wrong numbers. Scale is
  unobservable from images alone for the same reason; a ChArUco board in the
  scene settles datum and scale together.

  ⚑ **"Slow" is a CHECKABLE precondition, not an assumption.** With creep rate r
  and a capture lasting T, the within-state drift is r times T, and it has to be
  small against the noise floor. Both numbers are the run's own, so we report it
  rather than trust it, exactly as the noise floor is put against the movement it
  qualifies.

  Available today with no new dependency, which was the surprise:
  `cv::multicalib::MultiCameraCalibration` (OpenCV ccalib) is a
  bundle-adjustment camera-network calibration and is already installed; so are
  ArUco and ChArUco for the datum, and `solvePnPRansac`, `findEssentialMat`,
  `recoverPose` and `triangulatePoints` in calib3d. Ceres is packaged and
  BSD-3-Clause, so licence-clean if a purpose-built adjustment is ever wanted.
  OpenCorr has none of this and stops at two calibrated cameras.

  ⚑ **After stereo, never instead of it.** This is stereo DIC plus a camera
  network plus a registration datum -- strictly larger, and it reuses the
  calibration screen, the 3D view and the N-view project model that stereo
  builds. The one thing it changes TODAY is that constraint on the project
  model, which is why it is written down now rather than when it is started.

  Adjacent, and deliberately not this item: single-camera stereo through a
  biprism or mirrors, which splits one sensor into two simultaneous views. That
  works on DYNAMIC tests, needs optics, and halves the resolution. A different
  answer to a different question, noted so the two are not conflated.

- **Sparse point tracking, alongside the dense field.** Tracking discrete
  markers rather than a continuous speckle: locate each target and follow it,
  instead of correlating subsets. A different algorithm class, not a degraded
  one -- a well-formed circular target's centroid locates more precisely than a
  subset does, and the trade is coverage for accuracy.

  ⚑ **We are catching up here, not innovating.** Every commercial vendor sells
  it: Dantec as Point Marker Tracking, ZEISS/GOM as PONTOS and inside ARAMIS,
  Correlated Solutions as a VIC module that shows speckle and marker data
  CONCURRENTLY in one dataset. No open DIC GUI has it. Two details worth taking
  from them: GOM fits an ELLIPSE rather than a plain centroid, which is what
  perspective foreshortening requires, and uses CODED markers so each carries a
  unique identity across frames.

  Four uses, all already on our own table:

  1. **A cut or islanded sticker**, where the pattern is deliberately divided so
     it cannot stiffen the specimen. Its islands move independently, which is
     fatal to subset correlation and native to marker tracking.
  2. **The datum for the multi-view work above**, which is the thing named there
     as most likely to produce beautiful wrong numbers. Reference points known
     not to move, on the fixture rather than the specimen, ARE markers.
  3. **Camera drift over a long test.** Track fixed markers, subtract the
     rigid-body part. Directly serves creep, where a rig sits for days.
  4. **Where speckle cannot be applied or displacements are large** -- big
     structures, dirty environments, which is the commercial pitch for it.

  What we already have, checked rather than assumed: the fork's
  `CameraCalibrator` carries DICe's dot-target path, with blob detection,
  sub-pixel centroids and donut markers for origin and axis. And `Strain`
  neighbours through nanoflann over the POIs' actual positions rather than over
  a lattice, so a scattered cloud feeds the existing strain fit unchanged;
  `RegionFit2D` likewise, so the recovery pass would work on markers too.

  ⚑ What that inventory does NOT include, stated because it looks like more than
  it is: that detector is tuned for a CALIBRATION BOARD -- a regular grid, a
  known count, donut markers giving orientation. Tracking arbitrary markers on a
  specimen through deformation needs identity persistence frame to frame and no
  assumption of a grid, and that is the actual work.

  And one open question worth settling before any of it: whether centroid
  fitting is needed at all, or whether running the EXISTING correlation on
  subsets centred at each marker gets most of the value for a fraction of the
  work. The vendors fit ellipses, which suggests centroids win on accuracy; the
  cheap version should still be measured before it is dismissed.

  ⚑ The real cost is on our side, not the engine's: **SurView's result model is
  lattice-shaped.** `CorrelationPoint::gridIndex`, `gridColumns`/`gridRows`,
  `layoutField()` and the quad-cell mesh all assume a rectangle. Markers are a
  point cloud. That is the same generalisation the stereo project model needs,
  which is two independent reasons to do it once and deliberately.

## Later

- **GPU acceleration** through VTK's WebGPU compute pipeline, cross-vendor.
  Sequenced after a working CPU-path GUI, per tenet 10. Needs a prototype
  spike before it is a commitment.
- **Global/regularized DIC** as a third solver family, for specimens where
  local subset solving diverges. Scoped, not planned; revisit on concrete need.
- **Topology-aware ROI** beyond the multiply-connected regions already
  supported. Value is concentrated in near-discontinuity precision.

## Directions, not yet features

- **A phone as the reader.** A field diagnostic whose reader is a workstation is
  not a field diagnostic. The recovered-strip direction below only works if the
  measurement happens next to the machine that is stuck.

  Three things make it more tractable than it looks. The artefact can carry its
  own scale bar and fiducials, so the phone needs no calibration rig. The
  handheld perspective problem is the same coarse pre-alignment the entry above
  already needs. And the monochrome objection INVERTS: a phone camera is colour,
  so the indicator layers a DIC rig cannot read become the easy part.

  ⚑ **It also makes the core/GUI separation load-bearing rather than tidy.**
  `surview_core` is engine-free and widget-free by construction and only two
  files know OpenCorr exists, which is most of what a second front end needs.
  VTK is the part that does not travel -- and a strip reader wants a 2D overlay
  and a chart, not a 3D scene, so that may cost nothing. The rule this implies
  is worth keeping deliberately: **VTK stays out of `surview_core`.**

- **Reading a passive, recoverable strip.** David's, and it is the use case that
  gives several entries above a concrete first application. Photograph a
  speckled strip, put it somewhere no camera can go, work the mechanism, recover
  it, photograph it again. From the pair: what moved, and from indicator layers
  on the same strip, peak pressure and peak temperature. Maximum and static, and
  accepted as such.

  Prior art, checked: every channel exists separately and is mature. Fujifilm
  sells Prescale for pressure and Thermoscale for heat; Sensor Products sells
  Pressurex and Thermex, the latter changing colour permanently with intensity
  related to temperature. Passive STRAIN recording is older still -- brittle
  lacquer, Stresscoat, 1937 and still in use, plus patented peak-strain devices
  (US 5675089, US 5932810). What none of them do is return a full-field
  displacement VECTOR field, and what the lacquer cannot do is go somewhere you
  cannot look. That combination is what is unclaimed. Four web searches is not a
  freedom-to-operate search, and this would need one.

  What it needs from us, all of it already listed above: reference and target
  from separate sessions; multi-view, for reading the strip in its recovered
  shape rather than flattening it, since flattening a strip bent to radius R
  adds about t/2R of apparent strain and 100 um round 5 mm is one per cent;
  second-modality registration, for the colour layers; and sparse point
  tracking, if the strip is islanded rather than continuous.

  ⚑ **The caution that belongs on the tin: a strip records what happened to the
  STRIP.** Relating that to what the mechanism did needs to know how the strip
  was constrained. It is a witness plate, which is exactly what a stuck
  mechanism calls for, and it is not metrology. Nobody should ever report a foil
  strain as a machine displacement.

## Test and tooling debt

- **Mutation testing** exists (`tools/mutants.py`) and was run in earnest for
  the first time on 2026-09-09. The 22 core sources other than
  `Correlation.cpp` scored **61.7%**: of 637 viable mutants, 244 changed the
  meaning of a line and no test went red. `Correlation.cpp` is swept
  separately, against the complete suite including the slow accuracy cases,
  since those are what cover it.

  `Correlation.cpp` swept separately against the complete suite scored
  **48.2%** - 41 killed of 85 viable, 44 survivors - which is markedly worse
  than the rest of core and is the file where the measurement happens. Sixteen
  of those were the chunking arithmetic and are closed (the queue had only ever
  been divided into one piece, since every grid in the suite is smaller than
  the chunk size); the rest are the engine-boundary guards and the recovery
  pass's own loop, and they are the most valuable survivors in the project.

  ⚑ **61.7% WAS A FLOOR, NOT THE SCORE.** That sweep excluded the four slow
  test executables to fit in a night, so any mutant only they would have killed
  was reported as a survivor. Re-running the 244 survivors against the complete
  suite killed 49 of them outright: the real figure was **69.4%**, and the real
  work list was 195 rather than 244. `tools/mutants.py --rerun` exists for
  exactly this, and doing it first is what stops a night being spent writing
  tests for behaviour that was already covered.

  Working through those 195 on 2026-09-09/10 took it to **78.8%** - 60 more
  killed, measured rather than assumed, by replaying the survivor list against
  the suite afterwards. Fifteen files, and the commits carry the reasoning.
  What it found was not only missing tests: a reference that re-anchored on a
  field which had not decayed, because a float correlation was widened to
  compare against a double threshold and 0.9f is 0.899999976 - the same defect
  this project had already diagnosed and fixed once in `Recovery.cpp`, six
  lines away, without the fix travelling.

  ⚑ **AND MOST OF WHAT REMAINS CANNOT BE KILLED.** Of the 129 still standing,
  the large majority were chased individually to a reason and the reason is in
  the test file beside them: guard conditions the code above them makes
  unreachable, defensive bounds nothing can call out of range, ternaries
  equivalent by construction, and one mutant that only a sanitizer or an
  implementation-derived expectation could catch. That accounting is the point
  rather than the percentage: a survivor with an argument beside it is closed
  work, and the next person to run a sweep should not re-derive it.

  ⚑ **AND A SWEEP OF `Correlation.cpp` ON 2026-09-10 CORRECTED THAT ACCOUNT
  ONCE MORE, in the direction that matters.** Sixteen of its survivors were
  recorded as closed by `tests/test_chunking.cpp`. They were not. Re-run in the
  configuration a sweep actually uses - a fresh build directory with no
  `CMAKE_BUILD_TYPE` - the mutant that takes this project's Debug build down
  mid-case instead corrupts the heap quietly: every case reports PASS, Qt Test's
  own crash handler prints a stack trace during teardown, and the process exits
  ZERO, which ctest reads as green. The verdict on a mutant whose whole effect
  is undefined behaviour was being decided by heap layout. They are held by an
  assertion now - a stage cannot report more points than it has - and the same
  hazard applies to any case anywhere in this suite that is "killed" by a
  crash rather than by a failure.

  Measured afterwards rather than assumed, by replaying all 36 that were still
  standing against the suite as it now is: **23 killed, 13 left**, and of those
  thirteen, twelve carry a written reason beside the case they belong to. The
  replay earned its cost immediately - it exposed four nobody had noticed, one
  in each chunked loop, where `start < total` widened to `start <= total` adds
  an empty pass ONLY when the chunk size divides the queue exactly. Every case
  in the file uses a deliberately ragged chunk, so `start` steps over `total`
  and never lands on it. A chunk of one point divides every queue there is.

  Seventeen more of that file's survivors were killed on 2026-09-10 and six
  closed by argument beside the cases they belong to: the reason a solver
  combination is refused, the thread count every run is dispatched with (lifted
  out of the run, since a number nothing can call is a number nothing can
  check), what a stopped run reports about the points it never reached, the
  agreement between the repair count and the repair marks, the stage names the
  status bar shows, the strain measure handed to the engine, the points left out
  of every fit, a target of a different height alone, and a hole with three
  corners.

  ⚑ **One gap that file exposed is worth more than the mutants that revealed
  it**: the choice between the first- and second-order solvers can be INVERTED
  with the whole suite green. Nothing in this repository deforms in a way a
  first-order shape function cannot describe - every synthetic example is a
  uniform deformation gradient by construction, which first order represents
  exactly - and on the real photographs the two orders agree to within a
  thousandth of a correlation in neither direction consistently. Closing it
  needs a fixture with a QUADRATIC displacement field, and the generator's
  exactness comes from a uniform F mapping a Gaussian blob to a Gaussian blob,
  which a non-uniform one does not. Worth doing: it is the only way anything
  here can tell whether a user asking for second order gets it.

  **Worked to the end on 2026-09-10**, file by file, across every core source
  the sweep had left standing. Fourteen commits carry the reasoning; the two
  findings worth more than the count are recorded above (the chunking family,
  and the second-order solver gap).

  Measured afterwards rather than assumed, by replaying the standing survivors
  against the suite as it now is: of the 98 replayed, **40 are now killed and 58
  still stand, every one of the 58 carrying a written reason beside the case it
  belongs to.** The replay was stopped at 98 of 142 deliberately: the remaining
  44 are Series' sixteen (argued down in the previous session), the Sequence
  equivalents settled by brute force over 355,216 ordered pairs, and
  Correlation's own thirteen, every one of which had been negative-checked
  individually as its case was written. Confirming those would have cost three
  more hours of machine time and told us nothing new.

  ⚑ **THE REPLAY IS WHAT FOUND WHAT THE FILE-BY-FILE PASS WALKED PAST**, four
  times, and that is its real argument rather than the score: the Magnitude row
  the panel prints (a second copy of an expression whose first copy was tested),
  a pixel at the image's darkest value claiming the pixel type's own limit
  (the fixture's darkest pixel is 0, which IS a byte's floor, so both facts were
  true at once in every case), the off-picture row one flag away from cautioning
  every time the pointer crossed an edge, and an EMPTY field re-anchoring the
  reference - which banks no increment, marks every point lost, and leaves the
  rest of the sequence measuring nothing.

  ⚑ **AND THREE PATTERNS ACCOUNT FOR MOST OF WHAT WAS FOUND**, worth knowing
  before the next sweep rather than after it:

    - **An index check tightened at its LOWER bound**, three times in three
      files. Cases ask about the middle of a grid and about indices past the
      end; nothing asks about the FIRST one. The point readout refused point
      zero, the repair pass dropped it from every round, and three of
      FieldMesh's four corner checks were free because mesh point zero is only
      ever a top-left corner when points arrive in grid order.
    - **A fixture symmetric in the thing under test.** test_roi probed only
      along x, so three mutants lived in the subset rectangle's height; the
      irreflexivity case used a name whose digits hold no leading zero, so
      image_0000.png could precede itself.
    - **A case asking whether something was refused without asking WHICH
      refusal.** An image with no pixels was refused for its subset radius; a
      region inside the border margin on one axis alone was not refused at all.

  ⚑ **AND THE SWEEP ITSELF WAS THE WASTE.** It ran all 33 test executables for
  every mutant, when a mutation in `Sequence.cpp` cannot be noticed by a test
  that never compiles against it. `tools/mutants.py` now works in two stages:
  first the tests whose source includes the mutated file's header, then - only
  for what survives that - the complete suite before anything is recorded as a
  survivor. A narrowed run can produce a false SURVIVOR, never a false kill, and
  a false survivor costs one more run, so the narrowing buys time and cannot
  cost accuracy.

  The coverage set comes from the include graph rather than a hand-written map,
  so it cannot rot. Measured on the same six mutants, warm build directory, same
  verdicts both ways: **2 min 26 against 6 min 17**, and the run reports how many
  mutants the narrow set killed on its own against how many got past it - which
  is the number that says whether the heuristic is still any good. Two tests
  instead of thirty-three for most core files; `Correlation.h` is included by
  nineteen of them, so the central types benefit least.

  ⚑ **A COMPLETE FRESH SWEEP OF CORE, 2026-09-10/11 overnight, is the first
  one this project has had: 84.8%, 612 killed of 722 viable, 110 survivors**
  out of 1062 mutants generated (340 did not compile and are excluded). Not a
  replay of a recorded list - every mutant generated afresh, so the figure is
  the suite's own, measured against the tree as it now stands.

  It is a FLOOR, for the reason the fast half always is: the slow cases are
  excluded, so anything only they would catch is counted as a survivor. Sampled
  afterwards against the complete suite, 8 of the first 14 survivors died at
  once - so the real figure is meaningfully higher, and the honest number to
  quote is the floor with that sample beside it rather than an extrapolation.

  The localisation is what made a fresh sweep possible at all, and the run
  reports its own quality: **601 of the 612 kills were found by the narrow test
  set alone, and only 11 needed the whole suite.** The include graph is a good
  guide to coverage on this project, and now says so with a number.

  Four more gaps came out of the night's survivors, each one a case that covered
  a rule and missed the shape everybody uses: a hole with FOUR corners (the case
  used a triangle, which is the boundary, so "three or more" and "fewer than
  four" were the same rule to it); a flag channel's colour scale, which could
  report no range at all and paint a whole field as "repaired" on a run that
  repaired nothing; the provenance sentence, where asking that "drawn by hand"
  and "detected from the speckle" merely DIFFER lets them be swapped; and the
  four-corner rule in Series.cpp, recorded as closed because the wholesale swap
  segfaults - where a single condition turned into an AND lets a cell missing
  exactly that corner through, and the case had removed one corner of four.

  ⚑ **AND THE LARGEST REMAINING CLASS IS NOW CLOSED BY A DIFFERENT TOOL.** Of
  the survivors that stand with a note saying no case is coming, the large
  majority are reads or writes ONE ELEMENT past the end of a vector: what comes
  back is whatever sits there, nothing downstream need change, and no assertion
  can see it. `tools/sanitize.sh` catches the ACCESS rather than its
  consequences, which is the only thing that closes them, and it has its own CI
  job so it stays true. The whole suite is green under it, walkthrough and
  accuracy cases included - so on every path the suite exercises there is no
  out-of-bounds access and no undefined arithmetic.

  ⚑ Leak detection is OFF there, deliberately: Qt and VTK keep singletons alive
  to exit by design and LeakSanitizer buries our own signal in that
  housekeeping. The engine fork has no Qt or VTK, so its run keeps leaks ON -
  and found a real one in its first minute, a whole output volume that
  `IO3D::saveMap3D()` allocated and never gave back.

  It is still not ROUTINE, which is the actual debt: a sweep costs hours and
  nothing runs one on a schedule. What this round showed is that the cheap half
  is the valuable half - replaying the SURVIVOR list is minutes of setup and
  finds real defects, where re-proving known kills is hours and finds none.
  Verify a kill when you write it, with one mutant against one test executable;
  replay the survivors when the work is done.

  Two things worth not re-deriving about running one. A mutant costs 6.5 s to
  build with mold and 90 s to test, so the slow cases are excluded by default
  and the file they cover is swept separately; and a sweep edits `src/` in
  place, so it wants its own git worktree if anyone is working in the tree
  meanwhile.

  ⚑ **And stop one with SIGTERM, never SIGKILL.** A sweep holds a mutated
  source file and a running ctest with test binaries under it. Killed outright
  it leaves both behind: a deliberately broken source that looks like someone's
  work in progress, and orphaned test processes that go on holding the machine
  (three sweeps' worth were found still running at a load average of 31 on
  2026-09-11). `tools/mutants.py` now handles SIGTERM and SIGINT by putting the
  source back and taking its children down with it, and runs ctest in its own
  process group so one kill reaches the test binaries too. SIGKILL still cannot
  be caught, which is why this says which signal to use.
- ⚑ **SURVIEW HAS NOW BEEN CHECKED AGAINST SOMETHING OTHER THAN ITSELF**, for
  the first time, on 2026-09-14. Until then the synthetic examples were ours,
  the answers they state were ours, and the accuracy bounds they were measured
  against were ours - sound verification of the IMPLEMENTATION, saying the code
  computes what we intended, and saying nothing at all about whether what we
  intended agrees with the field. This entry used to say that had never been
  done, and called it the largest single gap in this project's evidence.

  **What was run.** The iDICs/SEM DIC Challenge 1.0 **Sample 3** set: one
  speckle reference and eleven targets rigidly shifted by 0.00 to 1.00 px in
  steps of 0.10. Their images, their generator, their prescribed answer, stated
  in each file's own name. `tools/cross-validate.sh` runs it end to end,
  `tests/test_cross_validation.cpp` holds it, and `core/DicChallenge.h` reads
  the stated answer.

  **What it measured.** The worst disagreement across all eleven frames, in
  either axis, is **0.0032 px**, over 841 of 900 attempted points per frame.
  Their +Y is our +v, measured rather than assumed - a convention difference
  would have shown as a v of the right size and the wrong sign on every frame.

  ⚑ **AND THE ERROR IS NOT SCATTER, WHICH IS WORTH MORE THAN ITS SIZE.** It
  traces the S-shaped sub-pixel interpolation bias every subset-based method
  carries: zero at 0.0, 0.5 and 1.0 px, negative below half a pixel, positive
  above, period exactly one pixel, amplitude about 0.003 px. Reproducing a known
  systematic property of the method on somebody else's images is stronger
  evidence that the pipeline is right than any single error figure, because
  nothing in this project was built to produce that shape.

  | stated | measured u | measured v |
  |---|---|---|
  | 0.00 | -0.00041 | +0.00002 |
  | 0.10 | +0.09777 | +0.09779 |
  | 0.20 | +0.19757 | +0.19706 |
  | 0.30 | +0.29869 | +0.29790 |
  | 0.50 | +0.50016 | +0.50046 |
  | 0.70 | +0.70175 | +0.70262 |
  | 0.80 | +0.80264 | +0.80317 |
  | 1.00 | +0.99988 | +1.00003 |

  ⚑ **THEIR ANSWER IS PRESCRIBED, NOT EXACT BY CONSTRUCTION**, and the two must
  not be described in the same words. Our synthetic sets render each frame
  afresh from an analytic pattern, so no pixel is ever resampled and the stated
  deformation IS the truth. These frames were made by shifting one photograph in
  the Fourier domain with about two grey levels of noise added, so the image
  carries whatever that resampling does. A disagreement here is evidence about
  the two of us together. That is what an independent check is for, and it is
  why the tolerance in the case is argued from the bias rather than inherited
  from the synthetic cases.

  ⚑ **AND THE SET CANNOT BE COMMITTED.** The challenge sets state no licence or
  terms of use: they may be used, and redistribution is unestablished (see the
  MANIFEST beside the download in `~/code/dic-datasets`). The case reads the
  images from wherever they were unpacked and SKIPS when they are absent, so a
  fresh clone and CI say "skipped" rather than quietly checking nothing. The
  engine at the time of measurement was two commits ahead of the pin
  (`dd9bc29`), both in `.cine` I/O and a `saveMap3D` leak, neither on the
  correlation path.

  **Still open, in the order they are worth doing:**

  1. ⚑ **The measurement half cannot tell u from v, and Sample 3 cannot close
     it.** Every frame in that set shifts EQUALLY in x and y, so a build that
     reported u for both components passes it - confirmed by negative check,
     not argued. That is the fixture-agrees-with-itself trap, earned a fifth
     time. The reading half is closed by a lopsided name; the measurement half
     wants a set whose displacement is in one axis alone, which is Sample 14
     below.
  2. **Run our own examples through Ncorr or DICe** - the other step this entry
     always named, and still not done. Neither is installed here and DICe is
     Trilinos-coupled, so it is a real build rather than an afternoon. It
     answers a different question from the above: not "does our measurement
     match an external answer" but "do two independent implementations agree on
     one photograph".
  3. **A comparison with an independent implementation** - DONE 2026-10-01,
     as an internal check that took the place of step 2. Only SurView's own
     accuracy against the published answers is reported; see "Waiting on a
     decision", item 3.

- ⚑ **OUR POI GRID PLACES A ROW AND A COLUMN THAT CAN NEVER BE MEASURED**, and
  it took external data to notice. Found 2026-09-14 while checking the solve
  counts of the cross-validation above, which is the only reason anybody counted
  ATTEMPTED against SOLVED on a clean image rather than reading the solved
  figure on its own.

  On Sample 3 the run attempts 900 points and solves 841, every frame. The 59
  are not scattered and are nothing to do with the specimen: they are exactly
  the grid's first row and first column, the same 59 on every frame, and they
  fail on the frame shifted by 0.00 px as surely as on the rest.

  **The mechanism, verified rather than inferred** - the first two explanations
  tried were both wrong, and each looked right until it was checked:

  - It is NOT the engine's own subset bounds test. `ICGN2D1::compute()` refuses
    `poi->x - subset_rx < 0`, and at x = 16 with radius 16 that is 0, which is
    not less than 0. It passes.
  - It is NOT the two-pixel band at the image edge where `oc_gradient.cpp`
    computes no gradient (`for c = 2; c < width - 2`). A point at x = 17 has
    column 1 inside its subset and solves perfectly.
  - It IS `BicubicBspline::compute()`, which returns OUT_OF_BOUNDS for any
    sample at `x < 1 || y < 1 || x >= width - 2 || y >= height - 2`. Our grid
    lays its first point at exactly one subset radius from the edge, so that
    subset's outermost sample sits at 0, and the interpolator refuses it as soon
    as the specimen moves by anything less than a whole pixel.

  ⚑ **AND THE ANOMALY THAT LOOKED LIKE NOISE CONFIRMS IT EXACTLY.** The frame
  shifted by 1.00 px solves 858 rather than 841. At a displacement of exactly
  one pixel the outermost sample lands at 1.0, which is not less than 1, so it
  is in bounds - and only the points whose measured u came out at or above 1.0
  recover, which is why it is 17 of the 59 and not all of them. That knife edge
  is the mechanism above, seen from the other side. Settled empirically too: an
  ROI forcing the first column to x = 16 solves 0 of 8, and to x = 17 solves 8
  of 8.

  Why it matters more than 6 per cent of a field. The engine reports these as
  "subset out of image bounds, or invalid (NaN/out-of-range) initial guess",
  which reads to anybody looking at a hole as a problem with their PHOTOGRAPH -
  poor speckle, bad lighting, something at the edge of the specimen. It is
  neither: we knew the subset radius and the image size before laying a single
  point down.

  ⚑ **The same fault is on the far edge and we have not seen it only by luck.**
  `safeLastX` is `width - 1 - subsetRadius`, whose subset reaches sample
  `width - 1`, and the interpolator wants below `width - 2`. On a 512 px image
  with a 16 px step the last column lands at 480 rather than 495, so it never
  bites. A grid step that divides differently would lose the far row and column
  in exactly the same way.

  **What it wants, and it is two things rather than one.** The inset itself is a
  plain fix: the first placeable point is at `subsetRadius + 1`, not
  `subsetRadius`, and the last at `width - 3 - subsetRadius`. But an inset
  cannot be the whole answer, because the constraint is on the sample AFTER
  displacement and no grid can know the displacement in advance: a point one
  pixel in whose specimen moves half a pixel toward the edge is refused for a
  reason that IS about the measurement. So the two cases want telling apart -
  a point that was never placeable, which is ours and should not be attempted,
  and a point whose subset left the interpolatable area once the specimen moved,
  which is a real result and should say so in those words rather than in the
  engine's.

  ⚑ **And the margin is the ENGINE'S to state, not ours to hard-code.** A `1`
  and a `2` copied into `poiGridExtent()` are two magic numbers that go quietly
  wrong the day the interpolator changes - and upstream is mid-rewrite. The
  honest form is the fork stating its own interpolation margin and
  `core/Correlation.cpp` carrying it across, which is also the kind of small,
  generically-useful addition the upstreaming policy says goes upstream rather
  than staying in the fork.

- ⚑ **SAMPLE 14 IS REFUSED BY SURVIEW TODAY, and the refusal is correct in its
  own terms.** Found 2026-09-14 on first contact with external data. The DIC
  Challenge spatial-resolution set ships a reference 2048 px wide and targets
  2049 px wide, and the run refuses them: "Reference and target differ in size
  (2048x589 and 2049x589). They must describe the same pixel grid." That rule
  exists for a fixed rig, where a size change means somebody altered the setup
  mid-test, and it is the same rule *A reference and target photographed in
  SEPARATE sessions* under Next already wants to revisit. This is a second and
  quite different reason to revisit it: a benchmark set whose images
  legitimately differ by one column, with nothing wrong with either image.

  Measured anyway, by cropping the target's right-hand column outside the
  application. ⚑ **Which column to crop was settled by measurement, not by
  inference**: the prescribed displacement is exactly zero for x < 105 in all
  three wavelengths, so that strip is an answer the crop cannot influence.
  Cropping the right column reads +0.0018 px there; cropping the left reads
  -0.9982 px, one whole pixel out. The two differ by exactly 1.000 px, which is
  its own consistency check.

  **What it measured**, with a 33 px subset (radius 16) and a 4 px step, against
  the prescribed Lagrangian profile the set's own spreadsheet carries - the
  sheet is explicit that the Lagrangian column is the one to compare DIC
  against, and it ships an Eulerian column beside it that differs:

  | set | RMS error | worst | shortest wavelength present |
  |---|---|---|---|
  | L1 | 0.0031 px | 0.011 px | 619 px |
  | L3 | 0.0044 px | 0.013 px | 173 px |
  | L5 | 0.0074 px | 0.023 px | 101 px |

  ⚑ **AND IT YIELDS THE SPATIAL RESOLUTION NUMBER WE DO NOT REPORT.** The set is
  a chirp - constant 0.1 px amplitude, wavelength shortening along x - so the
  ratio of measured to prescribed amplitude per half-cycle IS the amplitude
  transfer function. Measured here: about 1.00 above 350 px, 0.93 to 0.98
  between 200 and 300 px, 0.87 to 0.95 between 120 and 160 px, and 0.78 to 0.89
  between 100 and 115 px. The 90 per cent point sits near a 130 px wavelength,
  about four times the subset. That is exactly the quantity *Report SPATIAL
  resolution, not only displacement resolution* under Next says we owe a reader,
  and it is a measured value for it rather than a derivation - worth holding
  whatever formula that entry ends up stating against.

  Not committed as a case, because it needs the crop and it needs the prescribed
  profile out of their spreadsheet, which is their data. Recorded here as
  measured evidence, reproducible from the notes above.

- **Coverage reporting** is present but not tracked over time.
- The **walkthrough suite races with the X server's own pointer motion**;
  synthetic and real mouse moves arrive in an order that is not deterministic.
  Every reading is taken from a click rather than a hover, which is
  unambiguous, and the panel says "Click to pin it" so the test is still only
  doing what the screen tells it to. The last case still reading a hover was
  found on 2026-09-02 by CI failing where the developer's machine passed.

## Fixed, kept here because the reason is worth remembering

- **The field's explanation drawn over the field it explains.** Found by
  screenshot on DIC Challenge Sample 3, while making the pictures for decision
  1: the bar naming the channel covered the top 20 image pixels, which is
  exactly where that decision's failed row sits, and over the tall tension
  specimen the coordinate legend covered its bottom as well. The camera was
  fitted to the whole widget and the bars are drawn over it. It is now fitted
  into the area the bars leave free (`core/ViewFit.h`), and beside or above the
  legend, whichever leaves the picture larger. It refits when a bar appears or
  the window resizes, but only while the view is where the last fit left it,
  so a zoom the reader chose is never thrown away.

- **No way back out of a zoom.** Zooming was a turn of the wheel, and nothing
  brought the whole image back short of reloading it. View > Fit Image to
  Window (Ctrl+0) does now.

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
