# SurView DIC — Context File

## What this is

The open-source DIC (Digital Image Correlation) world has good engines and
poor GUIs. The missing piece is not another correlation kernel — it is a
well-engineered, cross-platform, license-clean, ecosystem-native GUI that a
working scientist can adopt and hand to a colleague. SurView DIC fills that
space.

## Engine: OpenCorr

SurView DIC wraps **OpenCorr** (Jiang, `vincentjzy/OpenCorr`, MPL-2.0) as its
correlation engine, chosen for its maturity and technical capability:

- 2D DIC, stereo/3D DIC, and volumetric DVC in one library.
- CPU-path solvers (ICGN, NR, IC-LM), self-adaptive subsets,
  SIFT-feature-guided and epipolar-constrained stereo matching — open,
  buildable, MPL-2.0.
- MPL-2.0 license (file-level copyleft, linkable into a larger work) does
  not constrain SurView DIC's own license — see License below.
- Actively maintained and citable (Jiang, *Optics and Lasers in
  Engineering*, 2023).

OpenCorr also ships a GPU-accelerated ICGN path, but it is closed-source
(no CUDA source in the repo), Windows/NVIDIA-only, has an open unresolved
crash bug, and its binaries carry no verifiable license. SurView DIC treats
it as unavailable — not something we can build, fix, or ship. See GPU
acceleration under Roadmap for the actual plan.

The upstream engine stays a dependency; this repo is the GUI/packaging
layer. Do not merge OpenCorr into this repo.

## Why Qt + VTK

SurView DIC is built in Qt and VTK to be a native complement to the
open-source technical-computing ecosystem it sits alongside (FreeCAD,
ParaView, and similar Kitware/VTK-stack tools), not just for the GUI toolkit
itself.

VTK specifically is a design decision, not an incidental library choice:

- DIC/DVC output is a displacement/strain field on a mesh — exactly what VTK
  represents and renders.
- Native `.vtu`/`.vtk` output means ParaView opens it directly and FreeCAD's
  FEM workbench consumes it, with no conversion step.
- This closes a real loop: DIC-measured fields validating FEA predictions,
  both in the same pipeline.
- It makes SurView DIC a citizen of the VTK/Kitware ecosystem (ParaView,
  3D Slicer), not merely a standalone Qt app.

**Build constraint:** Qt's GPL-3.0-only add-on modules (Qt Charts, Qt Quick
3D) are off-limits — using either would force SurView DIC's own license to
GPL. All charting and 3D rendering goes through VTK (`vtkChart`, `vtkPlot`,
etc.), which is BSD-3-Clause and imposes no such constraint. Qt itself stays
scoped to the application shell (windows, menus, dialogs, docking) under its
LGPLv3 essential modules.

## License

SurView DIC is licensed **LGPL-2.1-or-later**, matching FreeCAD's choice in
the same ecosystem. Fully open-source, no proprietary/open-core tier.

Reasons:

- Protects against the scenario this project actually cares about — someone
  taking the whole application, rebranding it, and reselling it closed-source
  — about as effectively as GPL would, since there's no "larger work" for a
  whole-app fork to hide behind: the copyleft obligation to share modified
  source applies to the rebranded fork itself.
- Stays welcoming to outside contributors, who already trust and understand
  LGPL from FreeCAD, VTK, and the rest of this ecosystem.
- Leaves a narrow, accepted gap: someone could extract a specific SurView
  DIC module (e.g. the calibration workflow) into their own separate closed
  product, keeping only that module's source open. Accepted trade-off for
  the contributor-friendliness and ecosystem fit above.
- Neither OpenCorr's MPL-2.0 nor Qt's LGPLv3 essential modules constrain this
  choice — both permit combination with a differently-licensed larger work.

## Roadmap: cross-vendor GPU acceleration

