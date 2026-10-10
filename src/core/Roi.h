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
//
// ⚑ A REGION IS AN ORDERED LIST OF SHAPES, each added or cut (2026-10-10, David's
// choice of the ordered rule). A pixel is inside when the LAST shape containing it
// adds, and outside when that shape cuts or when no shape contains it -- shapes
// apply one after another, as paint does, so a later added shape can put back an
// island inside an earlier cut. The order is listed, numbered, in the project, so
// it is never a hidden rule. The region this replaced -- one outer polygon with
// polygonal holes -- is the special case "+ polygon, then a cut polygon per hole",
// and a project written in that form opens as exactly that.

// One shape of a region.
struct RegionShape
{
    enum Kind { Rectangle, Ellipse, Polygon };

    Kind kind = Polygon;

    // Removes what it covers rather than adding it.
    bool subtract = false;

    // A polygon's corners, as an open ring: the last joins back to the first.
    // A rectangle or an ellipse: exactly two points, opposite corners of its
    // box, both inclusive -- the ellipse is the one inscribed in that box.
    QVector<QPoint> points;

    // A polygon of three corners or more; a box of two corners with some width
    // and some height. An invalid shape adds and removes nothing.
    bool isValid() const;

    // Smallest pixel rectangle holding the shape, inclusive of its own edges.
    QRect bounds() const;

    // The points a person can drag. A polygon's corners; a box's FOUR corners,
    // clockwise from the top left, although only two are stored -- a handle on
    // only two corners of a rectangle would read as a defect.
    QVector<QPoint> handles() const;

    // "+ rectangle", "- polygon of 5 corners": for the project, the log and the
    // exported files.
    QString describe() const;

    bool operator==(const RegionShape &other) const
    {
        return kind == other.kind && subtract == other.subtract && points == other.points;
    }
};

struct RegionOfInterest
{
    // How this region came to exist. Carried with it and reported rather than
    // inferred later: a boundary a person drew and one an algorithm proposed
    // are not the same kind of claim, and the difference should survive as far
    // as whoever reads the result.
    enum Origin { Drawn, Detected };

    // In the order they apply. See the header.
    QVector<RegionShape> shapes;

    Origin origin = Drawn;

    // What the region's own maker could not guarantee, in its own words: the
    // detector cannot represent a hole or a second patch, so a region that came
    // from it carries that sentence with it. Empty when there is nothing to
    // qualify.
    QString limitation;

    // At least one valid shape that ADDS. A region of nothing but cuts encloses
    // nothing, since every pixel starts outside.
    bool isValid() const;

    // A valid cut in the region, which is what a hole has become.
    bool hasCuts() const;

    // Smallest pixel rectangle holding every shape that ADDS, inclusive. Cuts
    // cannot enlarge it: they remove, and a bounds that grew to include one
    // would put the grid's origin somewhere no point can be placed.
    QRect bounds() const;

    QString originText() const;
};

// The region as one line for a reader: "3 shapes: + rectangle, - ellipse,
// + polygon of 5 corners". Empty for an invalid region.
QString regionSummary(const RegionOfInterest &roi);

// What a field measured with no region drawn has to say about itself, in the
// one wording the field bar, the run report and the tests share.
//
// ⚑ Without a region the run measures every place on the picture, and the
// background, grips and glare are not the specimen. Some of them correlate and
// are reported as solved -- glare off the bench on the tension example does,
// with a plausible reading of no movement. No rule about the answers can catch
// that reliably, because the answers look like measurements; leaving those
// places out does. So the remedy is named, by the control that applies it.
QString wholeImageMeasuredNote();

// --- membership ------------------------------------------------------------

// Whether one shape covers the pixel, by the rule the engine applies to it.
//
// ⚑ Engine-free, and therefore NOT what a run uses: a correlation asks the
// engine's own shapes (regionInsideTest() in core/RoiDetect.h), so the boundary
// means the same thing there as everywhere else the shape is used. This mirrors
// them for the places that must answer without an engine -- drawing on screen,
// and the tests that hold the two to each other pixel by pixel.
bool shapeContains(const RegionShape &shape, int x, int y);

// Whether the pixel is inside the region, by the ordered rule in the header.
bool regionContains(const RegionOfInterest &roi, int x, int y);

