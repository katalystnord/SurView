// What SurView measures, against the answer the example carries.
//
// WHY THIS EXISTS. examples/synthetic states the exact deformation of every
// frame it ships, and nothing checked a run against it. Solve counts were
// checked and accuracy was not, so the suite could tell you that 10,733 points
// converged without anything at all asserting that what they converged ON was
// right.
//
// ⚑ The answer here is exact by construction, not accurate to within a warp.
// The generator renders each frame from an analytic pattern of Gaussian blobs
// under a uniform deformation gradient, which maps a Gaussian to a Gaussian
// exactly, so no pixel is ever resampled and the displacement in
// ground_truth.json is the truth rather than a good estimate of it. That is
// what makes a tolerance here meaningful.
//
// These are slow by nature: each case is a real correlation by the real engine.
// They earn it by being the only tests in the suite that can catch a solver
// that converges confidently on the wrong number.
//
// NEGATIVE CHECK (2026-09-01): three breaks in the path between the engine and
// the result, each reverted after.
//   - u and v swapped on their way out: the translation case went red.
//   - exx and eyy transposed: the tension case went red.
//   - the displacement reported as the strain: both strain cases went red,
//     which is the confusion these fixtures exist to catch.
//
// WHAT THIS DOES NOT CATCH, stated because it looks like coverage and is not:
// at these strains the Cauchy and Green-Lagrange forms agree to well within the
// tolerances here, so a run that ignored the chosen strain measure would pass
// all four. That property is pinned separately, by the run reporting which
// measure it actually used.

#include "core/Correlation.h"
#include "core/Roi.h"
#include "core/Series.h"
#include "core/StrainFit.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <cmath>

