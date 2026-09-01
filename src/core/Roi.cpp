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
