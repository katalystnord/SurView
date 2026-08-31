#pragma once

#include "core/Correlation.h"

#include <QString>
#include <QVector>

// What one measured point has to say about itself, and which point a position
// is asking about.
//
// A colour map answers "where", never "how much": a reader can see that one
// region is redder than another and cannot ask what any point measured. This is
// the other half, and it is carved out of the widget for the usual reason --
// deciding which point owns a position, and what that point is entitled to
// claim, are both easy to get wrong in ways that still look right on screen.
//
// The rule every line here keeps is the one the export and the colour map
// already keep: nothing unmeasured is reported as a number. A rejected point
// has no displacement, an unfitted strain is not a strain of zero, and a
// conditioning the probe could not establish is a warning rather than a blank.

// One row of the readout. `note` qualifies the value where it needs
// qualifying, and `warning` marks the rows that say the measurement cannot
// carry the weight a bare number would be given.
struct ReadoutLine
{
    QString label;
    QString value;
    QString note;
    bool warning = false;
};

struct PointReadout
{
    // A point of the grid sits at the position asked about at all.
    bool measured = false;
    // That point converged. False is a real answer, not an absence of one.
    bool solved = false;

    QVector<ReadoutLine> lines;
};

// How far from a measured point a position may sit and still be reporting on
// it. One grid step: the field is DRAWN as cells one step across, so every
// position inside the picture of the field is within a step of the point that
// coloured it, and a position beyond the field's edge belongs to no point at
// all. Half a step -- the distance at which a point stops being the nearest --
// would leave most of a drawn field reporting nothing, which reads as a broken
// readout rather than as an honest edge.
float pointPickRadius(const CorrelationResult &result);

// Index into `result.points` of the point nearest (x, y) within
// pointPickRadius(), or -1 where the position is not over the measured field.
//
// Positions are reference-image pixels, the frame everything else in SurView
// uses. Returning the nearest point regardless of distance would attribute a
// measurement to a place nothing was measured, which is the same error as
// filling an unmeasured cell with zero.
int pointNearestTo(const CorrelationResult &result, float x, float y);

// Whether this point's displacement is smaller than the noise floor of its own
// subset -- that is, whether the movement it reports is distinguishable from
// image noise at all.
//
// ⚑ This is the question the noise floor exists to answer and the one nothing
// on screen could ask before. A point can converge, correlate at 0.99 and
// report a displacement that its own subset could never have resolved; it is
// drawn in an ordinary colour like any other. Compared against the magnitude,
// because u and v share one floor between them.
//
// False for a point that did not converge: its displacement is zero only
// because nothing was ever written there, and reading that as "below the noise
// floor" would turn an absence into a finding.
bool displacementIsBelowNoiseFloor(const CorrelationPoint &point);

// Everything `result.points[index]` has to say, ready to be shown. An index
// outside the result returns a readout with `measured` false and one line
// saying so, rather than an empty one: a panel that empties itself looks
// broken, where a panel that says "no point measured here" has answered.
PointReadout pointReadout(const CorrelationResult &result, int index);
