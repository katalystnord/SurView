#include "core/Cautions.h"

#include "core/Clipping.h"
#include "core/Correlation.h"
#include "core/PointReadout.h"
#include "core/StrainFit.h"

#include <QObject>
#include <QStringList>

QVector<Caution> allCautions()
{
    return {Caution::MostlyClipped, Caution::BelowNoiseFloor,
            Caution::PoorCorrelation, Caution::ConditioningUnusable};
}

QVector<Caution> cautionsAt(const CorrelationPoint &point)
{
    QVector<Caution> found;
    if (!point.converged)
        return found;
    if (point.clippedShareMeasured && double(point.clippedShare) > kMostlyClipped)
        found.append(Caution::MostlyClipped);
    if (displacementIsBelowNoiseFloor(point))
        found.append(Caution::BelowNoiseFloor);
    if (point.zncc < kStrainFitCorrelationFloor)
        found.append(Caution::PoorCorrelation);
    // At a converged point an unestablished conditioning can only mean the
    // probe found the cost too flat (see CorrelationResult::conditioningUnusable).
    if (!point.conditioningMeasured)
        found.append(Caution::ConditioningUnusable);
    return found;
}

QString cautionMark(Caution caution)
{
    // Four shapes that stay distinct when two are drawn over one another.
    switch (caution) {
    case Caution::MostlyClipped:
        return QStringLiteral("×");          // multiplication sign
    case Caution::BelowNoiseFloor:
        return QStringLiteral("○");          // white circle
    case Caution::PoorCorrelation:
        return QStringLiteral("△");          // white up-pointing triangle
    case Caution::ConditioningUnusable:
        return QStringLiteral("□");          // white square
    }
    return QString();
}

QString cautionName(Caution caution)
{
    switch (caution) {
    case Caution::MostlyClipped:
        return QObject::tr("more than half the subset clipped");
    case Caution::BelowNoiseFloor:
        return QObject::tr("moved less than its own noise floor");
    case Caution::PoorCorrelation:
        return QObject::tr("correlation below %1")
            .arg(double(kStrainFitCorrelationFloor), 0, 'g', 2);
    case Caution::ConditioningUnusable:
        return QObject::tr("match too flat to probe");
    }
    return QString();
}

CautionCount countCautions(const CorrelationResult &result, const QRectF &view)
{
    CautionCount count;
    count.byCause = QVector<int>(allCautions().size(), 0);
    count.inView = !view.isNull();
    const QVector<Caution> causes = allCautions();
    for (const CorrelationPoint &point : result.points) {
        if (!point.converged)
            continue;
        // Inclusive at every edge: a point sitting on the view's border is on
        // screen.
        if (count.inView
            && (point.x < view.left() || point.x > view.right()
                || point.y < view.top() || point.y > view.bottom())) {
            continue;
        }
        count.measured++;
        const QVector<Caution> found = cautionsAt(point);
        if (!found.isEmpty())
            count.cautioned++;
        for (Caution caution : found)
            count.byCause[causes.indexOf(caution)]++;
    }
    return count;
}

QString cautionSummary(const CautionCount &count)
{
    const QString where = count.inView ? QObject::tr("In view") : QObject::tr("Over the whole field");
    if (count.measured == 0)
        return QObject::tr("%1: no measured points.").arg(where);
    if (count.cautioned == 0) {
        return QObject::tr("%1: none of the %2 measured points carries a caution.")
            .arg(where)
            .arg(count.measured);
    }

    QStringList causes;
    const QVector<Caution> all = allCautions();
    for (int i = 0; i < all.size(); i++) {
        if (count.byCause.value(i) > 0) {
            causes << QObject::tr("%1 %2 %3")
                          .arg(cautionMark(all[i]))
                          .arg(count.byCause.value(i))
                          .arg(cautionName(all[i]));
        }
    }
    return QObject::tr("%1: %2 of %3 measured points carry no caution; %4 do, marked "
                       "on the field: %5. A count of what is stated about each "
                       "point, not a confidence.")
        .arg(where)
        .arg(count.measured - count.cautioned)
        .arg(count.measured)
        .arg(count.cautioned)
        .arg(causes.join(QStringLiteral(", ")));
}
