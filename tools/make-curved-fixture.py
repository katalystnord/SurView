#!/usr/bin/env python3
"""Render tests/fixtures/curved_reference.tif and curved_target.tif.

A pair whose displacement field is QUADRATIC, so a second-order shape function
can describe it across a subset and a first-order one cannot. Every other
synthetic image in this repository moves by a uniform deformation gradient,
which first order represents exactly; on those the choice between the two
orders is invisible, and the line making it could be inverted with the suite
green (see tests/test_solver_choices.cpp).

With dX = X - 80 and dY = Y - 80, X the material point in the reference:

    u = 0.9 + 0.006 dX - 0.004 dY + 0.0012 dX^2 + 0.0007 dX dY + 0.0008 dY^2
    v = -0.6 + 0.003 dX + 0.005 dY - 0.0006 dX^2 + 0.0010 dX dY - 0.0011 dY^2

Every coefficient is non-zero and no two are equal, so no term can be
swapped or dropped without changing the field.

Rendered the way tools/make-hole-fixture.py renders, and exact for the same
reason: the speckle is a sum of Gaussian dots at MATERIAL positions, and each
target sample shows the pattern at the material point that has moved to it.
That holds for any invertible mapping, uniform or not; a dot is no longer a
Gaussian in the target, and it does not need to be. The one thing a curved
map changes is that the material point under a sample has no closed form, so
it is found by Newton's method on x = X + u(X), to 1e-12 px. The map has no
fold anywhere on the image (its Jacobian determinant stays above 0.5).

160 x 160, supersampled 4 x 4 per pixel, no noise, 8-bit TIFF. The tests
measure only within 40 px of the centre, where the displacement stays under
6 px; further out it is larger, and nothing is claimed about it.

The tests state these coefficients and measure against them; this script is
the record of how the images were made, not something the tests run.

Usage:
    tools/make-curved-fixture.py [output-directory]
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

SIZE = 160
CENTRE = 80.0
SUPERSAMPLE = 4
SEED = 20261012

# (constant, dX, dY, dX^2, dX dY, dY^2)
U = (0.9, 0.006, -0.004, 0.0012, 0.0007, 0.0008)
V = (-0.6, 0.003, 0.005, -0.0006, 0.0010, -0.0011)


def displacement(X, Y):
    dx, dy = X - CENTRE, Y - CENTRE
    terms = (1.0, dx, dy, dx * dx, dx * dy, dy * dy)
    return sum(c * t for c, t in zip(U, terms)), sum(c * t for c, t in zip(V, terms))


def jacobian(X, Y):
    """d(x, y) / d(X, Y) for x = X + u(X, Y)."""
    dx, dy = X - CENTRE, Y - CENTRE
    a = 1.0 + U[1] + 2 * U[3] * dx + U[4] * dy
    b = U[2] + U[4] * dx + 2 * U[5] * dy
    c = V[1] + 2 * V[3] * dx + V[4] * dy
    d = 1.0 + V[2] + V[4] * dx + 2 * V[5] * dy
    return a, b, c, d


def material(px, py):
    """The material point X with X + u(X) = (px, py), by Newton's method."""
    X, Y = px.copy(), py.copy()
    for _ in range(50):
        u, v = displacement(X, Y)
        rx, ry = X + u - px, Y + v - py
        if max(np.abs(rx).max(), np.abs(ry).max()) < 1e-12:
            return X, Y
        a, b, c, d = jacobian(X, Y)
        det = a * d - b * c
        X -= (d * rx - b * ry) / det
        Y -= (-c * rx + a * ry) / det
    raise RuntimeError("Newton did not converge")


def dots(rng, count, margin):
    x = rng.uniform(-margin, SIZE + margin, count)
    y = rng.uniform(-margin, SIZE + margin, count)
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


def samples():
    offsets = (np.arange(SUPERSAMPLE) + 0.5) / SUPERSAMPLE - 0.5
    sx = (np.arange(SIZE)[None, :, None, None] + offsets[None, None, None, :])
    sy = (np.arange(SIZE)[:, None, None, None] + offsets[None, None, :, None])
    shape = (SIZE, SIZE, SUPERSAMPLE, SUPERSAMPLE)
    return (np.broadcast_to(sx, shape).astype(np.float64),
            np.broadcast_to(sy, shape).astype(np.float64))


def to_image(value):
    return Image.fromarray(np.clip(np.round(value.mean(axis=(2, 3))), 0, 255).astype(np.uint8))


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "tests" / "fixtures"
    rng = np.random.default_rng(SEED)
    # a wide margin: toward the corners the material point under a target
    # sample lies up to about 25 px off the image
    specimen = dots(rng, 2400, 40)

    px, py = samples()
    a, b, c, d = jacobian(px, py)
    assert (a * d - b * c).min() > 0.5, "the map folds"

    to_image(speckle(px, py, specimen)).save(out / "curved_reference.tif")
    mx, my = material(px, py)
    to_image(speckle(mx, my, specimen)).save(out / "curved_target.tif")


if __name__ == "__main__":
    main()
