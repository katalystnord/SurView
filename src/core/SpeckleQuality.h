#pragma once

#include <QString>

#include <memory>

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

// One image's speckle, prepared for asking about any number of regions.
//
// ⚑ THE EXPENSIVE HALF DOES NOT DEPEND ON THE REGION. Reading the image,
// running the engine's windowed gradient pass over all of it and estimating the
// noise are decided by the image and the subset radius alone - only the
// averaging is about the boundary. Measured 2026-09-11: the one-shot call below
// takes about a second on the images that ship, and the panel recomputes it on
// every change to the region, so drawing or adjusting a boundary meant a second
// of frozen window per edit.
//
// Prepared once, the same questions cost a walk over a bounding box.
//
// Opaque by design: what it holds is the engine's own maps, and a caller that
// could reach them would be a second place deciding what they mean.
class SpeckleFieldData;

struct SpeckleField
{
    // The radius the windows were computed at. Carried because a field asked
    // about a different subset would answer with the wrong one's figure, which
    // is indistinguishable from the right one.
    int subsetRadiusPx = 0;

    // Why there is no field, when there is none.
    QString note;

    bool isValid() const { return bool(data); }

    std::shared_ptr<const SpeckleFieldData> data;
};

// Read `imagePath` and compute what does not depend on the region. Runs on the
// calling thread and takes about as long as the one-shot call below.
SpeckleField prepareSpeckleField(const QString &imagePath, int subsetRadius);

// Estimate for `roi` from an already prepared image. Cheap: an average over the
// region's own pixels.
SpeckleQuality speckleQualityIn(const SpeckleField &field,
                                const RegionOfInterest &roi);

// Estimate for `roi` on `imagePath`, for subsets of `subsetRadius`.
//
// Prepares and asks in one go. Kept for callers with a single question to ask;
// anything asking repeatedly about one image should prepare once instead.
SpeckleQuality speckleQualityIn(const QString &imagePath,
                                const RegionOfInterest &roi, int subsetRadius);
