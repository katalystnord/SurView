#include "core/DisplacementArrows.h"

#include <QObject>

#include <algorithm>
#include <cmath>

namespace {

bool measured(const CorrelationPoint &p)
{
    return p.converged && std::isfinite(p.u) && std::isfinite(p.v);
}

QString ordinal(int n)
{
    const int lastTwo = n % 100;
    const char *suffix = (lastTwo >= 11 && lastTwo <= 13) ? "th"
                         : n % 10 == 1                    ? "st"
                         : n % 10 == 2                    ? "nd"
                         : n % 10 == 3                    ? "rd"
                                                          : "th";
    return QString::number(n) + QLatin1String(suffix);
}

}  // namespace

ArrowLayout layoutDisplacementArrows(const CorrelationResult &result,
                                     double gridStepOnScreen, double minimumSpacing)
{
    ArrowLayout layout;
    if (result.gridColumns <= 0 || result.step <= 0 || !(gridStepOnScreen > 0.0))
        return layout;

    // Thinned by WHOLE grid steps, so every arrow still sits on a point the
    // run measured rather than somewhere between points.
    layout.stride = std::max(1, int(std::ceil(minimumSpacing / gridStepOnScreen - 1e-9)));

    // The scale comes from every measured point, not only the ones drawn at
    // this zoom, so the longest movement anywhere still fits its space.
    double longest = 0.0;
    for (const CorrelationPoint &p : result.points) {
        if (!measured(p))
            continue;
        layout.measured++;
        longest = std::max(longest, std::hypot(double(p.u), double(p.v)));
    }
    if (layout.measured == 0)
        return layout;
    if (!(longest > 0.0)) {
        layout.nothingMoved = true;
        return layout;
    }
    layout.scale = kArrowFill * layout.stride * result.step / longest;

    for (const CorrelationPoint &p : result.points) {
        if (!measured(p))
            continue;
        const int column = p.gridIndex % result.gridColumns;
        const int row = p.gridIndex / result.gridColumns;
        if (column % layout.stride != 0 || row % layout.stride != 0)
            continue;
        // A point measured at exactly no movement has nothing to point at,
        // and is a measurement rather than an absence; it is simply arrowless.
        if (p.u == 0.f && p.v == 0.f)
            continue;
        DisplacementArrow arrow;
        arrow.x = p.x;
        arrow.y = p.y;
        arrow.dx = layout.scale * p.u;
        arrow.dy = layout.scale * p.v;
        layout.arrows.append(arrow);
    }
    return layout;
}

QString arrowNote(const ArrowLayout &layout)
{
    if (layout.measured == 0)
        return QObject::tr("No point was measured, so there is nothing to draw an arrow for.");
    if (layout.nothingMoved) {
        return QObject::tr("Every measured point reads no movement at all: the "
                           "specimen did not move, so there are no arrows.");
    }
    const QString which =
        layout.stride == 1
            ? QObject::tr("an arrow at every measured point")
            : QObject::tr("an arrow at every %1 point each way at this zoom; zoom "
                          "in for more")
                  .arg(ordinal(layout.stride));
    // Shorter than true on a large movement, longer on a small one, and the
    // sentence has to read either way.
    const QString scale = QString::number(layout.scale, 'g', layout.scale < 1.0 ? 2 : 3);
    const QString drawn =
        layout.scale < 1.0 ? QObject::tr("drawn at %1 of their true length").arg(scale)
                           : QObject::tr("drawn %1 times their true length").arg(scale);
    return QObject::tr("Arrows show which way each point moved, %1, with %2. No "
                       "arrow where a point was not measured.")
        .arg(drawn, which);
}
