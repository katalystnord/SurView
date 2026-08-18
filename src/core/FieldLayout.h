#pragma once

#include <QVector>

struct CorrelationResult;

// Laying a measured result out over its own grid, ready to be rendered.
//
// Separated from the viewport because it is where a result stops being a list
// and becomes a picture, and getting it wrong is invisible: points placed by
// their position in the list rather than by the cell they belong to still
// produce a smooth, plausible field — just shifted. That was a real risk the
// moment a region of interest made the list sparse.
//
// The value is displacement magnitude in pixels. A cell with no point, and a
// point the solver rejected, are BOTH not-a-number: neither was measured, and
// a rejected point is not a displacement of zero.
QVector<float> layoutDisplacementMagnitude(const CorrelationResult &result);
