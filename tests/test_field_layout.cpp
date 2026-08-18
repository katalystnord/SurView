// Turning a measured result into the array a renderer draws.
//
// Every failure mode here produces a picture that still looks like a field.
// A point placed by its position in the list rather than by its own cell gives
// a smooth, plausible, WRONG map; a rejected point written as zero gives a
// cold spot that reads as a real measurement of no movement.

#include "core/Correlation.h"
#include "core/FieldLayout.h"

#include <QTest>

#include <cmath>

namespace {

CorrelationPoint measured(int gridIndex, float u, float v)
{
    CorrelationPoint p;
    p.gridIndex = gridIndex;
    p.u = u;
    p.v = v;
    p.converged = true;
    return p;
}

CorrelationPoint rejected(int gridIndex)
{
    CorrelationPoint p;
    p.gridIndex = gridIndex;
    p.u = 999.f;   // the solver's leftover guess, which is not a measurement
    p.v = 999.f;
    p.converged = false;
    return p;
}

}  // namespace

class TestFieldLayout : public QObject
{
    Q_OBJECT

private slots:
    void a_value_lands_in_the_cell_its_point_recorded();
    void a_sparse_result_leaves_gaps_rather_than_sliding_along();
    void a_rejected_point_is_not_a_displacement_of_zero();
    void a_cell_no_point_reached_is_not_a_displacement_of_zero();
    void the_magnitude_is_the_length_of_the_displacement();
    void an_out_of_range_cell_index_is_ignored_not_written();
    void an_empty_grid_lays_out_to_nothing();
};

void TestFieldLayout::a_value_lands_in_the_cell_its_point_recorded()
{
    CorrelationResult result;
    result.gridColumns = 4;
    result.gridRows = 3;
    result.points.append(measured(6, 3.f, 4.f));   // row 1, column 2

    const QVector<float> values = layoutDisplacementMagnitude(result);

    QCOMPARE(values.size(), 12);
    QCOMPARE(values[6], 5.f);
    QVERIFY(std::isnan(values[5]));
    QVERIFY(std::isnan(values[7]));
}

void TestFieldLayout::a_sparse_result_leaves_gaps_rather_than_sliding_along()
{
    // The region-of-interest case. Three points, at cells 0, 5 and 11 -- if
    // they were written in list order they would land at 0, 1 and 2, and the
    // whole field would be drawn shifted into the corner.
    CorrelationResult result;
    result.gridColumns = 4;
    result.gridRows = 3;
    result.restrictedToRoi = true;
    result.points.append(measured(0, 1.f, 0.f));
    result.points.append(measured(5, 2.f, 0.f));
    result.points.append(measured(11, 3.f, 0.f));

    const QVector<float> values = layoutDisplacementMagnitude(result);

    QCOMPARE(values[0], 1.f);
    QCOMPARE(values[5], 2.f);
    QCOMPARE(values[11], 3.f);
    QVERIFY(std::isnan(values[1]));
    QVERIFY(std::isnan(values[2]));
}

void TestFieldLayout::a_rejected_point_is_not_a_displacement_of_zero()
{
    CorrelationResult result;
    result.gridColumns = 2;
    result.gridRows = 1;
    result.points.append(measured(0, 0.f, 1.f));
    result.points.append(rejected(1));

    const QVector<float> values = layoutDisplacementMagnitude(result);

    QCOMPARE(values[0], 1.f);
    QVERIFY(std::isnan(values[1]));
    QVERIFY(values[1] != 0.f);
}

void TestFieldLayout::a_cell_no_point_reached_is_not_a_displacement_of_zero()
{
    CorrelationResult result;
    result.gridColumns = 3;
    result.gridRows = 1;
    result.points.append(measured(1, 0.f, 2.f));

    const QVector<float> values = layoutDisplacementMagnitude(result);

    QVERIFY(std::isnan(values[0]));
    QCOMPARE(values[1], 2.f);
    QVERIFY(std::isnan(values[2]));
}

void TestFieldLayout::the_magnitude_is_the_length_of_the_displacement()
{
    CorrelationResult result;
    result.gridColumns = 2;
    result.gridRows = 1;
    result.points.append(measured(0, -3.f, 4.f));   // sign must not matter
    result.points.append(measured(1, 0.f, -2.5f));

    const QVector<float> values = layoutDisplacementMagnitude(result);

    QCOMPARE(values[0], 5.f);
    QCOMPARE(values[1], 2.5f);
}

void TestFieldLayout::an_out_of_range_cell_index_is_ignored_not_written()
{
    // Defensive, and cheap: a bad index must not write past the array.
    CorrelationResult result;
    result.gridColumns = 2;
    result.gridRows = 1;
    result.points.append(measured(99, 1.f, 1.f));
    result.points.append(measured(-1, 1.f, 1.f));
    result.points.append(measured(0, 0.f, 7.f));

    const QVector<float> values = layoutDisplacementMagnitude(result);

    QCOMPARE(values.size(), 2);
    QCOMPARE(values[0], 7.f);
    QVERIFY(std::isnan(values[1]));
}

void TestFieldLayout::an_empty_grid_lays_out_to_nothing()
{
    QVERIFY(layoutDisplacementMagnitude(CorrelationResult()).isEmpty());
}

QTEST_MAIN(TestFieldLayout)
#include "test_field_layout.moc"
