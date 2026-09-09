// Reading a quantity across the frames of a sequence, and the virtual
// extensometer that is the usual way of doing it.
//
// WHY THIS EXISTS. A DIC test is a loading series, and the thing an
// experimentalist actually wants out of one is a CURVE: strain against load
// step, or elongation against frame. Until now every frame's field could be
// looked at one at a time and nothing put them on the same axes, which is the
// single largest thing every commercial tool has that we did not.
//
// A virtual extensometer is the digital form of the clip gauge it is named
// after: two points on the specimen, and the change in the distance between
// them. It is a derived quantity twice over -- from displacements that were
// themselves fitted at grid points either side of it -- so most of this file is
// about the ways it can quietly report a number it has no business reporting.
//
// ⚑ ENGINE-FREE, like core/ReferenceUpdate.h and core/Recovery.h, and for the
// same reason: it is arithmetic over measured fields, it is where the mistakes
// live, and here it can be exercised exhaustively in microseconds against
// fields written by hand with known answers.
//
// Three rules, each with cases below:
//
//   1. An anchor between grid points is INTERPOLATED, and only where the four
//      grid points around it were all measured. Anywhere else the frame has no
//      reading -- not a zero, and not a value borrowed from further away.
//   2. A frame with no reading breaks the curve; it does not join the points
//      either side of it into a straight line through territory nobody
//      measured.
//   3. Strain along the extensometer is measured against the length in the
//      REFERENCE frame, which is the frame the whole series is reported in.
//
// NEGATIVE CHECK (2026-09-02). Each rule removed in turn, the suite re-run,
// then put back. Every one turned at least one case red:
//
//   accept a cell with unmeasured corners -> a_reading_with_any_of_its_four_...
//   read one anchor's displacement        -> a_uniform_translation_moves_an_...
//                                            and three more
//   drop unmeasured frames from a series  -> a_frame_the_extensometer_could_...
//   average unmeasured points into a mean -> a_field_series_averages_only_...
//   allow a zero-length gauge             -> an_extensometer_of_no_length_...
//   plot an empty frame as zero           -> a_frame_where_nothing_solved_...
//
// The second is worth reading twice: substituting one anchor's displacement for
// the change in distance broke four cases, but the case that names the fault is
// a_uniform_translation_moves_an_extensometer_without_straining_it, and it
// exists precisely because a stretch test alone would NOT catch it -- under a
// pure stretch about the origin the two happen to agree.

#include "core/Correlation.h"
#include "core/Series.h"

#include <QTest>

#include <cmath>

namespace {

// A field on a `columns` x `rows` grid at `step` px spacing, every point
// measured, displaced by the function given. Written this way so a case states
// the deformation it wants and the expected answer follows from arithmetic the
// test does not have to repeat.
CorrelationResult uniformField(int columns, int rows, int step,
                               float du, float dv)
{
    CorrelationResult result;
    result.gridColumns = columns;
    result.gridRows = rows;
    result.step = step;
    result.originX = 0.f;
    result.originY = 0.f;

    for (int row = 0; row < rows; row++) {
        for (int column = 0; column < columns; column++) {
            CorrelationPoint point;
            point.gridIndex = row * columns + column;
            point.x = float(column * step);
            point.y = float(row * step);
            point.u = du;
            point.v = dv;
            point.zncc = 0.95f;
            point.converged = true;
            result.points.append(point);
            result.converged++;
        }
    }
    return result;
}

// The same, stretched along x about the origin: a point at x moves by x * strain.
CorrelationResult stretchedField(int columns, int rows, int step, double strain)
{
    CorrelationResult result = uniformField(columns, rows, step, 0.f, 0.f);
    for (CorrelationPoint &point : result.points)
        point.u = float(double(point.x) * strain);
    return result;
}

void takeOutPointAt(CorrelationResult &field, float x, float y)
{
    for (CorrelationPoint &point : field.points) {
        if (qFuzzyCompare(point.x, x) && qFuzzyCompare(point.y, y)) {
            point.converged = false;
            point.zncc = -3.f;
            point.u = 0.f;
            point.v = 0.f;
        }
    }
}

}  // namespace

