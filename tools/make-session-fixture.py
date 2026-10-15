#!/usr/bin/env python3
"""Render tests/fixtures/session_reference.tif and session_target.tif.

A reference and a target photographed in SEPARATE sessions: the camera was
moved between them, so the target sees the scene larger, turned and shifted,
in an image of a different size. Every other pair in this repository shares
one camera and one pixel grid.

The scene, in reference pixels (the reference camera sees the world directly):

  - a static rig covered in speckle;
  - a specimen disc of radius 60 about (120, 100) with its own speckle, which
    deforms between sessions by, with dX = X - 120 and dY = Y - 100,

        u = 1.3 + 0.012 dX + 0.004 dY
        v = -0.7 + 0.002 dX - 0.005 dY

    four different non-zero gradient terms, so no two can be confused;
  - the target camera: x = 1.07 R(3.5 deg) W + t, with t placing the
    specimen's centre (120, 100) at (140, 120) in the target.

Reference 240 x 200, target 280 x 240.

Exact for the reason the other fixtures are: the speckle is a sum of Gaussian
dots at MATERIAL positions (the specimen's) or WORLD positions (the rig's),
and each target sample shows the pattern at the point that has moved to it,
found here in closed form because both maps are affine. Supersampled 4 x 4
per pixel, no noise, 8-bit TIFF.

The tests state these numbers and measure against them; this script is the
record of how the images were made, not something the tests run.

Usage:
    tools/make-session-fixture.py [output-directory]
"""

import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

REF_W, REF_H = 240, 200
TAR_W, TAR_H = 280, 240
SUPERSAMPLE = 4
SEED = 20261015

CX, CY, DISC = 120.0, 100.0, 60.0
D0 = (1.3, -0.7)
E = ((0.012, 0.004), (0.002, -0.005))

SCALE = 1.07
ANGLE = math.radians(3.5)
COS, SIN = math.cos(ANGLE), math.sin(ANGLE)
# t places the specimen centre at (140, 120)
TX = 140.0 - SCALE * (COS * CX - SIN * CY)
TY = 120.0 - SCALE * (SIN * CX + COS * CY)


def unmap(px, py):
    """Target pixel -> world (reference) position."""
    rx, ry = (px - TX) / SCALE, (py - TY) / SCALE
    return COS * rx + SIN * ry, -SIN * rx + COS * ry


def undeform(x, y):
    """World position after deformation -> material point of the specimen."""
    a, b = 1 + E[0][0], E[0][1]
    c, d = E[1][0], 1 + E[1][1]
    det = a * d - b * c
    rx, ry = x - CX - D0[0], y - CY - D0[1]
    return CX + (d * rx - b * ry) / det, CY + (-c * rx + a * ry) / det


def dots(rng, count, x0, x1, y0, y1):
    x = rng.uniform(x0, x1, count)
    y = rng.uniform(y0, y1, count)
    sigma = rng.uniform(1.1, 2.1, count)
    return x, y, sigma


def speckle(px, py, field, base, depth):
    x, y, sigma = field
    out = np.zeros_like(px)
    for cx, cy, s in zip(x, y, sigma):
        near = (np.abs(px - cx) < 4 * s) & (np.abs(py - cy) < 4 * s)
        if near.any():
            out[near] += np.exp(-((px[near] - cx) ** 2 + (py[near] - cy) ** 2) / (2 * s * s))
    return base - depth * np.clip(out, 0.0, 1.0)


def samples(w, h):
    offsets = (np.arange(SUPERSAMPLE) + 0.5) / SUPERSAMPLE - 0.5
    sx = np.arange(w)[None, :, None, None] + offsets[None, None, None, :]
    sy = np.arange(h)[:, None, None, None] + offsets[None, None, :, None]
    shape = (h, w, SUPERSAMPLE, SUPERSAMPLE)
    return (np.broadcast_to(sx, shape).astype(np.float64),
            np.broadcast_to(sy, shape).astype(np.float64))


def scene(wx, wy, mx, my, rig, specimen):
    """The rig at world (wx, wy), unless the material point (mx, my) is on the disc."""
    on_disc = (mx - CX) ** 2 + (my - CY) ** 2 <= DISC * DISC
    out = speckle(wx, wy, rig, 225.0, 190.0)
    out[on_disc] = speckle(mx[on_disc], my[on_disc], specimen, 240.0, 200.0)
    return out


def to_image(value):
    return Image.fromarray(np.clip(np.round(value.mean(axis=(2, 3))), 0, 255).astype(np.uint8))


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "tests" / "fixtures"
    rng = np.random.default_rng(SEED)
    # the target sees world x from about -20 to 260, so the rig's dots cover more
    rig = dots(rng, 3600, -60, REF_W + 60, -60, REF_H + 60)
    specimen = dots(rng, 650, CX - DISC - 10, CX + DISC + 10, CY - DISC - 10, CY + DISC + 10)

    px, py = samples(REF_W, REF_H)
    to_image(scene(px, py, px, py, rig, specimen)).save(out / "session_reference.tif")

    px, py = samples(TAR_W, TAR_H)
    wx, wy = unmap(px, py)
    mx, my = undeform(wx, wy)
    to_image(scene(wx, wy, mx, my, rig, specimen)).save(out / "session_target.tif")


if __name__ == "__main__":
    main()
