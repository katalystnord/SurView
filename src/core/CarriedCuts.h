#pragma once

#include "core/ReferenceUpdate.h"
#include "core/Roi.h"

#include <QPointF>
#include <QString>
#include <QVector>

#include <optional>

// The cuts of a region, carried to a reference the run has re-anchored to.
//
// Cuts are drawn on the original reference. Once a sequence re-anchors, every
// frame after it is measured against a later picture, where the hole has moved
// and deformed with the specimen; a mask left where the cut was drawn would
// remove the wrong pixels while saying it had removed the right ones. The run
// already knows how far every point around the hole has moved -- that is what
// re-anchoring banks -- so the cut is carried by that displacement.
//
// ⚑ ENGINE-FREE ON PURPOSE, for the reason core/ReferenceUpdate.h gives: this is
// arithmetic on positions and displacements, and it is tested exhaustively here
// against an exact deformation, with no image and no solver.
//
// How a shape is carried: its boundary is walked every kCarrySpacing px, each
// sample is moved by an affine fit to the banked displacements of the points
// around it, and the moved samples become the corners of a polygon. A fit rather
// than the nearest point, because a hole is where the displacement changes
// fastest, and a fit rather than one mean movement for the whole region, because
// a specimen that stretches moves each side of a hole by a different amount.
//
// ⚑ THE FIT USES ONLY POINTS WHOSE POSITION IS STILL KNOWN. A lost point's banked
// displacement is whatever it held when it was lost, which is exactly the value
// that must not steer a boundary.
//
// The neighbourhood is the recovery pass's own (deriveRecoveryNeighbourhood()):
// within four grid steps, at least nine points. A sample with fewer is not
// carried by guessing; the cut is refused, the frame is measured without
// masking, and says which cut and why.
//
// Shapes before the first cut are left as they were drawn. With the whole image
// added first (core/RoiDetect.h, engineRegion()), an addition before any cut
// changes nothing in the mask, and carrying it could only invent a refusal over
// ground that does not matter. The region they leave is still a valid region.

// Spacing of the samples along a carried boundary, px.
inline constexpr double kCarrySpacing = 2.0;

// A carried cut is pulled back kMaskPullIn, as a drawn one is. Its corners are
// whole pixels, so carrying moves its edge by up to half a pixel, and an extra
// pixel of pull-in was tried against that and measured: on the hole sequence,
// 2, 3 and 4 px gave 0.0029, 0.0031 and 0.0030 px on the first re-anchored
// frame and 0.0035, 0.0038 and 0.0042 px on the second. It bought nothing, so
// both edges follow one rule and the screen states one number.

struct CarriedCuts
{
    // The region as it sits in the current reference. Only for the mask: the
    // grid, the display and the files keep the region as it was drawn.
    RegionOfInterest region;

    // Why the cuts could not be carried, in a sentence for the frame's report;
    // empty when they were.
    QString refusal;

    bool isValid() const { return refusal.isEmpty() && region.isValid() && region.hasCuts(); }
};

// Where one point drawn on the original reference sits in the current one: moved
// by the affine fit to the followed points around it. Empty when the
// neighbourhood holds too few points still followed, or they lie on one line.
std::optional<QPointF> carriedTo(const QPointF &drawnAt,
                                 const QVector<TrackedPoint> &tracked,
                                 int gridStep);

// The drawn region's cuts, and every shape after the first of them, carried by
// the banked displacements of the followed points. `gridStep` sets the
// neighbourhood each boundary sample is fitted from.
CarriedCuts carryCuts(const RegionOfInterest &drawn,
                      const QVector<TrackedPoint> &tracked,
                      int gridStep);
