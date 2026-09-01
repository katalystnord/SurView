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


def required_margin(name, steps):
    """How far beyond the picture the pattern has to extend, in px.

    ⚑ A deformed frame is filled with material that was somewhere ELSE in the
    reference, and for rotation that somewhere is well outside the picture: a
    15 degree turn of a 640x480 frame draws its corners from up to 160 px past
    the top and bottom edges. Generated only 8 px beyond, as the pattern
    originally was, those corners come out as bare background -- a large,
    perfectly smooth region that no correlation can track and that looks like a
    fault in the instrument rather than in the example.

    Computed from the deformations themselves rather than guessed, so a set
    added later cannot quietly reintroduce it. Every corner of the picture is
    mapped BACK through each step to find where its material came from.
    """
    corners = np.array([[0.0, 0.0], [WIDTH, 0.0], [0.0, HEIGHT], [WIDTH, HEIGHT]])
    worst = 0.0
    for amount in steps:
        F, shift = deformation_of(name, amount)
        source = (corners - shift) @ np.linalg.inv(F).T
        worst = max(worst,
                    float(np.max(np.abs(source[:, 0] - np.clip(source[:, 0], 0.0, WIDTH)))),
                    float(np.max(np.abs(source[:, 1] - np.clip(source[:, 1], 0.0, HEIGHT)))))
    return math.ceil(worst) + 8


def make_pattern(rng, margin=8, count=None, sigma_range=BLOB_SIGMA_RANGE):
    """Random speckle as an analytic list of Gaussian blobs.

    The defaults are a GOOD pattern. `count` and `sigma_range` are arguments so
    a deliberately poor one can be made too: sparse, soft speckle carries little
    gradient energy, which is exactly what the noise floor is computed from.

    A larger `margin` does not thin the speckle out: the count is scaled with
    the area so DENSITY is what stays fixed, which is what decides whether a
    pattern is good.
    """
    density = BLOB_COUNT / ((WIDTH + 16.0) * (HEIGHT + 16.0))
    area = (WIDTH + 2.0 * margin) * (HEIGHT + 2.0 * margin)
    if count is None:
        count = int(round(density * area))
    centres = np.column_stack([
        rng.uniform(-margin, WIDTH + margin, count),
        rng.uniform(-margin, HEIGHT + margin, count),
    ])
    sigmas = rng.uniform(*sigma_range, count)
    return centres, sigmas


def estimate_noise_floor(image, radius=16):
    """The finest displacement this pattern can resolve, in px, per DIC's sigma.

    sqrt(2 * noise^2 / min(sum gx^2, sum gy^2)) over a subset, which is the
    same expression the engine's Uncertainty2D evaluates. Two differences worth
    knowing: the noise here is the exact figure the generator added rather than
    an estimate recovered from the picture, and this samples subsets on a coarse
    lattice rather than measuring at every point.

    ⚑ Computed rather than asserted. Whether a set's movement is below what its
    speckle can resolve is a property of the PIXELS, and writing it into the
    answer file as prose would be a claim nothing checks. The generator reads it
    off the frames it just rendered.
    """
    gy, gx = np.gradient(image.astype(np.float64))
    floors = []
    for cy in range(radius + 20, HEIGHT - radius - 20, 60):
        for cx in range(radius + 20, WIDTH - radius - 20, 60):
            sx = gx[cy - radius:cy + radius + 1, cx - radius:cx + radius + 1]
            sy = gy[cy - radius:cy + radius + 1, cx - radius:cx + radius + 1]
            energy = min((sx * sx).sum(), (sy * sy).sum())
            floors.append(math.sqrt(2.0 * NOISE_GREY_LEVELS ** 2 / energy))
    return float(np.mean(floors)), float(np.max(floors))


def check_speckled_everywhere(image, filename):
    """Refuse to ship a frame with a featureless region in it.

    Independent of the margin arithmetic above, and that is the point: this
    looks at the rendered pixels and asks whether every part of the picture
    actually carries speckle. It is what caught the bare corners, and it will
    catch them again whatever the cause.
    """
    tile = 40
    flat = []
    for y in range(0, HEIGHT - tile + 1, tile):
        for x in range(0, WIDTH - tile + 1, tile):
            if image[y:y + tile, x:x + tile].std() < 8.0:
                flat.append((x, y))
    if flat:
        raise SystemExit(
            f"{filename}: {len(flat)} tile(s) carry no speckle, the first at "
            f"{flat[0]}. The pattern does not reach far enough beyond the "
            f"picture for this deformation."
        )


def place(centres, F, shift):
    """Where the pattern sits under deformation gradient F and rigid shift.

    ⚑ ONE function, used both to render a frame and to state the answer in
    ground_truth.json. They were separate, and they disagreed: write_set()
    shifted the centres and then applied F, giving F @ (c + shift), while the
    reported displacement used F @ c + shift. Every set that existed had either
    F = I or shift = 0, so the two agreed by accident and nothing was wrong on
    disk. The first set with both -- a rotation about the image centre, which
    needs a shift to keep the specimen in frame -- would have shipped a picture
    and an answer that were not of the same thing.

    Fixed by construction rather than by testing for it: there is now no second
    formula that can drift from this one.
    """
    return centres @ F.T + shift


