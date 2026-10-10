#pragma once

#include "core/Roi.h"

#include <QString>

#include <functional>

// Proposing a region of interest by segmenting the speckled part of an image.
//
// One of the two files that name an OpenCorr type (see Correlation.h); the
// detection itself is the engine's AutoROI, not a second implementation living
// here.

struct RoiDetection
{
    bool found = false;
    RegionOfInterest roi;

    // Why nothing was proposed, in terms of what was actually looked for. The
    // detector declines on purpose in several distinct situations, and
    // "detection failed" would collapse them into one unactionable sentence.
    QString reason;

    // How long the pass took. Reported because it is a whole-image gradient and
    // segmentation pass, and on a large image that is a wait the user just sat
    // through without being told what it bought.
    double secondsElapsed = 0.0;
};

// Segment the largest speckled region of `imagePath` into a boundary.
//
// Runs on the calling thread. Blocking is deliberate at this size: the pass is
// a windowed gradient map plus a threshold and a contour trace, without the
// per-point solving a correlation does.
RoiDetection detectSpeckleRegion(const QString &imagePath);

// Whether a pixel is inside `roi`, asked of the ENGINE's own shapes: Polygon2D
// for a polygon and for a rectangle's four corners, Ellipse2D for an ellipse,
// applied in the region's order (core/Roi.h). This is what a run uses, so the
// boundary means the same thing to the measurement as to the engine everywhere
// else; it lives here so that no third file has to name an OpenCorr type. A
// region that is not valid contains nothing.
std::function<bool(int x, int y)> regionInsideTest(const RegionOfInterest &roi);
