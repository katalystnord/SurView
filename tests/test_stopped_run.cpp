// What a run reports when it is stopped.
//
// WHY THIS FILE EXISTS. Stop is not a cosmetic control: a correlation is
// minutes of work, and the button that abandons it decides what the
// application then SAYS about the points it never got to. Those points still
// hold the integer-pixel FFTCC estimate, which is a starting guess and not a
// measurement, and reporting them as results would put guesses on the colour
// map wearing the same colour as measurements -- the standing rule of this
// project, one layer in from "a rejected point is not a displacement of zero".
//
// ⚑ Nothing tested the single-run cancel path at all. The sequence runner's
// own cancel is covered (and a defect in it is recorded in CLAUDE.md), but the
// point-by-point accounting underneath it was reached by no test, and the
// mutation sweep of 2026-09-09 found it: `i >= solvedUpTo` widened to `>` lets
// exactly one unmeasured point through as a measurement, and `converged =
// false` flipped to `true` lets every one of them through. Both survived 29
// test executables.
//
// The run here is stopped BEFORE it starts, which is what makes the case
// deterministic: mid-solve, how far the solver got depends on the machine, and
// an assertion about a specific point would be a race. Stopped at the start,
// the solver reached nothing and every point in the grid is a point the run
// never measured -- the same accounting, with a known answer.

// ⚑ ONE SURVIVOR HERE IS EQUIVALENT, and it is recorded rather than chased.
// The gate on the repair pass reads `recovery.enabled && !m_cancelled`, and
// widening it to `||` changes no behaviour at all, because BOTH halves are
// guarded again inside: the round loop tests `!m_cancelled` itself, and
// recoveryCanRun() tests policy.enabled itself. Established by breaking it and
// watching the case below stay green, not by reading the code -- the case does
// hold the behaviour that matters (a run stopped partway through does not
// carry on repairing a field of first guesses), it simply cannot distinguish
// this mutant from the original, because two other guards get there first.

#include "core/Correlation.h"
#include "core/Roi.h"

#include <QTest>

namespace {

QString fixture(const QString &name)
{
    return QStringLiteral(SURVIEW_TEST_FIXTURES "/") + name;
}

CorrelationSettings coarseSettings()
{
    CorrelationSettings settings;
    settings.subsetRadius = 16;
    settings.gridStep = 24;
    settings.maxIterations = 15;
    settings.convergence = 0.001;
    return settings;
}

}  // namespace

class TestStoppedRun : public QObject
{
    Q_OBJECT

private slots:
    void a_run_stopped_before_it_measured_anything_reports_no_measurements();
    void every_point_a_stopped_run_never_reached_says_so_in_words();
    void a_stopped_run_does_not_start_a_repair_pass();
};

void TestStoppedRun::a_run_stopped_before_it_measured_anything_reports_no_measurements()
{
    CorrelationRunner runner(coarseSettings(), RegionOfInterest(),
                             fixture(QStringLiteral("shift_reference.tif")),
                             fixture(QStringLiteral("shift_target.tif")));

    CorrelationResult result;
    QString failure;
    QObject::connect(&runner, &CorrelationRunner::finished,
                     [&result](const CorrelationResult &r) { result = r; });
    QObject::connect(&runner, &CorrelationRunner::failed,
                     [&failure](const QString &reason) { failure = reason; });

    runner.cancel();
    runner.run();

    QVERIFY2(failure.isEmpty(), qPrintable(failure));

    // It still lays out the grid it set out to measure: what the run was asked
    // to do is part of what happened, and a stopped run that reported an empty
    // field would be indistinguishable from one over an empty region.
    QVERIFY2(result.total() > 0, "a stopped run reported no grid at all");
    QVERIFY2(result.cancelled, "a stopped run reported itself as a clean finish");

    // ⚑ And not one of those points is a measurement. Every one of them holds
    // whatever the queue was initialised with, which is a position and nothing
    // else.
    QCOMPARE(result.converged, 0);
    for (const CorrelationPoint &point : result.points) {
        QVERIFY2(!point.converged,
                 qPrintable(QStringLiteral("point at %1,%2 was reported as "
                                           "measured by a run that never "
                                           "reached it")
                                .arg(point.x).arg(point.y)));
    }
}

void TestStoppedRun::every_point_a_stopped_run_never_reached_says_so_in_words()
{
    // Counted and named, not silently dropped. The run report groups failures
    // by their reason, and "the run was stopped" is a different thing to tell a
    // reader than "the solver could not match this subset" -- the first is
    // about the operator, the second about the specimen.
    CorrelationRunner runner(coarseSettings(), RegionOfInterest(),
                             fixture(QStringLiteral("shift_reference.tif")),
                             fixture(QStringLiteral("shift_target.tif")));

    CorrelationResult result;
    QObject::connect(&runner, &CorrelationRunner::finished,
                     [&result](const CorrelationResult &r) { result = r; });

    runner.cancel();
    runner.run();

    QVERIFY(result.total() > 0);

    int reported = 0;
    for (auto it = result.failuresByReason.constBegin();
         it != result.failuresByReason.constEnd(); ++it) {
        QVERIFY2(it.key().contains(QStringLiteral("stopped")),
                 qPrintable(QStringLiteral("a stopped run blamed the specimen: %1")
                                .arg(it.key())));
        reported += it.value();
    }

    // Every point in the grid is accounted for by name. A count that fell short
    // would leave points that simply are not mentioned anywhere.
    QCOMPARE(reported, result.total());
    for (const CorrelationPoint &point : result.points) {
        QVERIFY2(point.failureReason.contains(QStringLiteral("stopped")),
                 qPrintable(QStringLiteral("point at %1,%2: %3")
                                .arg(point.x).arg(point.y)
                                .arg(point.failureReason)));
    }
}

void TestStoppedRun::a_stopped_run_does_not_start_a_repair_pass()
{
    // Stop means stop. The second pass runs after the solve and is on by
    // default, so a run abandoned partway through is exactly where it could
    // quietly carry on working -- and it would be working on a field of first
    // guesses, repairing points against neighbours that were never measured.
    //
    // Cancelled from the progress signal rather than before the run, so the
    // solver really has been through part of the queue: a run stopped before it
    // began has nothing for a repair pass to seed from either way, and would
    // pass this case without asking the question.
    CorrelationSettings settings = coarseSettings();
    QVERIFY2(settings.recovery.enabled,
             "the pass is off by default, so this case asks nothing");

    CorrelationRunner runner(settings, RegionOfInterest(),
                             fixture(QStringLiteral("shift_reference.tif")),
                             fixture(QStringLiteral("shift_target.tif")));

    CorrelationResult result;
    QObject::connect(&runner, &CorrelationRunner::finished,
                     [&result](const CorrelationResult &r) { result = r; });
    QObject::connect(&runner, &CorrelationRunner::progress,
                     &runner, [&runner](int, int, const QString &) { runner.cancel(); });

    runner.run();

    QVERIFY2(result.cancelled, "the run was not stopped, so this case asks nothing");
    QCOMPARE(result.recoveredPoints, 0);
    for (const CorrelationPoint &point : result.points) {
        QVERIFY2(!point.recovered,
                 qPrintable(QStringLiteral("point at %1,%2 was repaired after "
                                           "the run was stopped")
                                .arg(point.x).arg(point.y)));
    }
}

QTEST_MAIN(TestStoppedRun)
#include "test_stopped_run.moc"
