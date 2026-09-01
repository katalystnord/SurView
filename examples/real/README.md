# Real speckle sequences

Three real-experiment sequences, so SurView can be pointed at photographs of an
actual specimen and not only at patterns we generated ourselves. The synthetic
sets next door carry an exactly known answer and no real-world defect; these
carry every real-world defect and no known answer. Neither substitutes for the
other, which is why both ship.

Each folder is one experiment: `image_0000.png` is the undeformed reference and
`image_0001.png` to `image_0004.png` are successive loaded frames.

## Where they come from

Taken unmodified from the pyALDIC repository, `examples/quickstart/`, which is
released under the BSD 3-Clause licence reproduced in `LICENSE` beside this
file. They are downsampled crops of larger experiments; the full-resolution
originals are not published.

> Tong, Z. and Yang, J. (2026). pyALDIC: A Python Implementation of Augmented
> Lagrangian Digital Image Correlation with a GUI, Adaptive Meshing, and
> Mask-Aware Subset Splitting. arXiv:2607.22755.
> <https://doi.org/10.48550/arXiv.2607.22755>
>
> Software: <https://github.com/zachtong/pyALDIC>, DOI 10.5281/zenodo.19521071

Anyone publishing work measured from these images should cite that paper, and
so should we.

## The sets

### `01_tension_without_holes`

Uniaxial tension of an aluminium dog-bone specimen: a smooth uniaxial field.
The real counterpart of `../synthetic/tension`, and worth running against it -
the synthetic set states its answer exactly, this one shows what the same
measurement looks like with a real camera, real lighting and a real specimen in
front of it.

### `02_tension_with_holes`

Uniaxial tension of a perforated aluminium specimen, with stress concentration
around the holes. The one set here that needs a region of interest drawn around
something rather than over everything: the holes are not specimen, and a subset
straddling a hole edge is correlating against a boundary rather than a pattern.

### `03_rotation`

Rigid-body rotation of a speckled plate, displacement growing with distance from
the centre. ⚑ The pyALDIC authors state that the total rotation across this
sequence is too large for single-reference correlation, which decorrelates, and
that their incremental mode is required. That is our reference updating, and this
is the only example here whose need for it is attested by somebody other than us:
run it with re-anchoring off, then on, and compare how many points survive to the
last frame.

Their own recommended settings, as a starting point rather than as our defaults:
subset radius around 15 to 20 px, grid step 16 px, and a search range that covers
the per-step edge motion.

## What these cannot tell you

There is no ground truth here. The specimen's real deformation is not known
independently of a DIC measurement of it, so these sets show whether a run is
plausible, stable and complete - never whether it is correct. For correctness,
use `../synthetic`, where the answer is exact by construction.
