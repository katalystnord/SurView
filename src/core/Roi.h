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

    // Places inside the boundary that are NOT to be measured, each an open ring
    // like the outer one.
    //
    // ⚑ A hole is not decoration. A specimen with a hole through it shows
    // BACKGROUND there, and background does not move with the specimen: a point
    // measured in a hole correlates the picture behind the specimen against
    // itself and reports, confidently, that nothing moved. On a strain map that
    // is a cold spot exactly where the stress concentrates, which is the one
    // place anybody is looking. Until this existed the region could not say
    // "not there", and the shipped open-hole tension example is precisely a
    // specimen it could not describe.
    //
    // A ring of fewer than three corners encloses nothing and is ignored, so
    // nothing downstream has to keep asking whether a hole is real.
    QVector<QVector<QPoint>> holes;

    Origin origin = Drawn;

    // What the region's own maker could not guarantee, in its own words: the
    // detector cannot represent a hole or a second patch, so a region that came
    // from it carries that sentence with it. Empty when there is nothing to
    // qualify.
    QString limitation;

    // Three vertices is the least that can enclose any area at all.
    bool isValid() const { return vertices.size() >= 3; }

    // True when at least one hole encloses any area.
    bool hasHoles() const;

    // Smallest pixel rectangle containing every vertex of the OUTER boundary,
    // inclusive of its own edges. Null when there are no vertices.
    //
    // Holes cannot enlarge it: they lie inside the outer ring by construction,
    // and a bounds that grew to include one would put the grid's origin
    // somewhere no point can be placed.
    QRect bounds() const;

    QString originText() const;
};

// Which corner of `roi` is within `reach` pixels of `at`, nearest first, or -1
// when none is. Used to decide whether a press on the picture grabs a corner or
// belongs to whatever else the pointer does there, so returning the nearest
// corner regardless of distance would mean a click anywhere inside a boundary
// silently moved a corner the user was nowhere near.
int cornerNear(const RegionOfInterest &roi, const QPoint &at, double reach);

// Whether the pixel is inside the region: within the outer boundary and within
// none of the holes.
//
// ⚑ Engine-free, and therefore NOT what a run uses. A correlation asks the
// engine's own RegionWithHoles2D instead, so the boundary means the same thing
// there as it does everywhere else the shape is used. This exists for the
// places that must answer the question without an engine -- drawing on screen,
// and these tests.
bool regionContains(const RegionOfInterest &roi, int x, int y);

// Whether a subset of `subsetRadius` centred on this pixel reaches into any
// hole.
//
// ⚑ Excluding a point whose CENTRE is in a hole does not stop a point just
// outside one from correlating over a subset that reaches in, and those pixels
// are background: they drag the answer toward no movement, which is a plausible
// number and so the dangerous kind. Such points are COUNTED rather than
// excluded, because exactly the same is already true and accepted at the outer
// boundary, and quietly applying a stricter rule to one boundary of a region
// than to the other would be a difference nobody could see or account for. The
// run states the count and the reader decides.
bool subsetReachesAHole(const RegionOfInterest &roi, int x, int y, int subsetRadius);

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
