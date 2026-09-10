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
