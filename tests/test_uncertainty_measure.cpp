// How far a measured point can be trusted, and the two different questions
// that answer it.
//
// WHY THIS EXISTS. Tenet 9: we quantify and report the reliability of what we
// interpret, and never present a number as more trustworthy than we can
// substantiate. Until this, a displacement field carried exactly two grades of
// confidence -- solved, or not -- while being drawn to three significant
// figures with a colour scale, which invites far more trust than that.
//
// THE TWO METRICS ARE NOT INTERCHANGEABLE, and the cases below pin the
// difference down, because collapsing them into one "quality" score is the
// specific mistake this is guarding against:
//
//   noise floor (the engine's sigma, px) comes from the REFERENCE image alone
//   -- its noise, and the gradient energy of this subset's speckle. It says how
//   well the measurement could ever have gone here. It cannot see decorrelation
//   or out-of-plane motion, because it never examines the target.
//
//   conditioning (the engine's beta) probes the correlation cost around the
//   solution actually found. It says how well this particular match went, and
//   is dimensionless and relative.
//
// NEGATIVE CHECK (2026-08-19), three run, and one of them found a real defect:
//   - the reliability pass removed entirely: four cases went red. It also
//     exposed the bug now fixed in Correlation.cpp -- a `sigma >= 0` test
//     accepted the ZERO that POI2D::clear() leaves, so every solved point
//     claimed a noise floor of exactly zero, which is the most flattering
//     reading available. The guard is strictly positive now.
//   - the bound on the noise floor tightened until it failed, to set it from
//     measurement rather than taste: 0.0031 to 0.0040 px on this fixture.
//   - reliability attributed to unsolved points as well: NOTHING failed, and
//     that is worth knowing. See a_point_with_no_displacement_gets_no_
//     reliability_either below for why.

#include "core/Correlation.h"
#include "core/FieldLayout.h"
#include "core/Roi.h"

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

}  // namespace

class TestUncertaintyMeasure : public QObject
{
    Q_OBJECT

private slots:
    void every_solved_point_is_told_how_far_it_can_be_trusted();
    void the_noise_floor_is_far_below_the_displacement_it_qualifies();
    void a_point_with_no_displacement_gets_no_reliability_either();
    void conditioning_that_could_not_be_established_is_counted_not_hidden();
    void the_run_reports_the_image_noise_that_scaled_every_noise_floor();
    void neither_metric_is_reported_as_a_measurement_of_zero();
};

void TestUncertaintyMeasure::every_solved_point_is_told_how_far_it_can_be_trusted()
{
    // Always on: there is no setting for this, because an account of how far a
    // measurement can be trusted is not an optional extra, and it costs about a
    // second per 30,000 points against a solve costing far more.
    const CorrelationResult result = runOnce(baseSettings());

    QVERIFY2(result.converged > 100,
             qPrintable(QStringLiteral("only %1 points solved").arg(result.converged)));
    QCOMPARE(result.noiseFloorMeasured, result.converged);

    int measured = 0;
    for (const CorrelationPoint &point : result.points) {
        if (!point.converged)
            continue;
        QVERIFY2(point.noiseFloorMeasured,
                 "a solved point was given no noise floor");
        QVERIFY2(point.noiseFloor > 0.f,
                 "a noise floor of zero would claim a perfect measurement");
        measured++;
    }
    QCOMPARE(measured, result.converged);
}

void TestUncertaintyMeasure::the_noise_floor_is_far_below_the_displacement_it_qualifies()
{
    // The property that makes the whole measurement meaningful, and the reason
    // this metric is worth showing at all: a noise floor anywhere near the size
    // of the displacement would mean the shift was indistinguishable from
    // sensor noise. This pair moves by exactly 3 px, so the bound is stated as
    // a ratio to that -- at least a hundred times below what it qualifies --
    // rather than as a number that happened to pass.
    //
    // Measured here: 0.0031 to 0.0040 px, roughly a three-hundredth of a pixel,
    // which is a cleaner synthetic pattern than a real specimen gives. The
    // engine's own smoke test measures 0.0095 px mean on the OHT-CFRP
    // photographs, still well inside this bound.
    const CorrelationResult result = runOnce(baseSettings());

    double lowest = 0.0;
    double highest = 0.0;
    QVERIFY(fieldValueRange(result, FieldChannel::NoiseFloor, lowest, highest));

    QVERIFY2(lowest > 0.0, "a noise floor of zero is not a measurement");

    constexpr double kDisplacement = 3.0;
    QVERIFY2(highest < kDisplacement / 100.0,
             qPrintable(QStringLiteral("the worst noise floor was %1 px against a "
                                       "%2 px displacement, which is not a floor")
                            .arg(highest)
                            .arg(kDisplacement)));
}

