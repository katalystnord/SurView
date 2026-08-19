// The arithmetic that decides whether a strain fit can happen at all.
//
// WHY THIS EXISTS. Strain is not measured at a point -- it is fitted, from the
// displacements of the points around it inside a subregion of a chosen radius.
// If that subregion holds fewer points than the fit needs, the engine does NOT
// refuse: it silently falls back to searching for the nearest N points however
// far away they are, and returns a strain fitted over a neighbourhood nobody
// asked for. The user sees a full-looking strain field either way.
//
// That is exactly the invisible precondition this project's rules forbid, so
// the count is computed here, before the run, and said out loud.
//
// NEGATIVE CHECK (2026-08-19): run against a stub returning 0 and an empty
// string, 7 of the 9 cases went red. Two did NOT, and are worth naming, since
// each looks like coverage it is not:
//   - `an_exactly_sufficient_subregion_draws_no_warning` passes against any
//     implementation that never warns. It pins the threshold from the far side
//     and is only meaningful paired with the case above it.
//   - `a_nonsense_grid_step_counts_nothing_rather_than_dividing_by_zero` passes
//     against a function that counts nothing at all. It guards a crash, not an
//     answer.

#include "core/StrainFit.h"

#include <QTest>

class TestStrainFit : public QObject
{
    Q_OBJECT

private slots:
    void the_centre_point_counts_toward_what_the_fit_can_see();
    void a_subregion_holds_the_lattice_points_inside_it();
    void a_point_exactly_on_the_boundary_is_inside();
    void a_subregion_covering_one_step_is_too_small();
    void an_exactly_sufficient_subregion_draws_no_warning();
    void the_warning_names_what_the_engine_will_do_instead();
    void the_warning_speaks_for_the_best_case_only();
    void a_nonsense_grid_step_counts_nothing_rather_than_dividing_by_zero();
    void every_offered_strain_measure_has_a_name();
};

void TestStrainFit::the_centre_point_counts_toward_what_the_fit_can_see()
{
    // The engine compares its neighbour count against the minimum, and that
    // count comes from a radius search over a cloud the point itself is in --
    // so the centre is one of them. Counting it differently here would make
    // the warning fire on the wrong side of the engine's own test.
    QCOMPARE(gridPointsInSubregion(1.0, 10), 1);
}

void TestStrainFit::a_subregion_holds_the_lattice_points_inside_it()
{
    // Step 10, radius 10: the centre and its four edge-sharing neighbours.
    // The diagonal ones are 14.1 px away and fall outside.
    QCOMPARE(gridPointsInSubregion(10.0, 10), 5);

    // Widen past the diagonal and they arrive: 3x3 minus nothing.
    QCOMPARE(gridPointsInSubregion(15.0, 10), 9);

    // Radius 20 is two steps: the 3x3 block, plus the four points two steps
    // out along the axes. The ones two steps out diagonally, and the knight's
    // moves at 22.4 px, are outside. 9 + 4 = 13.
    QCOMPARE(gridPointsInSubregion(20.0, 10), 13);
}

void TestStrainFit::a_point_exactly_on_the_boundary_is_inside()
{
    // nanoflann's radius search keeps points at exactly the radius, so a
    // subregion of exactly one step does include the four neighbours at one
    // step. Off-by-one here would move the warning by a whole ring of points.
    QCOMPARE(gridPointsInSubregion(12.0, 12), 5);
    QCOMPARE(gridPointsInSubregion(11.999, 12), 1);
}

void TestStrainFit::a_subregion_covering_one_step_is_too_small()
{
    // Five points, and the fit wants six: the engine will abandon the
    // subregion and take the nearest six instead, wherever they are.
    const QString warning = strainSubregionWarning(10.0, 10, 6);
    QVERIFY(!warning.isEmpty());
}

void TestStrainFit::an_exactly_sufficient_subregion_draws_no_warning()
{
    // The same subregion, one fewer neighbour required: nothing to say.
    QVERIFY(strainSubregionWarning(10.0, 10, 5).isEmpty());
}

void TestStrainFit::the_warning_names_what_the_engine_will_do_instead()
{
    // A warning that says only "too small" leaves the user believing nothing
    // was measured. Something WAS measured -- over a different neighbourhood
    // than the one on screen -- and that is the part they cannot see.
    const QString warning = strainSubregionWarning(10.0, 10, 8);

    QVERIFY(warning.contains(QStringLiteral("5")));   // what the subregion holds
    QVERIFY(warning.contains(QStringLiteral("8")));   // what the fit needs
    QVERIFY(warning.contains(QStringLiteral("nearest")));
}

void TestStrainFit::the_warning_speaks_for_the_best_case_only()
{
    // The count is for a point with a full grid all around it. Points at the
    // edge of the image, or of a region, always have fewer -- so the warning
    // must not be read as "everywhere else is fine".
    const QString warning = strainSubregionWarning(10.0, 10, 8);
    QVERIFY(warning.contains(QStringLiteral("best case")));
}

void TestStrainFit::a_nonsense_grid_step_counts_nothing_rather_than_dividing_by_zero()
{
    QCOMPARE(gridPointsInSubregion(10.0, 0), 0);
    QCOMPARE(gridPointsInSubregion(10.0, -5), 0);
    QCOMPARE(gridPointsInSubregion(-1.0, 10), 0);
}

void TestStrainFit::every_offered_strain_measure_has_a_name()
{
    // Same rule as the solver list: the panel builds from this, so anything in
    // it reaches a user and must be presentable and distinct.
    const QVector<StrainMeasureChoice> measures = offeredStrainMeasures();
    QVERIFY(measures.size() >= 2);

    QStringList names;
    for (const StrainMeasureChoice &choice : measures) {
        const QString name = strainMeasureName(choice.measure);
        QVERIFY2(!name.isEmpty(), "a strain measure the panel offers has no name");
        QCOMPARE(choice.name, name);
        QVERIFY2(!choice.note.isEmpty(),
                 "a strain measure the panel offers says nothing about when to pick it");
        QVERIFY2(!names.contains(name), "two strain measures share a name");
        names << name;
    }
}

QTEST_MAIN(TestStrainFit)
#include "test_strain_fit.moc"
