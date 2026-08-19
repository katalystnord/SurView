#!/usr/bin/env python3
"""Generate synthetic speckle examples with an exactly known answer.

WHY THESE ARE NOT INTERPOLATED. The obvious way to make a deformed speckle
image is to render one pattern and then warp the pixels. That makes OUR
interpolation part of the ground truth, so an example built to demonstrate
accuracy would carry our own resampling error inside the answer it claims -- and
a user comparing SurView's result against it would be measuring the generator as
much as the engine.

Instead the pattern is analytic: a list of Gaussian blobs. A uniform deformation
gradient F maps a Gaussian to another Gaussian exactly -- the centre moves to
F @ c and the covariance becomes F @ S @ F.T -- so every frame is rendered from
first principles at its own deformation. Nothing is ever resampled. The only
approximation left is pixel integration, and that is handled by supersampling,
which is what a real sensor does anyway.

The displacement each frame encodes is therefore exact by construction, and is
written out beside the images so the example has an ANSWER rather than just
pictures.

Usage:
    tools/make-synthetic-examples.py [output-directory]
"""

import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

# --- what a good speckle pattern looks like ---------------------------------
# Dot size around 3-5 px and roughly half the area covered is the usual advice
# for DIC: smaller and the pattern aliases against the sensor, larger and a
# subset contains too few features to locate.
WIDTH, HEIGHT = 640, 480
BLOB_COUNT = 9000
BLOB_SIGMA_RANGE = (1.1, 2.1)   # px; a dot is roughly 4 sigma across
SUPERSAMPLE = 4                 # sub-samples per pixel per axis
BACKGROUND = 235.0              # bright field, dark speckle
BLOB_DEPTH = 200.0              # how far a dot pulls the background down
NOISE_GREY_LEVELS = 1.2         # sensor noise, so the reliability metrics have
                                # something real to report
SEED = 20260819


def make_pattern(rng):
    """Random speckle as an analytic list of Gaussian blobs."""
    centres = np.column_stack([
        rng.uniform(-8.0, WIDTH + 8.0, BLOB_COUNT),
        rng.uniform(-8.0, HEIGHT + 8.0, BLOB_COUNT),
    ])
    sigmas = rng.uniform(*BLOB_SIGMA_RANGE, BLOB_COUNT)
    return centres, sigmas


def render(centres, sigmas, F, rng):
    """Render the pattern under deformation gradient F, exactly.

    A Gaussian blob under a uniform affine map stays Gaussian: its centre moves
    to F @ c and its covariance sigma^2 I becomes F @ (sigma^2 I) @ F.T. So the
    deformed frame is rendered from the pattern itself rather than warped from
    another image, and carries no resampling error at all.
    """
    # Pixel centres, supersampled. The +0.5 puts samples at pixel centres, and
    # averaging them integrates the pattern over the pixel the way a sensor
    # collects light over its area.
    step = 1.0 / SUPERSAMPLE
    offsets = (np.arange(SUPERSAMPLE) + 0.5) * step - 0.5

    accumulated = np.zeros((HEIGHT, WIDTH), dtype=np.float64)

    moved = centres @ F.T
    covariance = F @ F.T
    # Inverse covariance per blob is (sigma^2 * F F^T)^-1.
    inverse = np.linalg.inv(covariance)
    # How far out to bother evaluating: past about 3.5 sigma a Gaussian
    # contributes less than a grey level.
    spread = math.sqrt(max(covariance[0, 0], covariance[1, 1]))
    reach = 3.5 * BLOB_SIGMA_RANGE[1] * spread

    for dy in offsets:
        for dx in offsets:
            frame = np.full((HEIGHT, WIDTH), BACKGROUND, dtype=np.float64)
            _splat(frame, moved, sigmas, inverse, reach, dx, dy)
            accumulated += frame

    image = accumulated / (SUPERSAMPLE * SUPERSAMPLE)
    if NOISE_GREY_LEVELS > 0.0:
        image += rng.normal(0.0, NOISE_GREY_LEVELS, image.shape)
    return np.clip(image, 0.0, 255.0).astype(np.uint8)


