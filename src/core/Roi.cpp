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
