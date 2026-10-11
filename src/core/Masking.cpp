#include "core/Masking.h"

#include <QObject>

#include <algorithm>

namespace {

QString percent(double share)
{
    return QObject::tr("%1 %").arg(qRound(share * 100.0));
}

}  // namespace

QString maskAtCutsLabel()
{
    return QObject::tr("Leave cuts out of the subsets beside them");
}

QString minValidShareLabel()
{
    return QObject::tr("Least of a subset clear of the cuts");
}

QString maskingPanelNote(const CorrelationSettings &settings, const RegionOfInterest &roi)
{
    if (!roi.isValid() || !roi.hasCuts()) {
        return QObject::tr("The region has no cuts, so every subset keeps all its "
                           "pixels. A cut is made with Analysis > Cut from Region.");
    }
    if (!settings.maskAtCuts) {
        return QObject::tr("Off: a subset beside a cut is correlated over the cut "
                           "too. What shows through a hole does not move with the "
                           "specimen, so those points are pulled toward no movement "
                           "while still correlating well.");
    }
    if (settings.solver != CorrelationSettings::ICGN) {
        return QObject::tr("%1 cannot leave a cut out yet, so a point whose subset "
                           "reaches a cut will be refused, saying so. ICGN can.")
            .arg(solverDisplayName(settings.solver));
    }
    return QObject::tr("A subset beside a cut is correlated only on the ground clear "
                       "of it, pulled back %1 px from the cut, because the engine's "
                       "gradient and interpolation reach %1 px across an edge. The "
                       "region's outer edge is not masked: a subset reaching past it "
                       "keeps every pixel. A point with less than %2 of its subset "
                       "clear is refused. Each point's share is in its readout.")
        .arg(kMaskPullIn)
        .arg(percent(settings.minValidShare));
}

QStringList maskingReport(const CorrelationResult &result)
{
    QStringList lines;
    if (result.maskingApplied) {
        if (result.maskedPoints > 0) {
            float least = 1.f;
            for (const CorrelationPoint &point : result.points) {
                if (point.masked && point.converged)
                    least = std::min(least, point.validShare);
            }
            lines << QObject::tr("%1 measured point(s) beside a cut were measured on "
                                 "the ground clear of it, pulled back %2 px; the "
                                 "smallest share any of them kept was %3. The outer "
                                 "edge of the region was not masked.")
                         .arg(result.maskedPoints)
                         .arg(result.maskPullIn)
                         .arg(percent(double(least)));
        } else {
            lines << QObject::tr("No measured point's subset reached a cut, so "
                                 "nothing was masked.");
        }
        return lines;
    }

    if (!result.maskingNote.isEmpty())
        lines << result.maskingNote;

    if (result.subsetsReachingAHole > 0) {
        lines << QObject::tr("%1 measured point(s) have a subset reaching into a "
                             "cut. Those subsets include pixels that do not move "
                             "with the specimen, which pulls their answer toward no "
                             "movement. \"%2\" in the Analysis panel leaves them out.")
                     .arg(result.subsetsReachingAHole)
                     .arg(maskAtCutsLabel());
    }
    return lines;
}

QString maskingStatement(const CorrelationResult &result)
{
    if (result.maskingApplied) {
        return QObject::tr("subsets beside a cut were correlated only on the ground "
                           "clear of it, pulled back %1 px from the cut; a point with "
                           "less than %2 of its subset clear was refused. The "
                           "region's outer edge was not masked. measured_share is "
                           "the share, 0 to 1, of each point's subset that took "
                           "part; %3 solved point(s) were measured on part of theirs.")
            .arg(result.maskPullIn)
            .arg(percent(result.minValidShare))
            .arg(result.maskedPoints);
    }
    if (!result.maskingNote.isEmpty())
        return result.maskingNote + QLatin1Char(' ')
               + QObject::tr("measured_share is 1 at every point.");
    if (result.subsetsReachingAHole > 0) {
        return QObject::tr("subsets beside a cut were NOT masked: %1 measured "
                           "point(s) correlated over a cut. measured_share is 1 at "
                           "every point.")
            .arg(result.subsetsReachingAHole);
    }
    return QObject::tr("no subset reached a cut. measured_share is 1 at every point.");
}
