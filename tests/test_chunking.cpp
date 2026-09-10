// The queue is handed to the engine in chunks, and that must not change the
// answer.
//
// ⚑ WHY THIS FILE EXISTS. The chunking is not an optimisation. The engine's
// compute() blocks with neither progress nor cancellation, so this application
// composes around it by feeding the queue in pieces -- and every field it draws
// comes back through that loop. CLAUDE.md records a cancel bug found in it once
// already, whose whole shape was "on any grid small enough to be a single chunk
// that was EVERY cancel".
//
// The same blind spot was still open, one layer down. kDefaultChunkPoints is
// 2000 and every grid in the suite is smaller, so the loop had only ever run
// ONCE, with `start` at zero -- where `total - start` and `total + start` are
// the same expression, and `begin() + start` and `begin() - start` are the same
// iterator. A mutation sweep of Correlation.cpp against the complete suite,
// 2026-09-09, found sixteen mutants living there, untouched by 29 test
// executables.
//
// So `setChunkPoints()` exists: production code carrying a seam for exactly one
// reason, stated where it is declared. The alternative was a fixture of more
// than 2000 points, which is a slow correlation to run for arithmetic that a
// nine-point grid can pin.
//
// Negative checks, recorded as they actually went rather than as intended:
//   + start + count -> + start - count    RED at once: the field comes back
//                                         empty, so the fixture assertion
//                                         itself fails.
//   total - start -> total + start        RED, but only after the ragged chunk
//                                         below was arranged. It asks for a
//                                         chunk longer than the queue's tail,
//                                         reads past the end and takes the
//                                         case down with a fatal error -- five
//                                         minutes of thrashing first, which is
//                                         what reading far past the end of a
//                                         vector costs.
//   begin() + start -> begin() - start    NOT ESTABLISHED. The run did not
//                                         finish in five minutes and was
//                                         abandoned; writing before the start
//                                         of the queue is undefined behaviour
//                                         and what it does is not something to
//                                         claim either way. Written down
//                                         because a green suite must not be
//                                         read as more than it is.
//
// ⚑ AND THE REST OF THAT ACCOUNT WAS WRONG, corrected 2026-09-10 by measuring
// it rather than repeating it. Those negative checks were run in this project's
// Debug build, where reading off the end of the queue happens to take the
// process down mid-case. Re-run in the configuration a mutation sweep actually
// uses -- a fresh build directory with no CMAKE_BUILD_TYPE set -- the same
// mutant corrupts the heap quietly, EVERY CASE REPORTS PASS, and Qt Test's own
// crash handler prints a stack trace during teardown and exits ZERO. ctest
// reads that as a green suite. So the sixteen chunking mutants recorded as
// closed were not being killed by this file at all; they were being killed by
// an allocator, in one build, on one machine.
//
// ⚑ AND ONE SURVIVOR IS LEFT STANDING ON PURPOSE, for the same reason. Copying
// a repaired chunk back with `trial.begin() - start` writes BEFORE the start of
// the vector, and what that does is not something to claim either way: it is
// the recovery loop's copy of the mutant already recorded as NOT ESTABLISHED
// above. Nothing observable distinguishes it until the memory it wrote decides
// to matter, which is the definition of a verdict that does not travel between
// builds.
//
// Undefined behaviour is not an assertion. What holds them now is a property
// the run states out loud before any memory is touched wrongly: a stage cannot
// report more points than it has, so a chunk sized from `total + start` says
// "56 of 54 points" and the case goes red on the sentence rather than on the
// heap. Verified in that same no-build-type configuration: exit 1 with a named
// failure where the crash alone had exited 0.
//

#include "core/Correlation.h"
#include "core/Roi.h"

#include <QSignalSpy>
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
    // Coarse on purpose: this is about how the queue is divided, not about how
    // finely it is measured, and a small grid divided into threes is exactly
    // the case that was never run.
    settings.gridStep = 24;
    settings.maxIterations = 15;
    settings.convergence = 0.001;
    return settings;
}

