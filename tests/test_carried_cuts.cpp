// Carrying a region's cuts to a re-anchored reference (core/CarriedCuts.h).
//
// Engine-free: the followed points are laid out here on a grid, and their banked
// displacement is an EXACT affine deformation, x = X + t + G (X - C), so where a
// cut should have gone is known at every pixel by inverting it. Lopsided in
// every axis the code could confuse: the translation differs in x and y and in
// sign, x stretches while y shortens, there is shear, and the deformation is
// centred well away from every cut, so a cut carried by one mean movement for
// the whole field lands visibly off.
//
// As a run would have them, no point is followed from inside a cut: the grid
// never places one there, so every boundary is fitted from one side only.
//
// Written red first (2026-10-11): four of six cases failed against a stub that
// returned the region as drawn. The two that passed describe a field that did
// not move, or ground the carry must leave alone, which the stub was.
//
// NEGATIVE CHECKS, 14 of 14 caught: lost points used in the fit; one mean
// movement for the whole field; each point's own movement with no slope; the
// outer shape carried too; shapes numbered from zero; the floor of nine not
// applied; the reach not applied; a carried ellipse kept as an ellipse; the
// cut flag lost; an ellipse sampled at eight points; polygon edges not walked;
// an ellipse's axes swapped; nothing carried. Two of those survived the first
// version of this file and are why two cases exist: on an affine movement a
// straight edge stays straight, so carrying only the corners was exact until
// the bending crack; and eight scattered points left some edge samples with
// fewer than three anyway, so the floor is pinned at one point, eight refused
// and nine accepted.

#include "core/CarriedCuts.h"
#include "core/Roi.h"

#include <QTest>

#include <cmath>

namespace {

const double kTx = 3.1, kTy = -1.7;
const double kG[2][2] = {{0.030, 0.010}, {0.000, -0.020}};
const double kCx = 30.0, kCy = 20.0;
const int kStep = 8;

QPointF deformed(double X, double Y)
{
    return {X + kTx + kG[0][0] * (X - kCx) + kG[0][1] * (Y - kCy),
            Y + kTy + kG[1][0] * (X - kCx) + kG[1][1] * (Y - kCy)};
}

// The material point now at (x, y).
QPointF undeformed(double x, double y)
{
    const double a = 1.0 + kG[0][0], b = kG[0][1];
    const double c = kG[1][0], d = 1.0 + kG[1][1];
    const double det = a * d - b * c;
    const double rx = x - kCx - kTx, ry = y - kCy - kTy;
    return {(d * rx - b * ry) / det + kCx, (-c * rx + a * ry) / det + kCy};
}

RegionShape box(RegionShape::Kind kind, int left, int top, int right, int bottom, bool cut)
{
    RegionShape shape;
    shape.kind = kind;
    shape.role = cut ? RegionShape::Cut : RegionShape::Add;
    shape.points = {QPoint(left, top), QPoint(right, bottom)};
    return shape;
}

// The specimen, an ellipse cut, a lopsided polygon cut, and an island put back
// inside the ellipse.
RegionOfInterest drawnRegion()
{
    RegionOfInterest roi;
    RegionShape pentagon;
    pentagon.kind = RegionShape::Polygon;
    pentagon.role = RegionShape::Cut;
    pentagon.points = {QPoint(170, 40), QPoint(205, 52), QPoint(214, 90),
                       QPoint(181, 101), QPoint(163, 70)};
    roi.shapes << box(RegionShape::Rectangle, 6, 9, 232, 188, false)
               << box(RegionShape::Ellipse, 80, 100, 140, 150, true)
               << pentagon
               << box(RegionShape::Rectangle, 104, 118, 115, 129, false);
    return roi;
}

// A grid of followed points over the drawn region, none inside a cut, each
// banked by the exact deformation.
QVector<TrackedPoint> followed(const RegionOfInterest &roi)
{
    QVector<TrackedPoint> points;
    int index = 0;
    for (int y = 13; y <= 184; y += kStep) {
        for (int x = 10; x <= 228; x += kStep) {
            index++;
            if (!regionContains(roi, x, y))
                continue;
            TrackedPoint point;
            point.gridIndex = index;
            point.x0 = float(x);
            point.y0 = float(y);
            const QPointF to = deformed(x, y);
            point.bankedU = float(to.x() - x);
            point.bankedV = float(to.y() - y);
            points.append(point);
        }
    }
    return points;
}

// Pixels of the current reference where the carried region and the drawn one,
// seen through the exact deformation, disagree about being cut away. Pixels
// within `band` px of a drawn boundary are not counted: a boundary carried to
// whole pixels cannot be exact there; the pull-in covers it (measured in
// core/CarriedCuts.h).
int disagreements(const RegionOfInterest &drawn, const RegionOfInterest &carried,
                  double band, QString *first = nullptr)
{
    // Removed by the mask's own rule: the whole image added first, then the
    // shapes in order. An addition before the first cut therefore changes
    // nothing, which is why carryCuts() may leave it as drawn.
    const auto removed = [](const RegionOfInterest &roi, double x, double y) {
        const int px = qRound(x), py = qRound(y);
        for (int i = roi.shapes.size() - 1; i >= 0; i--) {
            if (shapeContains(roi.shapes.at(i), px, py))
                return roi.shapes.at(i).isCut();
        }
        return false;
    };

    int count = 0;
    for (int y = 0; y < 200; y++) {
        for (int x = 0; x < 240; x++) {
            const QPointF at = undeformed(x, y);
            const bool truth = removed(drawn, at.x(), at.y());
            bool clear = true;
            for (int k = 0; k < 16 && clear; k++) {
                const double angle = k * M_PI / 8.0;
                clear = removed(drawn, at.x() + band * std::cos(angle),
                                at.y() + band * std::sin(angle)) == truth;
            }
            if (!clear)
                continue;
            if (removed(carried, x, y) != truth) {
                if (count == 0 && first)
                    *first = QStringLiteral("(%1, %2) is %3 where it should not be")
                                 .arg(x).arg(y)
                                 .arg(truth ? QStringLiteral("kept") : QStringLiteral("cut"));
                count++;
            }
        }
    }
    return count;
}

}  // namespace

