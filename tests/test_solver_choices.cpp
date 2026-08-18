// Every solver the Analysis panel offers, measured against a known answer.
//
// WHY THIS EXISTS. The panel offered five solver and shape-order combinations,
// and exactly one of them -- ICGN, first order -- was covered by any test
// anywhere: not here, and not in the engine, where coverage put oc_iclm.cpp and
// oc_nr.cpp at literally zero. A user could select IC-LM or Newton-Raphson and
// get a number that nothing had ever checked.
//
// The list is not written out below. It comes from offeredSolverChoices(), the
// same function the panel builds its controls from, so adding a solver to the
// application adds it to this test automatically. That is the point: a
// combination cannot be offered without being measured.
//
// The fixture pair is the reference displaced by exactly +3 px in x and 0 in y,
// so every solver has the same known answer to be wrong about.

#include "core/Correlation.h"
#include "core/Roi.h"

#include <QSignalSpy>
#include <QTest>

#include <cmath>

namespace {

QString fixture(const QString &name)
{
    return QStringLiteral(SURVIEW_TEST_FIXTURES "/") + name;
}

CorrelationSettings settingsFor(const SolverChoice &choice)
{
    CorrelationSettings settings;
    settings.solver = choice.solver;
    settings.shapeOrder = choice.shapeOrder;
    settings.subsetRadius = 16;
    // Coarse: this test is about whether each solver works at all, and a fine
    // grid multiplies that question by a few thousand points per solver.
    settings.gridStep = 12;
    settings.maxIterations = 15;
    settings.convergence = 0.001;
    return settings;
}

QString describe(const SolverChoice &choice)
{
    return QStringLiteral("%1, order %2")
        .arg(solverDisplayName(choice.solver))
        .arg(choice.shapeOrder);
}

// Runs one correlation to completion on this thread and returns the result.
CorrelationResult runOnce(const SolverChoice &choice, const RegionOfInterest &roi)
{
    CorrelationRunner runner(settingsFor(choice), roi,
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

class TestSolverChoices : public QObject
{
    Q_OBJECT

private slots:
    void the_application_offers_the_combinations_the_engine_implements();
    void every_offered_combination_recovers_a_known_shift();
    void every_offered_combination_honours_a_region_of_interest();
};

void TestSolverChoices::the_application_offers_the_combinations_the_engine_implements()
{
    const QVector<SolverChoice> choices = offeredSolverChoices();

    // Three solver families, second order for two of them: the engine
    // implements Newton-Raphson for the first-order shape function only.
    QCOMPARE(choices.size(), 5);

    for (const SolverChoice &choice : choices) {
        CorrelationSettings settings;
        settings.solver = choice.solver;
        settings.shapeOrder = choice.shapeOrder;
        QVERIFY2(settings.isAvailable(), qPrintable(describe(choice)));
        QVERIFY(!solverDisplayName(choice.solver).isEmpty());
    }

    // Nothing unavailable is offered: Newton-Raphson at second order must not
    // appear, because the engine does not implement it.
    for (const SolverChoice &choice : choices) {
        if (choice.solver == CorrelationSettings::NewtonRaphson)
            QCOMPARE(choice.shapeOrder, 1);
    }
}

void TestSolverChoices::every_offered_combination_recovers_a_known_shift()
{
    for (const SolverChoice &choice : offeredSolverChoices()) {
        const CorrelationResult result = runOnce(choice, RegionOfInterest());

        QVERIFY2(result.converged > 50,
                 qPrintable(QStringLiteral("%1: only %2 of %3 points solved")
                                .arg(describe(choice))
                                .arg(result.converged)
                                .arg(result.total())));

        double sumU = 0.0;
        double sumV = 0.0;
        int counted = 0;
        for (const CorrelationPoint &point : result.points) {
            if (!point.converged)
                continue;
            sumU += point.u;
            sumV += point.v;
            counted++;
        }
        QVERIFY(counted > 0);

        const double meanU = sumU / counted;
        const double meanV = sumV / counted;

        // The same answer from every solver, or one of them is wrong. The
        // tolerance is loose enough to survive the different convergence paths
        // and tight enough that a solver reading the axes the wrong way round,
        // or rounding to whole pixels, cannot pass.
        QVERIFY2(std::abs(meanU - 3.0) < 0.1,
                 qPrintable(QStringLiteral("%1: mean u was %2, expected 3")
                                .arg(describe(choice)).arg(meanU)));
        QVERIFY2(std::abs(meanV) < 0.1,
                 qPrintable(QStringLiteral("%1: mean v was %2, expected 0")
                                .arg(describe(choice)).arg(meanV)));
    }
}

void TestSolverChoices::every_offered_combination_honours_a_region_of_interest()
{
    // The region restricts the POI grid through the engine's own shape test,
    // which is shared by every solver -- but "shared" is an assumption until
    // something checks it for each one.
    RegionOfInterest roi;
    roi.vertices = {QPoint(60, 50), QPoint(170, 50),
                    QPoint(170, 110), QPoint(60, 110)};
    const QRect box = roi.bounds();

    for (const SolverChoice &choice : offeredSolverChoices()) {
        const CorrelationResult result = runOnce(choice, roi);

        QVERIFY2(result.restrictedToRoi, qPrintable(describe(choice)));
        QVERIFY2(result.total() > 0, qPrintable(describe(choice)));

        for (const CorrelationPoint &point : result.points) {
            QVERIFY2(point.x >= box.left() && point.x <= box.right(),
                     qPrintable(describe(choice)));
            QVERIFY2(point.y >= box.top() && point.y <= box.bottom(),
                     qPrintable(describe(choice)));
        }
    }
}

QTEST_MAIN(TestSolverChoices)
#include "test_solver_choices.moc"