class TestSeries : public QObject
{
    Q_OBJECT

private slots:
    // sampling the field where no point sits
    void a_reading_at_a_grid_point_is_that_point_s_own_displacement();
    void a_reading_between_grid_points_is_interpolated_from_the_four_around_it();
    void a_reading_with_any_of_its_four_corners_unmeasured_is_no_reading();
    void a_reading_outside_the_measured_grid_is_no_reading();

    // the extensometer
    void an_extensometer_on_an_undeformed_field_reads_no_strain();
    void a_uniform_translation_moves_an_extensometer_without_straining_it();
    void a_known_stretch_is_read_as_the_engineering_strain_it_is();
    void an_extensometer_reports_length_and_elongation_as_well_as_strain();
    void an_extensometer_of_no_length_is_refused_rather_than_dividing_by_zero();

    // the series across frames
    void a_series_carries_one_reading_per_frame_in_frame_order();
    void a_frame_the_extensometer_could_not_read_breaks_the_curve();
    void a_series_says_how_many_of_its_frames_were_read();

    // whole-field series
    void a_field_series_averages_only_the_points_that_were_measured();
    void a_frame_where_nothing_solved_has_no_field_reading_rather_than_zero();
    void every_series_the_screen_offers_can_be_produced();

    // Written against the mutation sweep of 2026-09-09, which found 35 mutants
    // in this file surviving every case above. Each one below is a mutation
    // that changed what a reader would be shown, negative-checked at the time
    // it was written.
    //
    // ⚑ Three of them survived their first attempt HERE too, and always for
    // the same reason: a fixture symmetric in the thing under test. The grid
    // sat at the origin, so `x - originX` and `x + originX` agreed. The cell
    // was row zero, so `cy - row` and `cy + row` agreed. The sample sat at
    // fx == fy, so the blend's two axes could be exchanged wholesale. All
    // three put a wrong displacement into an extensometer reading that looks
    // entirely ordinary on screen, which is the failure this tool can least
    // afford.
    //
    // One mutation is caught by a SEGFAULT rather than an assertion: relaxing
    // the all-four-corners rule to an AND dereferences a null corner. The
    // suite fails, so it is caught, but the harness reports it as a crash and
    // it is recorded here so the next reader knows why.
    void a_field_series_numbers_its_frames_the_way_the_rest_of_the_window_does();
    void the_largest_aggregate_takes_the_largest_MAGNITUDE_signs_included();
    void the_mean_is_a_mean_and_the_largest_is_not();
    void the_solved_share_is_a_percentage_of_the_points_attempted();
    void a_reading_exactly_on_the_far_edge_of_the_grid_belongs_to_the_last_cell();
    void a_reading_just_outside_the_grid_is_refused_on_each_side_in_turn();
    void an_interpolated_reading_weights_the_corner_it_is_nearest_to();
    void strain_is_the_one_quantity_without_a_unit_in_pixels();
};

// --- sampling ---------------------------------------------------------------

void TestSeries::a_reading_at_a_grid_point_is_that_point_s_own_displacement()
{
    const CorrelationResult field = uniformField(4, 4, 10, 2.f, -3.f);

    FieldSample sample = sampleFieldAt(field, 20.0, 10.0);
    QVERIFY(sample.measured);
    QCOMPARE(sample.u, 2.0);
    QCOMPARE(sample.v, -3.0);
}

void TestSeries::a_reading_between_grid_points_is_interpolated_from_the_four_around_it()
{
    // A stretch of 0.01 puts u = 0.01 * x. Halfway between x = 10 and x = 20
    // the answer is 0.15, which no measured point holds: it can only come from
    // interpolating, and it is exact for a field that is linear in x.
    const CorrelationResult field = stretchedField(4, 4, 10, 0.01);

    FieldSample sample = sampleFieldAt(field, 15.0, 15.0);
    QVERIFY(sample.measured);
    QVERIFY2(std::abs(sample.u - 0.15) < 1e-6,
             qPrintable(QStringLiteral("interpolated u = %1, expected 0.15")
                            .arg(sample.u)));
}

