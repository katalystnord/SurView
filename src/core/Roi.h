#pragma once

#include <QMetaType>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>

// A region of interest, expressed in reference-image pixel coordinates.
//
// That is the same frame the file's own rows use, the same frame the viewport
// renders in, and the same frame the engine's points are given in -- so a
// boundary drawn on screen reaches the correlation without being converted
// between conventions on the way. Nothing here is in screen or widget units.
//
// What a region of interest does and does not do, stated once here because the
// distinction is easy to assume wrongly: it selects the point CENTRES that get
// measured. Each of those points is still correlated over a subset that reaches
// up to one subset radius beyond the boundary, because the shape model can
// answer whether a pixel is inside the region and cannot clip a subset's own
// interior to it. Pixels outside the region therefore still contribute to the
// points measured near its edge.
struct RegionOfInterest
{
    // How this region came to exist. Carried with it and reported rather than
    // inferred later: a boundary a person drew and one an algorithm proposed
    // are not the same kind of claim, and the difference should survive as far
    // as whoever reads the result.
    enum Origin { Drawn, Detected };

    // The boundary, as an open ring -- the last vertex joins back to the first.
    QVector<QPoint> vertices;

    Origin origin = Drawn;

    // What the region's own maker could not guarantee, in its own words: the
    // detector cannot represent a hole or a second patch, so a region that came
    // from it carries that sentence with it. Empty when there is nothing to
    // qualify.
    QString limitation;

    // Three vertices is the least that can enclose any area at all.
    bool isValid() const { return vertices.size() >= 3; }

    // Smallest pixel rectangle containing every vertex, inclusive of its own
    // edges. Null when there are no vertices.
    QRect bounds() const;

    QString originText() const;
};

// Which corner of `roi` is within `reach` pixels of `at`, nearest first, or -1
// when none is. Used to decide whether a press on the picture grabs a corner or
// belongs to whatever else the pointer does there, so returning the nearest
// corner regardless of distance would mean a click anywhere inside a boundary
// silently moved a corner the user was nowhere near.
int cornerNear(const RegionOfInterest &roi, const QPoint &at, double reach);

// `roi` with one corner moved. Out-of-range indices return it untouched.
//
// ⚑ The result is always Drawn, whatever it was before. A region a person has
// adjusted is no longer the one the detector proposed: the origin is what the
// project states and what an exported file records, and the detector's own
// caveat about holes stops describing a shape somebody has since altered.
RegionOfInterest withCornerMoved(const RegionOfInterest &roi, int corner,
                                 const QPoint &to);

// Carried as a signal argument from the viewport to the window.
Q_DECLARE_METATYPE(RegionOfInterest)