// Chosen so that the fixture's point count does NOT divide by it. The case
// below asserts that, because an even division makes the whole exercise blind.
constexpr int kRaggedChunk = 7;

CorrelationResult runWithChunkSize(int chunkPoints, int &progressReports)
{
    CorrelationRunner runner(coarseSettings(), RegionOfInterest(),
                             fixture(QStringLiteral("shift_reference.tif")),
                             fixture(QStringLiteral("shift_target.tif")));
    if (chunkPoints > 0)
        runner.setChunkPoints(chunkPoints);

    CorrelationResult result;
    QString failure;
    progressReports = 0;
    QObject::connect(&runner, &CorrelationRunner::finished,
                     [&result](const CorrelationResult &r) { result = r; });
    QObject::connect(&runner, &CorrelationRunner::failed,
                     [&failure](const QString &reason) { failure = reason; });
    QObject::connect(&runner, &CorrelationRunner::progress,
                     [&progressReports](int, int, const QString &) { progressReports++; });

    runner.run();

    if (!failure.isEmpty())
        qWarning("%s", qPrintable(failure));
    return result;
}

}  // namespace

class TestChunking : public QObject
{
    Q_OBJECT

private slots:
    void dividing_the_queue_does_not_change_what_is_measured();
    void the_solve_leaves_a_core_for_the_interface_and_never_asks_for_none();
    void a_divided_queue_reports_progress_more_than_once();
    void the_run_names_the_stage_it_is_in_and_counts_forward_through_it();
    void every_stage_of_a_full_run_counts_within_its_own_total();
    void an_even_division_does_not_report_its_last_chunk_twice();
};

void TestChunking::the_solve_leaves_a_core_for_the_interface_and_never_asks_for_none()
{
    // The other half of how a run is dispatched: how many points go over at
    // once, and how many threads work on them. This number is handed straight
    // to omp_set_num_threads() for every correlation the application runs, and
    // until it was lifted out of the run it could not be called by anything --
    // the sweep of 2026-09-09 found four mutants in its one line, including
    // one that asks for MORE threads than the machine has and one that, on a
    // single-core machine, asks for none at all.
    //
    // Two properties, both about the machine rather than about the expression:
    // one core is left free so the window keeps repainting while the engine
    // runs, and a solve is never dispatched to nothing.
    QCOMPARE(solverThreadCount(12), 11);
    QCOMPARE(solverThreadCount(4), 3);
    QCOMPARE(solverThreadCount(2), 1);

    // ⚑ A single core is the boundary, and it is the one that matters: there
    // is no core to spare, and the solve still has to happen.
    QCOMPARE(solverThreadCount(1), 1);

    // idealThreadCount() returns -1 when it cannot tell.
    QCOMPARE(solverThreadCount(0), 1);
    QCOMPARE(solverThreadCount(-1), 1);

    for (int cores = 1; cores <= 64; cores++) {
        const int threads = solverThreadCount(cores);
        QVERIFY2(threads >= 1,
                 qPrintable(QStringLiteral("%1 cores dispatched %2 threads")
                                .arg(cores).arg(threads)));
        QVERIFY2(threads <= cores || cores < 1,
                 qPrintable(QStringLiteral("%1 cores dispatched %2 threads: "
                                           "more work than the machine has")
                                .arg(cores).arg(threads)));
    }
}

