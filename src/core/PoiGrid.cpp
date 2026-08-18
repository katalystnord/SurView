#include "core/PoiGrid.h"

#include <QCoreApplication>
#include <QRect>

#include <algorithm>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("PoiGrid", text);
}

}  // namespace

PoiGrid buildPoiGrid(int imageWidth, int imageHeight, int subsetRadius,
                     int gridStep, const RegionOfInterest &roi,
                     const PoiInsideTest &inside)
{
    PoiGrid grid;
    grid.step = gridStep;

    if (imageWidth <= 0 || imageHeight <= 0) {
        grid.refusal = tr("The image has no pixels to measure.");
        return grid;
    }
    if (subsetRadius <= 0) {
        grid.refusal = tr("A subset radius must be at least 1 px.");
        return grid;
    }
    if (gridStep <= 0) {
        grid.refusal = tr("A grid step must be at least 1 px.");
        return grid;
    }

    // A subset must lie wholly inside the image, so no point may sit closer
    // than one subset radius to any edge.
    const int safeFirstX = subsetRadius;
    const int safeFirstY = subsetRadius;
    const int safeLastX  = imageWidth - 1 - subsetRadius;
    const int safeLastY  = imageHeight - 1 - subsetRadius;

    if (safeLastX < safeFirstX || safeLastY < safeFirstY) {
        grid.refusal = tr("A subset radius of %1 px leaves no room for a single "
                          "point in a %2×%3 image.")
                           .arg(subsetRadius)
                           .arg(imageWidth)
                           .arg(imageHeight);
        return grid;
    }

    int firstX = safeFirstX;
    int firstY = safeFirstY;
    int lastX  = safeLastX;
    int lastY  = safeLastY;

    grid.restricted = roi.isValid();
    if (grid.restricted) {
        const QRect box = roi.bounds();
        firstX = std::max(firstX, box.left());
        firstY = std::max(firstY, box.top());
        lastX  = std::min(lastX, box.right());
        lastY  = std::min(lastY, box.bottom());

        if (lastX < firstX || lastY < firstY) {
            grid.refusal =
                tr("The region of interest lies entirely within %1 px of the "
                   "image border, and a subset of that radius cannot be centred "
                   "there.")
                    .arg(subsetRadius);
            return grid;
        }
    }

    grid.columns = (lastX - firstX) / gridStep + 1;
    grid.rows    = (lastY - firstY) / gridStep + 1;
    grid.originX = firstX;
    grid.originY = firstY;

    grid.cells.reserve(grid.columns * grid.rows);
    for (int r = 0; r < grid.rows; r++) {
        for (int c = 0; c < grid.columns; c++) {
            const int x = firstX + c * gridStep;
            const int y = firstY + r * gridStep;
            if (grid.restricted && inside && !inside(x, y))
                continue;
            grid.cells.append(PoiGridCell{x, y, r * grid.columns + c});
        }
    }

    if (grid.cells.isEmpty()) {
        grid.refusal = tr("The region of interest contains no measurement point "
                          "at a grid step of %1 px. A smaller step, or a larger "
                          "region, would give the run something to measure.")
                           .arg(gridStep);
    }

    return grid;
}
