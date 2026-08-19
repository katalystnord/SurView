// Strain, measured against a case whose answer is known from physics rather
// than from the code: a rigid translation.
//
// WHY THIS FIXTURE. The pair is the reference displaced by exactly +3 px in x
// and 0 in y. Every point moves by the same amount, so the displacement
// GRADIENT is zero everywhere, so the strain is zero everywhere -- in both the
// Cauchy and the Green-Lagrange form, which agree exactly when the gradients
// are zero. Nothing about that answer comes from SurView or from the engine, so
// a fit that transposes exx with eyy, loses a factor of two on the shear, or
// quietly reports the displacement itself as a strain, is caught here.
//
// It is deliberately the same fixture the solver suite uses. A strain field is
// built on top of a displacement field, and pinning both to one pair means a
// strain failure can be told apart from a displacement failure by running the
// other suite.
//
// NEGATIVE CHECK (2026-08-19), both watched red before being trusted:
//   - the fit made to report the displacement instead of its gradient:
//     `a_rigid_translation_is_not_a_strain` failed with "exx reached 3", which
//     is the mean u in px. Exactly the confusion the fixture exists to catch.
//   - the not-a-number sentinel replaced by zero, so an unfitted point reads as
//     unstrained: `a_point_the_fit_could_not_reach_is_marked_not_zeroed` failed.
//     The sentinel is load-bearing, not decoration.
// The measures are walked from offeredStrainMeasures() rather than listed, so a
// measure cannot be offered to a user without arriving here.

#include "core/Correlation.h"
#include "core/FieldLayout.h"
#include "core/Roi.h"
#include "core/StrainFit.h"

#include <QTest>

#include <cmath>

namespace {

QString fixture(const QString &name)
{
    return QStringLiteral(SURVIEW_TEST_FIXTURES "/") + name;
}

CorrelationSettings baseSettings()
{
    CorrelationSettings settings;
    settings.solver = CorrelationSettings::ICGN;
    settings.shapeOrder = 1;
    settings.subsetRadius = 16;
    settings.gridStep = 12;
    settings.maxIterations = 15;
    settings.convergence = 0.001;

    settings.strainEnabled = true;
    // Three grid steps across, which holds 29 points at this spacing -- an
    // ordinary choice, not one tuned until the test passed.
    settings.strainRadius = 36.0;
    settings.strainMinPoints = 6;
    return settings;
}

CorrelationResult runOnce(const CorrelationSettings &settings)
{
    CorrelationRunner runner(settings, RegionOfInterest(),
                             fixture(QStringLiteral("shift_reference.tif")),
                             fixture(QStringLiteral("shift_target.tif")));

    CorrelationResult result;
    QString failure;
    QObject::connect(&runner, &CorrelationRunner::finished,
                     [&result](const CorrelationResult &r) { result = r; });
    QObject::connect(&runner, &CorrelationRunner::failed,
                     [&failure](const QString &reason) { failure = reason; });
    runner.run();

    if (!failure.isEmpty())
        qWarning("%s", qPrintable(failure));
    return result;
}

// Largest absolute value of a channel over every point that carries one.
double worst(const CorrelationResult &result, FieldChannel channel)
{
    double lowest = 0.0;
    double highest = 0.0;
    if (!fieldValueRange(result, channel, lowest, highest))
        return std::numeric_limits<double>::quiet_NaN();
    return std::max(std::abs(lowest), std::abs(highest));
}

}  // namespace

class TestStrainMeasure : public QObject
{
    Q_OBJECT

private slots:
    void a_rigid_translation_is_not_a_strain();
    void the_translation_it_is_not_a_strain_of_was_measured_all_the_same();
    void turning_strain_off_leaves_no_strain_field_rather_than_a_field_of_zeros();
    void a_point_the_fit_could_not_reach_is_marked_not_zeroed();
    void the_run_reports_the_subregion_and_measure_it_actually_used();
};

