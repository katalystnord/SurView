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

## Status

Working, and narrow. Import a reference and target image, draw or auto-detect a
region of interest, run a correlation, read the displacement and strain fields
on screen, and export them as `.vtu` for ParaView or FreeCAD. Linux, Qt6,
VTK 9.5, CMake >= 3.21.

Run the tests - both SurView's and the pinned engine's - with:

```sh
tools/run-tests.sh
```

## License

LGPL-2.1-or-later. See [LICENSE](LICENSE).
