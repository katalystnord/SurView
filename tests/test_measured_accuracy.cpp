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

CorrelationResult measure(const QString &reference, const QString &target)
{
    CorrelationRunner runner(settings(), RegionOfInterest(),
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

QTEST_MAIN(TestMeasuredAccuracy)
#include "test_measured_accuracy.moc"
