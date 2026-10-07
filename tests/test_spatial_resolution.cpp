// How far a measurement is averaged across the specimen.
//
// WHY THIS EXISTS. Displacement resolution and spatial resolution trade
// directly against each other: a larger subset resolves movement more finely
// and resolves detail on the specimen more coarsely. The noise floor reports
// the first half of that trade, and nothing reported the second, which is the
// "two questions, never one score" mistake one level up.
//
// The two lengths are the iDIC Good Practices Guide's own: a displacement is
// averaged over its subset, and a strain over the virtual strain gauge, the
// span of the points its fit uses plus one subset. They are stated as lengths
// AVERAGED OVER, never as the smallest feature measured at full size, because
// that would be false: on DIC Challenge 2.0 Star 1 a 33 px subset keeps 90 per
// cent of a displacement wave's amplitude only at about a 130 px wavelength
// (ROADMAP.md, Test and tooling debt).
//
// NEGATIVE CHECK (2026-10-07), four mutations, each caught:
//   - the subset without its centre pixel (2r): 5 cases red.
//   - 2R in place of the lattice span: ONE case red, the lopsided one. At the
//     default 25 px on a 5 px step the two agree exactly, so the headline
//     case cannot see it -- a fixture that agrees with itself.
//   - dropping the check for the engine reaching outside the subregion: 1 red.
//   - a gauge stated for a subregion holding only its centre: 1 red.
// Against a stub returning zero and empty text, two cases pass either way:
// `a_subregion_holding_only_its_centre...` and `nonsense_settings...` pin
// refusals, and a function that refuses everything satisfies them.

#include "core/SpatialResolution.h"

#include <QTest>

class TestSpatialResolution : public QObject
{
    Q_OBJECT

private slots:
    void a_displacement_is_averaged_over_its_whole_subset_centre_pixel_included();
    void a_strain_gauge_spans_its_outermost_fitted_points_plus_one_subset();
    void a_radius_between_grid_points_reaches_only_the_points_inside_it();
    void a_subregion_holding_only_its_centre_states_no_strain_gauge();
    void a_subregion_the_engine_would_reach_outside_states_no_length();
    void the_note_says_the_length_is_averaged_over_not_resolved();
    void nonsense_settings_state_no_length_rather_than_a_wrong_one();
};

void TestSpatialResolution::a_displacement_is_averaged_over_its_whole_subset_centre_pixel_included()
{
    QCOMPARE(displacementAveragingLength(16), 33);
    QCOMPARE(displacementAveragingLength(3), 7);
}

void TestSpatialResolution::a_strain_gauge_spans_its_outermost_fitted_points_plus_one_subset()
{
    // 25 px at a 5 px step reaches five steps out each way: 50 px between the
    // outermost points, and each of those is itself a 33 px average.
    QCOMPARE(strainGaugeLength(25.0, 5, 16), 83);
}

void TestSpatialResolution::a_radius_between_grid_points_reaches_only_the_points_inside_it()
{
    // Lopsided on purpose: 24 px at a 10 px step holds points two steps out,
    // so 40 px between them, plus a 21 px subset. Taking 2R instead gives 69,
    // and swapping the step and the subset radius gives 51.
    QCOMPARE(strainGaugeLength(24.0, 10, 10), 61);
}

void TestSpatialResolution::a_subregion_holding_only_its_centre_states_no_strain_gauge()
{
    // A plane cannot be fitted through one point, so there is no length to
    // state, and stating one subset would claim a strain was measured.
    QCOMPARE(strainGaugeLength(4.0, 5, 16), 0);
}

void TestSpatialResolution::a_subregion_the_engine_would_reach_outside_states_no_length()
{
    // 5 px at a 5 px step holds 5 points; asked for 9, the engine fetches the
    // nearest 9 from wherever they are, so the subregion is not what is
    // averaged over and its length must not be stated as though it were.
    const QString note = strainResolutionNote(5.0, 5, 16, 9);
    QVERIFY2(!note.contains(QLatin1String("43 px")), qPrintable(note));
    QVERIFY2(note.contains(QLatin1String("outside")), qPrintable(note));

    const QString enough = strainResolutionNote(5.0, 5, 16, 5);
    QVERIFY2(enough.contains(QLatin1String("43 px")), qPrintable(enough));
}

void TestSpatialResolution::the_note_says_the_length_is_averaged_over_not_resolved()
{
    const QString displacement = displacementResolutionNote(16);
    QVERIFY2(displacement.contains(QLatin1String("33 px")), qPrintable(displacement));
    QVERIFY2(displacement.contains(QLatin1String("not the smallest")),
             qPrintable(displacement));

    const QString strain = strainResolutionNote(25.0, 5, 16, 5);
    QVERIFY2(strain.contains(QLatin1String("83 px")), qPrintable(strain));
    QVERIFY2(strain.contains(QLatin1String("50 px")), qPrintable(strain));
    QVERIFY2(strain.contains(QLatin1String("not the smallest")), qPrintable(strain));
}

void TestSpatialResolution::nonsense_settings_state_no_length_rather_than_a_wrong_one()
{
    QCOMPARE(displacementAveragingLength(0), 0);
    QCOMPARE(displacementAveragingLength(-4), 0);
    QCOMPARE(strainGaugeLength(25.0, 0, 16), 0);
    QCOMPARE(strainGaugeLength(-1.0, 5, 16), 0);
    QCOMPARE(strainGaugeLength(25.0, 5, 0), 0);
    QVERIFY(displacementResolutionNote(0).isEmpty());
    QVERIFY(strainResolutionNote(25.0, 0, 16, 5).isEmpty());
}

QTEST_GUILESS_MAIN(TestSpatialResolution)
#include "test_spatial_resolution.moc"
