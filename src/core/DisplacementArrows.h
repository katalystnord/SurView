#pragma once

#include "core/Correlation.h"

#include <QString>
#include <QVector>

// Displacement drawn as arrows over the field: which points get one, how far
// apart, and at what drawing scale. The viewport draws; this decides.
//
// ⚑ Only where a point was MEASURED. A rejected point has no arrow -- a
// zero-length one would read as a point that did not move, the same trap as a
// rejected point coloured as zero.

// The longest arrow is drawn this share of the space between arrows, so
// neighbouring arrows cannot touch.
constexpr double kArrowFill = 0.9;

struct DisplacementArrow
{
    double x = 0.0, y = 0.0;     // the point, in reference image pixels
    double dx = 0.0, dy = 0.0;   // the arrow as DRAWN, image pixels, y down
};

struct ArrowLayout
{
    QVector<DisplacementArrow> arrows;
    int stride = 0;          // an arrow at every stride-th point each way
    double scale = 0.0;      // drawn length / true length
    int measured = 0;        // points that could carry an arrow at stride 1
    bool nothingMoved = false;
};

// `gridStepOnScreen`: how many screen pixels one grid step currently spans,
// asked of the renderer. `minimumSpacing`: the fewest screen pixels allowed
// between neighbouring arrows.
ArrowLayout layoutDisplacementArrows(const CorrelationResult &result,
                                     double gridStepOnScreen, double minimumSpacing);

// What the drawing is, in words, shown beside it.
QString arrowNote(const ArrowLayout &layout);
