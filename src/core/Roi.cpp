#include "core/Roi.h"

#include <algorithm>
#include <cmath>

#include <QCoreApplication>
#include <QStringList>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("RegionOfInterest", text);
}

// A box shape's two stored corners as a rectangle, inclusive of both.
//
// ⚑ From minima and maxima, NOT QRect::normalized(). Given corners in the
// other order -- a box drawn from its bottom right, or a corner dragged past
// the opposite one -- normalized() shifts each edge in by a pixel: (70, 31)
// and (10, 11) come back as 11..69 by 12..30. Found by a case that stored its
// corners last-first on purpose.
QRect boxOf(const RegionShape &shape)
{
    if (shape.points.size() != 2)
        return QRect();
    const QPoint &a = shape.points.at(0);
    const QPoint &b = shape.points.at(1);
    return QRect(QPoint(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
                 QPoint(std::max(a.x(), b.x()), std::max(a.y(), b.y())));
}

QRect ringBounds(const QVector<QPoint> &ring)
{
    if (ring.isEmpty())
        return QRect();
    QRect box(ring.first(), ring.first());
    for (const QPoint &vertex : ring)
        box = box.united(QRect(vertex, vertex));
    return box;
}

// The engine's Polygon2D rule, mirrored: a pixel ON an edge is inside, decided
// first and exactly in integers; anything else by whether a ray crosses the ring
// an odd number of times. The engine decides the interior by a winding angle,
// which agrees with crossing parity for every ring that does not cross itself.
//
// ⚑ The boundary check is load-bearing. Without it this test takes a polygon's
// left and top edges and drops its right and bottom ones, while the run takes
// all four -- so the region drawn, the overlays and the speckle estimate
// disagreed with the run along half of every boundary. Found 2026-10-10 by
// holding the two to each other pixel by pixel, which no earlier case did.
bool ringContains(const QVector<QPoint> &ring, int x, int y)
{
    const int n = ring.size();
    if (n < 3)
        return false;

    for (int i = 0; i < n; i++) {
        const QPoint &a = ring.at(i);
        const QPoint &b = ring.at((i + 1) % n);
        const long long ex = b.x() - a.x();
        const long long ey = b.y() - a.y();
        const long long length = ex * ex + ey * ey;
        if (length == 0)
            continue;
        const long long px = x - a.x();
        const long long py = y - a.y();
        if (ex * py - ey * px == 0) {
            const long long along = px * ex + py * ey;
            if (along >= 0 && along <= length)
                return true;
        }
    }

    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const double xi = ring.at(i).x();
        const double yi = ring.at(i).y();
        const double xj = ring.at(j).x();
        const double yj = ring.at(j).y();
        if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi))
            inside = !inside;
    }
    return inside;
}

// Squared distance from a point to a segment, so nothing takes a square root to
// answer a question that only compares.
double squaredDistanceToSegment(const QPoint &at, const QPoint &from, const QPoint &to)
{
    const double dx = to.x() - from.x();
    const double dy = to.y() - from.y();
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 0.0) {
        const double px = at.x() - from.x();
        const double py = at.y() - from.y();
        return px * px + py * py;
    }
    // Clamped to the segment's ends: past either end the nearest point IS that
    // end, and an unclamped projection would measure to a point the edge does
    // not reach.
    double t = ((at.x() - from.x()) * dx + (at.y() - from.y()) * dy) / lengthSquared;
    t = std::clamp(t, 0.0, 1.0);
    const double ox = at.x() - (from.x() + t * dx);
    const double oy = at.y() - (from.y() + t * dy);
    return ox * ox + oy * oy;
}

RegionOfInterest asDrawn(RegionOfInterest roi)
{
    roi.origin = RegionOfInterest::Drawn;
    roi.limitation.clear();
    return roi;
}

}  // namespace

// --- the shape ---------------------------------------------------------------

bool RegionShape::isValid() const
{
    if (kind == Polygon)
        return points.size() >= 3;
    const QRect box = boxOf(*this);
    return points.size() == 2 && box.width() > 1 && box.height() > 1;
}

QRect RegionShape::bounds() const
{
    return kind == Polygon ? ringBounds(points) : boxOf(*this);
}

QVector<QPoint> RegionShape::handles() const
{
    if (kind == Polygon)
        return points;
    const QRect box = boxOf(*this);
    if (box.isNull())
        return {};
    return {box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()};
}

