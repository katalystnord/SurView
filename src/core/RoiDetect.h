#pragma once

#include "core/Roi.h"

#include <QString>

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