void TestChunking::dividing_the_queue_does_not_change_what_is_measured()
{
    int reports = 0;
    const CorrelationResult whole = runWithChunkSize(0, reports);
    QVERIFY2(whole.points.size() > 3,
             "the fixture lays out enough points to be worth dividing");

    // ⚑ THE LAST CHUNK MUST BE RAGGED, and this assertion is the whole reason
    // the case works. The count of points in a chunk is min(chunkSize, total -
    // start), and the subtraction only matters where fewer points remain than
    // a chunk holds. With a chunk size that divides the total exactly, every
    // chunk is full, `total - start` never binds, and it may become `total +
    // start` with nothing to notice -- which is what the first version of this
    // case did, and it passed.
    const int chunk = kRaggedChunk;
    QVERIFY2(whole.points.size() % chunk != 0,
             qPrintable(QStringLiteral("%1 points in chunks of %2 divides evenly, so the "
                                       "last chunk is full and the case is blind")
                            .arg(whole.points.size())
                            .arg(chunk)));

    const CorrelationResult divided = runWithChunkSize(chunk, reports);

    QCOMPARE(divided.points.size(), whole.points.size());
    QCOMPARE(divided.converged, whole.converged);

    for (int i = 0; i < whole.points.size(); i++) {
        const CorrelationPoint &a = whole.points.at(i);
        const CorrelationPoint &b = divided.points.at(i);
        QCOMPARE(b.gridIndex, a.gridIndex);
        QCOMPARE(b.converged, a.converged);
        if (!a.converged)
            continue;
        // ⚑ Point for point, not a summary. A queue copied back over itself
        // wrongly leaves a field that still has the right NUMBER of solved
        // points and the wrong answers in them, which every aggregate here
        // would report as a clean run.
        QVERIFY2(qAbs(double(b.u) - double(a.u)) < 1e-6
                     && qAbs(double(b.v) - double(a.v)) < 1e-6,
                 qPrintable(QStringLiteral("point %1 measured %2, %3 whole and %4, %5 divided")
                                .arg(i)
                                .arg(double(a.u))
                                .arg(double(a.v))
                                .arg(double(b.u))
                                .arg(double(b.v))));
    }
}

void TestChunking::the_run_names_the_stage_it_is_in_and_counts_forward_through_it()
{
    // What the status bar says while a run is going: "estimating displacement
    // - 300 of 494 points", then "refining to sub-pixel". Both halves of that
    // sentence come from this loop, and neither was checked anywhere.
    //
    // The solve is two passes over the same queue -- a whole-pixel estimate
    // first, then the sub-pixel refinement that uses it as its starting guess
    // -- and they are not interchangeable: told the wrong way round, the
    // status bar says the run is refining while it is still estimating, and a
    // reader watching a long run has no way to know which half they are in.
    // Two mutants sat here: one swapping the two names, and one running a
    // THIRD pass, which repeats the refinement under the name of the second
    // and sends the progress count backwards under an unchanged label.
    CorrelationSettings settings = coarseSettings();
    settings.strainEnabled = false;
    settings.recovery.enabled = false;   // its rounds restart the count by design

    CorrelationRunner runner(settings, RegionOfInterest(),
                             fixture(QStringLiteral("shift_reference.tif")),
                             fixture(QStringLiteral("shift_target.tif")));
    runner.setChunkPoints(kRaggedChunk);

    struct Report { QString stage; int done; int total; };
    QVector<Report> reports;
    CorrelationResult result;
    QObject::connect(&runner, &CorrelationRunner::finished,
                     [&result](const CorrelationResult &r) { result = r; });
    QObject::connect(&runner, &CorrelationRunner::progress,
                     [&reports](int done, int total, const QString &stage) {
                         reports.append({stage, done, total});
                     });

    runner.run();
    QVERIFY(result.total() > 0);
    QVERIFY(!reports.isEmpty());

    QStringList stagesInOrder;
    for (const Report &report : reports) {
        if (stagesInOrder.isEmpty() || stagesInOrder.last() != report.stage)
            stagesInOrder.append(report.stage);

        // ⚑ AND NO STAGE EVER REPORTS MORE POINTS THAN IT HAS. This is the
        // deterministic form of the chunk arithmetic: a chunk sized from
        // `total + start` instead of `total - start` runs the last chunk of a
        // ragged division off the end of the queue, and the progress bar says
        // so - "175 of 170 points" - before anything else goes wrong.
        //
        // ⚑ IT HAS TO BE SAID HERE, because the crash that mutant also causes
        // is NOT a reliable red. Measured 2026-09-10: the same mutant takes the
        // process down mid-case in this project's Debug build, and in a build
        // with no build type set it corrupts the heap quietly, every case
        // reports PASS, and Qt Test's crash handler prints a stack trace during
        // teardown and exits ZERO. ctest then reports the suite green. A
        // mutation sweep in that configuration scored the whole chunking family
        // as survivors, which is what it should have scored them all along:
        // undefined behaviour is not an assertion.
        QVERIFY2(report.done <= report.total,
                 qPrintable(QStringLiteral("%1 reported %2 of %3 points")
                                .arg(report.stage).arg(report.done)
                                .arg(report.total)));
        QVERIFY2(report.done >= 0,
                 qPrintable(QStringLiteral("%1 reported %2 points")
                                .arg(report.stage).arg(report.done)));
    }

    // Three, with strain and the repair pass switched off: the two solve
    // passes, and the reliability pass, which has no setting to turn it off
    // because under tenet 9 the account of how far a measurement can be
    // trusted is not an optional extra.
    QCOMPARE(stagesInOrder.size(), 3);
    QVERIFY2(stagesInOrder.at(0).contains(QStringLiteral("estimating")),
             qPrintable(stagesInOrder.join(QStringLiteral(", "))));
    QVERIFY2(stagesInOrder.at(1).contains(QStringLiteral("refining")),
             qPrintable(stagesInOrder.join(QStringLiteral(", "))));
    QVERIFY2(stagesInOrder.at(2).contains(QStringLiteral("reliability")),
             qPrintable(stagesInOrder.join(QStringLiteral(", "))));

    // ⚑ And the count only ever goes forward under one name. A progress bar
    // that runs to the end and starts again while the label is unchanged says
    // the run is doing something other than what it is doing.
    for (int i = 1; i < reports.size(); i++) {
        if (reports.at(i).stage != reports.at(i - 1).stage)
            continue;
        QVERIFY2(reports.at(i).done > reports.at(i - 1).done,
                 qPrintable(QStringLiteral("%1 counted %2 after %3")
                                .arg(reports.at(i).stage)
                                .arg(reports.at(i).done)
                                .arg(reports.at(i - 1).done)));
    }

    // Each pass finishes the whole queue before the next begins.
    QCOMPARE(reports.last().done, result.total());
}

