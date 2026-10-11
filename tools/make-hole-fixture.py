#!/usr/bin/env python3
"""Render tests/fixtures/hole_reference.tif and hole_target.tif.

A speckled specimen with a round hole through it, moving by an exact
translation, and a second, static speckle behind it that shows through the
hole. That is the case masked subsets exist for: a subset beside the hole
otherwise correlates part of a pattern that did not move.

Rendered the way tools/make-synthetic-examples.py renders, analytically and
never by warping pixels, so the movement is exact by construction:

- specimen: Gaussian dots at material positions X; in the target each dot sits
  at X + (2.4, -1.3), so every specimen pixel moved by exactly that.
- hole: material points within 20 px of (120, 100) show the background instead.
  In the target the hole has moved with the specimen to (122.4, 98.7).
- background: a separate set of dots in IMAGE positions, identical in both
  frames, so what shows through the hole stayed put.
- supersampled 4 x 4 per pixel, no noise, written as 8-bit TIFF.

A SEQUENCE as well, hole_sequence_1.tif to hole_sequence_3.tif, measured
from hole_reference.tif: the same specimen, hole and background, but deformed
as well as moved, x = X + t + G (X - C) with C = (110, 95) and

    frame 1: t = (6.5, -4.2), G = [[-0.010, -0.008], [-0.006,  0.010]]
    frame 2: t = (5.5, -3.6), G = [[ 0.030,  0.022], [ 0.024, -0.026]]
    frame 3: t = (3.1, -1.9), G = [[ 0.012,  0.034], [ 0.028, -0.010]]

so the displacement differs from place to place, every term of every
gradient is non-zero and differs from frame to frame, and the hole is both
moved (7.6 px by frame 1) and distorted between frames. That is what a cut
carried to a re-anchored reference has to follow, and what a point measured
between pixels has to be carried back across: a translation alone could be
followed by moving every cut by one mean displacement, and a gradient term
that is zero cannot be told from one that was dropped.

The tests state these properties and measure against them; this script is the
record of how the images were made, not something the tests run.

Usage:
    tools/make-hole-fixture.py [output-directory]
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

WIDTH, HEIGHT = 240, 200
SHIFT = (2.4, -1.3)
HOLE = (120.0, 100.0, 20.0)
SUPERSAMPLE = 4
SEED = 20261011


def dots(rng, count, margin):
    x = rng.uniform(-margin, WIDTH + margin, count)
    y = rng.uniform(-margin, HEIGHT + margin, count)
    sigma = rng.uniform(1.1, 2.1, count)
    return x, y, sigma


def speckle(px, py, field):
    """Bright paint with dark dots: 235 minus the dots, as the examples are."""
    x, y, sigma = field
    out = np.zeros_like(px)
    for cx, cy, s in zip(x, y, sigma):
        near = (np.abs(px - cx) < 4 * s) & (np.abs(py - cy) < 4 * s)
        if near.any():
            out[near] += np.exp(-((px[near] - cx) ** 2 + (py[near] - cy) ** 2) / (2 * s * s))
    return 235.0 - 200.0 * np.clip(out, 0.0, 1.0)


def render(shift, specimen, background):
    offsets = (np.arange(SUPERSAMPLE) + 0.5) / SUPERSAMPLE - 0.5
    sx = (np.arange(WIDTH)[None, :, None, None] + offsets[None, None, None, :])
    sy = (np.arange(HEIGHT)[:, None, None, None] + offsets[None, None, :, None])
    px = np.broadcast_to(sx, (HEIGHT, WIDTH, SUPERSAMPLE, SUPERSAMPLE)).astype(np.float64)
    py = np.broadcast_to(sy, (HEIGHT, WIDTH, SUPERSAMPLE, SUPERSAMPLE)).astype(np.float64)

    # the material point now at each sample
    mx, my = px - shift[0], py - shift[1]
    in_hole = (mx - HOLE[0]) ** 2 + (my - HOLE[1]) ** 2 <= HOLE[2] ** 2

    value = np.where(in_hole, speckle(px, py, background), speckle(mx, my, specimen))
    return np.clip(np.round(value.mean(axis=(2, 3))), 0, 255).astype(np.uint8)


CENTRE = (110.0, 95.0)
SEQUENCE = [
    ((6.5, -4.2), ((-0.010, -0.008), (-0.006, 0.010))),
    ((5.5, -3.6), ((0.030, 0.022), (0.024, -0.026))),
    ((3.1, -1.9), ((0.012, 0.034), (0.028, -0.010))),
]


def material(px, py, t, g):
    """The material point X now at image position x = X + t + G (X - C)."""
    # x - C - t = (I + G)(X - C)
    a, b = 1.0 + g[0][0], g[0][1]
    c, d = g[1][0], 1.0 + g[1][1]
    det = a * d - b * c
    rx, ry = px - CENTRE[0] - t[0], py - CENTRE[1] - t[1]
    return (d * rx - b * ry) / det + CENTRE[0], (-c * rx + a * ry) / det + CENTRE[1]


def render_deformed(t, g, specimen, background):
    offsets = (np.arange(SUPERSAMPLE) + 0.5) / SUPERSAMPLE - 0.5
    sx = (np.arange(WIDTH)[None, :, None, None] + offsets[None, None, None, :])
    sy = (np.arange(HEIGHT)[:, None, None, None] + offsets[None, None, :, None])
    px = np.broadcast_to(sx, (HEIGHT, WIDTH, SUPERSAMPLE, SUPERSAMPLE)).astype(np.float64)
    py = np.broadcast_to(sy, (HEIGHT, WIDTH, SUPERSAMPLE, SUPERSAMPLE)).astype(np.float64)

    mx, my = material(px, py, t, g)
    in_hole = (mx - HOLE[0]) ** 2 + (my - HOLE[1]) ** 2 <= HOLE[2] ** 2

    value = np.where(in_hole, speckle(px, py, background), speckle(mx, my, specimen))
    return np.clip(np.round(value.mean(axis=(2, 3))), 0, 255).astype(np.uint8)


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "tests" / "fixtures"
    rng = np.random.default_rng(SEED)
    specimen = dots(rng, 2600, 12)
    background = dots(rng, 2600, 12)
    Image.fromarray(render((0.0, 0.0), specimen, background)).save(out / "hole_reference.tif")
    Image.fromarray(render(SHIFT, specimen, background)).save(out / "hole_target.tif")
    for frame, (t, g) in enumerate(SEQUENCE, start=1):
        Image.fromarray(render_deformed(t, g, specimen, background)).save(
            out / f"hole_sequence_{frame}.tif")


if __name__ == "__main__":
    main()