void TestSeries::a_reading_with_any_of_its_four_corners_unmeasured_is_no_reading()
{
    // ⚑ THE RULE THAT KEEPS AN EXTENSOMETER HONEST. An anchor sits between grid
    // points, so its reading is always borrowed from its neighbours -- and if
    // one of those neighbours is a hole, the obvious repairs are all lies. Using
    // the three that remain silently changes what is being averaged; reaching
    // further out reports a displacement from somewhere the anchor is not;
    // filling the hole with zero drags the reading toward no movement, which is
    // the most plausible-looking wrong answer available.
    //
    // So: no reading. The curve breaks, visibly, and the user moves the anchor.
    CorrelationResult field = stretchedField(4, 4, 10, 0.01);
    takeOutPointAt(field, 20.f, 20.f);

    QVERIFY2(!sampleFieldAt(field, 15.0, 15.0).measured,
             "a sample was returned from a cell with an unmeasured corner");

    // ...and a cell away from the hole still reads perfectly well, so the rule
    // costs only what it must.
    QVERIFY(sampleFieldAt(field, 5.0, 5.0).measured);
}

void TestSeries::a_reading_outside_the_measured_grid_is_no_reading()
{
    const CorrelationResult field = uniformField(4, 4, 10, 2.f, -3.f);

    QVERIFY(!sampleFieldAt(field, -1.0, 5.0).measured);
    QVERIFY(!sampleFieldAt(field, 5.0, 400.0).measured);
    // The far edge is measured AT the last point but has no cell beyond it.
    QVERIFY(sampleFieldAt(field, 30.0, 30.0).measured);
    QVERIFY(!sampleFieldAt(field, 30.1, 30.0).measured);
}

// --- the extensometer -------------------------------------------------------

void TestSeries::an_extensometer_on_an_undeformed_field_reads_no_strain()
{
    const CorrelationResult field = uniformField(6, 6, 10, 0.f, 0.f);

    Extensometer gauge;
    gauge.name = QStringLiteral("E1");
    gauge.ax = 5.0;
    gauge.ay = 5.0;
    gauge.bx = 45.0;
    gauge.by = 5.0;

    const ExtensometerReading reading = readExtensometer(gauge, field);
    QVERIFY(reading.measured);
    QCOMPARE(reading.referenceLength, 40.0);
    QCOMPARE(reading.length, 40.0);
    QCOMPARE(reading.elongation, 0.0);
    QCOMPARE(reading.strain, 0.0);
}

void TestSeries::a_uniform_translation_moves_an_extensometer_without_straining_it()
{
    // ⚑ The case worth having most, and the one a careless implementation fails:
    // an extensometer must measure the change in the distance between two
    // points, not the movement of either. A specimen carried bodily across the
    // frame has strained by nothing at all, and an implementation that took the
    // displacement of one anchor as its answer would report a huge strain from
    // a rigid-body motion -- which looks entirely plausible on a curve.
    const CorrelationResult field = uniformField(6, 6, 10, 7.f, -4.f);

    Extensometer gauge;
    gauge.ax = 5.0;
    gauge.ay = 5.0;
    gauge.bx = 45.0;
    gauge.by = 5.0;

    const ExtensometerReading reading = readExtensometer(gauge, field);
    QVERIFY(reading.measured);
    QVERIFY2(std::abs(reading.strain) < 1e-9,
             qPrintable(QStringLiteral("a rigid translation read as strain %1")
                            .arg(reading.strain)));
    QVERIFY2(std::abs(reading.length - 40.0) < 1e-9,
             "a rigid translation changed the gauge length");
}

