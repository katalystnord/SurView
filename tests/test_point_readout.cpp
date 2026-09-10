// What one measured point has to say about itself, when somebody points at it.
//
// WHY THIS EXISTS. Until now the only way to read a measured field was its
// COLOUR. A reader could see that one region is redder than another and had no
// way to ask what any point actually measured -- and no way at all to reach the
// two reliability channels as numbers, which is where the answer to "is this
// movement real" lives.
//
// The arithmetic that matters is all here rather than in the widget: which
// point owns the position under the pointer, and what that point is entitled
// to claim. Both are easy to get wrong in ways that look right on screen.
//
// ⚑ The case this file exists for is
// `a_displacement_smaller_than_its_own_noise_floor_is_flagged`. A point can
// converge beautifully, report a displacement, and have measured nothing: if
// the movement is smaller than the noise floor of its own subset, the number is
// indistinguishable from image noise. It is drawn in a perfectly ordinary
// colour, and nothing anywhere on screen says so.
//
// NEGATIVE CHECK (2026-08-31): nine deliberate breaks, each reverted after.
// Every one of them turned exactly the case named for it red, and nothing else:
//   - pick radius ignored, so the nearest point wins at any distance
//   - the below-floor guard drops its converged test
//   - the below-floor comparison inverted so it never fires
//   - a rejected point prints a displacement of 0.0 px
//   - an unfitted strain prints the three zeros POI2D::clear() left
//   - an unusable conditioning probe prints the raw -1
//   - strain shown even when the run never asked for it
//   - the noise floor arrives without its note
//   - a position with no point returns no lines at all
//
// ⚑ That last break found a real gap rather than confirming coverage. The
// header of core/PointReadout.h asserted that a readout for no point still says
// so, and NOTHING enforced it: the break went green. This project's rule is
// that a comment may not assert what the design requires unless a test of that
// name exists, so `a_position_with_no_point_says_so_rather_than_going_blank`
// was written afterwards, and the break was re-run to watch it go red.

#include "core/Correlation.h"
#include "core/FieldLayout.h"
#include "core/PointReadout.h"

#include <QTest>

namespace
{

// A 3x3 grid at origin (100, 200), 5 px apart, every point solved and
// unremarkable. Cases below spoil the one point they are about, so that what
// the test asserts is the difference rather than the whole fixture.
CorrelationResult plainResult()
{
    CorrelationResult result;
    result.gridColumns = 3;
    result.gridRows = 3;
    result.originX = 100.f;
    result.originY = 200.f;
    result.step = 5;
    result.strainRequested = true;

    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
            CorrelationPoint point;
            point.gridIndex = row * 3 + column;
            point.x = result.originX + float(column * result.step);
            point.y = result.originY + float(row * result.step);
            point.u = 2.5f;
            point.v = -0.5f;
            point.zncc = 0.98f;
            point.converged = true;
            point.exx = 1.9e-3f;
            point.eyy = -6.0e-4f;
            point.exy = 2.0e-5f;
            point.strainFitted = true;
            point.noiseFloor = 0.0034f;
            point.noiseFloorMeasured = true;
            point.conditioning = 0.27f;
            point.conditioningMeasured = true;
            result.points.append(point);
            result.converged++;
            result.strainFitted++;
        }
    }
    result.noiseFloorMeasured = result.converged;
    return result;
}

// The whole readout as one string, for asserting that something is said
// SOMEWHERE without pinning which line says it.
QString spoken(const PointReadout &readout)
{
    QStringList parts;
    for (const ReadoutLine &line : readout.lines)
        parts << line.label << line.value << line.note;
    return parts.join(QLatin1Char('\n'));
}

bool anyLineWarns(const PointReadout &readout)
{
    for (const ReadoutLine &line : readout.lines)
        if (line.warning)
            return true;
    return false;
}