class TestCarriedCuts : public QObject
{
    Q_OBJECT

private slots:
    void a_point_on_an_edge_is_carried_exactly_by_a_movement_affine_around_it();
    void a_cut_is_carried_to_where_the_specimen_took_it();
    void an_island_inside_a_cut_is_carried_with_it_and_the_order_is_kept();
    void shapes_before_the_first_cut_are_left_as_drawn();
    void a_lost_point_does_not_steer_the_boundary();
    void a_cut_with_too_few_measured_points_around_it_is_refused_and_named();
    void nothing_banked_leaves_the_cuts_where_they_were_drawn();
    void a_crack_along_a_bending_specimen_bends_with_it();
    void a_fit_from_fewer_than_nine_points_is_not_trusted();
    void ground_is_not_carried_and_cannot_refuse_a_frame();
};

void TestCarriedCuts::a_point_on_an_edge_is_carried_exactly_by_a_movement_affine_around_it()
{
    // Exactly, not to within a pixel: a fit is exact on a movement that is
    // affine around the point, and the nearest point's own movement is not,
    // by the gradient times however far away it sits -- a third of a pixel
    // here, which a boundary rounded to whole pixels would hide.
    const RegionOfInterest drawn = drawnRegion();
    const QVector<TrackedPoint> points = followed(drawn);
    // Edge points of the ellipse and the pentagon, fitted from one side only.
    const QVector<QPointF> edges = {QPointF(80.0, 125.0), QPointF(140.0, 125.0),
                                    QPointF(110.0, 100.0), QPointF(110.0, 150.0),
                                    QPointF(214.0, 90.0), QPointF(163.0, 70.0)};
    for (const QPointF &at : edges) {
        const std::optional<QPointF> to = carriedTo(at, points, kStep);
        QVERIFY(to.has_value());
        const QPointF truth = deformed(at.x(), at.y());
        QVERIFY2(std::hypot(to->x() - truth.x(), to->y() - truth.y()) < 1e-3,
                 qPrintable(QStringLiteral("(%1, %2) carried to (%3, %4), not (%5, %6)")
                                .arg(at.x()).arg(at.y()).arg(to->x()).arg(to->y())
                                .arg(truth.x()).arg(truth.y())));
    }
}

