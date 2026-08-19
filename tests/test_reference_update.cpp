// Following a material point through a sequence when the reference moves.
//
// WHY THIS IS THE DELICATE PART. Measuring every frame against the original
// reference is simple and eventually stops working: the specimen deforms away
// from the picture it is being compared against and correlation collapses.
// Re-anchoring fixes that by measuring against a later frame instead -- and
// immediately raises the question this file exists to answer, which is what the
// numbers then MEAN. A displacement measured against frame 7 is not comparable
// to one measured against frame 0, and a series that silently mixes the two
// looks perfectly smooth right up until the re-anchor, where the whole field
// steps by however far the specimen had moved.
//
// Two rules keep it honest, and everything below tests one of them:
//
//   1. A frame's field is always REPORTED on the original reference's grid, in
//      displacement relative to the original reference, whatever frame it was
//      measured against. That is what makes the series comparable and what lets
//      one exported .vtu sit on top of another.
//   2. A point whose increment was not measured cannot advance, and after a
//      re-anchor its position in the new reference is unknown. It is lost, not
//      frozen: a frozen point reports its last displacement forever, which
//      reads as a region that stopped moving.
//
// NEGATIVE CHECK (2026-08-19): recorded against the cases below.

#include "core/Correlation.h"
#include "core/ReferenceUpdate.h"

#include <QTest>

namespace {

CorrelationPoint solved(int gridIndex, float x, float y, float u, float v,
                        float zncc)
{
    CorrelationPoint point;
    point.gridIndex = gridIndex;
    point.x = x;
    point.y = y;
    point.u = u;
    point.v = v;
    point.zncc = zncc;
    point.converged = true;
    return point;
}

CorrelationPoint rejected(int gridIndex, float x, float y)
{
    CorrelationPoint point;
    point.gridIndex = gridIndex;
    point.x = x;
    point.y = y;
    point.zncc = -3.f;   // a status code, as the engine reports failure
    point.converged = false;
    return point;
}

// A field of `count` points, all solved, all at the given correlation.
CorrelationResult fieldAt(float zncc, int count)
{
    CorrelationResult result;
    result.gridColumns = count;
    result.gridRows = 1;
    for (int i = 0; i < count; i++)
        result.points.append(solved(i, float(i), 0.f, 0.f, 0.f, zncc));
    result.converged = count;
    return result;
}

}  // namespace

class TestReferenceUpdate : public QObject
{
    Q_OBJECT

private slots:
    void a_point_starts_where_it_was_placed_with_nothing_banked();
    void the_tracked_position_is_always_the_original_plus_what_is_banked();
    void a_frames_total_is_what_is_banked_plus_what_was_just_measured();
    void a_frame_is_reported_on_the_original_grid_whatever_it_was_measured_against();
    void a_policy_that_is_off_never_re_anchors();
    void a_well_correlated_field_does_not_re_anchor();
    void a_field_that_has_lost_too_many_points_re_anchors();
    void losing_points_counts_against_the_reference();
    void a_few_points_that_can_never_be_measured_do_not_re_anchor_a_good_run();
    void a_point_that_was_not_measured_is_lost_rather_than_frozen();
    void a_lost_point_stays_lost_and_reports_nothing();
};

void TestReferenceUpdate::a_point_starts_where_it_was_placed_with_nothing_banked()
{
    const QVector<TrackedPoint> tracked = startTracking(fieldAt(0.99f, 3));

    QCOMPARE(tracked.size(), 3);
    for (int i = 0; i < tracked.size(); i++) {
        QCOMPARE(tracked.at(i).gridIndex, i);
        QCOMPARE(tracked.at(i).bankedU, 0.f);
        QCOMPARE(tracked.at(i).bankedV, 0.f);
        QVERIFY(!tracked.at(i).lost);
        QCOMPARE(trackedX(tracked.at(i)), tracked.at(i).x0);
    }
}