void TestChunking::every_stage_of_a_full_run_counts_within_its_own_total()
{
    // The case above switches strain and the repair pass off, to keep the
    // stages it names down to the three that always run. That leaves the OTHER
    // two chunked loops - the repair pass and the strain fit - carrying the
    // same arithmetic with nothing watching it, which is exactly the blind spot
    // the whole file was written for, one loop along.
    //
    // Everything on, a ragged chunk, and one invariant across every stage:
    // a stage cannot report more points than it has. That is what a chunk sized
    // off the end of its queue says out loud before the memory it read decides
    // whether to crash.
    CorrelationSettings settings = coarseSettings();
    settings.strainEnabled = true;
    settings.strainRadius = 60.0;
    settings.strainMinPoints = 6;
    settings.recovery.enabled = true;

    CorrelationRunner runner(settings, RegionOfInterest(),
                             fixture(QStringLiteral("shift_reference.tif")),
                             fixture(QStringLiteral("shift_target.tif")));
    runner.setChunkPoints(kRaggedChunk);

    QStringList stages;
    bool overrun = false;
    QString worst;
    CorrelationResult result;
    QObject::connect(&runner, &CorrelationRunner::finished,
                     [&result](const CorrelationResult &r) { result = r; });
    QObject::connect(&runner, &CorrelationRunner::progress,
                     [&](int done, int total, const QString &stage) {
                         if (!stages.contains(stage))
                             stages.append(stage);
                         if (done > total || done < 0) {
                             overrun = true;
                             worst = QStringLiteral("%1 reported %2 of %3 points")
                                         .arg(stage).arg(done).arg(total);
                         }
                     });

    runner.run();

    QVERIFY(result.strainRequested);
    QVERIFY2(!overrun, qPrintable(worst));

    // And all five stages really did run, or the invariant above was checked
    // over a run that never reached the loops this case exists for.
    QVERIFY2(stages.size() >= 4,
             qPrintable(QStringLiteral("only these stages ran: %1")
                            .arg(stages.join(QStringLiteral(", ")))));
    QVERIFY2(stages.filter(QStringLiteral("strain")).size() == 1,
             qPrintable(stages.join(QStringLiteral(", "))));
    QVERIFY2(stages.filter(QStringLiteral("repairing")).size() == 1,
             qPrintable(stages.join(QStringLiteral(", "))));
}

