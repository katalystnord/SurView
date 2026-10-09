#include "core/PoiGrid.h"

#include <QCoreApplication>
#include <QRect>

#include <algorithm>
#include <cmath>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("PoiGrid", text);
}

}  // namespace

PoiGridExtent poiGridExtent(int imageWidth, int imageHeight, int subsetRadius,
                            int gridStep, const RegionOfInterest &roi)
{
    PoiGridExtent extent;
    extent.step = gridStep;

    if (imageWidth <= 0 || imageHeight <= 0) {
        extent.refusal = tr("The image has no pixels to measure.");
        return extent;
    }
    if (subsetRadius <= 0) {
        extent.refusal = tr("A subset radius must be at least 1 px.");
        return extent;
    }
    if (gridStep <= 0) {
        extent.refusal = tr("A grid step must be at least 1 px.");
        return extent;
    }

    // ⚑ Not merely inside the image: inside the part of it the engine can
    // MEASURE, with room to move. Laid one subset radius from the edge, the
    // first row and column failed on every frame of DIC Challenge Sample 3
    // (ROADMAP decision 1), because the engine's bicubic interpolator refuses
    // any sample at x < 1 or x >= width - 2 and a subset touching the edge has
    // its outermost samples there as soon as the specimen moves at all.
    // tests/test_grid_margin.cpp asks the engine, so these numbers cannot
    // drift from it unseen.
    const int safeFirstX = subsetRadius + kGridMarginBefore;
    const int safeFirstY = subsetRadius + kGridMarginBefore;
    const int safeLastX  = imageWidth - 1 - kGridMarginAfter - subsetRadius;
    const int safeLastY  = imageHeight - 1 - kGridMarginAfter - subsetRadius;

    if (safeLastX < safeFirstX || safeLastY < safeFirstY) {
        extent.refusal = tr("A subset radius of %1 px leaves no room for a single "
                            "point in a %2×%3 image.")
                             .arg(subsetRadius)
                             .arg(imageWidth)
                             .arg(imageHeight);
        return extent;
    }

    extent.firstX = safeFirstX;
    extent.firstY = safeFirstY;
    extent.lastX  = safeLastX;
    extent.lastY  = safeLastY;

    if (roi.isValid()) {
        const QRect box = roi.bounds();
        extent.firstX = std::max(extent.firstX, box.left());
        extent.firstY = std::max(extent.firstY, box.top());
        extent.lastX  = std::min(extent.lastX, box.right());
        extent.lastY  = std::min(extent.lastY, box.bottom());

        if (extent.lastX < extent.firstX || extent.lastY < extent.firstY) {
            extent.refusal =
                tr("The region of interest lies entirely within %1 px of the "
                   "image border, and a subset of that radius cannot be centred "
                   "there.")
                    .arg(subsetRadius);
            return extent;
        }
    }

    extent.valid = true;
    return extent;
}

PoiGrid buildPoiGrid(int imageWidth, int imageHeight, int subsetRadius,
                     int gridStep, const RegionOfInterest &roi,
                     const PoiInsideTest &inside)
{
    PoiGrid grid;
    grid.step = gridStep;

    const PoiGridExtent extent =
        poiGridExtent(imageWidth, imageHeight, subsetRadius, gridStep, roi);
    if (!extent.valid) {
        grid.refusal = extent.refusal;
        return grid;
    }

    const int firstX = extent.firstX;
    const int firstY = extent.firstY;
    const int lastX  = extent.lastX;
    const int lastY  = extent.lastY;

    grid.restricted = roi.isValid();
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

const QString kCarriedPastTheEdge = QStringLiteral(
    "the specimen carried this subset past the edge of the image");

bool subsetCarriedPastTheEdge(double x, double y, int subsetRadius, double u,
                              double v, int imageWidth, int imageHeight)
{
    if (!std::isfinite(u) || !std::isfinite(v) || subsetRadius <= 0)
        return false;

    // Room between the outermost sample and the limit the interpolator
    // refuses: x < 1 before, x >= width - 2 after.
    const double r = subsetRadius;
    const double roomLeft = (x - r) - 1.0;
    const double roomRight = (imageWidth - 2.0) - (x + r);
    const double roomTop = (y - r) - 1.0;
    const double roomBottom = (imageHeight - 2.0) - (y + r);

    // Movement toward each side, plus the pixel of slack a sub-pixel solve
    // moves through on its way from an integer estimate.
    constexpr double kSlack = 1.0;
    return roomLeft < std::max(0.0, -u) + kSlack
           || roomRight < std::max(0.0, u) + kSlack
           || roomTop < std::max(0.0, -v) + kSlack
           || roomBottom < std::max(0.0, v) + kSlack;
}
