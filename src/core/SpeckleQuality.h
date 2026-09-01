#pragma once

#include <QString>

struct RegionOfInterest;

// What the speckle inside a region is good for, before anything is measured.
//
// Subset radius and region were chosen blind: both largely decide how reliable
// a run will be, and neither said anything about the result until a correlation
// had been sat through.
//
// ⚑ It reports a DISPLACEMENT RESOLUTION rather than a quality score. A bare
// score invites a threshold, and any threshold here would have to be invented,
// because what counts as good speckle depends on the movement being measured.
// This is the same expression the run reports per point afterwards, evaluated
// from the reference image alone, so it is in pixels and can be put against the
// movement expected.

struct SpeckleQuality
{
    bool measured = false;

    // Mean over the region of the engine's windowed maps, at the subset radius
    // asked about. Reported because they are the standard speckle figures and
    // somebody comparing patterns will want them.
    double meanMig = 0.0;
    double meanSssig = 0.0;

    // The reference image's own noise, in grey levels, by Immerkaer's estimator
    // -- the same one the run uses.
    double noiseStdDev = 0.0;

    // The finest displacement a subset of this radius should resolve on this
    // speckle, in pixels.
    double resolutionPx = 0.0;

    // What the number assumes, or why there is no number. Never empty when
    // something is offered.
    QString note;
};

// Estimate for `roi` on `imagePath`, for subsets of `subsetRadius`.
//
// Runs on the calling thread: a windowed gradient pass over the image, without
// the per-point solving a correlation does.
SpeckleQuality speckleQualityIn(const QString &imagePath,
                                const RegionOfInterest &roi, int subsetRadius);