SurView DIC will target GPU-accelerated solvers via VTK's own WebGPU compute
pipeline (built on Dawn) instead of OpenCorr's existing GPU path. Dawn
targets Vulkan (Linux/Android), Metal (macOS), and DX12 (Windows) under one
API, covering NVIDIA/AMD/Intel and the existing Android target from a single
implementation, and reuses the VTK dependency instead of adding a second
graphics/compute API.

- This is new engineering, not a port: OpenCorr's own CUDA kernels aren't
  open-sourced, so there's nothing to adapt — the published ICGN algorithm
  would be reimplemented as compute shaders from scratch.
- Architecturally a sibling solver module alongside OpenCorr's own classes
  (mirroring their `ICGN2D1`/`ICGN2D1GPU` pattern), not a modification of
  OpenCorr — consistent with "do not merge OpenCorr into this repo."
- Open question, not yet decided: does this module live in this repo or as
  a separate library, mirroring the OpenCorr-stays-a-dependency pattern.
- VTK's WebGPU compute path is new (still landing per VTK's own 2026
  roadmap) and unproven for a workload this numerically heavy — needs its
  own prototype spike before it's a real commitment.
- Sequencing: after a working CPU-path GUI exists, not before.

## Engine capability roadmap (OpenCorr fork)

OpenCorr remains the correlation engine — re-validated 2026-07-18 against
the broader headless C++ DIC/DVC landscape (DICe, ncorr_2D_cpp; both read
down to actual source, not just docs). Neither is a viable alternative
*engine*: DICe is Trilinos-coupled (an HPC framework built for clusters,
wrong shape for a single-workstation desktop app); ncorr_2D_cpp is
unmaintained since 2018, with unfixed compiler-breaking issues and a
heavier dependency footprint than OpenCorr. Both are, however, sources of
small, self-contained, BSD-3-Clause capabilities worth porting.

We maintain **katalystnord/OpenCorr**, a fork for SurView-driven capability
work (new classes mirroring OpenCorr's own patterns, or changes touching
private internals) — distinct from small, generically-useful fixes, which
go upstream directly (e.g. PR #24, the missing `<random>` include on
Linux/GCC builds).