QString RegionShape::describe() const
{
    const QString sign = subtract ? QStringLiteral("-") : QStringLiteral("+");
    switch (kind) {
    case Rectangle:
        return tr("%1 rectangle %2 x %3 px")
            .arg(sign).arg(bounds().width()).arg(bounds().height());
    case Ellipse:
        return tr("%1 ellipse %2 x %3 px")
            .arg(sign).arg(bounds().width()).arg(bounds().height());
    case Polygon:
        return tr("%1 polygon of %2 corners").arg(sign).arg(points.size());
    }
    return QString();
}

// --- the region --------------------------------------------------------------

bool RegionOfInterest::isValid() const
{
    for (const RegionShape &shape : shapes) {
        if (!shape.subtract && shape.isValid())
            return true;
    }
    return false;
}

bool RegionOfInterest::hasCuts() const
{
    for (const RegionShape &shape : shapes) {
        if (shape.subtract && shape.isValid())
            return true;
    }
    return false;
}

QRect RegionOfInterest::bounds() const
{
    QRect box;
    for (const RegionShape &shape : shapes) {
        if (!shape.subtract && shape.isValid())
            box = box.isNull() ? shape.bounds() : box.united(shape.bounds());
    }
    return box;
}

QString RegionOfInterest::originText() const
{
    return origin == Drawn ? tr("drawn by hand") : tr("detected from the speckle pattern");
}

QString regionSummary(const RegionOfInterest &roi)
{
    if (!roi.isValid())
        return QString();
    QStringList parts;
    int counted = 0;
    for (const RegionShape &shape : roi.shapes) {
        if (!shape.isValid())
            continue;
        parts << shape.describe();
        counted++;
    }
    return counted == 1 ? tr("1 shape: %1").arg(parts.first())
                        : tr("%1 shapes, applied in order: %2")
                              .arg(counted)
                              .arg(parts.join(QStringLiteral(", ")));
}

QString wholeImageMeasuredNote()
{
    return tr("No region was drawn, so the whole image was measured, including "
              "anything in it that is not the specimen. Use Add to Region to "
              "measure only the specimen.");
}

// --- membership --------------------------------------------------------------

bool shapeContains(const RegionShape &shape, int x, int y)
{
    if (!shape.isValid())
        return false;
    switch (shape.kind) {
    case RegionShape::Rectangle: {
        // Boundary-inclusive, as the engine's polygon of the same four corners is.
        const QRect box = boxOf(shape);
        return x >= box.left() && x <= box.right() && y >= box.top() && y <= box.bottom();
    }
    case RegionShape::Ellipse: {
        // The engine's Ellipse2D, inscribed in the box: centre and semi-axes
        // reach the box's edge pixels, which are on the boundary and inside.
        const QRect box = boxOf(shape);
        const double cx = 0.5 * (box.left() + box.right());
        const double cy = 0.5 * (box.top() + box.bottom());
        const double sx = 0.5 * (box.right() - box.left());
        const double sy = 0.5 * (box.bottom() - box.top());
        const double dx = (double(x) - cx) / sx;
        const double dy = (double(y) - cy) / sy;
        return dx * dx + dy * dy <= 1.0;
    }
    case RegionShape::Polygon:
        return ringContains(shape.points, x, y);
    }
    return false;
}

bool regionContains(const RegionOfInterest &roi, int x, int y)
{
    // The last shape containing the pixel decides. Walked from the end so the
    // first one found is that shape.
    for (int i = roi.shapes.size() - 1; i >= 0; i--) {
        const RegionShape &shape = roi.shapes.at(i);
        if (shapeContains(shape, x, y))
            return !shape.subtract;
    }
    return false;
}

bool subsetReachesACut(const RegionOfInterest &roi, int x, int y, int subsetRadius)
{
    if (subsetRadius < 0 || !roi.hasCuts())
        return false;

    // Pixel by pixel over the subset: a pixel is cut ground when the last shape
    // containing it is a cut. Exact rather than a bounding-box guess, since a
    // cut ellipse's box reaches well past the ellipse at its corners.
    for (int py = y - subsetRadius; py <= y + subsetRadius; py++) {
        for (int px = x - subsetRadius; px <= x + subsetRadius; px++) {
            for (int i = roi.shapes.size() - 1; i >= 0; i--) {
                const RegionShape &shape = roi.shapes.at(i);
                if (shapeContains(shape, px, py)) {
                    if (shape.subtract)
                        return true;
                    break;
                }
            }
        }
    }
    return false;
}

// --- editing -----------------------------------------------------------------