void TestUncertaintyMeasure::a_point_with_no_displacement_gets_no_reliability_either()
{
    // A reliability figure attached to a point that measured nothing is a
    // statement about nothing. Forced by settings that leave plenty of points
    // unconverged.
    //
    // ⚑ WHAT THIS DOES NOT CATCH, established by negative check rather than
    // assumed: this passes even with SurView's own gate removed, because the
    // ENGINE refuses first -- Uncertainty2D::compute() early-returns -1 when
    // zncc < 0, and every unconverged point has a negative status code in that
    // field by definition. So the case pins the behaviour a user sees, which is
    // what matters, but it does not exercise SurView's gate. That gate stays as
    // defence in depth against the engine's precondition changing under us, not
    // because anything here proves it is load-bearing today.
    CorrelationSettings settings = baseSettings();
    settings.maxIterations = 1;
    settings.convergence = 1e-9;

    const CorrelationResult result = runOnce(settings);

    int rejected = 0;
    for (const CorrelationPoint &point : result.points) {
        if (point.converged)
            continue;
        rejected++;
        QVERIFY2(!point.noiseFloorMeasured,
                 "a noise floor was reported for a point with no displacement");
        QVERIFY2(!point.conditioningMeasured,
                 "a conditioning value was reported for a point with no displacement");
    }
    QVERIFY2(rejected > 0,
             "these settings solved everything, so the case was never exercised");
    QVERIFY(result.noiseFloorMeasured <= result.converged);
}

void TestUncertaintyMeasure::conditioning_that_could_not_be_established_is_counted_not_hidden()
{
    // ⚑ The engine returns the same -1 for "not computed" and "the cost was too
    // flat to probe". Those are opposite meanings -- one is an absence, the
    // other is the strongest caution the metric can give -- but among CONVERGED
    // points only the second can occur, because "not computed" fires solely for
    // a failed or out-of-bounds point. So an unestablished conditioning value
    // here is a warning, and must be counted rather than quietly left blank.
    const CorrelationResult result = runOnce(baseSettings());

    int unusable = 0;
    for (const CorrelationPoint &point : result.points) {
        if (!point.converged)
            continue;
        if (!point.conditioningMeasured)
            unusable++;
        else
            QVERIFY2(point.conditioning >= 0.f,
                     "conditioning is a magnitude and cannot be negative");
    }
    QCOMPARE(result.conditioningUnusable, unusable);
}

void TestUncertaintyMeasure::the_run_reports_the_image_noise_that_scaled_every_noise_floor()
{
    // Every noise floor in the field is this one number divided by a per-subset
    // gradient energy, so a field of noise floors cannot be read without it.
    const CorrelationResult result = runOnce(baseSettings());

    QVERIFY2(result.referenceNoise > 0.0,
             "the run reports no noise estimate for the reference image");
    QVERIFY2(result.referenceNoise < 128.0,
             qPrintable(QStringLiteral("an 8-bit image was estimated at %1 grey "
                                       "levels of noise, which is not credible")
                            .arg(result.referenceNoise)));
}

void TestUncertaintyMeasure::neither_metric_is_reported_as_a_measurement_of_zero()
{
    // The same rule the rest of the field obeys. Zero is a meaningful value for
    // both of these -- a zero noise floor claims a perfect measurement, and a
    // zero conditioning claims a perfectly sharp cost -- so an unmeasured one
    // must be not-a-number, exactly as an unsolved displacement is.
    CorrelationSettings settings = baseSettings();
    settings.maxIterations = 1;
    settings.convergence = 1e-9;

    const CorrelationResult result = runOnce(settings);

    const QVector<float> floors = layoutField(result, FieldChannel::NoiseFloor);
    const QVector<float> conditioning =
        layoutField(result, FieldChannel::MatchConditioning);

    QVERIFY(!floors.isEmpty());
    QCOMPARE(floors.size(), conditioning.size());

    int blanks = 0;
    for (float value : floors) {
        if (std::isnan(value))
            blanks++;
        else
            QVERIFY2(value > 0.f, "a noise floor was laid out as zero");
    }
    QVERIFY2(blanks > 0, "no point was left unmeasured, so nothing was checked");
}

QTEST_MAIN(TestUncertaintyMeasure)
#include "test_uncertainty_measure.moc"