void TestStrainMeasure::a_rigid_translation_is_not_a_strain()
{
    // WHAT THIS DOES NOT CATCH: with the engine's approximation left at its
    // default instead of following the chosen measure, this still passes. Both
    // forms give zero over a rigid translation -- which is the property that
    // makes one fixture serve both, and the reason the measure a run used is
    // pinned separately, in the_run_reports_the_subregion_and_measure_it_
    // actually_used below.
    for (const StrainMeasureChoice &choice : offeredStrainMeasures()) {
        CorrelationSettings settings = baseSettings();
        settings.strainMeasure = choice.measure;

        const CorrelationResult result = runOnce(settings);
        QVERIFY2(result.hasStrain(),
                 qPrintable(QStringLiteral("%1 fitted no strain at all")
                                .arg(choice.name)));

        // 1e-4 is 100 microstrain. The bound was set by measurement, not by
        // taste: tightened until it failed, the fit reaches 6.4e-8 here, so
        // this leaves three orders of margin for a float solve that lands
        // slightly differently on another compiler while still being 50 times
        // finer than the smallest strain anyone would report.
        for (FieldChannel channel : {FieldChannel::StrainXX,
                                     FieldChannel::StrainYY,
                                     FieldChannel::StrainXY}) {
            const double reach = worst(result, channel);
            QVERIFY2(reach < 1e-4,
                     qPrintable(QStringLiteral("%1: %2 reached %3 over a rigid "
                                               "translation, which has none")
                                    .arg(choice.name)
                                    .arg(fieldChannelName(channel))
                                    .arg(reach)));
        }
    }
}

void TestStrainMeasure::the_translation_it_is_not_a_strain_of_was_measured_all_the_same()
{
    // Guards the way this test could pass while measuring nothing: a run that
    // solved no displacement would also report no strain, and every bound
    // above would hold vacuously.
    const CorrelationResult result = runOnce(baseSettings());

    QVERIFY2(result.converged > 100,
             qPrintable(QStringLiteral("only %1 points solved").arg(result.converged)));
    QVERIFY2(result.strainFitted > 100,
             qPrintable(QStringLiteral("only %1 points got a strain fit")
                            .arg(result.strainFitted)));

    double lowest = 0.0;
    double highest = 0.0;
    QVERIFY(fieldValueRange(result, FieldChannel::DisplacementX, lowest, highest));
    QVERIFY2(lowest > 2.9 && highest < 3.1,
             qPrintable(QStringLiteral("u ran from %1 to %2, expected 3 throughout")
                            .arg(lowest)
                            .arg(highest)));
}

void TestStrainMeasure::turning_strain_off_leaves_no_strain_field_rather_than_a_field_of_zeros()
{
    // NEGATIVE CHECK: watched fail against an earlier version that ran the fit
    // unconditionally -- hasStrain() came back true with the setting off.
    CorrelationSettings settings = baseSettings();
    settings.strainEnabled = false;

    const CorrelationResult result = runOnce(settings);

    QVERIFY(result.converged > 100);          // the displacement still happened
    QVERIFY(!result.hasStrain());
    QVERIFY(!result.strainRequested);
    QCOMPARE(result.strainFitted, 0);

    // And the channel reads as absent, not as a measurement of no strain.
    for (const CorrelationPoint &point : result.points)
        QVERIFY(!point.strainFitted);
    const QVector<float> values = layoutField(result, FieldChannel::StrainXX);
    for (float value : values)
        QVERIFY(std::isnan(value));
}

void TestStrainMeasure::a_point_the_fit_could_not_reach_is_marked_not_zeroed()
{
    // A subregion far too small for the minimum forces the engine's own
    // fallback, and a minimum larger than the whole grid defeats even that --
    // which is the case that must come back marked rather than zeroed.
    CorrelationSettings settings = baseSettings();
    settings.strainRadius = 1.0;
    settings.strainMinPoints = 100000;

    // The condition is visible before the run, which is the point of the
    // warning existing at all.
    QVERIFY(!settings.strainWarning().isEmpty());

    const CorrelationResult result = runOnce(settings);

    QVERIFY(result.strainRequested);
    QCOMPARE(result.strainFitted, 0);
    QVERIFY(!result.hasStrain());
    for (const CorrelationPoint &point : result.points) {
        QVERIFY(!point.strainFitted);
        // Not merely unfitted: it must not have been left holding a value that
        // would read as a measurement if the flag were ever dropped.
        QVERIFY(point.exx == 0.f);
    }
}

void TestStrainMeasure::the_run_reports_the_subregion_and_measure_it_actually_used()
{
    // The neighbourhood a strain was fitted over is part of what the number
    // means. A result that carried the field but not the fit that produced it
    // could not be reported honestly, let alone exported.
    CorrelationSettings settings = baseSettings();
    settings.strainMeasure = StrainMeasure::GreenLagrange;
    settings.strainRadius = 30.0;

    const CorrelationResult result = runOnce(settings);

    QVERIFY(result.strainRequested);
    QCOMPARE(result.strainRadius, 30.0);
    QVERIFY(result.strainMeasure == StrainMeasure::GreenLagrange);
}

QTEST_MAIN(TestStrainMeasure)
#include "test_strain_measure.moc"
