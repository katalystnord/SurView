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

// ⚑ AND THE GAP THIS FILE COULD NOT CLOSE UNTIL 2026-10-12. The two lines
// choosing between the first- and second-order solvers (ICGN2D1 against ICGN2D2,
// ICLM2D1 against ICLM2D2) could be INVERTED with the whole suite green: every
// user asking for a second-order shape function would have got a first-order one.
// Nothing here deformed in a way first order cannot describe -- every synthetic
// example is a uniform deformation gradient, and on the real photographs the two
// orders agree to within a thousandth of a correlation. The curved pair closes it;
// see second_order_measures_a_curved_field_that_first_order_cannot.

#include "core/Correlation.h"
#include "core/Roi.h"

#include <QSignalSpy>
#include "roi_helpers.h"

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
CorrelationResult runOnce(const SolverChoice &choice, const RegionOfInterest &roi,
                          const QString &reference = QStringLiteral("shift_reference.tif"),
                          const QString &target = QStringLiteral("shift_target.tif"),
                          int gridStep = 12)
{
    CorrelationSettings settings = settingsFor(choice);
    settings.gridStep = gridStep;
    CorrelationRunner runner(settings, roi, fixture(reference), fixture(target));

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
    void the_combination_that_is_not_offered_says_why_it_is_not();
    void every_offered_combination_recovers_a_known_shift();
    void every_offered_combination_honours_a_region_of_interest();
    void second_order_measures_a_curved_field_that_first_order_cannot();
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

void TestSolverChoices::the_combination_that_is_not_offered_says_why_it_is_not()
{
    // The case above asks what IS offered. Nothing asked what happens at the
    // one combination that is not, and the panel does not simply omit it: the
    // second-order entry stays in the shape list, disabled, carrying
    // unavailableReason() as its tooltip. That sentence is the only thing on
    // screen explaining why the option cannot be chosen, and it was written by
    // a function no test had ever called.
    //
    // ⚑ Found by the mutation sweep of 2026-09-09: three mutants lived in that
    // one condition -- refusing every second-order solver, refusing
    // Newton-Raphson at first order, and refusing on either half instead of
    // both -- and the suite watched all three go by. The reason a combination
    // is refused is checked from both sides here: the refused one carries a
    // reason, and every offered one carries none.
    CorrelationSettings refused;
    refused.solver = CorrelationSettings::NewtonRaphson;
    refused.shapeOrder = 2;

    QVERIFY2(!refused.isAvailable(),
             "Newton-Raphson at second order was offered, and the engine does "
             "not implement it");
    QVERIFY2(!refused.unavailableReason().isEmpty(),
             "an option the panel disables with nothing said about why");
    QVERIFY2(refused.unavailableReason().contains(QStringLiteral("Newton-Raphson")),
             qPrintable(refused.unavailableReason()));
    QVERIFY2(refused.unavailableReason().contains(QStringLiteral("first-order")),
             qPrintable(refused.unavailableReason()));

    // And the other side of it: a combination that IS offered must not be
    // explained away. A reason here would disable a control that works.
    for (const SolverChoice &choice : offeredSolverChoices()) {
        CorrelationSettings settings;
        settings.solver = choice.solver;
        settings.shapeOrder = choice.shapeOrder;
        QVERIFY2(settings.unavailableReason().isEmpty(),
                 qPrintable(QStringLiteral("%1 is offered and yet reports: %2")
                                .arg(describe(choice))
                                .arg(settings.unavailableReason())));
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
    setOutline(roi, {QPoint(60, 50), QPoint(170, 50),
                    QPoint(170, 110), QPoint(60, 110)});
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

namespace {

// The field tests/fixtures/curved_target.tif was rendered under, as
// tools/make-curved-fixture.py states it: quadratic about (80, 80).
QPointF curvedDisplacement(double x, double y)
{
    const double dx = x - 80.0;
    const double dy = y - 80.0;
    return QPointF(0.9 + 0.006 * dx - 0.004 * dy
                       + 0.0012 * dx * dx + 0.0007 * dx * dy + 0.0008 * dy * dy,
                   -0.6 + 0.003 * dx + 0.005 * dy
                       - 0.0006 * dx * dx + 0.0010 * dx * dy - 0.0011 * dy * dy);
}

}  // namespace

void TestSolverChoices::second_order_measures_a_curved_field_that_first_order_cannot()
{
    // The pair's displacement is QUADRATIC (tools/make-curved-fixture.py), which a
    // second-order shape function describes across a subset and a first-order one
    // cannot. A first-order fit to it is biased at the centre by the quadratic
    // terms averaged over the subset: r(r + 1) / 3 = 90.7 px^2 per squared term
    // for r = 16, so (0.0012 + 0.0008) * 90.7 = 0.181 px in u and
    // (-0.0006 - 0.0011) * 90.7 = -0.154 px in v, 0.238 px together. That is
    // theory, not this code, and the first-order solvers land on it (0.235), which
    // is what says the fixture is the field it claims to be. Second order: 0.0026.
    //
    // Negative-checked: with the order line in makeSolver() inverted for ICGN, and
    // separately for IC-LM, this case goes red -- the first-order choice then reads
    // 0.0026 px, nowhere near the bias a first-order fit must show. It is the case
    // the header of this file said was missing.
    RegionOfInterest roi;  // only where the fixture is stated: 40 px about the centre
    setOutline(roi, {QPoint(40, 40), QPoint(120, 40), QPoint(120, 120), QPoint(40, 120)});
    const double firstOrderBias = std::hypot((0.0012 + 0.0008) * 16 * 17 / 3.0,
                                             (-0.0006 - 0.0011) * 16 * 17 / 3.0);

    for (const SolverChoice &choice : offeredSolverChoices()) {
        const CorrelationResult result = runOnce(choice, roi,
                                                 QStringLiteral("curved_reference.tif"),
                                                 QStringLiteral("curved_target.tif"), 8);
        double sum = 0.0;
        int counted = 0;
        for (const CorrelationPoint &point : result.points) {
            if (!point.converged)
                continue;
            const QPointF truth = curvedDisplacement(point.x, point.y);
            sum += std::hypot(point.u - truth.x(), point.v - truth.y());
            counted++;
        }
        QVERIFY2(result.total() > 100 && counted == result.total(),
                 qPrintable(QStringLiteral("%1: %2 of %3 points solved")
                                .arg(describe(choice)).arg(counted).arg(result.total())));
        const double meanError = sum / counted;

        if (choice.shapeOrder == 2) {
            QVERIFY2(meanError < 0.01,
                     qPrintable(QStringLiteral("%1 asked for second order and measured the "
                                               "curved field %2 px off, as first order does")
                                    .arg(describe(choice)).arg(meanError)));
        } else {
            QVERIFY2(std::abs(meanError - firstOrderBias) < 0.2 * firstOrderBias,
                     qPrintable(QStringLiteral("%1: mean error %2 px, where a first-order fit "
                                               "to this field is biased by %3 px")
                                    .arg(describe(choice)).arg(meanError)
                                    .arg(firstOrderBias)));
        }
    }
}

QTEST_MAIN(TestSolverChoices)
#include "test_solver_choices.moc"
