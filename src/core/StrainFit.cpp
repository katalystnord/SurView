#include "core/StrainFit.h"

#include <QObject>

#include <cmath>

QVector<StrainMeasureChoice> offeredStrainMeasures()
{
    return {
        StrainMeasureChoice{
            StrainMeasure::Cauchy,
            strainMeasureName(StrainMeasure::Cauchy),
            QObject::tr("Linear. Valid while displacement gradients stay small, "
                        "which covers most stiff-material tests."),
        },
        StrainMeasureChoice{
            StrainMeasure::GreenLagrange,
            strainMeasureName(StrainMeasure::GreenLagrange),
            QObject::tr("Keeps the quadratic terms. Use it once the strains are "
                        "large enough that the linear form under-reports them."),
        },
    };
}

QString strainMeasureName(StrainMeasure measure)
{
    switch (measure) {
    case StrainMeasure::Cauchy:
        return QObject::tr("Cauchy (small strain)");
    case StrainMeasure::GreenLagrange:
        return QObject::tr("Green-Lagrange (large strain)");
    }
    return QString();
}

int gridPointsInSubregion(double radius, int gridStep)
{
    if (gridStep <= 0 || radius < 0.0)
        return 0;

    // Counted, not estimated. The lattice is walked out to the furthest ring
    // the radius can touch and each point tested against the radius itself,
    // in the squared form the engine's own search uses -- so a point sitting
    // exactly on the boundary lands on the same side here as it does there.
    const double squaredRadius = radius * radius;
    const int reach = int(std::floor(radius / gridStep));

    int inside = 0;
    for (int row = -reach; row <= reach; row++) {
        for (int column = -reach; column <= reach; column++) {
            const double dx = double(column) * gridStep;
            const double dy = double(row) * gridStep;
            if (dx * dx + dy * dy <= squaredRadius)
                inside++;
        }
    }
    return inside;
}

QString strainSubregionWarning(double radius, int gridStep, int minNeighbours)
{
    const int available = gridPointsInSubregion(radius, gridStep);
    if (available >= minNeighbours)
        return QString();

    // Names the substitution, not just the shortfall. "Too small" would leave
    // the reader believing nothing was measured; something is measured either
    // way, over a neighbourhood that is not the one on screen, and that is the
    // part they cannot otherwise see.
    return QObject::tr(
               "A %1 px strain subregion holds %2 points at a %3 px grid step, "
               "and the fit needs %4. The engine will not refuse: it falls back "
               "to the nearest %4 points however far outside the subregion they "
               "lie, so the strain field would be fitted over a neighbourhood "
               "you did not choose. Widen the subregion, tighten the grid step, "
               "or lower the minimum. This count is the best case, for a point "
               "with grid all around it; points at the edge of the image or of "
               "a region always have fewer.")
        .arg(radius, 0, 'g', 4)
        .arg(available)
        .arg(gridStep)
        .arg(minNeighbours);
}