// Which rows warn, by label. `anyLineWarns` above answers whether the readout
// warns at all, which is a much weaker question: a readout that marked EVERY
// row would satisfy it just as well as one that marked the right row.
QStringList warningLabels(const PointReadout &readout)
{
    QStringList labels;
    for (const ReadoutLine &line : readout.lines)
        if (line.warning)
            labels << line.label;
    return labels;
}

}  // namespace

class TestPointReadout : public QObject
{
    Q_OBJECT

private slots:
    // --- which point is being asked about ----------------------------------
    void a_position_on_a_measured_point_finds_that_point();
    void a_position_between_points_finds_the_nearer_one();
    void a_position_off_the_measured_field_finds_nothing();
    void a_result_with_no_points_finds_nothing_rather_than_crashing();
    void a_position_with_no_point_says_so_rather_than_going_blank();

    // --- what the point is entitled to claim -------------------------------
    void a_solved_point_reports_the_displacement_it_measured();
    void a_rejected_point_reports_no_displacement_rather_than_zero();
    void a_rejected_point_names_the_reason_it_was_rejected();

    // --- the reliability half, which is why this exists --------------------
    void a_displacement_smaller_than_its_own_noise_floor_is_flagged();
    void a_displacement_larger_than_its_own_noise_floor_is_not_flagged();
    void a_rejected_point_is_not_flagged_against_a_displacement_it_never_measured();
    void the_noise_floor_is_reported_with_what_it_cannot_see();
    void a_conditioning_the_probe_could_not_establish_is_a_warning_not_a_blank();

    // --- strain, which is fitted rather than measured ----------------------
    void strain_is_absent_when_the_run_never_asked_for_it();
    void an_unfitted_strain_says_the_fit_declined_rather_than_reading_zero();

    // --- the shape of the thing itself -------------------------------------
    void every_line_carries_a_label_and_something_to_read();
    void a_good_point_reports_itself_without_a_single_warning();
    void only_the_row_that_cannot_carry_a_bare_number_warns();
    void a_correlation_exactly_at_the_strain_floor_is_not_excluded_by_it();
    void a_click_exactly_at_the_reach_of_the_grid_still_finds_its_point();
    void the_first_point_in_the_grid_reads_out_like_any_other();
    void a_point_repaired_on_the_second_pass_reports_it_without_apology();
    void a_strain_the_fit_declined_is_the_row_that_warns();
    void a_displacement_exactly_at_its_own_noise_floor_is_marked();
    void the_magnitude_row_is_the_length_of_the_displacement_it_follows();
    void two_points_the_same_distance_away_resolve_the_same_way_every_time();
    void a_displacement_is_compared_to_its_floor_as_a_length_not_as_one_axis();
};

void TestPointReadout::a_position_on_a_measured_point_finds_that_point()
{
    const CorrelationResult result = plainResult();

    // The middle point of the 3x3 grid, at its exact position.
    QCOMPARE(pointNearestTo(result, 105.f, 205.f), 4);
}

void TestPointReadout::a_position_between_points_finds_the_nearer_one()
{
    const CorrelationResult result = plainResult();

    // Two pixels right of the middle point, three short of its neighbour.
    QCOMPARE(pointNearestTo(result, 107.f, 205.f), 4);
    // And a pixel past halfway, where the neighbour takes over.
    QCOMPARE(pointNearestTo(result, 108.f, 205.f), 5);
}

void TestPointReadout::a_position_off_the_measured_field_finds_nothing()
{
    const CorrelationResult result = plainResult();

    // Well outside the grid. Reporting the nearest point regardless would
    // attribute a measurement to a place nothing was measured, which is the
    // same error as filling an unmeasured cell with zero.
    QCOMPARE(pointNearestTo(result, 400.f, 205.f), -1);
    QCOMPARE(pointNearestTo(result, 105.f, 40.f), -1);
}

void TestPointReadout::a_result_with_no_points_finds_nothing_rather_than_crashing()
{
    const CorrelationResult empty;
    QCOMPARE(pointNearestTo(empty, 0.f, 0.f), -1);
}

