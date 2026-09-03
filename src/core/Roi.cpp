#include "core/Roi.h"

#include <QCoreApplication>

QRect RegionOfInterest::bounds() const
{
    if (vertices.isEmpty())
        return QRect();

    int minX = vertices.first().x();
    int maxX = minX;
    int minY = vertices.first().y();
    int maxY = minY;

    for (const QPoint &vertex : vertices) {
        minX = std::min(minX, vertex.x());
        maxX = std::max(maxX, vertex.x());
        minY = std::min(minY, vertex.y());
        maxY = std::max(maxY, vertex.y());
    }

    // Built from corners rather than a width/height, so the rectangle includes
    // the pixels the extreme vertices themselves sit on.
    return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

QString RegionOfInterest::originText() const
{
    return origin == Drawn
               ? QCoreApplication::translate("RegionOfInterest", "drawn by hand")
               : QCoreApplication::translate("RegionOfInterest",
                                             "detected from the speckle pattern");
}

int cornerNear(const RegionOfInterest &roi, const QPoint &at, double reach)
{
    int nearest = -1;
    double nearestDistance = reach * reach;

    for (int i = 0; i < roi.vertices.size(); i++) {
        const QPoint &vertex = roi.vertices.at(i);
        const double dx = vertex.x() - at.x();
        const double dy = vertex.y() - at.y();
        const double distance = dx * dx + dy * dy;
        // Strictly nearer, so the first of two equally close corners wins and
        // the answer does not depend on the order they happen to be stored in.
        if (distance <= nearestDistance && (nearest < 0 || distance < nearestDistance)) {
            nearest = i;
            nearestDistance = distance;
        }
    }
    return nearest;
}

RegionOfInterest withCornerMoved(const RegionOfInterest &roi, int corner,
                                 const QPoint &to)
{
    if (corner < 0 || corner >= roi.vertices.size())
        return roi;

    RegionOfInterest moved = roi;
    moved.vertices[corner] = to;

    // No longer the boundary the detector proposed, so it stops saying it is,
    // and drops the caveat that described the detector's own output.
    moved.origin = RegionOfInterest::Drawn;
    moved.limitation.clear();
    return moved;
}

bool RegionOfInterest::hasHoles() const
{
    for (const QVector<QPoint> &hole : holes) {
        if (hole.size() >= 3)
            return true;
    }
    return false;
}

namespace {

// Even-odd crossing test over one ring. The engine's own Polygon2D is what a
// RUN asks; this is for the places that have no engine to ask.
bool ringContains(const QVector<QPoint> &ring, double x, double y)
{
    if (ring.size() < 3)
        return false;

    bool inside = false;
    for (int i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        const double xi = ring.at(i).x();
        const double yi = ring.at(i).y();
        const double xj = ring.at(j).x();
        const double yj = ring.at(j).y();
        if (((yi > y) != (yj > y))
            && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }
    return inside;
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

}  // namespace

bool regionContains(const RegionOfInterest &roi, int x, int y)
{
    if (!roi.isValid())
        return false;
    if (!ringContains(roi.vertices, x, y))
        return false;

    for (const QVector<QPoint> &hole : roi.holes) {
        if (hole.size() < 3)
            continue;   // encloses nothing, so it excludes nothing
        if (ringContains(hole, x, y))
            return false;
    }
    return true;
}

bool subsetReachesAHole(const RegionOfInterest &roi, int x, int y, int subsetRadius)
{
    if (subsetRadius < 0)
        return false;

    // The subset is a square of side 2r+1 about the point. Tested against each
    // hole's bounding rectangle rather than its outline: the rectangle is the
    // conservative answer, and being told a subset MIGHT reach a hole when it
    // only reaches the corner of the hole's box is the harmless direction to be
    // wrong in for a figure whose whole job is to caution.
    const QRect subset(x - subsetRadius, y - subsetRadius,
                       2 * subsetRadius + 1, 2 * subsetRadius + 1);

    for (const QVector<QPoint> &hole : roi.holes) {
        if (hole.size() < 3)
            continue;
        if (ringBounds(hole).intersects(subset))
            return true;
    }
    return false;
}