void TestCarriedCuts::a_cut_is_carried_to_where_the_specimen_took_it()
{
    const RegionOfInterest drawn = drawnRegion();
    const CarriedCuts carried = carryCuts(drawn, followed(drawn), kStep);
    QVERIFY2(carried.isValid(), qPrintable(carried.refusal));

    // Before carrying, the cuts are visibly in the wrong place: the case can
    // tell a carried cut from one left where it was drawn.
    QVERIFY(disagreements(drawn, drawn, 1.0) > 200);

    QString where;
    const int wrong = disagreements(drawn, carried.region, 1.0, &where);
    QVERIFY2(wrong == 0, qPrintable(QStringLiteral("%1 pixels disagree; %2")
                                        .arg(wrong).arg(where)));
}

void TestCarriedCuts::an_island_inside_a_cut_is_carried_with_it_and_the_order_is_kept()
{
    const RegionOfInterest drawn = drawnRegion();
    const CarriedCuts carried = carryCuts(drawn, followed(drawn), kStep);
    QVERIFY2(carried.isValid(), qPrintable(carried.refusal));

    QCOMPARE(carried.region.shapes.size(), drawn.shapes.size());
    for (int i = 0; i < drawn.shapes.size(); i++)
        QCOMPARE(int(carried.region.shapes.at(i).role), int(drawn.shapes.at(i).role));

    // The island's centre, carried, is ground again in the carried region.
    const QPointF island = deformed(109.5, 123.5);
    QVERIFY(regionContains(carried.region, qRound(island.x()), qRound(island.y())));
    // And the cut around it is still cut, on the side the specimen moved away from.
    const QPointF hole = deformed(92.0, 140.0);
    QVERIFY(!regionContains(carried.region, qRound(hole.x()), qRound(hole.y())));
}

void TestCarriedCuts::shapes_before_the_first_cut_are_left_as_drawn()
{
    const RegionOfInterest drawn = drawnRegion();
    QVector<TrackedPoint> points = followed(drawn);
    // Lose every point along the outer boundary: had the outer rectangle been
    // carried, it would now be refused for want of neighbours.
    for (TrackedPoint &p : points) {
        if (p.x0 < 40 || p.x0 > 200 || p.y0 < 30 || p.y0 > 170) {
            if (!(p.x0 > 150 && p.y0 < 120))   // the pentagon keeps its ground
                p.lost = true;
        }
    }
    const CarriedCuts carried = carryCuts(drawn, points, kStep);
    QVERIFY2(carried.isValid(), qPrintable(carried.refusal));
    QCOMPARE(carried.region.shapes.first(), drawn.shapes.first());
}

void TestCarriedCuts::a_lost_point_does_not_steer_the_boundary()
{
    const RegionOfInterest drawn = drawnRegion();
    QVector<TrackedPoint> points = followed(drawn);
    // Points close round the right of the ellipse lost, holding a banked
    // displacement far from the truth, as a lost point does: what it had when it
    // was lost. The ground round the island, left of it, is still followed.
    int lostHere = 0;
    for (TrackedPoint &p : points) {
        const double dx = (p.x0 - 110.0) / 30.0, dy = (p.y0 - 125.0) / 25.0;
        if (p.x0 > 140.0 && dx * dx + dy * dy < 2.4) {
            p.lost = true;
            p.bankedU = 40.f;
            p.bankedV = -35.f;
            lostHere++;
        }
    }
    QVERIFY(lostHere > 5);

    const CarriedCuts carried = carryCuts(drawn, points, kStep);
    QVERIFY2(carried.isValid(), qPrintable(carried.refusal));
    QString where;
    const int wrong = disagreements(drawn, carried.region, 1.0, &where);
    QVERIFY2(wrong == 0, qPrintable(QStringLiteral("%1 pixels disagree; %2")
                                        .arg(wrong).arg(where)));
}

