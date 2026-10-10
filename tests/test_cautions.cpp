// Which measured points carry a stated caution, and the count the bar shows.
//
// The fixture is lopsided by cause: each cause appears a different number of
// times, one point carries two at once, one point is rejected while looking
// like every caution at once, and the points sit unevenly either side of the
// view rectangle used below, so a count that confused causes, double-counted a
// point, counted a hole or ignored the view gives a different number.
//
// NEGATIVE CHECKS (2026-10-10), each red: a rejected point cautioned; the view's
// edges made exclusive; a point counted once per cause; the clipped line moved
// to "half or more" (red only once the exactly-half case was added -- the first
// run let it survive); the flat-match cause dropped; a cause with nothing in
// view listed at zero.

#include "core/Cautions.h"
#include "core/Correlation.h"

#include <QTest>

class TestCautions : public QObject
{
    Q_OBJECT

private slots:
    void a_clean_measured_point_carries_no_caution();
    void each_cause_is_recognised_on_its_own();
    void a_rejected_point_carries_no_caution_however_it_looks();
    void a_point_with_two_cautions_is_one_point_under_two_causes();
    void a_view_counts_only_the_points_inside_it_edges_included();
    void the_summary_is_a_count_with_its_causes_and_never_a_confidence();
    void a_field_with_nothing_to_say_says_so();
};

namespace {

CorrelationPoint clean(float x, float y)
{
    CorrelationPoint p;
    p.x = x;
    p.y = y;
    p.u = 2.f;
    p.v = 0.5f;
    p.zncc = 0.98f;
    p.converged = true;
    p.noiseFloor = 0.004f;
    p.noiseFloorMeasured = true;
    p.conditioning = 0.3f;
    p.conditioningMeasured = true;
    p.clippedShare = 0.02f;
    p.clippedShareMeasured = true;
    return p;
}

CorrelationPoint clipped(float x, float y)
{
    CorrelationPoint p = clean(x, y);
    p.clippedShare = 0.9f;
    return p;
}

CorrelationPoint still(float x, float y)
{
    CorrelationPoint p = clean(x, y);
    p.u = 0.001f;
    p.v = 0.f;
    return p;
}

CorrelationPoint poor(float x, float y)
{
    CorrelationPoint p = clean(x, y);
    p.zncc = 0.7f;
    return p;
}

CorrelationPoint flat(float x, float y)
{
    CorrelationPoint p = clean(x, y);
    p.conditioningMeasured = false;
    return p;
}

// Ten points: 3 clean, 2 clipped, 1 still, 1 poor and clipped, 1 flat,
// and 2 rejected. Measured 8; cautioned 5; by cause: clipped 3, still 1,
// poor 1, flat 1.
CorrelationResult lopsided()
{
    CorrelationResult result;
    result.points << clean(10, 10) << clean(20, 10) << clean(90, 70)
                  << clipped(30, 10) << clipped(80, 60)
                  << still(40, 20)
                  << [] { CorrelationPoint p = poor(50, 20); p.clippedShare = 0.75f; return p; }()
                  << flat(60, 60);
    for (float x : {70.f, 15.f}) {
        CorrelationPoint gone = clipped(x, 15);
        gone.zncc = 0.2f;
        gone.conditioningMeasured = false;
        gone.u = 0.f;
        gone.converged = false;
        result.points << gone;
    }
    return result;
}

}  // namespace

void TestCautions::a_clean_measured_point_carries_no_caution()
{
    QVERIFY(cautionsAt(clean(1, 1)).isEmpty());
}

void TestCautions::each_cause_is_recognised_on_its_own()
{
    QCOMPARE(cautionsAt(clipped(1, 1)), QVector<Caution>{Caution::MostlyClipped});
    QCOMPARE(cautionsAt(still(1, 1)), QVector<Caution>{Caution::BelowNoiseFloor});
    QCOMPARE(cautionsAt(poor(1, 1)), QVector<Caution>{Caution::PoorCorrelation});
    QCOMPARE(cautionsAt(flat(1, 1)), QVector<Caution>{Caution::ConditioningUnusable});

    // "More than half" means more than half, the same line the readout draws.
    CorrelationPoint half = clean(1, 1);
    half.clippedShare = 0.5f;
    QVERIFY(cautionsAt(half).isEmpty());
}

void TestCautions::a_rejected_point_carries_no_caution_however_it_looks()
{
    // It is a hole already. A caution is said about a measurement.
    QVERIFY(cautionsAt(lopsided().points.last()).isEmpty());
}

void TestCautions::a_point_with_two_cautions_is_one_point_under_two_causes()
{
    const CautionCount count = countCautions(lopsided());
    QCOMPARE(count.measured, 8);
    QCOMPARE(count.cautioned, 5);
    QCOMPARE(count.byCause, (QVector<int>{3, 1, 1, 1}));
    QVERIFY(!count.inView);
}

void TestCautions::a_view_counts_only_the_points_inside_it_edges_included()
{
    // x 20..60, y 10..20: holds clean(20,10) on its corner, clipped(30,10),
    // still(40,20) on its edge, and poor-and-clipped(50,20) on its corner.
    const CautionCount count = countCautions(lopsided(), QRectF(QPointF(20, 10), QPointF(60, 20)));
    QVERIFY(count.inView);
    QCOMPARE(count.measured, 4);
    QCOMPARE(count.cautioned, 3);
    QCOMPARE(count.byCause, (QVector<int>{2, 1, 1, 0}));
}

void TestCautions::the_summary_is_a_count_with_its_causes_and_never_a_confidence()
{
    const QString said = cautionSummary(countCautions(lopsided()));
    QVERIFY2(said.startsWith(QStringLiteral("Over the whole field: 3 of 8 measured points "
                                            "carry no caution; 5 do")),
             qPrintable(said));
    QVERIFY2(said.contains(cautionMark(Caution::MostlyClipped) + QStringLiteral(" 3 ")
                           + cautionName(Caution::MostlyClipped)),
             qPrintable(said));
    QVERIFY2(said.contains(QStringLiteral("not a confidence")), qPrintable(said));
    QVERIFY2(!said.contains(QLatin1Char('%')), "a percentage reads as a confidence");

    const QString inView =
        cautionSummary(countCautions(lopsided(), QRectF(QPointF(20, 10), QPointF(60, 20))));
    QVERIFY2(inView.startsWith(QStringLiteral("In view: 1 of 4")), qPrintable(inView));
    // A cause with nothing in view is not listed at zero.
    QVERIFY2(!inView.contains(cautionName(Caution::ConditioningUnusable)), qPrintable(inView));
}

void TestCautions::a_field_with_nothing_to_say_says_so()
{
    CorrelationResult result;
    result.points << clean(1, 1) << clean(2, 2);
    QCOMPARE(cautionSummary(countCautions(result)),
             QStringLiteral("Over the whole field: none of the 2 measured points carries a caution."));
}

QTEST_GUILESS_MAIN(TestCautions)
#include "test_cautions.moc"
