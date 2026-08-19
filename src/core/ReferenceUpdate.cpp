#include "core/ReferenceUpdate.h"

#include "core/Correlation.h"

float trackedX(const TrackedPoint &point)
{
    return point.x0 + point.bankedU;
}

float trackedY(const TrackedPoint &point)
{
    return point.y0 + point.bankedV;
}

bool fieldNeedsReanchor(const CorrelationResult &measured,
                        const ReferenceUpdatePolicy &policy)
{
    if (!policy.enabled)
        return false;

    int measuredPoints = 0;
    int stillTracking = 0;
    for (const CorrelationPoint &point : measured.points) {
        if (!point.converged)
            continue;
        measuredPoints++;
        if (double(point.zncc) >= policy.znccThreshold)
            stillTracking++;
    }

    // Nothing measured is not a verdict on the reference. Re-anchoring to a
    // frame that correlated with nothing would replace a reference that may
    // still be good with one known to be useless.
    if (measuredPoints == 0)
        return false;

    return double(stillTracking) / double(measuredPoints) < policy.percentile;
}

QVector<TrackedPoint> startTracking(const CorrelationResult &first)
{
    QVector<TrackedPoint> tracked;
    tracked.reserve(first.points.size());
    for (const CorrelationPoint &point : first.points) {
        TrackedPoint followed;
        followed.gridIndex = point.gridIndex;
        followed.x0 = point.x;
        followed.y0 = point.y;
        tracked.append(followed);
    }
    return tracked;
}

void bankIncrement(QVector<TrackedPoint> &tracked,
                   const CorrelationResult &increment)
{
    const int count = std::min(tracked.size(), increment.points.size());
    for (int i = 0; i < count; i++) {
        TrackedPoint &followed = tracked[i];
        if (followed.lost)
            continue;

        const CorrelationPoint &point = increment.points.at(i);
        if (!point.converged) {
            // No increment, so no way to say where this point sits in the frame
            // about to become the reference. Lost rather than left in place:
            // left in place it would be measured at the wrong pixels from here
            // on, and would report its last displacement as though it were
            // current.
            followed.lost = true;
            continue;
        }

        followed.bankedU += point.u;
        followed.bankedV += point.v;
    }
}

int lostCount(const QVector<TrackedPoint> &tracked)
{
    int lost = 0;
    for (const TrackedPoint &point : tracked) {
        if (point.lost)
            lost++;
    }
    return lost;
}

CorrelationResult composeTotalField(const QVector<TrackedPoint> &tracked,
                                    const CorrelationResult &increment)
{
    // Everything the increment knows about the run is still true of the total:
    // the grid, the region, the timings, the reliability. Only the points are
    // re-expressed.
    CorrelationResult total = increment;
    total.points.clear();
    total.converged = 0;

    const int count = std::min(tracked.size(), increment.points.size());
    total.points.reserve(count);

    for (int i = 0; i < count; i++) {
        const TrackedPoint &followed = tracked.at(i);
        CorrelationPoint point = increment.points.at(i);

        // Reported where the point started, not where it has moved to: the
        // field of every frame then lies on one grid, so frames can be laid
        // over one another and over the reference photograph.
        point.gridIndex = followed.gridIndex;
        point.x = followed.x0;
        point.y = followed.y0;

        if (followed.lost) {
            // Whatever the solver returned for a point whose position is
            // unknown is a measurement of the wrong pixels.
            point.converged = false;
            point.strainFitted = false;
            point.noiseFloorMeasured = false;
            point.conditioningMeasured = false;
        } else if (point.converged) {
            point.u += followed.bankedU;
            point.v += followed.bankedV;
            total.converged++;
        }

        total.points.append(point);
    }

    return total;
}