void TestCarriedCuts::a_cut_with_too_few_measured_points_around_it_is_refused_and_named()
{
    const RegionOfInterest drawn = drawnRegion();
    QVector<TrackedPoint> points = followed(drawn);
    // Everything around the pentagon, the THIRD shape, lost.
    for (TrackedPoint &p : points) {
        if (p.x0 > 140 && p.y0 < 135)
            p.lost = true;
    }
    const CarriedCuts carried = carryCuts(drawn, points, kStep);
    QVERIFY(!carried.isValid());
    QVERIFY2(carried.refusal.contains(QStringLiteral("shape 3")), qPrintable(carried.refusal));
    QVERIFY2(!carried.refusal.contains(QStringLiteral("shape 2")), qPrintable(carried.refusal));
    // Says how many it needed, in the neighbourhood's own figures.
    QVERIFY2(carried.refusal.contains(QStringLiteral("32 px")), qPrintable(carried.refusal));
    QVERIFY2(carried.refusal.contains(QStringLiteral("fewer than 9")), qPrintable(carried.refusal));
}

void TestCarriedCuts::nothing_banked_leaves_the_cuts_where_they_were_drawn()
{
    const RegionOfInterest drawn = drawnRegion();
    QVector<TrackedPoint> points = followed(drawn);
    for (TrackedPoint &p : points)
        p.bankedU = p.bankedV = 0.f;
    const CarriedCuts carried = carryCuts(drawn, points, kStep);
    QVERIFY2(carried.isValid(), qPrintable(carried.refusal));

    // Away from the boundaries, every pixel agrees with the drawn region.
    int wrong = 0;
    for (int y = 0; y < 200; y++) {
        for (int x = 0; x < 240; x++) {
            bool nearEdge = false;
            const bool here = regionContains(drawn, x, y);
            for (int dy = -1; dy <= 1 && !nearEdge; dy++)
                for (int dx = -1; dx <= 1 && !nearEdge; dx++)
                    nearEdge = regionContains(drawn, x + dx, y + dy) != here;
            if (!nearEdge && regionContains(carried.region, x, y) != here)
                wrong++;
        }
    }
    QCOMPARE(wrong, 0);
}

namespace {

// A beam bending: v grows with the square of the distance from x = 120, by
// 5.1 px at either end of the crack below. A straight edge does not stay
// straight under it, which no affine movement can show.
const double kBend = 0.0008;

QPointF bent(double X, double Y)
{
    return {X + 1.3, Y - 0.6 + kBend * (X - 120.0) * (X - 120.0)};
}

QPointF unbent(double x, double y)
{
    const double X = x - 1.3;
    return {X, y + 0.6 - kBend * (X - 120.0) * (X - 120.0)};
}

}  // namespace

