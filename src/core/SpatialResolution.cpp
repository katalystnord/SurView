#include "core/SpatialResolution.h"

#include "core/StrainFit.h"

#include <QObject>

#include <cmath>

int displacementAveragingLength(int subsetRadius)
{
    if (subsetRadius <= 0)
        return 0;
    return 2 * subsetRadius + 1;
}

int strainGaugeLength(double strainRadius, int gridStep, int subsetRadius)
{
    const int subset = displacementAveragingLength(subsetRadius);
    if (subset == 0 || gridStep <= 0 || !(strainRadius >= 0.0))
        return 0;

    // The outermost points on an axis are the same ones gridPointsInSubregion()
    // counts: whole steps out, no further than the radius.
    const int reach = int(std::floor(strainRadius / gridStep));
    if (reach < 1)
        return 0;
    return 2 * reach * gridStep + subset;
}

QString displacementResolutionNote(int subsetRadius)
{
    const int length = displacementAveragingLength(subsetRadius);
    if (length == 0)
        return QString();
    return QObject::tr("Each displacement is an average over its %1 px subset. "
                       "That is the scale of the averaging, not the smallest "
                       "feature measured at full size: movement that varies "
                       "over a few subsets is still measured smaller than it is.")
        .arg(length);
}

QString strainResolutionNote(double strainRadius, int gridStep, int subsetRadius,
                             int minNeighbours)
{
    const int length = strainGaugeLength(strainRadius, gridStep, subsetRadius);
    if (displacementAveragingLength(subsetRadius) == 0 || gridStep <= 0
        || !(strainRadius >= 0.0)) {
        return QString();
    }

    if (length == 0 || gridPointsInSubregion(strainRadius, gridStep) < minNeighbours) {
        return QObject::tr("No strain gauge length can be stated: the fit would "
                           "use points from outside the subregion, so it is not "
                           "the subregion that strain is averaged over.");
    }

    const int span = length - displacementAveragingLength(subsetRadius);
    return QObject::tr("Each strain is an average over %1 px: the %2 px between "
                       "the outermost points the fit uses, plus one %3 px subset. "
                       "That is the virtual strain gauge, not the smallest "
                       "feature measured at full size: a strain concentration "
                       "narrower than this is spread out and reads lower.")
        .arg(length)
        .arg(span)
        .arg(displacementAveragingLength(subsetRadius));
}
