#include "core/Recovery.h"

#include "core/Correlation.h"

#include <QCoreApplication>

namespace {

// The engine's own worked example searches four grid steps and requires nine
// neighbours. Kept here as named constants so the derivation reads as a
// citation rather than as two literals in an expression.
constexpr float kSearchRadiusInGridSteps = 4.f;
constexpr int   kMinNeighbours = 9;

// ⚑ COMPARED IN FLOAT, NOT IN DOUBLE, and the difference is a whole class of
// point. A correlation is a float; a threshold typed into a spin box is a
// double. Widen the float to compare and 0.9f becomes 0.899999976, which is
// below a threshold of 0.9 -- so a point reported at exactly the bar sits just
// under it, invisibly, and only at the boundary. Narrowing the threshold once
// puts both sides in the same representation, and the point at the bar clears
// it. Found by only_confidently_correlated_points_seed_the_fit.
bool isSeed(const CorrelationPoint &point, const RecoveryPolicy &policy)
{
    return point.converged && point.zncc >= float(policy.reliableZncc);
}

bool needsRecovery(const CorrelationPoint &point, const RecoveryPolicy &policy)
{
    return !point.converged || point.zncc < float(policy.retryBelowZncc);
}

}  // namespace

RecoveryNeighbourhood deriveRecoveryNeighbourhood(int gridStep)
{
    RecoveryNeighbourhood derived;
    derived.searchRadius = kSearchRadiusInGridSteps * float(gridStep);
    derived.minNeighbours = kMinNeighbours;
    return derived;
}

RecoveryNeighbourhood recoveryNeighbourhood(const RecoveryPolicy &policy,
                                            int gridStep)
{
    RecoveryNeighbourhood used = deriveRecoveryNeighbourhood(gridStep);
    if (policy.searchRadius > 0.f)
        used.searchRadius = policy.searchRadius;
    if (policy.minNeighbours > 0)
        used.minNeighbours = policy.minNeighbours;
    return used;
}

QString recoveryNeighbourhoodDerivation(int gridStep)
{
    const RecoveryNeighbourhood derived = deriveRecoveryNeighbourhood(gridStep);
    return QCoreApplication::translate(
               "Recovery",
               "Fits from reliable points within %1 px, four times the %2 px "
               "grid step, and needs at least %3 of them.")
        .arg(derived.searchRadius, 0, 'g', 3)
        .arg(gridStep)
        .arg(derived.minNeighbours);
}

QVector<int> pointsNeedingRecovery(const CorrelationResult &field,
                                   const RecoveryPolicy &policy)
{
    QVector<int> queued;
    if (!policy.enabled)
        return queued;

    for (int i = 0; i < field.points.size(); i++) {
        if (needsRecovery(field.points[i], policy))
            queued.append(i);
    }
    return queued;
}

QVector<int> recoverySeeds(const CorrelationResult &field,
                           const RecoveryPolicy &policy)
{
    QVector<int> seeds;
    for (int i = 0; i < field.points.size(); i++) {
        if (isSeed(field.points[i], policy))
            seeds.append(i);
    }
    return seeds;
}

bool recoveryCanRun(const CorrelationResult &field,
                    const RecoveryPolicy &policy)
{
    if (!policy.enabled)
        return false;
    if (pointsNeedingRecovery(field, policy).isEmpty())
        return false;

    const RecoveryNeighbourhood used = recoveryNeighbourhood(policy, field.step);
    return recoverySeeds(field, policy).size() >= used.minNeighbours;
}

bool recoveryImproves(const CorrelationPoint &before,
                      const CorrelationPoint &after)
{
    // Convergence first, and on its own terms. A solver that ran out of
    // iterations can still be sitting on a high correlation at the moment it
    // gives up, and that is not a measurement.
    if (!after.converged)
        return false;

    // A correlation of zero is RegionFit2D's mark for a displacement it fitted
    // rather than measured, so it is the absence of a correlation and not a
    // poor one. Nothing gets into the field on the strength of it.
    if (after.zncc <= 0.f)
        return false;

    if (!before.converged)
        return true;
    return after.zncc > before.zncc;
}

QVector<int> acceptRecoveryRound(CorrelationResult &field,
                                 const QVector<int> &attempted,
                                 const QVector<CorrelationPoint> &resolved)
{
    const int count = qMin(attempted.size(), resolved.size());

    QVector<int> accepted;
    for (int i = 0; i < count; i++) {
        const int index = attempted[i];
        if (index < 0 || index >= field.points.size())
            continue;

        CorrelationPoint &point = field.points[index];
        if (!recoveryImproves(point, resolved[i]))
            continue;

        // The point's identity is its own: where it sits and which cell it
        // reports to are not taken from the answer.
        const int gridIndex = point.gridIndex;
        const float x = point.x;
        const float y = point.y;

        point = resolved[i];
        point.gridIndex = gridIndex;
        point.x = x;
        point.y = y;
        point.recovered = true;
        point.failureReason.clear();
        accepted.append(index);
    }

    field.converged = 0;
    for (const CorrelationPoint &point : field.points) {
        if (point.converged)
            field.converged++;
    }

    return accepted;
}

bool recoveryShouldContinue(int roundsDone, int acceptedThisRound,
                            const RecoveryPolicy &policy)
{
    return acceptedThisRound > 0 && roundsDone < policy.maxRounds;
}
