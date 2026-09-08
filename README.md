# SurView DIC

A cross-platform, ecosystem-native GUI for Digital Image Correlation (DIC/DVC),
built on the [OpenCorr](https://github.com/vincentjzy/OpenCorr) engine.

Built on Qt (application shell) and VTK (rendering, field data, `.vtu`/`.vtk`
export compatible with ParaView and FreeCAD's FEM workbench).

See [CLAUDE.md](CLAUDE.md) for design rationale and roadmap.

## Engine

SurView builds against [katalystnord/OpenCorr](https://github.com/katalystnord/OpenCorr)
(branch `surview-dev`), our working fork of OpenCorr - a staging area for
SurView-driven capability work and for contributions back upstream, not a
competing version. Small, generically-useful fixes go upstream directly.

The exact engine commit is recorded in [`cmake/opencorr.pin`](cmake/opencorr.pin)
and bumped as its own visible commit, so a build is always traceable to one
engine revision. CMake checks the pin at configure time and **warns without
failing** if your checkout differs - it expects the fork beside this repo, or
pass `-DSURVIEW_OPENCORR_DIR=<path>`.

To verify the whole chain (upstream → fork → pin), including whether fixes we
sent upstream are actually present in the fork's source:

```sh
tools/check-engine.sh [path-to-OpenCorr-checkout]
```

## Packaging

A single-file AppImage, which is how somebody who did not build SurView gets to
run it:

```sh
tools/make-appimage.sh          # dist/SurView-<version>-x86_64.AppImage
tools/make-appimage.sh --stage-only   # build and stage only, no packaging tools needed
```

It needs `patchelf`, `linuxdeploy` and `linuxdeploy-plugin-qt` on PATH; the
script names what is missing rather than substituting anything. The AppImage
carries Qt, VTK and OpenCV with it, so it runs on a machine that has never had
them installed - which is the point, and also why it is large. It refuses to
package against an OpenCorr checkout that is not the pinned commit: every field
SurView exports states that commit, and a package built against something else
would attribute measurements to an engine that did not make them.

## Status

Working, and narrow. Import a reference image and a sequence of targets, draw or
auto-detect a region of interest, run the correlation, step through the measured
frames reading displacement, strain and per-point reliability, and export the
series as numbered `.vtu` files for ParaView or FreeCAD. Linux, Qt6, VTK 9.5,
CMake >= 3.21.

Run the tests - both SurView's and the pinned engine's - with:

```sh
tools/run-tests.sh
```

## License

LGPL-2.1-or-later. See [LICENSE](LICENSE).
