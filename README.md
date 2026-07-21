# SurView DIC

A cross-platform, ecosystem-native GUI for Digital Image Correlation (DIC/DVC),
wrapping the [OpenCorr](https://github.com/vincentjzy/OpenCorr) engine.

Built on Qt (application shell) and VTK (rendering, field data, `.vtu`/`.vtk`
export compatible with ParaView and FreeCAD's FEM workbench).

See [CLAUDE.md](CLAUDE.md) for design rationale and roadmap.

## Status

Early scaffolding. Builds and runs (Qt shell + VTK link) on Linux with
Qt6, VTK 9.5, CMake ≥ 3.21.

## License

LGPL-2.1-or-later. See [LICENSE](LICENSE).