void TestReferenceUpdate::the_tracked_position_is_always_the_original_plus_what_is_banked()
{
    // The invariant the whole scheme rests on. Where a point sits in the
    // CURRENT reference is its original position plus everything banked so far,
    // because that is precisely what banking means. Held separately it would be
    // a second copy of the same fact, free to drift.
    TrackedPoint point;
    point.x0 = 100.f;
    point.y0 = 50.f;
    point.bankedU = 7.5f;
    point.bankedV = -2.25f;

    QCOMPARE(trackedX(point), 107.5f);
    QCOMPARE(trackedY(point), 47.75f);
}

void TestReferenceUpdate::a_frames_total_is_what_is_banked_plus_what_was_just_measured()
{
    QVector<TrackedPoint> tracked = startTracking(fieldAt(0.99f, 2));
    tracked[0].bankedU = 10.f;
    tracked[0].bankedV = 1.f;
    tracked[1].bankedU = 10.f;
    tracked[1].bankedV = 1.f;

    // Measured against the CURRENT reference, so these are increments.
    CorrelationResult increment;
    increment.gridColumns = 2;
    increment.gridRows = 1;
    increment.points.append(solved(0, 110.f, 1.f, 0.5f, 0.25f, 0.98f));
    increment.points.append(solved(1, 111.f, 1.f, 1.5f, -0.25f, 0.97f));
    increment.converged = 2;

    const CorrelationResult total = composeTotalField(tracked, increment);

    QCOMPARE(total.points.size(), 2);
    QCOMPARE(total.points.at(0).u, 10.5f);
    QCOMPARE(total.points.at(0).v, 1.25f);
    QCOMPARE(total.points.at(1).u, 11.5f);
    QCOMPARE(total.points.at(1).v, 0.75f);
}

void TestReferenceUpdate::a_frame_is_reported_on_the_original_grid_whatever_it_was_measured_against()
{
    // The measurement happens at the tracked position, which has moved. The
    // REPORT belongs at the original position, or the field drifts across the
    // picture as the specimen does and no two frames can be laid over one
    // another -- or over the reference photograph they are supposed to explain.
    QVector<TrackedPoint> tracked = startTracking(fieldAt(0.99f, 1));
    tracked[0].bankedU = 40.f;
    tracked[0].bankedV = 5.f;
    const float originalX = tracked.at(0).x0;
    const float originalY = tracked.at(0).y0;

    CorrelationResult increment;
    increment.gridColumns = 1;
    increment.gridRows = 1;
    increment.points.append(solved(0, 40.f, 5.f, 2.f, 0.f, 0.95f));
    increment.converged = 1;

    const CorrelationResult total = composeTotalField(tracked, increment);

    QCOMPARE(total.points.at(0).x, originalX);
    QCOMPARE(total.points.at(0).y, originalY);
    QCOMPARE(total.points.at(0).u, 42.f);

    // And the grid it is laid out on is the original one, so the field of every
    // frame in the sequence has the same shape.
    QCOMPARE(total.gridColumns, increment.gridColumns);
    QCOMPARE(total.gridRows, increment.gridRows);
    QCOMPARE(total.points.at(0).gridIndex, 0);
}

void TestReferenceUpdate::a_policy_that_is_off_never_re_anchors()
{
    // Kept apart from the case below on purpose. Written as one, with the
    // default policy, that case passed for the wrong reason: the default is
    // OFF, so nothing re-anchors and the field's own quality was never
    // consulted at all. Two cases, so neither can stand in for the other.
    ReferenceUpdatePolicy off;
    off.enabled = false;

    CorrelationResult hopeless;
    hopeless.gridColumns = 10;
    hopeless.gridRows = 1;
    for (int i = 0; i < 10; i++)
        hopeless.points.append(solved(i, float(i), 0.f, 0.f, 0.f, 0.1f));
    hopeless.converged = 10;

    QVERIFY2(!fieldNeedsReanchor(hopeless, off),
             "a field correlating at 0.1 re-anchored under a policy that is off");
}

