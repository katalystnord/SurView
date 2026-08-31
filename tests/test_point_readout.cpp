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

QTEST_MAIN(TestPointReadout)
#include "test_point_readout.moc"