void TestPointReadout::a_position_with_no_point_says_so_rather_than_going_blank()
{
    const CorrelationResult result = plainResult();
    const PointReadout readout = pointReadout(result, -1);

    QVERIFY(!readout.measured);

    // A panel that empties itself reads as broken, and a reader cannot tell an
    // absence of measurement from an absence of software. It has to answer.
    QVERIFY2(!readout.lines.isEmpty(), "a readout for no point must still say something");
    QVERIFY2(spoken(readout).contains(QStringLiteral("no point measured here")),
             qPrintable(spoken(readout)));
}

void TestPointReadout::a_solved_point_reports_the_displacement_it_measured()
{
    const CorrelationResult result = plainResult();
    const PointReadout readout = pointReadout(result, 4);

    QVERIFY(readout.measured);
    QVERIFY(readout.solved);

    const QString text = spoken(readout);
    // The position it was measured at, and both components of what it found.
    QVERIFY2(text.contains(QStringLiteral("105")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("205")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("2.5")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("-0.5")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("0.98")), qPrintable(text));
}

void TestPointReadout::a_rejected_point_reports_no_displacement_rather_than_zero()
{
    CorrelationResult result = plainResult();
    CorrelationPoint &point = result.points[4];
    point.converged = false;
    point.zncc = -3.f;
    point.u = 0.f;
    point.v = 0.f;
    point.strainFitted = false;
    point.noiseFloorMeasured = false;
    point.conditioningMeasured = false;

    const PointReadout readout = pointReadout(result, 4);

    QVERIFY(readout.measured);
    QVERIFY(!readout.solved);

    // The same rule the export and the colour map already keep: a rejected
    // point is not a displacement of zero. A readout printing "0.0 px" here
    // would be the one place in the application that says otherwise, and it
    // would be believed, because it is a number next to a label.
    const QString text = spoken(readout);
    QVERIFY2(!text.contains(QStringLiteral("0.0 px")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("not measured")), qPrintable(text));
}

void TestPointReadout::a_rejected_point_names_the_reason_it_was_rejected()
{
    CorrelationResult result = plainResult();
    CorrelationPoint &point = result.points[4];
    point.converged = false;
    point.zncc = -3.f;
    point.failureReason = QStringLiteral("subset outside the image");

    const PointReadout readout = pointReadout(result, 4);

    // The run report already counts failures by reason. This is the same fact
    // at the one place a reader is actually asking about it: the point they
    // pointed at.
    QVERIFY2(spoken(readout).contains(QStringLiteral("subset outside the image")),
             qPrintable(spoken(readout)));
}

void TestPointReadout::a_displacement_smaller_than_its_own_noise_floor_is_flagged()
{
    CorrelationResult result = plainResult();
    CorrelationPoint &point = result.points[4];
    // Moved by about two thousandths of a pixel, on a subset that could not
    // resolve better than three thousandths.
    point.u = 0.0015f;
    point.v = 0.0005f;
    point.noiseFloor = 0.0034f;
    point.noiseFloorMeasured = true;

    QVERIFY(displacementIsBelowNoiseFloor(point));

    const PointReadout readout = pointReadout(result, 4);
    QVERIFY2(anyLineWarns(readout), "a movement below its own noise floor must warn");
    QVERIFY2(spoken(readout).contains(QStringLiteral("noise floor")),
             qPrintable(spoken(readout)));
}

void TestPointReadout::a_displacement_larger_than_its_own_noise_floor_is_not_flagged()
{
    const CorrelationResult result = plainResult();
    // The fixture moved 2.5 px against a floor of 0.0034 px.
    QVERIFY(!displacementIsBelowNoiseFloor(result.points[4]));
}

