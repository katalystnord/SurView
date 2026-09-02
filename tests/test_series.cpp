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

QTEST_MAIN(TestSeries)
#include "test_series.moc"
