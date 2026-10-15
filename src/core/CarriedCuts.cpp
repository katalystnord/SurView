#include "core/CarriedCuts.h"

#include "core/Recovery.h"

#include <QCoreApplication>
#include <QPointF>

#include <cmath>
#include <optional>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("CarriedCuts", text);
}

// The boundary of one drawn shape, walked every kCarrySpacing px or closer, as
// an open ring. A box is walked round its four corners and an ellipse round the
// one inscribed in that box, by the same centre and semi-axes the engine uses
// (core/RoiDetect.cpp, engineRegion()).
QVector<QPointF> boundaryOf(const RegionShape &shape)
{
    QVector<QPointF> ring;

    const auto walk = [&ring](const QVector<QPointF> &corners) {
        for (int i = 0; i < corners.size(); i++) {
            const QPointF from = corners.at(i);
            const QPointF to = corners.at((i + 1) % corners.size());
            const double length = std::hypot(to.x() - from.x(), to.y() - from.y());
            const int steps = std::max(1, int(std::ceil(length / kCarrySpacing)));
            for (int k = 0; k < steps; k++)
                ring.append(from + (to - from) * (double(k) / steps));
        }
    };

    if (shape.kind == RegionShape::Polygon) {
        QVector<QPointF> corners;
        for (const QPoint &corner : shape.points)
            corners.append(QPointF(corner));
        walk(corners);
        return ring;
    }

    const QRect box = shape.bounds();
    if (shape.kind == RegionShape::Rectangle) {
        walk({QPointF(box.left(), box.top()), QPointF(box.right(), box.top()),
              QPointF(box.right(), box.bottom()), QPointF(box.left(), box.bottom())});
        return ring;
    }

    const double cx = 0.5 * (box.left() + box.right());
    const double cy = 0.5 * (box.top() + box.bottom());
    const double a = 0.5 * (box.right() - box.left());
    const double b = 0.5 * (box.bottom() - box.top());
    // Ramanujan's perimeter, to choose how many samples keep them kCarrySpacing
    // apart; an approximation is enough for that.
    const double h = std::pow(a - b, 2) / std::pow(a + b, 2);
    const double perimeter = M_PI * (a + b) * (1.0 + 3.0 * h / (10.0 + std::sqrt(4.0 - 3.0 * h)));
    const int samples = std::max(16, int(std::ceil(perimeter / kCarrySpacing)));
    for (int k = 0; k < samples; k++) {
        const double angle = 2.0 * M_PI * k / samples;
        ring.append(QPointF(cx + a * std::cos(angle), cy + b * std::sin(angle)));
    }
    return ring;
}

// The banked displacement at `at`, from an affine fit to the followed points
// within `reach` whose position is still known. Empty when fewer than `least`
// of them are there, or they lie on a line and so cannot fix a plane.
std::optional<QPointF> displacementAt(const QPointF &at,
                                      const QVector<TrackedPoint> &tracked,
                                      double reach, int least)
{
    // Normal equations of u = a0 + a1 dx + a2 dy, relative to `at`, so a0 is
    // the answer and the sums stay small.
    double sxx = 0, sxy = 0, syy = 0, sx = 0, sy = 0, n = 0;
    double su = 0, sxu = 0, syu = 0, sv = 0, sxv = 0, syv = 0;
    const double reach2 = reach * reach;
    for (const TrackedPoint &point : tracked) {
        if (point.lost)
            continue;
        const double dx = point.x0 - at.x();
        const double dy = point.y0 - at.y();
        if (dx * dx + dy * dy > reach2)
            continue;
        n += 1;
        sx += dx;
        sy += dy;
        sxx += dx * dx;
        sxy += dx * dy;
        syy += dy * dy;
        su += point.bankedU;
        sxu += dx * point.bankedU;
        syu += dy * point.bankedU;
        sv += point.bankedV;
        sxv += dx * point.bankedV;
        syv += dy * point.bankedV;
    }
    if (n < least)
        return std::nullopt;

    // | n  sx  sy  |   | a0 |   | s  |
    // | sx sxx sxy | . | a1 | = | sx.|
    // | sy sxy syy |   | a2 |   | sy.|
    const double det = n * (sxx * syy - sxy * sxy) - sx * (sx * syy - sxy * sy)
                       + sy * (sx * sxy - sxx * sy);
    // Scale-free test for points on one line: the determinant against the
    // product of the diagonal it would have if the spread were round.
    if (!(std::abs(det) > 1e-9 * n * sxx * syy))
        return std::nullopt;

    const auto first = [&](double s, double sxs, double sys) {
        // Cramer's rule for a0 alone.
        const double d0 = s * (sxx * syy - sxy * sxy) - sx * (sxs * syy - sxy * sys)
                          + sy * (sxs * sxy - sxx * sys);
        return d0 / det;
    };
    return QPointF(first(su, sxu, syu), first(sv, sxv, syv));
}

}  // namespace

std::optional<QPointF> carriedTo(const QPointF &drawnAt,
                                 const QVector<TrackedPoint> &tracked,
                                 int gridStep)
{
    const RecoveryNeighbourhood hood = deriveRecoveryNeighbourhood(gridStep);
    const std::optional<QPointF> by =
        displacementAt(drawnAt, tracked, hood.searchRadius, hood.minNeighbours);
    if (!by)
        return std::nullopt;
    return drawnAt + *by;
}

CarriedCuts carryCuts(const RegionOfInterest &drawn,
                      const QVector<TrackedPoint> &tracked,
                      int gridStep)
{
    CarriedCuts carried;
    carried.region = drawn;

    const RecoveryNeighbourhood hood = deriveRecoveryNeighbourhood(gridStep);

    bool pastFirstCut = false;
    for (int i = 0; i < drawn.shapes.size(); i++) {
        const RegionShape &shape = drawn.shapes.at(i);
        // Ground is not part of the mask, and is not carried (core/Roi.h).
        if (!shape.isValid() || shape.isGround())
            continue;
        if (shape.isCut())
            pastFirstCut = true;
        // An addition before any cut changes nothing in the mask (see header).
        if (!pastFirstCut)
            continue;

        RegionShape moved;
        moved.kind = RegionShape::Polygon;
        moved.role = shape.role;
        for (const QPointF &sample : boundaryOf(shape)) {
            const std::optional<QPointF> to = carriedTo(sample, tracked, gridStep);
            if (!to) {
                carried.refusal =
                    tr("Subsets beside a cut were not masked on this frame: the run "
                       "has re-anchored to a later frame, and shape %1 of the region "
                       "(%2) could not be carried there with the specimen, because "
                       "part of its edge has fewer than %3 measured points within "
                       "%4 px to follow.")
                        .arg(i + 1)
                        .arg(shape.describe())
                        .arg(hood.minNeighbours)
                        .arg(double(hood.searchRadius), 0, 'g', 3);
                return carried;
            }
            const QPoint corner = to->toPoint();
            if (moved.points.isEmpty() || moved.points.last() != corner)
                moved.points.append(corner);
        }
        if (moved.points.size() > 1 && moved.points.first() == moved.points.last())
            moved.points.removeLast();

        if (!moved.isValid()) {
            carried.refusal =
                tr("Subsets beside a cut were not masked on this frame: shape %1 of "
                   "the region (%2) is too small to carry to the frame the run "
                   "re-anchored to.")
                    .arg(i + 1)
                    .arg(shape.describe());
            return carried;
        }
        carried.region.shapes[i] = moved;
    }
    return carried;
}