void TestCarriedCuts::a_crack_along_a_bending_specimen_bends_with_it()
{
    // A long thin cut, as a crack is drawn: four corners, 160 px long.
    RegionOfInterest drawn;
    RegionShape crack;
    crack.kind = RegionShape::Polygon;
    crack.role = RegionShape::Cut;
    crack.points = {QPoint(40, 96), QPoint(200, 90), QPoint(200, 97), QPoint(40, 103)};
    drawn.shapes << box(RegionShape::Rectangle, 6, 9, 232, 188, false) << crack;

    QVector<TrackedPoint> points;
    int index = 0;
    for (int y = 13; y <= 184; y += kStep) {
        for (int x = 10; x <= 228; x += kStep) {
            index++;
            if (!regionContains(drawn, x, y))
                continue;
            TrackedPoint point;
            point.gridIndex = index;
            point.x0 = float(x);
            point.y0 = float(y);
            const QPointF to = bent(x, y);
            point.bankedU = float(to.x() - x);
            point.bankedV = float(to.y() - y);
            points.append(point);
        }
    }

    const CarriedCuts carried = carryCuts(drawn, points, kStep);
    QVERIFY2(carried.isValid(), qPrintable(carried.refusal));

    const auto removed = [](const RegionOfInterest &roi, double x, double y) {
        return !regionContains(roi, qRound(x), qRound(y))
               && shapeContains(roi.shapes.last(), qRound(x), qRound(y));
    };
    int wrong = 0, counted = 0;
    QString where;
    for (int y = 60; y < 140; y++) {
        for (int x = 20; x < 220; x++) {
            const QPointF at = unbent(x, y);
            const bool truth = removed(drawn, at.x(), at.y());
            bool clear = true;
            for (int k = 0; k < 16 && clear; k++) {
                const double angle = k * M_PI / 8.0;
                clear = removed(drawn, at.x() + std::cos(angle),
                                at.y() + std::sin(angle)) == truth;
            }
            if (!clear || !truth)
                continue;
            counted++;
            if (!removed(carried.region, x, y)) {
                if (wrong == 0)
                    where = QStringLiteral("(%1, %2)").arg(x).arg(y);
                wrong++;
            }
        }
    }
    QVERIFY(counted > 300);
    QVERIFY2(wrong == 0, qPrintable(QStringLiteral("%1 pixels of the bent crack are "
                                                   "not cut, first at %2")
                                        .arg(wrong).arg(where)));
}

void TestCarriedCuts::a_fit_from_fewer_than_nine_points_is_not_trusted()
{
    // Eight points round a boundary point, all within reach and well spread.
    // Eight fix an affine fit perfectly well on a movement that is exactly
    // affine, which is why this has to be asked separately: nine is the floor
    // the recovery pass uses (deriveRecoveryNeighbourhood()), and a boundary
    // carried on fewer is refused rather than guessed. A ninth is accepted.
    const QPointF at(100.0, 100.0);
    QVector<TrackedPoint> points;
    const auto follow = [&points](double x, double y) {
        TrackedPoint p;
        p.gridIndex = int(points.size());
        p.x0 = float(x);
        p.y0 = float(y);
        const QPointF to = deformed(x, y);
        p.bankedU = float(to.x() - x);
        p.bankedV = float(to.y() - y);
        points.append(p);
    };
    for (int k = 0; k < 8; k++) {
        const double angle = 2.0 * M_PI * k / 8.0 + 0.3;
        follow(at.x() + (12.0 + 2.0 * k) * std::cos(angle),
               at.y() + (12.0 + 2.0 * k) * std::sin(angle));
    }
    follow(at.x() + 40.0, at.y());   // beyond the 32 px reach, so not counted
    follow(at.x(), at.y() - 33.0);

    QVERIFY(!carriedTo(at, points, kStep).has_value());

    follow(at.x() - 5.0, at.y() + 7.0);
    const std::optional<QPointF> to = carriedTo(at, points, kStep);
    QVERIFY(to.has_value());
    const QPointF truth = deformed(at.x(), at.y());
    QVERIFY(std::hypot(to->x() - truth.x(), to->y() - truth.y()) < 1e-3);
}

void TestCarriedCuts::ground_is_not_carried_and_cannot_refuse_a_frame()
{
    // ⚑ Ground (core/Roi.h) is no part of the mask, and lies off the specimen
    // where no point is followed. Carried like a cut, it could not be fitted,
    // and the whole frame would go unmasked over a shape that masks nothing.
    // Negative check (2026-10-15): ground carried like a cut, red on the
    // refusal, which names the ground shape.
    RegionOfInterest drawn = drawnRegion();
    RegionShape bench = box(RegionShape::Rectangle, 300, 10, 380, 60, false);
    bench.role = RegionShape::Ground;
    drawn.shapes.append(bench);

    const CarriedCuts carried = carryCuts(drawn, followed(drawn), kStep);
    QVERIFY2(carried.isValid(), qPrintable(carried.refusal));
    QVERIFY2(carried.region.shapes.last() == bench, "the ground was moved");
}

QTEST_GUILESS_MAIN(TestCarriedCuts)
#include "test_carried_cuts.moc"
