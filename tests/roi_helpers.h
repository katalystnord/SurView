#pragma once

// Building regions in tests: a region of one added polygon, which is what every
// region was before shapes, and cut polygons, which is what a hole became.

#include "core/Roi.h"

inline void setOutline(RegionOfInterest &roi, const QVector<QPoint> &corners)
{
    RegionShape outline;
    outline.points = corners;
    roi.shapes = {outline};
}

inline void addCut(RegionOfInterest &roi, const QVector<QPoint> &corners)
{
    RegionShape cut;
    cut.subtract = true;
    cut.points = corners;
    roi.shapes.append(cut);
}

inline QVector<QPoint> outlineOf(const RegionOfInterest &roi)
{
    return roi.shapes.isEmpty() ? QVector<QPoint>() : roi.shapes.first().points;
}
