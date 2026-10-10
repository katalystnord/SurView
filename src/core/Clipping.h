#pragma once

#include <functional>

// How much of a subset the camera recorded nothing in.
//
// WHY THIS EXISTS. Where the sensor ran out of range -- glare off a shiny or
// wet specimen, a background lit past white, a shadow crushed to black -- every
// pixel holds the same number, and the speckle in it is gone rather than merely
// bright or dark. A subset made mostly of such pixels has no gradient for the
// correlation to lock onto, and it can still converge: flat matches flat, with
// a correlation near 1 and a displacement of nothing in particular. Found on
// the real tension example, where five subsets in glare off the background,
// 99.6 per cent at 255, were counted as solved with a correlation of 0.96 to
// 1.00. Drawing a region keeps the background out; glare ON the specimen it
// cannot, and this is what says so, point by point.
//
// ⚑ JUDGED AGAINST THE EXTREMES THE IMAGE ACTUALLY HOLDS, not only the pixel
// type's limits, for the reason ImageRecord and the pixel readout already give:
// 12-bit sensor data stored in a 16-bit file clips at 4095 while the type
// allows 65535, so a rule that only knew about 65535 would find no clipping in
// an image that is a third white.
//
// ⚑ A PROPERTY OF THE REFERENCE SUBSET, stated beside the measurement rather
// than used to refuse it. Whether a mostly clipped subset should also be
// refused is a separate decision, and kept separate on purpose: a figure on
// screen loses nothing, a refusal does.

// The share, 0 to 1, of the pixels in the (2r + 1) square centred on
// (`centreX`, `centreY`) whose value equals `darkest` or `brightest`.
// `pixel(x, y)` gives the reference image's value, x right and y down. Pixels
// of the square outside the image are not counted either way, so a subset
// reaching past an edge is judged on what it has. Negative when the square
// holds no pixel of the image at all, or `radius` is negative: no share, which
// is not a share of zero.
double clippedShare(const std::function<double(int x, int y)> &pixel, int width,
                    int height, int centreX, int centreY, int radius,
                    double darkest, double brightest);

// Above this share a subset is reported as MOSTLY clipped. One half, chosen
// against the only data that bears on it: on the tension example the glare
// subsets sit at 87 per cent or more and the specimen's at about 10 per cent
// or less, so a half separates them with room either side.
constexpr double kMostlyClipped = 0.5;