void TestReferenceUpdate::a_well_correlated_field_does_not_re_anchor()
{
    // Re-anchoring is not free: it resets every point's baseline and loses the
    // ones that were not measured. It has to be earned.
    ReferenceUpdatePolicy policy;
    policy.enabled = true;

    QVERIFY(!fieldNeedsReanchor(fieldAt(0.99f, 100), policy));
    QVERIFY(!fieldNeedsReanchor(fieldAt(0.91f, 100), policy));
}

void TestReferenceUpdate::a_field_that_has_lost_too_many_points_re_anchors()
{
    // ncorr's rule, in OpenCorr's higher-is-better convention: the reference
    // holds while at least `percentile` of the field still clears the
    // correlation threshold. It is a whole-field decision on purpose -- one
    // unlucky point should not re-anchor a sequence.
    ReferenceUpdatePolicy policy;
    policy.enabled = true;
    policy.znccThreshold = 0.9;
    policy.percentile = 0.75;

    CorrelationResult mostlyGood;
    mostlyGood.gridColumns = 100;
    mostlyGood.gridRows = 1;
    for (int i = 0; i < 100; i++)
        mostlyGood.points.append(solved(i, float(i), 0.f, 0.f, 0.f,
                                        i < 80 ? 0.95f : 0.5f));
    mostlyGood.converged = 100;
    QVERIFY2(!fieldNeedsReanchor(mostlyGood, policy),
             "80 of 100 points still tracking is above the 75% the policy asks");

    CorrelationResult mostlyBad;
    mostlyBad.gridColumns = 100;
    mostlyBad.gridRows = 1;
    for (int i = 0; i < 100; i++)
        mostlyBad.points.append(solved(i, float(i), 0.f, 0.f, 0.f,
                                       i < 60 ? 0.95f : 0.5f));
    mostlyBad.converged = 100;
    QVERIFY2(fieldNeedsReanchor(mostlyBad, policy),
             "60 of 100 is below 75% and the reference should have re-anchored");
}

void TestReferenceUpdate::losing_points_counts_against_the_reference()
{
    // ⚑ THIS CASE REPLACES ONE THAT ASSERTED THE OPPOSITE, and the story is
    // worth keeping. The original rule counted only points that SOLVED, on the
    // reasoning that a rejected point holds a status code rather than a poor
    // correlation and so has nothing to say about the reference. A test said so
    // in as many words, and it passed.
    //
    // Then a synthetic tension sequence was run through the application: across
    // five load steps, points solved fell from 97% to 47% of the field, and the
    // reference never re-anchored once. The half of the field that survived was
    // still correlating beautifully, and under that rule it was the only half
    // allowed to vote. The feature was inert on exactly the sequences it exists
    // for.
    //
    // A point lost to decorrelation is the strongest evidence there is that the
    // reference has gone stale. It votes.
    ReferenceUpdatePolicy policy;
    policy.enabled = true;
    policy.znccThreshold = 0.9;
    policy.percentile = 0.75;

    CorrelationResult halfLost;
    halfLost.gridColumns = 100;
    halfLost.gridRows = 1;
    for (int i = 0; i < 100; i++) {
        if (i < 50)
            halfLost.points.append(solved(i, float(i), 0.f, 0.f, 0.f, 0.99f));
        else
            halfLost.points.append(rejected(i, float(i), 0.f));
    }
    halfLost.converged = 50;

    QVERIFY2(fieldNeedsReanchor(halfLost, policy),
             "half the field was lost and the reference did not re-anchor");
}

