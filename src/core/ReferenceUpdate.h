#pragma once

#include <QVector>

struct CorrelationResult;

// Following a material point through a sequence when the reference moves.
//
// Measuring every frame against the original reference is simple and eventually
// stops working: the specimen deforms away from the picture it is compared
// against, and correlation collapses. Re-anchoring measures against a later
// frame instead, and immediately raises the question of what the numbers then
// mean -- a displacement measured against frame 7 is not comparable to one
// measured against frame 0, and a series that mixes the two looks perfectly
// smooth until the re-anchor, where the whole field steps by however far the
// specimen had already moved.
//
// ⚑ THIS FILE IS ENGINE-FREE ON PURPOSE. It is the trickiest bookkeeping in the
// project and it is pure arithmetic on positions and displacements, so it
// belongs where it can be tested exhaustively in microseconds without an image
// or a solver. That is most of the reason this is orchestrated here rather than
// inside the engine's own SequenceTracker2D, whose equivalent step can only be
// exercised through a real correlation.
//
// Two rules govern everything below:
//   1. A frame is REPORTED on the original reference's grid, in displacement
//      relative to the original reference, whatever frame it was measured
//      against. That is what makes the series comparable and lets one exported
//      field sit on top of another.
//   2. A point whose increment was not measured cannot advance, so after a
//      re-anchor its position in the new reference is unknown. It is lost, not
//      frozen: a frozen point reports its last displacement forever, which
//      reads as a region that stopped moving.

// One material point, followed from the original reference.
struct TrackedPoint
{
    int gridIndex = 0;   // cell of the ORIGINAL grid; where the field reports it

    float x0 = 0.f;      // where it sits in the original reference
    float y0 = 0.f;

    // Displacement from the original reference to the current one. Everything
    // measured since the last re-anchor is an increment on top of this.
    float bankedU = 0.f;
    float bankedV = 0.f;

    // Its position in the current reference is no longer known, because a
    // re-anchor happened on a frame where it could not be measured.
    bool lost = false;
};

// Where the point sits in the CURRENT reference frame. Derived rather than
// stored: it is exactly the original position plus what has been banked, and a
// second copy of that fact would be free to drift from this one.
float trackedX(const TrackedPoint &point);
float trackedY(const TrackedPoint &point);

// When the reference should move on. ncorr's rule, in OpenCorr's
// higher-is-better ZNCC convention.
struct ReferenceUpdatePolicy
{
    bool enabled = false;        // ncorr's own default is not to re-anchor
    double znccThreshold = 0.9;  // a point is "still tracking" at or above this
    double percentile = 0.75;    // the share of measured points that must be
};

// True when too little of the field is still tracking against the current
// reference. A whole-field decision on purpose: one unlucky point should not
// re-anchor a sequence. Points the solver rejected do not vote -- their zncc
// field holds a negative status code rather than a poor correlation, and
// counting those as poor matches would re-anchor a sequence that is tracking
// perfectly wherever it can be measured at all.
bool fieldNeedsReanchor(const CorrelationResult &measured,
                        const ReferenceUpdatePolicy &policy);

// Begin following the points of a first measured field.
QVector<TrackedPoint> startTracking(const CorrelationResult &first);

// Fold an increment into the banked displacements, which is what re-anchoring
// means. Points with no measurement in this increment become lost.
void bankIncrement(QVector<TrackedPoint> &tracked,
                   const CorrelationResult &increment);

// How many followed points no longer have a known position.
int lostCount(const QVector<TrackedPoint> &tracked);

// One frame's field as it should be reported: at the original positions, on the
// original grid, holding displacement relative to the original reference.
CorrelationResult composeTotalField(const QVector<TrackedPoint> &tracked,
                                    const CorrelationResult &increment);