def _splat(frame, centres, sigmas, inverse, reach, dx, dy):
    """Add every blob into the frame, each over its own small footprint."""
    for (cx, cy), sigma in zip(centres, sigmas):
        radius = reach * sigma / BLOB_SIGMA_RANGE[1]
        x0 = max(0, int(math.floor(cx - radius)))
        x1 = min(frame.shape[1], int(math.ceil(cx + radius)) + 1)
        y0 = max(0, int(math.floor(cy - radius)))
        y1 = min(frame.shape[0], int(math.ceil(cy + radius)) + 1)
        if x0 >= x1 or y0 >= y1:
            continue

        xs = np.arange(x0, x1) + dx - cx
        ys = np.arange(y0, y1) + dy - cy
        gx, gy = np.meshgrid(xs, ys)

        # Mahalanobis distance under the deformed covariance.
        q = (inverse[0, 0] * gx * gx
             + (inverse[0, 1] + inverse[1, 0]) * gx * gy
             + inverse[1, 1] * gy * gy) / (sigma * sigma)
        frame[y0:y1, x0:x1] -= BLOB_DEPTH * np.exp(-0.5 * q)


def deformation_of(name, amount):
    """The deformation gradient each example encodes."""
    if name == "translation":
        return np.eye(2), np.array([amount, 0.0])
    if name == "tension":
        # Uniaxial tension with a Poisson contraction, which is what a real
        # coupon does and what makes eyy worth looking at as well as exx.
        poisson = 0.33
        return np.diag([1.0 + amount, 1.0 - poisson * amount]), np.zeros(2)
    raise ValueError(name)


def write_set(out, name, steps, rng):
    centres, sigmas = make_pattern(rng)
    frames = []

    for index, amount in enumerate(steps):
        F, shift = deformation_of(name, amount)
        moved = centres + shift
        image = render(moved, sigmas, F, rng)

        filename = f"{name}_{index:02d}.tif"
        Image.fromarray(image).save(out / filename)

        # The exact answer, at the four corners and the centre, so a reader can
        # check any point without re-deriving the field.
        def displacement(x, y):
            p = F @ np.array([x, y]) + shift
            return [float(p[0] - x), float(p[1] - y)]

        frames.append({
            "file": filename,
            "amount": amount,
            "deformation_gradient": F.tolist(),
            "rigid_shift_px": shift.tolist(),
            "displacement_at": {
                "top_left": displacement(0.0, 0.0),
                "centre": displacement(WIDTH / 2.0, HEIGHT / 2.0),
                "bottom_right": displacement(float(WIDTH), float(HEIGHT)),
            },
        })
        print(f"  {filename}  {name} {amount:+.4g}")

    return frames


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "examples/synthetic")
    out.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(SEED)

    print("Sub-pixel translation:")
    translation = write_set(out, "translation", [0.0, 0.25, 0.5, 1.0, 2.5], rng)

    print("Uniaxial tension:")
    tension = write_set(out, "tension",
                        [0.0, 0.005, 0.010, 0.020, 0.035, 0.055], rng)

    answer = {
        "produced_by": "tools/make-synthetic-examples.py",
        "seed": SEED,
        "image": {
            "width": WIDTH,
            "height": HEIGHT,
            "bit_depth": 8,
            "supersample": SUPERSAMPLE,
            "noise_grey_levels": NOISE_GREY_LEVELS,
        },
        "how_the_answer_is_exact": (
            "The pattern is a list of Gaussian blobs, not an image. A uniform "
            "deformation gradient maps a Gaussian to another Gaussian exactly, "
            "so every frame is rendered from the pattern at its own deformation "
            "rather than warped from a previous frame. No pixel is ever "
            "resampled, so the displacement below is exact by construction "
            "rather than accurate to within the generator's interpolation. "
            "Pixel integration is by supersampling, which is what a sensor "
            "does. Sensor noise is added after rendering."
        ),
        "coordinate_frame": (
            "Image pixels: x right, y down, origin at the top-left pixel "
            "centre, matching SurView's own frame. The reference frame of each "
            "set is its 00 image."
        ),
        "sets": {
            "translation": {
                "what_it_shows": (
                    "Rigid sub-pixel translation along x. There is no strain "
                    "anywhere, so a strain map of these is a map of the "
                    "method's own noise."
                ),
                "frames": translation,
            },
            "tension": {
                "what_it_shows": (
                    "Uniaxial tension along x with a Poisson contraction of "
                    "0.33 in y, in rising load steps. Displacement grows from "
                    "the origin, so exx is uniform while displacement is not."
                ),
                "frames": tension,
            },
        },
    }

    with open(out / "ground_truth.json", "w") as f:
        json.dump(answer, f, indent=2)
        f.write("\n")

    print(f"\nWrote {out}/ground_truth.json")


if __name__ == "__main__":
    main()