void TestPointReadout::a_rejected_point_is_not_flagged_against_a_displacement_it_never_measured()
{
    CorrelationPoint point;
    point.converged = false;
    point.u = 0.f;
    point.v = 0.f;
    point.noiseFloor = 0.0034f;
    point.noiseFloorMeasured = true;

    // Its displacement is zero only because nothing was written there. Reading
    // that as "below the noise floor" would turn an absence into a finding.
    QVERIFY(!displacementIsBelowNoiseFloor(point));
}

void TestPointReadout::the_noise_floor_is_reported_with_what_it_cannot_see()
{
    const CorrelationResult result = plainResult();
    const QString text = spoken(pointReadout(result, 4));

    // Every field channel already carries a one-sentence note saying what it is
    // NOT. A per-point readout is the same number at closer range and needs it
    // more, not less: a single value next to a label reads as definitive.
    QVERIFY2(text.contains(fieldChannelNote(FieldChannel::NoiseFloor)),
             qPrintable(text));
}

void TestPointReadout::a_conditioning_the_probe_could_not_establish_is_a_warning_not_a_blank()
{
    CorrelationResult result = plainResult();
    CorrelationPoint &point = result.points[4];
    point.conditioningMeasured = false;

    const PointReadout readout = pointReadout(result, 4);
    const QString text = spoken(readout);

    // At a CONVERGED point the engine's -1 cannot mean "not computed" -- that
    // fires only for a failed or out-of-bounds point. It can only mean the cost
    // was too flat to probe, which is the strongest caution the metric has. A
    // blank would read as a detail nobody bothered to fill in.
    QVERIFY2(!text.contains(QStringLiteral("-1")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("too flat")), qPrintable(text));
    QVERIFY2(anyLineWarns(readout), "an unusable conditioning probe must warn");
}

void TestPointReadout::strain_is_absent_when_the_run_never_asked_for_it()
{
    CorrelationResult result = plainResult();
    result.strainRequested = false;
    result.strainFitted = 0;
    for (CorrelationPoint &point : result.points) {
        point.strainFitted = false;
        point.exx = 0.f;
        point.eyy = 0.f;
        point.exy = 0.f;
    }

    // Absent, not present-and-empty. The same rule the .vtu export keeps: an
    // empty strain row reads as a measurement that failed, when in fact none
    // was ever attempted.
    const QString text = spoken(pointReadout(result, 4));
    QVERIFY2(!text.contains(QStringLiteral("exx")), qPrintable(text));
}

void TestPointReadout::an_unfitted_strain_says_the_fit_declined_rather_than_reading_zero()
{
    CorrelationResult result = plainResult();
    CorrelationPoint &point = result.points[4];
    point.strainFitted = false;
    point.exx = 0.f;
    point.eyy = 0.f;
    point.exy = 0.f;

    const PointReadout readout = pointReadout(result, 4);
    const QString text = spoken(readout);

    // Strain was asked for, so the row belongs. What it must not do is print
    // the three zeros POI2D::clear() left behind as though they were a fit.
    QVERIFY2(text.contains(QStringLiteral("exx")), qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("0.000")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("not fitted")), qPrintable(text));
}

void TestPointReadout::every_line_carries_a_label_and_something_to_read()
{
    const CorrelationResult result = plainResult();
    const PointReadout readout = pointReadout(result, 4);

    QVERIFY(!readout.lines.isEmpty());
    for (const ReadoutLine &line : readout.lines) {
        QVERIFY2(!line.label.trimmed().isEmpty(), qPrintable(line.value));
        QVERIFY2(!line.value.trimmed().isEmpty(), qPrintable(line.label));
    }
}


void TestPointReadout::a_good_point_reports_itself_without_a_single_warning()
{
    // ⚑ The mutation sweep marked one row after another as a warning and the
    // suite noticed none of it: twelve survivors, every one a `false` becoming
    // `true` on an ordinary row. A readout that warns about everything warns
    // about nothing -- the mark is what tells a reader which number cannot
    // carry the weight it looks like it carries, and it only means that while
    // most numbers do not have it.
    const CorrelationResult result = plainResult();
    const PointReadout readout = pointReadout(result, 4);

    QVERIFY2(!anyLineWarns(readout),
             qPrintable(QStringLiteral("a point that measured cleanly warns about: %1")
                            .arg(warningLabels(readout).join(QStringLiteral(", ")))));
}

