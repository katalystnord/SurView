#include "core/FieldLayout.h"

#include "core/Correlation.h"

#include <cmath>
#include <limits>

QVector<float> layoutDisplacementMagnitude(const CorrelationResult &result)
{
    const int cells = result.gridColumns * result.gridRows;
    if (cells <= 0)
        return {};

    QVector<float> values(cells, std::numeric_limits<float>::quiet_NaN());

    for (const CorrelationPoint &point : result.points) {
        // Placed by the cell the point recorded, never by its position in the
        // list: with a region excluding cells the two differ, and filling in
        // list order slides the whole field sideways.
        if (point.gridIndex < 0 || point.gridIndex >= cells)
            continue;
        if (point.converged)
            values[point.gridIndex] = float(std::hypot(point.u, point.v));
    }

    return values;
}