CornerRef cornerNear(const RegionOfInterest &roi, const QPoint &at, double reach)
{
    CornerRef nearest;
    double nearestDistance = reach * reach;
    for (int s = 0; s < roi.shapes.size(); s++) {
        const QVector<QPoint> handles = roi.shapes.at(s).handles();
        for (int c = 0; c < handles.size(); c++) {
            const double dx = handles.at(c).x() - at.x();
            const double dy = handles.at(c).y() - at.y();
            const double distance = dx * dx + dy * dy;
            // Ties go to the later shape, which is drawn on top; within a shape
            // to the first corner, so the answer does not depend on storage.
            const bool later = nearest.shape >= 0 && s > nearest.shape;
            if (distance <= nearestDistance
                && (!nearest.isValid() || distance < nearestDistance || later)) {
                nearest = {s, c};
                nearestDistance = distance;
            }
        }
    }
    return nearest;
}

RegionOfInterest withCornerMoved(const RegionOfInterest &roi, CornerRef corner,
                                 const QPoint &to)
{
    if (corner.shape < 0 || corner.shape >= roi.shapes.size())
        return roi;
    const RegionShape &shape = roi.shapes.at(corner.shape);
    const QVector<QPoint> handles = shape.handles();
    if (corner.corner < 0 || corner.corner >= handles.size())
        return roi;

    RegionOfInterest moved = roi;
    RegionShape &target = moved.shapes[corner.shape];
    if (shape.kind == RegionShape::Polygon) {
        target.points[corner.corner] = to;
    } else {
        // The handle diagonally across stays put: handles run clockwise, so it
        // is two along.
        target.points = {handles.at((corner.corner + 2) % 4), to};
    }
    return asDrawn(moved);
}

RegionOfInterest withCornerInserted(const RegionOfInterest &roi, CornerRef edge,
                                    const QPoint &at)
{
    if (edge.shape < 0 || edge.shape >= roi.shapes.size())
        return roi;
    const RegionShape &shape = roi.shapes.at(edge.shape);
    if (shape.kind != RegionShape::Polygon || edge.corner < 0
        || edge.corner >= shape.points.size()) {
        return roi;
    }
    RegionOfInterest grown = roi;
    grown.shapes[edge.shape].points.insert(edge.corner + 1, at);
    return asDrawn(grown);
}

RegionOfInterest withCornerRemoved(const RegionOfInterest &roi, CornerRef corner)
{
    if (corner.shape < 0 || corner.shape >= roi.shapes.size())
        return roi;
    const RegionShape &shape = roi.shapes.at(corner.shape);
    // See the header: boxes keep their corners, polygons keep three.
    if (shape.kind != RegionShape::Polygon || shape.points.size() <= 3
        || corner.corner < 0 || corner.corner >= shape.points.size()) {
        return roi;
    }
    RegionOfInterest reduced = roi;
    reduced.shapes[corner.shape].points.remove(corner.corner);
    return asDrawn(reduced);
}

RegionOfInterest withRegionMoved(const RegionOfInterest &roi, const QPoint &by)
{
    if (by.isNull())
        return roi;
    RegionOfInterest moved = roi;
    for (RegionShape &shape : moved.shapes) {
        for (QPoint &point : shape.points)
            point += by;
    }
    return asDrawn(moved);
}

CornerRef edgeNear(const RegionOfInterest &roi, const QPoint &at, double reach)
{
    CornerRef nearest;
    double nearestDistance = reach * reach;
    for (int s = 0; s < roi.shapes.size(); s++) {
        const RegionShape &shape = roi.shapes.at(s);
        if (shape.kind != RegionShape::Polygon || !shape.isValid())
            continue;
        for (int i = 0; i < shape.points.size(); i++) {
            const double distance = squaredDistanceToSegment(
                at, shape.points.at(i), shape.points.at((i + 1) % shape.points.size()));
            if (distance <= nearestDistance) {
                nearest = {s, i};
                nearestDistance = distance;
            }
        }
    }
    return nearest;
}

RegionOfInterest withShapeAdded(const RegionOfInterest &roi, const RegionShape &shape)
{
    if (!shape.isValid())
        return roi;
    RegionOfInterest grown = roi;
    grown.shapes.append(shape);
    return asDrawn(grown);
}

RegionOfInterest withShapeRemoved(const RegionOfInterest &roi, int shape)
{
    if (shape < 0 || shape >= roi.shapes.size())
        return roi;
    RegionOfInterest reduced = roi;
    reduced.shapes.remove(shape);
    return asDrawn(reduced);
}