void TestChunking::an_even_division_does_not_report_its_last_chunk_twice()
{
    // ⚑ THE RAGGED CHUNK HIDES THIS ONE, which is the mirror image of why the
    // ragged chunk exists. `start < total` widened to `start <= total` adds an
    // extra pass through the loop ONLY when the chunk size divides the queue
    // exactly: `start` then lands on `total`, an empty chunk goes to the
    // engine, and the stage reports the same count a second time. With a
    // deliberately ragged chunk, `start` steps over `total` and never lands on
    // it, so every case in this file walked past four of these - one in each of
    // the four chunked loops. Found by replaying the survivor list on
    // 2026-09-10 after the rest of the family had been closed.
    //
    // A chunk of one point divides every queue there is, including the repair
    // pass's own, whose length is decided by how many points failed.
    CorrelationSettings settings = coarseSettings();
    settings.strainEnabled = true;
    settings.strainRadius = 60.0;
    settings.strainMinPoints = 6;
    settings.recovery.enabled = true;

    CorrelationRunner runner(settings, RegionOfInterest(),
                             fixture(QStringLiteral("shift_reference.tif")),
                             fixture(QStringLiteral("shift_target.tif")));
    runner.setChunkPoints(1);

    QString repeated;
    QString lastStage;
    int lastDone = -1;
    CorrelationResult result;
    QObject::connect(&runner, &CorrelationRunner::finished,
                     [&result](const CorrelationResult &r) { result = r; });
    QObject::connect(&runner, &CorrelationRunner::progress,
                     [&](int done, int total, const QString &stage) {
                         if (repeated.isEmpty() && stage == lastStage && done == lastDone) {
                             repeated = QStringLiteral("%1 reported %2 of %3 "
                                                       "points twice running")
                                            .arg(stage).arg(done).arg(total);
                         }
                         lastStage = stage;
                         lastDone = done;
                     });

    runner.run();

    QVERIFY(result.total() > 0);
    QVERIFY2(repeated.isEmpty(), qPrintable(repeated));

    // ⚑ A repair round legitimately restarts its count from zero, which is why
    // the rule is "not the same count twice RUNNING" rather than "always
    // forward". A round with nothing to attempt cannot happen, so a restart
    // never repeats the count before it.
    QVERIFY2(result.recoveryRequested, "the repair pass did not run, so its own "
                                       "loop was never exercised");
}

void TestChunking::a_divided_queue_reports_progress_more_than_once()
{
    // The reason the chunking exists at all: a reader watching a long run sees
    // it advance, and a Stop button has somewhere to take effect. One report
    // for the whole queue is the behaviour this class was written to avoid.
    int wholeReports = 0;
    const CorrelationResult whole = runWithChunkSize(0, wholeReports);
    int dividedReports = 0;
    const CorrelationResult divided = runWithChunkSize(kRaggedChunk, dividedReports);

    QVERIFY(whole.points.size() > 3);
    QVERIFY2(dividedReports > wholeReports,
             qPrintable(QStringLiteral("a queue in threes reported %1 times, undivided %2")
                            .arg(dividedReports)
                            .arg(wholeReports)));
    QVERIFY2(divided.points.size() == whole.points.size(),
             "and measured the same field while doing it");
}

QTEST_MAIN(TestChunking)
#include "test_chunking.moc"
