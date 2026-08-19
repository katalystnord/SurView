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

    // ⚑ THE DENOMINATOR IS EVERY POINT IN THE FIELD, not the ones that solved.
    //
    // This was the other way round to begin with, on the reasoning that a
    // rejected point holds a status code rather than a poor correlation and so
    // has nothing to contribute. That reasoning conflates two different
    // failures. A point that cannot be measured because its subset left the
    // image tells us nothing about the reference; a point that cannot be
    // measured because it DECORRELATED is the single strongest evidence there
    // is that the reference has gone stale. Excluding both made the rule blind
    // to the failure it exists to catch.
    //
    // Found by running a synthetic tension sequence: correlation against the
    // first frame fell from 97% of points solved to 47% across five load steps
    // and the reference never re-anchored once, because the half of the field
    // that survived was still correlating beautifully and was the only half
    // allowed to vote.
    const int placed = int(measured.points.size());
    if (placed == 0)
        return false;

    int stillTracking = 0;
    int anyMeasurement = 0;
    for (const CorrelationPoint &point : measured.points) {
        if (!point.converged)
            continue;
        anyMeasurement++;
        if (double(point.zncc) >= policy.znccThreshold)
            stillTracking++;
    }

    // ⚑ Nothing measured is the one case where re-anchoring is actively
    // harmful rather than merely unhelpful. Re-anchoring banks the increment
    // just measured; with no increment anywhere, it banks nothing and marks
    // EVERY point lost, so the rest of the sequence measures nothing at all.
    // A stale reference that still fails is recoverable on a later frame; a
    // field with no tracked points left is not.
    if (anyMeasurement == 0)
        return false;

    return double(stillTracking) / double(placed) < policy.percentile;
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