void TestPointReadout::only_the_row_that_cannot_carry_a_bare_number_warns()
{
    // A rejected point: the displacement row is the one that must warn, and it
    // is the only one. The rows around it are still ordinary facts -- where
    // the point sits, what the run asked for -- and marking those would bury
    // the one that matters.
    CorrelationResult result = plainResult();
    result.points[4].converged = false;
    result.points[4].failureReason = QStringLiteral("correlation too low");
    result.converged--;

    const PointReadout rejected = pointReadout(result, 4);
    QCOMPARE(warningLabels(rejected).size(), 1);
    QVERIFY2(warningLabels(rejected).first().contains(QStringLiteral("Displacement")),
             qPrintable(QStringLiteral("the one warning row is: %1")
                            .arg(warningLabels(rejected).join(QStringLiteral(", ")))));

    // And a point whose displacement is under its own noise floor warns on the
    // floor's row instead, without the displacement row acquiring one: the
    // measurement happened, it is the confidence in it that is qualified.
    CorrelationResult quiet = plainResult();
    quiet.points[4].u = 0.001f;
    quiet.points[4].v = 0.0f;
    const PointReadout small = pointReadout(quiet, 4);
    QVERIFY2(anyLineWarns(small), "a displacement under its own noise floor is marked");
    QCOMPARE(warningLabels(small).size(), 1);
}

void TestPointReadout::a_correlation_exactly_at_the_strain_floor_is_not_excluded_by_it()
{
    // The engine excludes points correlating BELOW 0.9 from every strain fit,
    // so a point sitting exactly on 0.9 is included and must not be told it
    // was dropped. The boundary is the only place that sentence can be wrong,
    // and two mutants sat on it.
    CorrelationResult atFloor = plainResult();
    atFloor.points[4].zncc = kStrainFitCorrelationFloor;
    QVERIFY2(!spoken(pointReadout(atFloor, 4)).contains(QStringLiteral("excluded from every strain fit")),
             "a point exactly at the correlation floor is not excluded by it");

    CorrelationResult below = plainResult();
    below.points[4].zncc = kStrainFitCorrelationFloor - 0.01f;
    QVERIFY2(spoken(pointReadout(below, 4)).contains(QStringLiteral("excluded from every strain fit")),
             "and one below it is told so");
}

void TestPointReadout::two_points_the_same_distance_away_resolve_the_same_way_every_time()
{
    // The same rule as the region's corner grab, one panel along: a position
    // exactly between two grid points belongs to the FIRST of them, so the
    // readout does not flicker between two points as the pointer sits still.
    const CorrelationResult result = plainResult();

    const float betweenX = result.originX + float(result.step) / 2.0f;
    const float y = result.originY;

    // Exactly half a step from each of the first two points.
    const int chosen = pointNearestTo(result, betweenX, y);
    QCOMPARE(chosen, 0);

    // Asked again, the same answer: nothing here depends on which point was
    // visited first or on floating-point luck.
    QCOMPARE(pointNearestTo(result, betweenX, y), chosen);
}