Punch list tracked in [fork issue #1](https://github.com/katalystnord/OpenCorr/issues/1)
is complete (9/9): uncertainty quantification (sigma/beta, from DICe),
calibration — checkerboard + dot-target/donut-marker detection + stereo
epipolar quality metric (from DICe), `.cine` high-speed-camera file I/O
(from DICe's `hypercine`), gradient-free simplex matching (from DICe),
phase-correlation initializer (from DICe), RG-DIC seed-propagation
flood-fill (from ncorr_2D_cpp), sequence/reference-update tracking (from
both), conformal subset shapes — data model only (from DICe). Two small,
generically-useful fixes found along the way went upstream directly instead
of staying fork-only (PRs [#25](https://github.com/vincentjzy/OpenCorr/pull/25),
[#26](https://github.com/vincentjzy/OpenCorr/pull/26)).

Explicitly rejected as not worth porting: DICe's global/regularized
(mesh/FE) DIC as literal code (Trilinos-saturated — a future clean-room
reimplementation against Eigen, not a port, and a large lift); MPI
parallelism (wrong parallelism model for a desktop app — OpenCorr's
existing per-POI OpenMP threading already covers it); DICe's Exodus/HDF5
export (Trilinos/SEACAS-locked — the existing VTK `.vtu` decision already
covers this niche).

## Second-pass capability candidates (DICe/ncorr deep dive, 2026-07-19)

With the original 9/9 punch list closed, a deeper pass over DICe and
ncorr_2D_cpp source (not just docs) surfaced further candidates, this time
including higher-effort ones previously passed over as "too big." Not yet
turned into fork issues — listed here for scoping, ordered by suggested
priority.

**Done:**

- Per-point failure-reason taxonomy — done, fork issue
  [#13](https://github.com/katalystnord/OpenCorr/issues/13). Named
  `StatusFlag` enum + `statusDescription()` (was DICe's `Status_Flag`),
  wired through every solver's existing bail-out points. Also added
  Hessian-invertibility rejection to ICGN (not ICLM, whose
  Levenberg-Marquardt damping already handles ill-conditioning).
- Dynamic obstruction/occlusion masking — done, fork issue
  [#14](https://github.com/katalystnord/OpenCorr/issues/14), but re-scoped
  to phase 1 (data model only) on `Subset2D`: making it actually affect
  correlation results turned out to need the same cross-cutting
  correlation-kernel integration as conformal shapes' own deferred phase 2
  below, not a small self-contained addition.

**Large, scoped and deferred — not currently planned, revisit on concrete need:**

- Topology-aware ROI — scoped, then split. Phase 1a (multiply-connected
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
- Global/regularized (mesh/FE) DIC — scoped in depth (~6-9 weeks, same
  order as topology-aware ROI above), and **not currently planned**, but
  for a different reason than topology-aware ROI: this one is purely
  additive (a new `GlobalDIC2D` solver alongside ICGN/ICLM, touching none
  of their already-validated code) with real, if narrower-than-universal,
  value — specimens with patchy/low-texture speckle or thin membranes,
  where local subset solving simply diverges and a whole-ROI regularized
  solve borrows information from well-textured neighbors. Third solver
  family alongside local (current) and topology-aware, selected per
  correlation run, not simultaneously. Meshing doesn't need
  `artem-ogre/CDT`/Delaunay at all for a v1: OpenCorr's ROI model has no
  hole concept today, so a simple regular-grid mesh (DICe's own fallback
  for hole-free rectangular ROIs) covers the realistic case — CDT only
  becomes relevant if/when ROI holes reach meshing. Revisit if a specimen
  class needing this shows up.

**Scoped and not worth building — revisit only on concrete need:**

- Harmonic/Laplacian inpainting for ROI-edge extrapolation (ncorr's
  `Data2D::inpaint_nlinfo`). Reality-checked, not just license-checked:
  the problem it solves in ncorr (a global FFTW-deconvolution biquintic
  interpolator that's fragile to garbage pixels near a cropped ROI edge)
  doesn't exist in OpenCorr — `BicubicBspline` computes coefficients from
  a small local stencil over the whole dense image, never a
  cropped/masked region. The one real present-day use case (failed POIs
  leaking unfiltered into exported raster maps) is cosmetic/export-only,
  already mostly covered by `Strain`'s existing neighbor-regression
  output, and SurView has no GUI export consumer yet to even show the
  artifact. (License note for if this ever gets revisited: ncorr's own
  implementation links SuiteSparseQR/CHOLMOD, GPL-2.0-or-later —
  disqualifying under SurView's LGPL posture; would need a clean-room
  Eigen-sparse reimplementation, never a code lift.)

**Done:**

- Crack/discontinuity full-field diagnostic — done, fork issue
  [#16](https://github.com/katalystnord/OpenCorr/issues/16), as
  `CrackResidual2D`. Backward-warps the reference image through a
  densely re-evaluated local displacement fit and diffs against the
  real target image. Verified empirically, not just argued: the smoke
  test's synthetic two-rigid-piece image gives every placed POI
  zncc>0.9 (56/56, individually perfect correlations) while the
  residual is ~26x higher right at the known discontinuity — confirming
  it catches exactly what pointwise metrics (`StatusFlag`, sigma/beta,
  `SpeckleQualityMap`) can't. Bonus fix landed alongside: `oc_strain.cpp`
  had no degenerate-fit guard and redundantly decomposed the same
  matrix twice — fixed, plus `Strain` got its first test coverage in
  this fork.
- Virtual-extensometer/line-probe UX (DICe's `Live_Plot_Post_Processor`):
  not a port target itself (trivial/file-driven in DICe), but a roadmap
  idea — point/line probes interpolated from the same strain-fit
  machinery the item above already needs.

**Confirmed no gap, nothing to port:** DICe has no DVC/volumetric support,
no true >2-camera triangulation (hard-coded to a stereo pair), and no
adaptive subset sizing beyond OpenCorr's own; its VSG strain is already
matched by `oc_strain.cpp`. Ncorr's `std::thread` domain-decomposition
parallelism is superseded by OpenCorr's existing per-POI OpenMP threading
— confirmed by reading the code, not assumed.

## Full-branch code audit (2026-07-19)

A 3-chunk, 12-agent code review across all 8 established angles, covering
~11K lines of fork content (calibration/cine I/O, matching strategies/
diagnostics, core taxonomy/hot-path) — not scoped to a single feature's
diff, looking for cross-module issues a per-feature review wouldn't catch.

Five confirmed, unambiguous bugs (mechanical fixes, no design judgment
needed) are fixed and committed to `surview-dev`: `FeatureAffine3D`'s
wy/wz copy-paste bug, `CameraCalibrator`'s missing `Calibration::prepare()`
call and an epipolar-residual distortion-domain mismatch, `CrackResidual2D`
missing its `zncc<0` gate, `SimplexMatch2D` discarding its own convergence
flag, and an `oc_io.cpp` cluster (sentinel values leaking into exported
heat-maps, a binary/text file-mode mismatch, missing CSV bounds-checking,
a save function with no load counterpart).

Remaining findings — items needing real design judgment before fixing
(e.g. `Uncertainty2D::beta`'s math may be measuring the wrong thing
entirely), plus a longer tail of smaller/structural findings — are tracked
in [fork issue #17](https://github.com/katalystnord/OpenCorr/issues/17),
not itemized here.

## Not yet decided — pending from 2026-07-17 research

Two research passes (competitive review of 11 open/commercial DIC GUIs;
deep OpenCorr source review) surfaced findings not yet turned into scope
decisions:

- **Calibration module**: adopt live numeric pose-coaching UX (angle/height
  variance thresholds, color-coded pass/fail, per GOM/ZEISS), a visual
  coverage heat-map (per MatchID), and drag-to-exclude-outlier on a live
  reprojection-error chart (per MATLAB Stereo Camera Calibrator/DuoDIC —
  the strongest single calibration screen found across the competitive
  review). Engine-side detection/quality-metric is now fully implemented
  (checkerboard and dot-target, both mono and stereo — see Engine capability
  roadmap above); the GUI/UX itself is still to be designed.
- **Uncertainty quantification module**: engine-side implementation now
  scoped (DICe's sigma/beta formulas — see Engine capability roadmap
  above); how to surface it in the GUI (quality heatmap layer, per-POI
  overlay) is still to be designed.
- **ROI tooling**: auto-detect/threshold-based segmentation was unclaimed by
  every tool reviewed (open or commercial) — engine-side implementation now
  done (`AutoROI`/`SpeckleQualityMap`, fork issues #11/#12 — MIG/SSSIG/
  SIFT-density quality maps feeding an Otsu-threshold segmentation into the
  existing `Polygon2D` ROI model). Known limitation: single-region-only, no
  hole support. The GUI/UX for surfacing this (live quality indicator,
  auto-detect button/workflow) is still to be designed.
- **VTK `.vtu` export**: confirmed a real differentiator empirically — only
  1 of 11 tools reviewed has any VTK-family export. Reinforces the existing
  VTK decision; no new action needed.
- **Build integration**: OpenCorr has no upstream CMake library target (we
  write our own); dependencies are Eigen 3.4.0, OpenCV 4.10.0, FFTW 3.3.5,
  nanoflann 1.7.0, OpenMP; Linux needs two small header-include patches (one
  still open upstream and unfixed: `oc_feature_affine.cpp` missing the
  `random` header).