void TestSeries::a_known_stretch_is_read_as_the_engineering_strain_it_is()
{
    // u = 0.01 * x, so a gauge from x = 5 to x = 45 becomes 40 * 1.01 long.
    const CorrelationResult field = stretchedField(6, 6, 10, 0.01);

    Extensometer gauge;
    gauge.ax = 5.0;
    gauge.ay = 5.0;
    gauge.bx = 45.0;
    gauge.by = 5.0;

    const ExtensometerReading reading = readExtensometer(gauge, field);
    QVERIFY(reading.measured);
    QVERIFY2(std::abs(reading.strain - 0.01) < 1e-6,
             qPrintable(QStringLiteral("read %1, expected 0.01")
                            .arg(reading.strain)));
}

void TestSeries::an_extensometer_reports_length_and_elongation_as_well_as_strain()
{
    // Three quantities, because they answer different questions and a reader
    // should not have to multiply. Elongation in px is what a clip gauge would
    // have shown; strain is what goes on the axis of a stress-strain curve.
    const CorrelationResult field = stretchedField(6, 6, 10, 0.01);

    Extensometer gauge;
    gauge.ax = 5.0;
    gauge.ay = 5.0;
    gauge.bx = 45.0;
    gauge.by = 5.0;

    const ExtensometerReading reading = readExtensometer(gauge, field);
    QVERIFY2(std::abs(reading.referenceLength - 40.0) < 1e-9, "reference length");
    QVERIFY2(std::abs(reading.elongation - 0.4) < 1e-6,
             qPrintable(QStringLiteral("elongation %1, expected 0.4")
                            .arg(reading.elongation)));
    QVERIFY2(std::abs(reading.length - 40.4) < 1e-6, "deformed length");
}

void TestSeries::an_extensometer_of_no_length_is_refused_rather_than_dividing_by_zero()
{
    // Two clicks in the same place. Strain is elongation over reference length,
    // so a zero-length gauge produces an infinity or a not-a-number and puts it
    // on a chart, where an axis silently rescales to it and every real reading
    // collapses onto the baseline.
    const CorrelationResult field = uniformField(6, 6, 10, 1.f, 1.f);

    Extensometer gauge;
    gauge.ax = 15.0;
    gauge.ay = 15.0;
    gauge.bx = 15.0;
    gauge.by = 15.0;

    QVERIFY(!gauge.isValid());
    QVERIFY(!readExtensometer(gauge, field).measured);
}

// --- the series -------------------------------------------------------------

void TestSeries::a_series_carries_one_reading_per_frame_in_frame_order()
{
    QVector<CorrelationResult> frames;
    for (int i = 0; i < 4; i++)
        frames.append(stretchedField(6, 6, 10, 0.005 * i));

    Extensometer gauge;
    gauge.name = QStringLiteral("E1");
    gauge.ax = 5.0;
    gauge.ay = 5.0;
    gauge.bx = 45.0;
    gauge.by = 5.0;

    const Series series = extensometerSeries(gauge, frames, ExtensometerQuantity::Strain);
    QCOMPARE(series.points.size(), 4);
    for (int i = 0; i < 4; i++) {
        // One-based, as the project tree and the log number frames.
        QCOMPARE(series.points[i].frame, i + 1);
        QVERIFY(series.points[i].measured);
        QVERIFY2(std::abs(series.points[i].value - 0.005 * i) < 1e-6,
                 qPrintable(QStringLiteral("frame %1 read %2")
                                .arg(i).arg(series.points[i].value)));
    }
    QVERIFY2(!series.name.isEmpty(), "a series with no name cannot be put on a legend");
    QVERIFY2(series.name.contains(gauge.name),
             "the series does not name the extensometer it came from, so two "
             "gauges on one chart cannot be told apart");
}