namespace
{

QString example(const QString &name)
{
    return QStringLiteral(SURVIEW_EXAMPLES "/synthetic/") + name;
}

// The stated answer for one frame, read from the file that ships beside it.
// Read rather than restated here: a copy of the expected numbers in the test
// would be a second source of truth, and the two would drift.
QJsonObject statedAnswer(const QString &set, int frame)
{
    QFile file(QStringLiteral(SURVIEW_EXAMPLES "/synthetic/ground_truth.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray frames =
        root[QStringLiteral("sets")].toObject()[set].toObject()[QStringLiteral("frames")].toArray();
    return frames.at(frame).toObject();
}

CorrelationSettings settings()
{
    CorrelationSettings s;
    s.solver = CorrelationSettings::ICGN;
    s.shapeOrder = 1;
    s.subsetRadius = 16;
    // Coarse on purpose. These cases measure whether the answer is right, not
    // how dense a field can be, and a fine grid would only make them slow.
    s.gridStep = 24;
    s.maxIterations = 15;
    s.convergence = 0.001;
    s.strainEnabled = true;
    s.strainRadius = 72.0;
    s.strainMinPoints = 6;
    return s;
}

CorrelationResult measure(const QString &reference, const QString &target,
                          CorrelationSettings use = settings())
{
    CorrelationRunner runner(use, RegionOfInterest(),
                             example(reference), example(target));
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

struct Mean
{
    double u = 0.0, v = 0.0, exx = 0.0, eyy = 0.0, exy = 0.0;
    int solved = 0, fitted = 0;
};

// Averaged over the interior only. Points within one strain subregion of the
// picture's edge are fitted from a lopsided neighbourhood, which is a genuine
// property of the method and not the thing under test here.
Mean interiorMean(const CorrelationResult &result, double margin)
{
    Mean mean;
    for (const CorrelationPoint &point : result.points) {
        if (!point.converged)
            continue;
        mean.solved++;
        mean.u += point.u;
        mean.v += point.v;

        if (!point.strainFitted)
            continue;
        if (point.x < margin || point.y < margin
            || point.x > 640.0 - margin || point.y > 480.0 - margin) {
            continue;
        }
        mean.fitted++;
        mean.exx += point.exx;
        mean.eyy += point.eyy;
        mean.exy += point.exy;
    }
    if (mean.solved > 0) {
        mean.u /= mean.solved;
        mean.v /= mean.solved;
    }
    if (mean.fitted > 0) {
        mean.exx /= mean.fitted;
        mean.eyy /= mean.fitted;
        mean.exy /= mean.fitted;
    }
    return mean;
}

}  // namespace

class TestMeasuredAccuracy : public QObject
{
    Q_OBJECT

private slots:
    void the_examples_state_an_answer_this_suite_can_read();
    void a_known_sub_pixel_translation_is_measured_to_a_hundredth_of_a_pixel();
    void a_known_uniaxial_tension_is_measured_as_the_strain_it_was_given();
    void a_known_rigid_rotation_is_measured_as_no_strain_at_all();
    void points_recovered_by_the_second_pass_measure_the_right_displacement();
    void the_second_pass_measures_far_more_of_a_hard_frame();
    void a_virtual_extensometer_reads_the_strain_the_sequence_was_given();
};

void TestMeasuredAccuracy::the_examples_state_an_answer_this_suite_can_read()
{
    // If this fails, every case below is vacuous: they would be comparing a
    // measurement against a default-constructed zero.
    const QJsonObject frame = statedAnswer(QStringLiteral("translation"), 3);
    QVERIFY2(!frame.isEmpty(), "ground_truth.json could not be read");
    QVERIFY(frame.contains(QStringLiteral("deformation_gradient")));

    const QJsonArray shift = frame[QStringLiteral("rigid_shift_px")].toArray();
    QCOMPARE(shift.size(), 2);
    QCOMPARE(shift.at(0).toDouble(), 1.0);
}

void TestMeasuredAccuracy::a_known_sub_pixel_translation_is_measured_to_a_hundredth_of_a_pixel()
{
    // translation_03 is the reference moved by exactly 1.0 px in x. Every point
    // moves by the same amount, so the mean IS the measurement and the spread
    // around it is the noise on it.
    const QJsonObject stated = statedAnswer(QStringLiteral("translation"), 3);
    const double expected =
        stated[QStringLiteral("rigid_shift_px")].toArray().at(0).toDouble();
    QVERIFY(expected > 0.0);

    const CorrelationResult result =
        measure(QStringLiteral("translation_00.tif"), QStringLiteral("translation_03.tif"));
    QVERIFY2(result.converged > 100,
             qPrintable(QStringLiteral("only %1 points solved").arg(result.converged)));

    const Mean mean = interiorMean(result, 0.0);
    QVERIFY2(std::abs(mean.u - expected) < 0.01,
             qPrintable(QStringLiteral("measured u %1 against a stated %2")
                            .arg(mean.u).arg(expected)));
    QVERIFY2(std::abs(mean.v) < 0.01,
             qPrintable(QStringLiteral("measured v %1 where the specimen did not "
                                       "move in y at all").arg(mean.v)));
}

void TestMeasuredAccuracy::a_known_uniaxial_tension_is_measured_as_the_strain_it_was_given()
{
    // The deformation gradient states the strain outright: F = diag(1+e, 1-ve),
    // so exx is F00 - 1 and eyy is F11 - 1 in the small-strain form the run is
    // using. Taken from the file rather than written here, so the test cannot
    // drift from the data it ships against.
    const QJsonObject stated = statedAnswer(QStringLiteral("tension"), 3);
    const QJsonArray F = stated[QStringLiteral("deformation_gradient")].toArray();
    const double expectedXX = F.at(0).toArray().at(0).toDouble() - 1.0;
    const double expectedYY = F.at(1).toArray().at(1).toDouble() - 1.0;
    QVERIFY(expectedXX > 0.0);
    QVERIFY(expectedYY < 0.0);

    const CorrelationResult result =
        measure(QStringLiteral("tension_00.tif"), QStringLiteral("tension_03.tif"));
    QVERIFY2(result.strainFitted > 50,
             qPrintable(QStringLiteral("strain fitted at only %1 points")
                            .arg(result.strainFitted)));

    const Mean mean = interiorMean(result, 80.0);
    QVERIFY2(mean.fitted > 20, "too few interior points to average");

    // A tenth of the strain itself. Loose enough that this is not a regression
    // pin on today's solver, tight enough that a factor of two, a transposition
    // or a lost Poisson term cannot pass.
    QVERIFY2(std::abs(mean.exx - expectedXX) < 0.1 * expectedXX,
             qPrintable(QStringLiteral("measured exx %1 against a stated %2")
                            .arg(mean.exx).arg(expectedXX)));
    QVERIFY2(std::abs(mean.eyy - expectedYY) < 0.1 * std::abs(expectedYY),
             qPrintable(QStringLiteral("measured eyy %1 against a stated %2")
                            .arg(mean.eyy).arg(expectedYY)));
    QVERIFY2(std::abs(mean.exy) < 0.1 * expectedXX,
             qPrintable(QStringLiteral("measured a shear of %1 in a set that has "
                                       "none").arg(mean.exy)));
}

// The displacement the stated deformation puts at one reference pixel. Derived
// from the frame's own deformation gradient and rigid shift rather than
// restated, for the reason statedAnswer() is read rather than copied.
void statedDisplacementAt(const QJsonObject &stated, double x, double y,
                          double &u, double &v)
{
    const QJsonArray f = stated[QStringLiteral("deformation_gradient")].toArray();
    const QJsonArray shift = stated[QStringLiteral("rigid_shift_px")].toArray();

    const double f00 = f.at(0).toArray().at(0).toDouble();
    const double f01 = f.at(0).toArray().at(1).toDouble();
    const double f10 = f.at(1).toArray().at(0).toDouble();
    const double f11 = f.at(1).toArray().at(1).toDouble();

    u = f00 * x + f01 * y + shift.at(0).toDouble() - x;
    v = f10 * x + f11 * y + shift.at(1).toDouble() - y;
}

void TestMeasuredAccuracy::a_known_rigid_rotation_is_measured_as_no_strain_at_all()
{
    // ⚑ The case worth having most. The specimen is turned, not deformed, so
    // the correct strain is zero everywhere at every step, and a measure that
    // reports rotation as strain is wrong in a way that looks entirely
    // plausible on a colour map. rotation_01 is one degree: enough movement
    // that a spurious strain would be visible, small enough that a
    // first-order shape function tracks it comfortably.
    const QJsonObject stated = statedAnswer(QStringLiteral("rotation"), 1);
    QCOMPARE(stated[QStringLiteral("amount")].toDouble(), 1.0);

    const CorrelationResult result =
        measure(QStringLiteral("rotation_00.tif"), QStringLiteral("rotation_01.tif"));
    QVERIFY2(result.strainFitted > 50,
             qPrintable(QStringLiteral("strain fitted at only %1 points")
                            .arg(result.strainFitted)));

    const Mean mean = interiorMean(result, 80.0);
    QVERIFY2(mean.fitted > 20, "too few interior points to average");

    // One degree of rotation carries displacements of several pixels across the
    // frame, so a fit that mistook rotation for strain would read around 0.017
    // (the sine of the angle), a thousand times this bound.
    for (const auto &named : {std::pair<const char *, double>{"exx", mean.exx},
                              {"eyy", mean.eyy},
                              {"exy", mean.exy}}) {
        QVERIFY2(std::abs(named.second) < 2e-4,
                 qPrintable(QStringLiteral("%1 reached %2 on a specimen that was "
                                           "only turned")
                                .arg(QString::fromLatin1(named.first))
                                .arg(named.second)));
    }
}

void TestMeasuredAccuracy::points_recovered_by_the_second_pass_measure_the_right_displacement()
{
    // ⚑ THE CASE THIS FEATURE MOST NEEDS. A recovered point starts from a
    // displacement BORROWED FROM ITS NEIGHBOURS, so the failure that matters is
    // not that it fails -- a failure is honest and stays in the report -- but
    // that it converges confidently on a plausible wrong answer inherited from
    // the fit. Nothing in a correlation coefficient can see that. Only an
    // exactly known answer can, which is what these fixtures are for.
    //
    // 7 degrees of rotation is far enough that the first pass loses most of the
    // field, so the great majority of what is measured here was recovered.
    const QJsonObject stated = statedAnswer(QStringLiteral("rotation"), 3);
    QCOMPARE(stated[QStringLiteral("amount")].toDouble(), 7.0);

    const CorrelationResult result =
        measure(QStringLiteral("rotation_00.tif"), QStringLiteral("rotation_03.tif"));

    int recovered = 0;
    double worst = 0.0;
    double worstAt = 0.0;
    for (const CorrelationPoint &point : result.points) {
        if (!point.recovered || !point.converged)
            continue;
        recovered++;

        double u = 0.0;
        double v = 0.0;
        statedDisplacementAt(stated, double(point.x), double(point.y), u, v);
        const double error = std::hypot(double(point.u) - u, double(point.v) - v);
        if (error > worst) {
            worst = error;
            worstAt = double(point.x);
        }
    }

    QVERIFY2(recovered > 100,
             qPrintable(QStringLiteral("only %1 points were recovered, too few "
                                       "for this case to mean anything")
                            .arg(recovered)));

    // A tenth of a pixel over displacements reaching 48 px.
    //
    // ⚑ WHAT THIS BOUND DOES NOT CATCH, said plainly because it looks like it
    // should. It does NOT catch a fitted value reported without being
    // re-solved: on a smoothly rotating field the affine fit is itself accurate
    // to well inside this bound, so displacement error cannot tell an
    // interpolation from a measurement here. Verified by negative check, which
    // deleted the re-solve and left every case in this file green. What catches
    // that is a_fitted_value_that_was_never_re_solved_is_never_accepted in
    // tests/test_recovery.cpp, which asks about the correlation instead --
    // RegionFit2D zeroes it on purpose to say the answer was borrowed.
    //
    // What this bound DOES catch is the other half, and the half no unit test
    // can reach: a point that genuinely re-solved but converged on the wrong
    // answer, plausibly and confidently, having been walked there by its
    // neighbours.
    QVERIFY2(worst < 0.1,
             qPrintable(QStringLiteral("a recovered point was %1 px from the "
                                       "stated displacement (near x = %2). A "
                                       "recovered point must be MEASURED, not "
                                       "inherited.")
                            .arg(worst).arg(worstAt)));
}

void TestMeasuredAccuracy::the_second_pass_measures_far_more_of_a_hard_frame()
{
    // The claim the feature is for, pinned against the same frame both ways, so
    // a change that quietly makes the pass inert is caught. Reference updating
    // had exactly that failure and nothing noticed until a sequence was run by
    // hand.
    CorrelationSettings off = settings();
    off.recovery.enabled = false;

    const CorrelationResult without =
        measure(QStringLiteral("rotation_00.tif"), QStringLiteral("rotation_03.tif"),
                off);
    const CorrelationResult with =
        measure(QStringLiteral("rotation_00.tif"), QStringLiteral("rotation_03.tif"));

    QVERIFY2(with.converged > without.converged * 2,
             qPrintable(QStringLiteral("the pass measured %1 points where the "
                                       "solver alone measured %2; it is barely "
                                       "doing anything")
                            .arg(with.converged).arg(without.converged)));
    QVERIFY2(!without.recoveryRequested && with.recoveryRequested,
             "the result must say whether the pass was asked for");
}

void TestMeasuredAccuracy::a_virtual_extensometer_reads_the_strain_the_sequence_was_given()
{
    // ⚑ The whole point of a loading curve is the NUMBER on its y axis, and
    // nothing else in the suite checks that a gauge reads the right one. An
    // extensometer is a derived quantity twice over -- interpolated between
    // grid points, then differenced -- so there is plenty of room for it to
    // produce a smooth, plausible, wrong curve.
    //
    // The tension set states an exact strain per frame, so the answer is known
    // rather than estimated. The gauge is laid along x, which is the direction
    // the specimen is stretched in.
    QVector<CorrelationResult> frames;
    QVector<double> stated;
    for (int frame = 1; frame <= 4; frame++) {
        frames.append(measure(QStringLiteral("tension_00.tif"),
                              QStringLiteral("tension_0%1.tif").arg(frame)));
        const QJsonObject answer = statedAnswer(QStringLiteral("tension"), frame);
        stated.append(
            answer[QStringLiteral("deformation_gradient")].toArray()
                .at(0).toArray().at(0).toDouble() - 1.0);
    }

    Extensometer gauge;
    gauge.name = QStringLiteral("E1");
    gauge.ax = 160.0;
    gauge.ay = 240.0;
    gauge.bx = 480.0;
    gauge.by = 240.0;

    const Series series =
        extensometerSeries(gauge, frames, ExtensometerQuantity::Strain);
    QCOMPARE(series.points.size(), 4);
    QCOMPARE(series.measuredCount(), 4);

    for (int i = 0; i < 4; i++) {
        const double read = series.points.at(i).value;
        QVERIFY2(std::abs(read - stated.at(i)) < 5e-4,
                 qPrintable(QStringLiteral("frame %1: gauge read %2, the set "
                                           "states %3")
                                .arg(i + 1).arg(read).arg(stated.at(i))));
    }

    // And it RISES, monotonically, because the specimen was loaded
    // monotonically. A curve that happens to hit four right values while
    // wandering between them is not a loading curve.
    for (int i = 1; i < 4; i++) {
        QVERIFY2(series.points.at(i).value > series.points.at(i - 1).value,
                 "the loading curve is not monotonic on a monotonic load");
    }
}

QTEST_MAIN(TestMeasuredAccuracy)
#include "test_measured_accuracy.moc"