void TestPointReadout::a_displacement_is_compared_to_its_floor_as_a_length_not_as_one_axis()
{
    // ⚑ The comparison is against the LENGTH of the displacement vector, and
    // the fixtures that were here could not tell that from any other
    // combination of u and v: one of them was zero, or both were far above the
    // floor. A point that moved 0.003 px in each direction moved 0.0042 in
    // total, which is above a 0.0034 floor -- while u squared MINUS v squared
    // is zero, and would report the strongest possible finding about a point
    // that is doing nothing of the kind.
    CorrelationResult result = plainResult();
    result.points[4].u = 0.003f;
    result.points[4].v = 0.003f;
    result.points[4].noiseFloor = 0.0034f;

    QVERIFY2(!anyLineWarns(pointReadout(result, 4)),
             "a displacement whose LENGTH clears its noise floor is not marked as under it");

    // And one whose length does not clear it is marked, so the case cannot
    // pass by never marking anything.
    result.points[4].noiseFloor = 0.005f;
    QVERIFY2(anyLineWarns(pointReadout(result, 4)),
             "and one whose length falls under it is");
}

void TestPointReadout::a_click_exactly_at_the_reach_of_the_grid_still_finds_its_point()
{
    // The pick radius is one grid step, so every position in the field belongs
    // to some point and a click between two of them goes to the nearer. At
    // EXACTLY one step away the point is still within reach: narrowed to a
    // strict comparison, a click that lands precisely on the limit reports
    // "no point measured here" while a click one thousandth nearer reads out
    // a full measurement. It is the boundary, and it is where a reader clicking
    // at a grid line lands.
    const CorrelationResult result = plainResult();
    const float reach = pointPickRadius(result);
    QCOMPARE(reach, float(result.step));

    // Straight out from the last point in x, exactly one step.
    const int found = pointNearestTo(result, result.originX + 2 * result.step + reach,
                                     result.originY);
    QVERIFY2(found >= 0, "a click exactly at the pick radius found no point");
    QCOMPARE(found, 2);

    // And a hair beyond it finds nothing, or the radius is not a radius.
    QVERIFY2(pointNearestTo(result, result.originX + 2 * result.step + reach + 0.5f,
                            result.originY) < 0,
             "a click beyond the pick radius found a point anyway");
}

void TestPointReadout::the_first_point_in_the_grid_reads_out_like_any_other()
{
    // ⚑ The index check reads `index < 0 || index >= size`, and the cases here
    // ask about the middle of the grid and about indices past the end. Neither
    // can see the lower bound tightening to `index <= 0`, which refuses point
    // ZERO: the top-left corner of every field would report "no point measured
    // here" while every other point read out normally. The same shape of defect
    // as a region whose first corner cannot be dragged.
    const CorrelationResult result = plainResult();
    const PointReadout first = pointReadout(result, 0);

    QVERIFY2(first.measured,
             "the first point in the grid reported nothing measured there");
    QVERIFY2(!anyLineWarns(first),
             qPrintable(warningLabels(first).join(QStringLiteral(", "))));
    QVERIFY2(spoken(first).contains(QStringLiteral("2.5")),
             qPrintable(spoken(first)));
}

void TestPointReadout::a_point_repaired_on_the_second_pass_reports_it_without_apology()
{
    // ⚑ A RECOVERED POINT IS A REAL MEASUREMENT. It reached its answer by a
    // different route and the readout says so, as provenance rather than as a
    // caveat - the project's own posture: permissive about what is attempted,
    // strict about what is claimed, and a mark that is not an apology.
    //
    // The fixture had no repaired point in it, so the row that says all this
    // was never built by any case, and the flag saying "this is not a warning"
    // could be flipped with nothing going red. A readout that warns about
    // everything warns about nothing.
    CorrelationResult result = plainResult();
    result.points[4].recovered = true;

    const PointReadout readout = pointReadout(result, 4);
    QVERIFY(readout.measured);
    QVERIFY2(spoken(readout).contains(QStringLiteral("second pass")),
             qPrintable(spoken(readout)));
    QVERIFY2(!anyLineWarns(readout),
             qPrintable(QStringLiteral("a repaired point warns about: %1")
                            .arg(warningLabels(readout).join(QStringLiteral(", ")))));
}