void TestSeries::a_frame_the_extensometer_could_not_read_breaks_the_curve()
{
    // ⚑ A gap must stay a gap. Dropping the point instead would join the frames
    // either side into a straight segment through a region nobody measured, and
    // that segment is indistinguishable from data. Writing a zero would be
    // worse still, since on a rising loading curve it reads as the specimen
    // springing back.
    QVector<CorrelationResult> frames;
    frames.append(stretchedField(6, 6, 10, 0.00));
    frames.append(stretchedField(6, 6, 10, 0.01));
    frames.append(stretchedField(6, 6, 10, 0.02));
    takeOutPointAt(frames[1], 0.f, 0.f);
    takeOutPointAt(frames[1], 10.f, 0.f);
    takeOutPointAt(frames[1], 0.f, 10.f);
    takeOutPointAt(frames[1], 10.f, 10.f);

    Extensometer gauge;
    gauge.ax = 5.0;
    gauge.ay = 5.0;
    gauge.bx = 45.0;
    gauge.by = 5.0;

    const Series series = extensometerSeries(gauge, frames, ExtensometerQuantity::Strain);
    QCOMPARE(series.points.size(), 3);
    QVERIFY(series.points[0].measured);
    QVERIFY2(!series.points[1].measured,
             "a frame the gauge could not read was given a value anyway");
    QVERIFY(series.points[2].measured);

    // And the frame is still THERE, at its own index, so the two readings that
    // exist stay on the frames they belong to.
    QCOMPARE(series.points[2].frame, 3);
}

void TestSeries::a_series_says_how_many_of_its_frames_were_read()
{
    QVector<CorrelationResult> frames;
    frames.append(stretchedField(6, 6, 10, 0.00));
    frames.append(stretchedField(6, 6, 10, 0.01));
    takeOutPointAt(frames[1], 0.f, 0.f);

    Extensometer gauge;
    gauge.ax = 5.0;
    gauge.ay = 5.0;
    gauge.bx = 45.0;
    gauge.by = 5.0;

    const Series series = extensometerSeries(gauge, frames, ExtensometerQuantity::Strain);
    QCOMPARE(series.measuredCount(), 1);
    QCOMPARE(series.points.size(), 2);
}

// --- whole-field series -----------------------------------------------------

void TestSeries::a_field_series_averages_only_the_points_that_were_measured()
{
    // A rejected point holds a leftover guess, not a displacement, so averaging
    // it in would let the solver's abandoned scratch values into a curve
    // somebody reads a material property off.
    CorrelationResult field = uniformField(4, 4, 10, 4.f, 0.f);
    for (CorrelationPoint &point : field.points) {
        if (point.gridIndex % 2 == 0) {
            point.converged = false;
            point.u = 1000.f;   // the leftover guess
        }
    }

    const Series series = fieldSeries({field}, FieldChannel::DisplacementX,
                                      FieldAggregate::Mean);
    QCOMPARE(series.points.size(), 1);
    QVERIFY(series.points[0].measured);
    QVERIFY2(std::abs(series.points[0].value - 4.0) < 1e-6,
             qPrintable(QStringLiteral("mean came out %1, so unmeasured points "
                                       "were averaged in")
                            .arg(series.points[0].value)));
}

void TestSeries::a_frame_where_nothing_solved_has_no_field_reading_rather_than_zero()
{
    CorrelationResult empty = uniformField(4, 4, 10, 0.f, 0.f);
    for (CorrelationPoint &point : empty.points)
        point.converged = false;
    empty.converged = 0;

    const Series series = fieldSeries({uniformField(4, 4, 10, 2.f, 0.f), empty},
                                      FieldChannel::DisplacementX,
                                      FieldAggregate::Mean);
    QCOMPARE(series.points.size(), 2);
    QVERIFY(series.points[0].measured);
    QVERIFY2(!series.points[1].measured,
             "a frame that measured nothing was plotted as a displacement of "
             "zero, which reads as a specimen that stopped moving");
}

void TestSeries::every_series_the_screen_offers_can_be_produced()
{
    // Single source of truth, in the same sense as offeredSolverChoices() and
    // offeredFieldChannels(): the plot panel builds its selector from this list
    // and this case walks it, so a series cannot be offered without something
    // checking that it comes out.
    const CorrelationResult field = stretchedField(6, 6, 10, 0.01);
    const QVector<CorrelationResult> frames{field, field};

    const QVector<FieldSeriesChoice> offered = offeredFieldSeries();
    QVERIFY2(offered.size() >= 3, "the plot panel offers almost nothing");

    QStringList names;
    for (const FieldSeriesChoice &choice : offered) {
        QVERIFY2(!choice.name.isEmpty(), "a series on offer has no name");
        QVERIFY2(!names.contains(choice.name), "two series share a name");
        names << choice.name;

        const Series series = fieldSeries(frames, choice.channel, choice.aggregate);
        QCOMPARE(series.points.size(), 2);
        QVERIFY2(series.name.contains(choice.name),
                 qPrintable(QStringLiteral("series %1 came out named %2")
                                .arg(choice.name, series.name)));
    }
}


