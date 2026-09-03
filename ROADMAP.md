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
with an exactly known answer and three real ones.

## Gaps against other DIC GUIs (reviewed 2026-09-01)

Measured against pyALDIC, iCorrVision-2D, Ncorr, and the commercial tools
(VIC-2D, MatchID, GOM Correlate, LaVision DaVis). Our three-column shape is
the idiom of the field, and these are what we do not have yet:

- **Binaries.** Everyone else ships installers. We ship a build. `install()`
  rules exist now, so this is packaging rather than plumbing.
- **Stereo and 3D.** VIC-3D, GOM and MatchID measure out-of-plane. The engine
  can; the application has no path to it.
- **A line probe.** We plot against frame and have virtual extensometers;
  reading a quantity along a line at one frame is still missing.
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

- **A line probe.** Plots over the sequence and virtual extensometers exist now;
  what is still missing is reading a quantity ALONG a line at one frame, which
  is the other half of what commercial tools offer. The sampling it needs is
  already built (`sampleFieldAt()` in `core/Series.h`), so this is a chart and a
  placement mode rather than new arithmetic.
## Then

- **Packaging.** `install()` rules exist, so the pieces are in place; what is
  missing is an installer and release artifacts anyone can download. Everyone
  else in this field ships binaries and we ship a build, which is the single
  biggest thing standing between the tool and somebody trying it.

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

- **Mutation testing** exists (`tools/mutants.py`) and is not routine.
- **Coverage reporting** is present but not tracked over time.
- The **walkthrough suite races with the X server's own pointer motion**;
  synthetic and real mouse moves arrive in an order that is not deterministic.
  Every reading is taken from a click rather than a hover, which is
  unambiguous, and the panel says "Click to pin it" so the test is still only
  doing what the screen tells it to. The last case still reading a hover was
  found on 2026-09-02 by CI failing where the developer's machine passed.

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