def render(centres, sigmas, F, shift, rng):
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

    moved = place(centres, F, shift)
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
    if name in ("tension", "large_strain"):
        # Uniaxial tension with a Poisson contraction, which is what a real
        # coupon does and what makes eyy worth looking at as well as exx.
        poisson = 0.33
        return np.diag([1.0 + amount, 1.0 - poisson * amount]), np.zeros(2)
    if name == "rotation":
        # Rigid rotation about the image centre, `amount` in degrees. About the
        # CENTRE rather than the origin, so the specimen stays in frame: that is
        # what the rigid shift is for, and it is the case that exposed the
        # disagreement place() now settles.
        theta = math.radians(amount)
        R = np.array([[math.cos(theta), -math.sin(theta)],
                      [math.sin(theta), math.cos(theta)]])
        centre = np.array([WIDTH / 2.0, HEIGHT / 2.0])
        return R, centre - R @ centre
    if name == "shear":
        # Simple shear: the only set that puts anything in exy, which the other
        # sets leave at zero throughout.
        return np.array([[1.0, amount], [0.0, 1.0]]), np.zeros(2)
    if name == "faint":
        # Movement far below what the speckle can resolve. Not a deformation
        # worth measuring -- the point is that it CANNOT be measured, and that
        # the instrument should say so rather than report the number anyway.
        return np.eye(2), np.array([amount, 0.0])
    raise ValueError(name)


def write_set(out, name, steps, rng, pattern=None):
    if pattern is None:
        pattern = make_pattern(rng, margin=required_margin(name, steps))
    centres, sigmas = pattern
    frames = []

    for index, amount in enumerate(steps):
        F, shift = deformation_of(name, amount)
        image = render(centres, sigmas, F, shift, rng)

        filename = f"{name}_{index:02d}.tif"
        check_speckled_everywhere(image, filename)
        Image.fromarray(image).save(out / filename)

        # The exact answer, at the corners and the centre, so a reader can
        # check any point without re-deriving the field.
        def displacement(x, y):
            here = np.array([[x, y]])
            p = place(here, F, shift)[0]
            return [float(p[0] - x), float(p[1] - y)]

        mean_floor, worst_floor = estimate_noise_floor(image)

        # ⚑ The LARGEST movement in the picture, not the movement at the
        # centre. Rotation is about the centre, so the centre does not move at
        # all: measured there, every rotation frame reported itself as moving
        # less than the noise floor while the corners swept through 80 px. The
        # first version of this field did exactly that.
        moved_by = max(
            math.hypot(*displacement(x, y))
            for x, y in [(0.0, 0.0), (WIDTH, 0.0), (0.0, HEIGHT),
                         (WIDTH, HEIGHT), (WIDTH / 2.0, HEIGHT / 2.0)]
        )

        frames.append({
            "file": filename,
            "amount": amount,
            "deformation_gradient": F.tolist(),
            "rigid_shift_px": shift.tolist(),
            "noise_floor_px": {
                "mean": mean_floor,
                "worst": worst_floor,
                "what_it_is": (
                    "DIC's sigma over a 33 px subset, computed from this "
                    "frame's own gradients and the exact sensor noise added to "
                    "it. The finest displacement this speckle could resolve."
                ),
            },
            "largest_movement_px": moved_by,
            "movement_is_below_the_noise_floor": bool(moved_by <= mean_floor),
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

    print("Rigid rotation:")
    rotation = write_set(out, "rotation", [0.0, 1.0, 3.0, 7.0, 15.0], rng)

    print("Simple shear:")
    shear = write_set(out, "shear", [0.0, 0.004, 0.010, 0.020, 0.040], rng)

    print("Large strain:")
    large_strain = write_set(out, "large_strain",
                             [0.0, 0.04, 0.09, 0.15, 0.22, 0.30], rng)

    print("Movement below the noise floor, on poor speckle:")
    # Sparse, soft dots: a quarter of the blobs at nearly twice the width, so a
    # subset holds little gradient energy and the noise floor rises to meet the
    # movement.
    poor = make_pattern(rng, margin=required_margin("faint", [0.008]),
                        count=BLOB_COUNT // 4, sigma_range=(2.6, 4.2))
    faint = write_set(out, "faint", [0.0, 0.002, 0.004, 0.008], rng,
                      pattern=poor)

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
            "rotation": {
                "what_it_shows": (
                    "Rigid rotation about the image centre, 0 to 15 degrees. "
                    "There is NO strain anywhere at any step: the specimen is "
                    "not deformed, only turned. Which is the point. A strain "
                    "measure that reports rotation as strain is wrong, and at "
                    "15 degrees the difference between the measures SurView "
                    "offers is large enough to see rather than argue about."
                ),
                "frames": rotation,
            },
            "shear": {
                "what_it_shows": (
                    "Simple shear, the only set that puts anything into exy. "
                    "The other sets leave the shear component at zero "
                    "throughout, so nothing else here would catch a fault in "
                    "it."
                ),
                "frames": shear,
            },
            "large_strain": {
                "what_it_shows": (
                    "The same uniaxial tension carried to 30 percent, far "
                    "enough that a subset no longer resembles its counterpart "
                    "in the original reference and correlation against it "
                    "degrades. This is the set reference updating exists for: "
                    "run it with re-anchoring off and then on, and compare how "
                    "many points survive to the last frame."
                ),
                "frames": large_strain,
            },
            "faint": {
                "what_it_shows": (
                    "Tiny movement on deliberately poor speckle: sparse, soft "
                    "dots carrying little gradient energy, so each subset's "
                    "noise floor is large. The displacements are real and are "
                    "stated exactly here, and they STRADDLE what the pattern "
                    "can resolve -- see movement_is_below_the_noise_floor on "
                    "each frame, which is measured from the rendered pixels "
                    "rather than asserted. A correlation returns "
                    "confident-looking numbers throughout. That is the failure "
                    "the per-point noise floor exists to make visible, and the "
                    "straddle is the useful part: the same run should warn on "
                    "the early frames and stop warning on the last."
                ),
                "frames": faint,
            },
        },
    }

    with open(out / "ground_truth.json", "w") as f:
        json.dump(answer, f, indent=2)
        f.write("\n")

    print(f"\nWrote {out}/ground_truth.json")


if __name__ == "__main__":
    main()