void TestPointReadout::a_strain_the_fit_declined_is_the_row_that_warns()
{
    // The other direction, and the one the marks exist for. "Not fitted here"
    // is the row a reader must not read as a strain of zero, so it carries the
    // mark; unmarked, it is one more grey row among the facts.
    CorrelationResult result = plainResult();
    result.points[4].strainFitted = false;

    const PointReadout readout = pointReadout(result, 4);
    const QStringList warned = warningLabels(readout);
    QCOMPARE(warned.size(), 1);
    QVERIFY2(warned.first().contains(QStringLiteral("Strain")),
             qPrintable(warned.join(QStringLiteral(", "))));

    // A point with no noise floor of its own says so and does NOT warn: the row
    // carries no number, and a row with no number cannot mislead anyone about
    // how much weight it will carry.
    CorrelationResult noFloor = plainResult();
    noFloor.points[4].noiseFloorMeasured = false;
    QVERIFY2(!anyLineWarns(pointReadout(noFloor, 4)),
             qPrintable(warningLabels(pointReadout(noFloor, 4))
                            .join(QStringLiteral(", "))));

    // And a position with no point at all is an absence, not a fault.
    QVERIFY2(!anyLineWarns(pointReadout(result, 99)),
             "empty space warns as though a measurement had gone wrong");
}

void TestPointReadout::a_displacement_exactly_at_its_own_noise_floor_is_marked()
{
    // The boundary of "not distinguishable from image noise", which the case
    // above steps over: it uses a displacement well under the floor, so a rule
    // reading "below" and one reading "at or below" are the same rule to it.
    // A displacement exactly equal to the floor is not distinguishable from it
    // either, and this is the only place that sentence can be wrong.
    CorrelationResult result = plainResult();
    result.points[4].u = result.points[4].noiseFloor;
    result.points[4].v = 0.f;

    const PointReadout readout = pointReadout(result, 4);
    QCOMPARE(warningLabels(readout).size(), 1);
    QVERIFY2(warningLabels(readout).first().contains(QStringLiteral("Noise floor")),
             qPrintable(warningLabels(readout).join(QStringLiteral(", "))));

    // And a displacement a shade above it is a measurement like any other.
    CorrelationResult above = plainResult();
    above.points[4].u = above.points[4].noiseFloor * 1.5f;
    above.points[4].v = 0.f;
    QVERIFY2(!anyLineWarns(pointReadout(above, 4)),
             qPrintable(warningLabels(pointReadout(above, 4))
                            .join(QStringLiteral(", "))));
}

void TestPointReadout::the_magnitude_row_is_the_length_of_the_displacement_it_follows()
{
    // ⚑ THE NUMBER ON THE PANEL, not the one the floor comparison uses. Those
    // are two separate expressions, and only the second had a case: a
    // difference of squares in place of a sum reads as an ordinary number
    // whenever the point moves further in x than in y, and as "nan px" the
    // moment it does not.
    //
    // The fixture moves 2.5 px in x and 0.5 in y, where a sum of squares and a
    // difference of them are 2.55 and 2.45 -- both entirely plausible on a
    // panel. A 3, 4, 5 triangle separates them, and puts the mutant on the
    // wrong side of a square root.
    CorrelationResult result = plainResult();
    result.points[4].u = 3.f;
    result.points[4].v = -4.f;

    const PointReadout readout = pointReadout(result, 4);

    bool found = false;
    for (const ReadoutLine &line : readout.lines) {
        if (!line.label.contains(QStringLiteral("Magnitude")))
            continue;
        found = true;
        QVERIFY2(line.value.startsWith(QStringLiteral("5")),
                 qPrintable(QStringLiteral("a point that moved 3 px by 4 px "
                                           "reports a magnitude of %1")
                                .arg(line.value)));
        QVERIFY2(!line.value.contains(QStringLiteral("nan"), Qt::CaseInsensitive),
                 qPrintable(line.value));
    }
    QVERIFY2(found, "the readout has no magnitude row at all");
}

QTEST_MAIN(TestPointReadout)
#include "test_point_readout.moc"