void TestReferenceUpdate::a_few_points_that_can_never_be_measured_do_not_re_anchor_a_good_run()
{
    // The other side of it. Some points fail for reasons that have nothing to
    // do with the reference going stale -- a subset overhanging the edge of the
    // image never solves at any load. A handful of those must not re-anchor a
    // sequence that is otherwise tracking perfectly, which is what the
    // threshold is for: it asks for MOST of the field, not all of it.
    ReferenceUpdatePolicy policy;
    policy.enabled = true;
    policy.znccThreshold = 0.9;
    policy.percentile = 0.75;

    CorrelationResult mostlyFine;
    mostlyFine.gridColumns = 100;
    mostlyFine.gridRows = 1;
    for (int i = 0; i < 100; i++) {
        if (i < 95)
            mostlyFine.points.append(solved(i, float(i), 0.f, 0.f, 0.f, 0.98f));
        else
            mostlyFine.points.append(rejected(i, float(i), 0.f));
    }
    mostlyFine.converged = 95;

    QVERIFY2(!fieldNeedsReanchor(mostlyFine, policy),
             "5 unmeasurable points out of 100 re-anchored a healthy run");

    // ⚑ And the degenerate case, which I got wrong once already by writing the
    // assertion to match the implementation instead of thinking about it.
    // Under a rule that counts losses, a frame where NOTHING correlated is the
    // most emphatic re-anchor signal there could be -- and acting on it is
    // ruinous. Re-anchoring banks the increment just measured; with no
    // increment anywhere it banks nothing and marks every point lost, so the
    // remainder of the sequence measures nothing at all. A stale reference that
    // still fails can be recovered from on a later frame. A field with no
    // tracked points left cannot.
    CorrelationResult nothing;
    nothing.gridColumns = 4;
    nothing.gridRows = 1;
    for (int i = 0; i < 4; i++)
        nothing.points.append(rejected(i, float(i), 0.f));
    QVERIFY2(!fieldNeedsReanchor(nothing, policy),
             "a frame that correlated with nothing re-anchored onto itself");
}

void TestReferenceUpdate::a_point_that_was_not_measured_is_lost_rather_than_frozen()
{
    // ⚑ The honesty rule. Banking advances a point by the increment just
    // measured; a point with no increment cannot advance, so after the
    // re-anchor nobody knows where it sits in the new reference. Left in place
    // it would be measured at the wrong pixels from then on, and would report
    // its last displacement forever -- a region that appears to have stopped
    // moving, which is a measurement, not an absence.
    QVector<TrackedPoint> tracked = startTracking(fieldAt(0.99f, 3));

    CorrelationResult increment;
    increment.gridColumns = 3;
    increment.gridRows = 1;
    increment.points.append(solved(0, 0.f, 0.f, 2.f, 0.f, 0.95f));
    increment.points.append(rejected(1, 1.f, 0.f));
    increment.points.append(solved(2, 2.f, 0.f, 3.f, 0.f, 0.95f));
    increment.converged = 2;

    bankIncrement(tracked, increment);

    QCOMPARE(tracked.at(0).bankedU, 2.f);
    QVERIFY(!tracked.at(0).lost);

    QVERIFY2(tracked.at(1).lost, "an unmeasured point survived a re-anchor");
    QCOMPARE(tracked.at(2).bankedU, 3.f);
}

void TestReferenceUpdate::a_lost_point_stays_lost_and_reports_nothing()
{
    QVector<TrackedPoint> tracked = startTracking(fieldAt(0.99f, 2));
    tracked[1].lost = true;

    CorrelationResult increment;
    increment.gridColumns = 2;
    increment.gridRows = 1;
    increment.points.append(solved(0, 0.f, 0.f, 1.f, 0.f, 0.95f));
    increment.points.append(solved(1, 1.f, 0.f, 9.f, 0.f, 0.95f));
    increment.converged = 2;

    const CorrelationResult total = composeTotalField(tracked, increment);

    // Whatever the solver returned for a point whose position is unknown is a
    // measurement of the wrong pixels, and must not reach the field.
    QCOMPARE(total.points.at(0).u, 1.f);
    QVERIFY(total.points.at(0).converged);
    QVERIFY2(!total.points.at(1).converged,
             "a lost point was reported as a measurement");

    // And it does not recover on a later frame just because that frame's solve
    // happened to converge somewhere.
    bankIncrement(tracked, increment);
    QVERIFY(tracked.at(1).lost);
}

QTEST_MAIN(TestReferenceUpdate)
#include "test_reference_update.moc"