void TestSeries::a_field_series_numbers_its_frames_the_way_the_rest_of_the_window_does()
{
    // ⚑ One-based, because that is what the project tree and the run log call
    // the same frame. The extensometer series is checked for this; the FIELD
    // series numbers its frames in its own loop and nothing checked that one,
    // so it could have started at zero -- putting the first target at "frame
    // 0" on a chart while the rest of the window called it Frame 1 -- or at
    // two, or anywhere.
    QVector<CorrelationResult> frames;
    for (int i = 0; i < 3; i++)
        frames.append(uniformField(4, 4, 10, float(i), 0.f));

    const Series series =
        fieldSeries(frames, FieldChannel::DisplacementMagnitude, FieldAggregate::Mean);
    QCOMPARE(series.points.size(), 3);
    QCOMPARE(series.points[0].frame, 1);
    QCOMPARE(series.points[1].frame, 2);
    QCOMPARE(series.points[2].frame, 3);
}

void TestSeries::the_largest_aggregate_takes_the_largest_MAGNITUDE_signs_included()
{
    // ⚑ A field that moved 5 px one way and 9 px the other has a largest
    // displacement of 9, not 5, and the answer keeps its SIGN. Compared
    // without the absolute values a field whose extreme is negative reports
    // its smallest movement as its largest, which on a specimen in
    // compression is every frame of the test.
    CorrelationResult field = uniformField(3, 1, 10, 0.f, 0.f);
    field.points[0].u = 5.f;
    field.points[1].u = -9.f;
    field.points[2].u = 2.f;

    const Series largest =
        fieldSeries({field}, FieldChannel::DisplacementX, FieldAggregate::Largest);
    QCOMPARE(largest.points.size(), 1);
    QVERIFY(largest.points[0].measured);
    QVERIFY2(std::abs(largest.points[0].value + 9.0) < 1e-9,
             qPrintable(QStringLiteral("largest read %1, expected -9")
                            .arg(largest.points[0].value)));

    // And the first point is not privileged: the same field with the extreme
    // first must still find it.
    CorrelationResult reordered = field;
    reordered.points[0].u = -9.f;
    reordered.points[1].u = 5.f;
    const Series again =
        fieldSeries({reordered}, FieldChannel::DisplacementX, FieldAggregate::Largest);
    QVERIFY2(std::abs(again.points[0].value + 9.0) < 1e-9,
             "the largest value is found wherever in the list it sits");
}

void TestSeries::the_mean_is_a_mean_and_the_largest_is_not()
{
    // The two aggregates must not collapse into one another: on a field whose
    // mean and extreme differ, each has to give its own answer. With the same
    // value for both, either could be computing the other.
    CorrelationResult field = uniformField(3, 1, 10, 0.f, 0.f);
    field.points[0].u = 1.f;
    field.points[1].u = 2.f;
    field.points[2].u = 6.f;   // mean 3, largest 6

    const Series mean =
        fieldSeries({field}, FieldChannel::DisplacementX, FieldAggregate::Mean);
    const Series largest =
        fieldSeries({field}, FieldChannel::DisplacementX, FieldAggregate::Largest);

    QVERIFY2(std::abs(mean.points[0].value - 3.0) < 1e-9,
             qPrintable(QStringLiteral("mean read %1, expected 3").arg(mean.points[0].value)));
    QVERIFY2(std::abs(largest.points[0].value - 6.0) < 1e-9,
             qPrintable(QStringLiteral("largest read %1, expected 6").arg(largest.points[0].value)));
}

