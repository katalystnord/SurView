#include "core/SubsetOverlay.h"

#include "core/PoiGrid.h"

#include <algorithm>
#include <cmath>

namespace {

// The lattice position nearest `value`, held inside the run's own extent. A
// pointer near the border still asks a real question about the settings; the
// box it gets is the nearest one the run could actually place.
int snapped(double value, int first, int last, int step)
{
    const double steps = std::round((value - first) / double(step));
    const int position = first + int(steps) * step;
    const int lastOnLattice = first + ((last - first) / step) * step;
    return std::clamp(position, first, lastOnLattice);
}

}  // namespace

SubsetOverlay subsetOverlayAt(double x, double y, int imageWidth, int imageHeight,
                              int subsetRadius, int gridStep,
                              const RegionOfInterest &roi,
                              bool strainEnabled, double strainRadius)
{
    SubsetOverlay overlay;

    // Asked of the grid rather than worked out again here, so the box is drawn
    // where the run would measure and a refusal is given in the run's own words.
    const PoiGridExtent extent =
        poiGridExtent(imageWidth, imageHeight, subsetRadius, gridStep, roi);
    if (!extent.valid) {
        overlay.refusal = extent.refusal;
        return overlay;
    }

    overlay.valid = true;
    overlay.subsetRadius = subsetRadius;
    overlay.centreX = snapped(x, extent.firstX, extent.lastX, gridStep);
    overlay.centreY = snapped(y, extent.firstY, extent.lastY, gridStep);

    if (!strainEnabled || strainRadius <= 0.0)
        return overlay;

    overlay.hasSubregion = true;
    overlay.subregionRadius = strainRadius;

    // The same lattice walk gridPointsInSubregion() counts, in the same squared
    // form, so the dots on the picture and the number in the panel cannot
    // disagree about the same subregion. What is dropped here and not there is
    // only what the run itself would drop: a point off the grid's extent, or one
    // the region excludes.
    const double squaredRadius = strainRadius * strainRadius;
    const int reach = int(std::floor(strainRadius / gridStep));
    for (int row = -reach; row <= reach; row++) {
        for (int column = -reach; column <= reach; column++) {
            const double dx = double(column) * gridStep;
            const double dy = double(row) * gridStep;
            if (dx * dx + dy * dy > squaredRadius)
                continue;

            const double px = overlay.centreX + dx;
            const double py = overlay.centreY + dy;
            if (px < extent.firstX || px > extent.lastX
                || py < extent.firstY || py > extent.lastY) {
                // Near an edge the fit genuinely has fewer points. Drawing them
                // anyway would promise a fit the run cannot make.
                continue;
            }
            // ⚑ Engine-free membership, which is what core/Roi.h's own
            // regionContains() exists for: this is drawing, not measuring. A
            // hole takes its points out of the neighbourhood here exactly as it
            // does in the run, and a hole is where a reader is most likely to
            // be looking.
            if (roi.isValid() && !regionContains(roi, int(px), int(py)))
                continue;

            overlay.neighbours.append(QPointF(px, py));
        }
    }
    return overlay;
}
