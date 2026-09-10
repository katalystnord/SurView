// The pairs a run refuses to start on, and what it says about them.
//
// A mismatch is knowable the moment both files are read. core/ImagePairing.h
// answers it before a run is offered at all and has its own suite; this is the
// backstop inside the runner, which is what a caller reaching the engine
// directly runs into -- and what would otherwise hand two differently shaped
// pixel grids to a correlation that assumes one.
//
// ⚑ WHY A SECOND FIXTURE. The sweep of 2026-09-09 left one mutant standing in
// that guard: `width differs OR height differs` narrowed to AND, which refuses
// only a pair differing in BOTH axes. Every image in this repository differs
// from every other in both, so nothing in the suite could tell the two apart --
// a target the same width as the reference and half its height would have gone
// straight into the solver. shift_reference_short.tif is the shift reference
// cropped to its top 240x120, so it differs in height alone. Its stated
// property is its size, which any image tool reports.

// NEGATIVE CHECK (2026-09-10). Narrowing the guard to AND does not merely
// report a wrong answer: the case below takes the whole test process down with
// an abort inside the engine, which is what correlating two differently shaped
// pixel grids costs. That is the strongest form of red there is, and it is
// what the guard exists to prevent.
//
// ⚑ AND ONE SURVIVOR HERE CANNOT BE KILLED, recorded so nobody chases it. The
// line above this one refuses an image of no width or height, and it is
// unreachable: OpenCorr's Image2D loads through cv::imread and THROWS a
// std::string when the file cannot be read, so an image that arrives at that
// test has already been decoded and has real dimensions. The guard is defence
// against a future loader that returns an empty image instead of throwing, and
// every mutant of it survives by construction. An unreadable file is refused
// too, by the catch below, which is a different sentence for a different
// situation.

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

// Runs the pair and returns the refusal, or an empty string if it went ahead.
QString refusalFor(const QString &reference, const QString &target,
                   bool &finished)
{
    CorrelationRunner runner(coarseSettings(), RegionOfInterest(),
                             reference, target);
    QString failure;
    finished = false;
    QObject::connect(&runner, &CorrelationRunner::failed,
                     [&failure](const QString &reason) { failure = reason; });
    QObject::connect(&runner, &CorrelationRunner::finished,
                     [&finished](const CorrelationResult &) { finished = true; });
    runner.run();
    return failure;
}

}  // namespace

class TestRunRefusals : public QObject
{
    Q_OBJECT

private slots:
    void a_target_of_a_different_height_alone_is_refused();
    void a_refusal_names_both_sizes();
    void a_matching_pair_is_not_refused();
};

void TestRunRefusals::a_target_of_a_different_height_alone_is_refused()
{
    bool finished = false;
    const QString refusal =
        refusalFor(fixture(QStringLiteral("shift_reference.tif")),
                   fixture(QStringLiteral("shift_reference_short.tif")), finished);

    QVERIFY2(!refusal.isEmpty(),
             "a target 40 rows short of the reference was correlated against it "
             "anyway");
    QVERIFY2(!finished, "a refused run also reported a result");
}

void TestRunRefusals::a_refusal_names_both_sizes()
{
    // Which of the two is the odd one out is the reader's next question, and
    // they are looking at a file list rather than at the pixels. Both sizes are
    // in the sentence so the answer is there rather than a further step away.
    bool finished = false;
    const QString refusal =
        refusalFor(fixture(QStringLiteral("shift_reference.tif")),
                   fixture(QStringLiteral("shift_reference_short.tif")), finished);

    QVERIFY(!refusal.isEmpty());
    QVERIFY2(refusal.contains(QStringLiteral("160")), qPrintable(refusal));
    QVERIFY2(refusal.contains(QStringLiteral("120")), qPrintable(refusal));
    QVERIFY2(refusal.contains(QStringLiteral("240")), qPrintable(refusal));
}

void TestRunRefusals::a_matching_pair_is_not_refused()
{
    // The other side of the guard, without which "refuse everything" passes the
    // two cases above.
    bool finished = false;
    const QString refusal =
        refusalFor(fixture(QStringLiteral("shift_reference.tif")),
                   fixture(QStringLiteral("shift_target.tif")), finished);

    QVERIFY2(refusal.isEmpty(), qPrintable(refusal));
    QVERIFY2(finished, "a pair of matching images produced no result at all");
}

QTEST_MAIN(TestRunRefusals)
#include "test_run_refusals.moc"