void TestSeries::the_solved_share_is_a_percentage_of_the_points_attempted()
{
    // Three of four solved is 75 per cent, and the denominator is what was
    // ATTEMPTED rather than what succeeded -- otherwise every frame reads 100.
    CorrelationResult field = uniformField(2, 2, 10, 1.f, 0.f);
    field.points[3].converged = false;
    field.converged = 3;

    const Series share =
        fieldSeries({field}, FieldChannel::DisplacementMagnitude,
                    FieldAggregate::SolvedShare);
    QCOMPARE(share.points.size(), 1);
    QVERIFY(share.points[0].measured);
    QVERIFY2(std::abs(share.points[0].value - 75.0) < 1e-9,
             qPrintable(QStringLiteral("solved share read %1, expected 75")
                            .arg(share.points[0].value)));
    QVERIFY2(share.unit.contains(QStringLiteral("%")),
             "a share of the field is a percentage and says so");

    // ⚑ An empty field has no share, not a share of zero: nothing was
    // attempted, so there is no proportion to report.
    const CorrelationResult nothing;
    const Series empty =
        fieldSeries({nothing}, FieldChannel::DisplacementMagnitude,
                    FieldAggregate::SolvedShare);
    QCOMPARE(empty.points.size(), 1);
    QVERIFY2(!empty.points[0].measured,
             "a frame with no points attempted has no share to report");
}

void TestSeries::a_reading_exactly_on_the_far_edge_of_the_grid_belongs_to_the_last_cell()
{
    // ⚑ The far edge is the boundary the clamp exists for, and no case landed
    // on it. A 4 by 4 grid at 10 px spans 0 to 30: a position at exactly 30
    // sits on the last grid line, which belongs to the last CELL (columns 2
    // and 3) rather than to a cell starting at column 3, which does not exist
    // and whose corners cannot be read.
    const CorrelationResult field = uniformField(4, 4, 10, 2.f, -1.f);

    const FieldSample onEdge = sampleFieldAt(field, 30.0, 30.0);
    QVERIFY2(onEdge.measured, "a position exactly on the far edge is inside the field");
    QVERIFY2(std::abs(onEdge.u - 2.0) < 1e-9 && std::abs(onEdge.v + 1.0) < 1e-9,
             "and reads the displacement the field holds there");

    const FieldSample onOrigin = sampleFieldAt(field, 0.0, 0.0);
    QVERIFY2(onOrigin.measured, "and so is one exactly on the near edge");
}

void TestSeries::a_reading_just_outside_the_grid_is_refused_on_each_side_in_turn()
{
    // Each side on its own, because the four comparisons are one OR: a fixture
    // outside on every side at once is refused by any one of them, and the
    // other three could be anything at all.
    const CorrelationResult field = uniformField(4, 4, 10, 2.f, -1.f);

    QVERIFY2(!sampleFieldAt(field, -0.5, 15.0).measured, "left of the grid is outside it");
    QVERIFY2(!sampleFieldAt(field, 30.5, 15.0).measured, "right of the grid is outside it");
    QVERIFY2(!sampleFieldAt(field, 15.0, -0.5).measured, "above the grid is outside it");
    QVERIFY2(!sampleFieldAt(field, 15.0, 30.5).measured, "below the grid is outside it");

    // A field with no grid at all is refused rather than divided by.
    CorrelationResult degenerate = uniformField(4, 4, 10, 2.f, -1.f);
    degenerate.step = 0;
    QVERIFY2(!sampleFieldAt(degenerate, 15.0, 15.0).measured,
             "a field with no step is refused rather than dividing by zero");
}