// Whether a subset of `subsetRadius` centred on this pixel reaches ground a
// CUT removed.
//
// ⚑ Excluding a point whose CENTRE was cut does not stop a point just outside
// the cut from correlating over a subset that reaches in, and those pixels are
// usually background seen through a hole: they drag the answer toward no
// movement, which is a plausible number and so the dangerous kind. Such points
// are COUNTED rather than excluded, because exactly the same is already true and
// accepted at the outer boundary, and quietly applying a stricter rule to one
// edge of a region than to another would be a difference nobody could see or
// account for. The run states the count and the reader decides.
bool subsetReachesACut(const RegionOfInterest &roi, int x, int y, int subsetRadius);

// --- editing ---------------------------------------------------------------
//
// ⚑ Every edit returns a Drawn region, whatever it was before. A region a person
// has adjusted is no longer the one the detector proposed: the origin is what the
// project states and what an exported file records, and the detector's own caveat
// stops describing a shape somebody has since altered.

// A handle of one shape: shapes[shape].handles()[corner].
struct CornerRef
{
    int shape = -1;
    int corner = -1;
    bool isValid() const { return shape >= 0 && corner >= 0; }
    bool operator==(const CornerRef &other) const
    {
        return shape == other.shape && corner == other.corner;
    }
};

// Which handle is within `reach` pixels of `at`, nearest first, or an invalid
// reference when none is. Used to decide whether a press on the picture grabs a
// handle or belongs to whatever else the pointer does there, so returning the
// nearest handle regardless of distance would mean a click anywhere inside a
// boundary silently moved a corner the user was nowhere near. Among handles
// equally near, the one belonging to the LATER shape wins, since it is drawn on
// top.
CornerRef cornerNear(const RegionOfInterest &roi, const QPoint &at, double reach);

// `roi` with one handle moved. A polygon's corner moves where it is put; a box's
// corner moves and the OPPOSITE corner stays, so dragging a rectangle's corner
// resizes it the way every drawing program does. An invalid reference returns
// `roi` untouched.
RegionOfInterest withCornerMoved(const RegionOfInterest &roi, CornerRef corner,
                                 const QPoint &to);

// `roi` with a corner added to a polygon's edge -- the edge that begins at
// `edge.corner` -- placed at `at`. Anything but a polygon's edge returns `roi`
// untouched: a rectangle with five corners is not a rectangle.
//
// ⚑ The new corner goes immediately AFTER that edge's first corner, which is
// what keeps the ring in order. Appended anywhere else the boundary crosses
// itself, and a self-crossing ring is not a region: what counts as inside is
// then decided by a parity rule that no longer means what the reader drew.
RegionOfInterest withCornerInserted(const RegionOfInterest &roi, CornerRef edge,
                                    const QPoint &at);

// `roi` with one corner taken out of a polygon.
//
// ⚑ A POLYGON OF THREE IS REFUSED, and so is any corner of a rectangle or an
// ellipse. Three corners is the fewest that enclose anything, and a box is
// defined by its corners; accepting would leave a reader holding a shape that
// has silently stopped being one, which is worse than a gesture that declines.
// A refusal returns `roi` untouched.
RegionOfInterest withCornerRemoved(const RegionOfInterest &roi, CornerRef corner);

// `roi` shifted bodily by `by`, every shape together.
//
// ⚑ THE CUTS MOVE WITH IT. A cut is part of the region, and a move that left one
// behind would be the worst kind of wrong: the boundary lands where the reader
// put it while the void it was drawn around stays where the specimen no longer
// is, and the run measures straight across a hole and reports confident numbers
// off the back of it.
//
// Nothing is clamped to the image. A region dragged off the picture is refused
// by the grid, in words, at the moment a run is asked for - which is a better
// place to say it than under a pointer that has stopped following the hand.
RegionOfInterest withRegionMoved(const RegionOfInterest &roi, const QPoint &by);

// The polygon edge whose line passes within `reach` of `at`, as the shape and
// the corner it begins at, or an invalid reference. Polygons only.
//
// ⚑ Asked instead of "which corner is nearest" because a click halfway along a
// side is far from both of its ends. Includes the CLOSING edge from the last
// corner back to the first, which is the one a walk over pairs of vertices
// leaves out.
CornerRef edgeNear(const RegionOfInterest &roi, const QPoint &at, double reach);

// `roi` with one more shape at the end, which is where a new shape applies:
// last, over everything before it. An invalid shape returns `roi` untouched.
RegionOfInterest withShapeAdded(const RegionOfInterest &roi, const RegionShape &shape);

// `roi` without one shape. Out of range returns `roi` untouched.
RegionOfInterest withShapeRemoved(const RegionOfInterest &roi, int shape);

// Carried as a signal argument from the viewport to the window.
Q_DECLARE_METATYPE(RegionOfInterest)
Q_DECLARE_METATYPE(RegionShape)