void TestSeries::an_interpolated_reading_weights_the_corner_it_is_nearest_to()
{
    // ⚑ THE FIXTURE IS OFFSET AND THE CELL IS NOT THE FIRST ONE, and both of
    // those are load-bearing. The existing interpolation case varies along x
    // alone, on a grid at the origin, in cell (0, 0) -- so it cannot tell fx
    // from fy, it cannot tell `x - originX` from `x + originX` because the
    // origin is zero, and it cannot tell `cy - row` from `cy + row` because
    // the row is zero. All three survived the sweep, and all three put a wrong
    // displacement into an extensometer reading that looks perfectly ordinary.
    //
    // So: origin at (100, 200), a 3 by 3 grid, and the reading taken inside
    // the FAR cell, where every one of those pairs differs.
    CorrelationResult field = uniformField(3, 3, 10, 0.f, 0.f);
    field.originX = 100.f;
    field.originY = 200.f;
    for (CorrelationPoint &point : field.points) {
        point.x = 100.f + point.x;
        point.y = 200.f + point.y;
    }
    // Cell (1, 1) spans grid points 4, 5, 7, 8. Give its four corners values
    // that differ in both directions, so no weight can be swapped for another.
    field.points[4].u = 0.f;     // (110, 210)
    field.points[5].u = 4.f;     // (120, 210)
    field.points[7].u = 8.f;     // (110, 220)
    field.points[8].u = 12.f;    // (120, 220)

    // A quarter across and a quarter down that cell: cx = 1.25, cy = 1.25,
    // so fx = fy = 0.25 and the blend is
    //   0*0.75*0.75 + 4*0.25*0.75 + 8*0.75*0.25 + 12*0.25*0.25 = 3.0
    const FieldSample quarter = sampleFieldAt(field, 112.5, 212.5);
    QVERIFY2(quarter.measured, "a position inside the far cell is inside the field");
    QVERIFY2(std::abs(quarter.u - 3.0) < 1e-9,
             qPrintable(QStringLiteral("read %1, expected 3").arg(quarter.u)));

    // Three quarters across and down the same cell: 9.0 by the same arithmetic.
    const FieldSample threeQuarters = sampleFieldAt(field, 117.5, 217.5);
    QVERIFY2(threeQuarters.u > quarter.u,
             "a position nearer the larger corner reads larger");
    QVERIFY2(std::abs(threeQuarters.u - 9.0) < 1e-9,
             qPrintable(QStringLiteral("read %1, expected 9").arg(threeQuarters.u)));

    // ⚑ AND A POSITION WHERE THE TWO FRACTIONS DIFFER. At fx == fy the blend
    // is symmetric in its two axes, so the x weights and the y weights can be
    // exchanged wholesale and every reading above still comes out right. A
    // quarter across and three quarters down:
    //   0*0.75*0.25 + 4*0.25*0.25 + 8*0.75*0.75 + 12*0.25*0.75 = 7.0
    // where the exchanged version reads 5.0.
    const FieldSample lopsided = sampleFieldAt(field, 112.5, 217.5);
    QVERIFY(lopsided.measured);
    QVERIFY2(std::abs(lopsided.u - 7.0) < 1e-9,
             qPrintable(QStringLiteral("read %1, expected 7 -- the x and y weights "
                                       "of the blend are interchangeable at this "
                                       "reading unless the two fractions differ")
                            .arg(lopsided.u)));

    // ⚑ And the position is measured FROM the origin: the same coordinates on
    // a field that starts at zero are a different place entirely, and asking
    // this field about them is asking about somewhere it does not cover.
    QVERIFY2(!sampleFieldAt(field, 2.5, 2.5).measured,
             "a position before the grid's origin is outside the field, not inside its first cell");
}

void TestSeries::strain_is_the_one_quantity_without_a_unit_in_pixels()
{
    // A ratio has no unit, and saying "dimensionless" beats both a blank -
    // which reads as an oversight - and inventing one. Length and elongation
    // are pixels. Nothing checked which was which, so the two could swap.
    QCOMPARE(extensometerQuantityUnit(ExtensometerQuantity::Strain),
             QStringLiteral("dimensionless"));
    QCOMPARE(extensometerQuantityUnit(ExtensometerQuantity::Length),
             QStringLiteral("px"));
    QCOMPARE(extensometerQuantityUnit(ExtensometerQuantity::Elongation),
             QStringLiteral("px"));
}

QTEST_MAIN(TestSeries)
#include "test_series.moc"
