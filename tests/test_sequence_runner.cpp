// Measuring a sequence: many targets against one reference, in frame order.
//
// WHAT MATTERS HERE that a single-pair run never had to answer:
//
//   - every frame must arrive, identified, and in the order it was given. A
//     sequence whose frames are correct but mislabelled is a time series read
//     backwards, and each individual field looks perfect.
//   - a result must be usable before the last frame finishes. A twelve-frame
//     run is minutes long, and holding all of it back until the end withholds
//     work that is already done.
//   - Stop must reach INTO the frame that is running, not merely end the loop
//     after it. A frame can take half a minute; a button that waits that long
//     reads as a button that did nothing.
//
// NEGATIVE CHECK (2026-08-19): recorded against the cases below.

#include "core/Correlation.h"
#include "core/Roi.h"
#include "core/ReferenceUpdate.h"
#include "core/SequenceRunner.h"

#include <QSignalSpy>
#include <QTest>
#include <QThread>

namespace {

QString fixture(const QString &name)
{
    return QStringLiteral(SURVIEW_TEST_FIXTURES "/") + name;
}

CorrelationSettings coarseSettings()
{
    CorrelationSettings settings;
    settings.solver = CorrelationSettings::ICGN;
    settings.shapeOrder = 1;
    settings.subsetRadius = 16;
    // Coarse on purpose: this suite is about the sequence, not the solver, and
    // a fine grid multiplies that question by thousands of points per frame.
    settings.gridStep = 24;
    settings.maxIterations = 10;
    settings.convergence = 0.001;
    settings.strainEnabled = false;
    return settings;
}

}  // namespace

class TestSequenceRunner : public QObject
{
    Q_OBJECT

private slots:
    void every_frame_arrives_identified_and_in_the_order_it_was_given();
    void a_frame_is_delivered_as_soon_as_it_is_measured();
    void each_frame_measures_the_target_it_was_given();
    void a_sequence_of_one_is_still_a_sequence();
    void a_frame_that_cannot_be_read_stops_the_run_and_names_itself();
    void an_empty_sequence_is_refused_rather_than_reported_as_done();
    void stopping_reaches_into_the_frame_that_is_running();
    void re_anchoring_does_not_change_the_answer_when_it_did_not_need_to();
    void a_re_anchor_is_announced_rather_than_done_quietly();
};

void TestSequenceRunner::every_frame_arrives_identified_and_in_the_order_it_was_given()
{
    // The same target three times, which is the cheapest way to ask the
    // question this case is about: the frames are indistinguishable by their
    // content, so only the index can tell them apart, and a runner that
    // scrambled or repeated an index would go unnoticed by any check on the
    // measurements themselves.
    const QStringList targets = {fixture(QStringLiteral("shift_target.tif")),
                                 fixture(QStringLiteral("shift_target.tif")),
                                 fixture(QStringLiteral("shift_target.tif"))};

    SequenceRunner runner(coarseSettings(), RegionOfInterest(),
                          fixture(QStringLiteral("shift_reference.tif")), targets);

    QVector<int> framesSeen;
    QObject::connect(&runner, &SequenceRunner::frameFinished,
                     [&framesSeen](int frame, const CorrelationResult &) {
                         framesSeen.append(frame);
                     });
    QSignalSpy done(&runner, &SequenceRunner::finished);
    QSignalSpy failed(&runner, &SequenceRunner::failed);

    runner.run();

    QVERIFY2(failed.isEmpty(), "a sequence of three good frames reported failure");
    QCOMPARE(framesSeen, QVector<int>({0, 1, 2}));
    QCOMPARE(done.size(), 1);
    QCOMPARE(done.at(0).at(0).toInt(), 3);        // frames measured
    QCOMPARE(done.at(0).at(1).toBool(), false);   // not cancelled
}

void TestSequenceRunner::a_frame_is_delivered_as_soon_as_it_is_measured()
{
    // Every frame must be out before the run reports itself finished. Held to
    // the end instead, the first frame of a twelve-frame run would sit
    // completed and unseen for minutes.
    const QStringList targets = {fixture(QStringLiteral("shift_target.tif")),
                                 fixture(QStringLiteral("shift_target.tif"))};

    SequenceRunner runner(coarseSettings(), RegionOfInterest(),
                          fixture(QStringLiteral("shift_reference.tif")), targets);

    int framesBeforeFinish = 0;
    bool finishedSeen = false;
    QObject::connect(&runner, &SequenceRunner::frameFinished,
                     [&framesBeforeFinish, &finishedSeen](int, const CorrelationResult &) {
                         QVERIFY2(!finishedSeen,
                                  "a frame arrived after the run said it had finished");
                         framesBeforeFinish++;
                     });
    QObject::connect(&runner, &SequenceRunner::finished,
                     [&finishedSeen](int, bool) { finishedSeen = true; });

    runner.run();

    QCOMPARE(framesBeforeFinish, 2);
    QVERIFY(finishedSeen);
}

void TestSequenceRunner::each_frame_measures_the_target_it_was_given()
{
    // Two DIFFERENT targets, so the measurement itself distinguishes them: the
    // shifted pair moves 3 px, and the reference against itself moves none.
    // A runner that measured the same file twice, or reused one frame's result
    // for another, cannot produce both answers.
    const QStringList targets = {fixture(QStringLiteral("shift_reference.tif")),
                                 fixture(QStringLiteral("shift_target.tif"))};

    SequenceRunner runner(coarseSettings(), RegionOfInterest(),
                          fixture(QStringLiteral("shift_reference.tif")), targets);

    QMap<int, double> meanU;
    QObject::connect(&runner, &SequenceRunner::frameFinished,
                     [&meanU](int frame, const CorrelationResult &result) {
                         double sum = 0.0;
                         int counted = 0;
                         for (const CorrelationPoint &point : result.points) {
                             if (!point.converged)
                                 continue;
                             sum += point.u;
                             counted++;
                         }
                         if (counted > 0)
                             meanU.insert(frame, sum / counted);
                     });

    runner.run();

    QCOMPARE(meanU.size(), 2);
    QVERIFY2(qAbs(meanU.value(0)) < 0.05,
             qPrintable(QStringLiteral("frame 0 is the reference against itself "
                                       "and must not move; mean u was %1")
                            .arg(meanU.value(0))));
    QVERIFY2(qAbs(meanU.value(1) - 3.0) < 0.1,
             qPrintable(QStringLiteral("frame 1 is the +3 px target; mean u was %1")
                            .arg(meanU.value(1))));
}

void TestSequenceRunner::a_sequence_of_one_is_still_a_sequence()
{
    // The single-pair case has to keep working through the same path, or the
    // application ends up with two ways of measuring and one of them is the
    // one nobody exercises.
    SequenceRunner runner(coarseSettings(), RegionOfInterest(),
                          fixture(QStringLiteral("shift_reference.tif")),
                          {fixture(QStringLiteral("shift_target.tif"))});

    QSignalSpy frames(&runner, &SequenceRunner::frameFinished);
    QSignalSpy done(&runner, &SequenceRunner::finished);
    runner.run();

    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0).at(0).toInt(), 0);
    QCOMPARE(done.at(0).at(0).toInt(), 1);
}

void TestSequenceRunner::a_frame_that_cannot_be_read_stops_the_run_and_names_itself()
{
    // Stopped rather than skipped: the same settings apply to every frame, so
    // whatever broke here breaks on the rest, and eleven identical complaints
    // help nobody. The frame is named because "the run failed" on a long
    // sequence sends the reader to look through all of them.
    const QStringList targets = {fixture(QStringLiteral("shift_target.tif")),
                                 fixture(QStringLiteral("no_such_frame.tif"))};

    SequenceRunner runner(coarseSettings(), RegionOfInterest(),
                          fixture(QStringLiteral("shift_reference.tif")), targets);

    QSignalSpy frames(&runner, &SequenceRunner::frameFinished);
    QString failure;
    QObject::connect(&runner, &SequenceRunner::failed,
                     [&failure](const QString &reason) { failure = reason; });

    runner.run();

    QCOMPARE(frames.size(), 1);   // the good frame was still delivered
    QVERIFY2(!failure.isEmpty(), "a missing frame was measured without complaint");
    QVERIFY2(failure.contains(QStringLiteral("2")),
             qPrintable(QStringLiteral("the failure does not name the frame: %1")
                            .arg(failure)));
}

void TestSequenceRunner::an_empty_sequence_is_refused_rather_than_reported_as_done()
{
    SequenceRunner runner(coarseSettings(), RegionOfInterest(),
                          fixture(QStringLiteral("shift_reference.tif")), {});

    QSignalSpy done(&runner, &SequenceRunner::finished);
    QSignalSpy failed(&runner, &SequenceRunner::failed);
    runner.run();

    QVERIFY2(failed.size() == 1, "an empty sequence did not say so");
    QVERIFY2(done.isEmpty(), "an empty sequence reported itself finished");
}

void TestSequenceRunner::stopping_reaches_into_the_frame_that_is_running()
{
    // Cancelled from another thread while a frame is mid-solve, which is what
    // pressing Stop actually does. The run must end without measuring every
    // frame, and must say it was cancelled rather than reporting a clean
    // finish over a sequence it did not complete.
    QStringList targets;
    for (int i = 0; i < 8; i++)
        targets << fixture(QStringLiteral("shift_target.tif"));

    CorrelationSettings settings = coarseSettings();
    settings.gridStep = 4;   // slow enough that Stop lands inside a frame

    SequenceRunner runner(settings, RegionOfInterest(),
                          fixture(QStringLiteral("shift_reference.tif")), targets);

    // Cancel as soon as the first frame reports any progress at all.
    QObject::connect(&runner, &SequenceRunner::frameProgress, &runner,
                     [&runner](int, int, int, int, const QString &) {
                         runner.cancel();
                     }, Qt::DirectConnection);

    int delivered = 0;
    bool aFrameWasInterrupted = false;
    QObject::connect(&runner, &SequenceRunner::frameFinished,
                     [&](int, const CorrelationResult &result) {
                         delivered++;
                         if (result.cancelled)
                             aFrameWasInterrupted = true;
                     });
    QSignalSpy done(&runner, &SequenceRunner::finished);

    runner.run();

    QCOMPARE(done.size(), 1);
    QVERIFY2(done.at(0).at(1).toBool(),
             "a cancelled sequence reported itself as a clean finish");
    QVERIFY2(delivered < targets.size(),
             qPrintable(QStringLiteral("cancelling measured %1 of %2 frames anyway")
                            .arg(delivered)
                            .arg(targets.size())));

    // ⚑ THE ASSERTION THAT ACTUALLY TESTS THE MECHANISM. The check above passes
    // whether or not Stop reaches into the running frame: a runner that merely
    // breaks out of the loop afterwards also measures fewer frames. Verified by
    // negative check -- with the reach-in removed this case still passed, and
    // only took 11 seconds instead of 3, because it sat through the whole frame
    // first. The frame in flight must come back marked as interrupted.
    QVERIFY2(aFrameWasInterrupted,
             "Stop ended the sequence but let the running frame finish first");
}


namespace {

// Mean measured displacement along x over the solved points of one field.
double meanU(const CorrelationResult &result)
{
    double sum = 0.0;
    int counted = 0;
    for (const CorrelationPoint &point : result.points) {
        if (!point.converged)
            continue;
        sum += point.u;
        counted++;
    }
    return counted > 0 ? sum / counted : 0.0;
}

// A three-frame sequence out of the two fixtures: still, moved, still again.
// Against a fixed reference the answers are 0, 3, 3 px.
QStringList threeFrames()
{
    return {fixture(QStringLiteral("shift_reference.tif")),
            fixture(QStringLiteral("shift_target.tif")),
            fixture(QStringLiteral("shift_target.tif"))};
}

QVector<double> measureSequence(const ReferenceUpdatePolicy &policy, int *reanchors)
{
    SequenceRunner runner(coarseSettings(), RegionOfInterest(),
                          fixture(QStringLiteral("shift_reference.tif")),
                          threeFrames(), policy);

    QVector<double> totals;
    QObject::connect(&runner, &SequenceRunner::frameFinished,
                     [&totals](int, const CorrelationResult &result) {
                         totals.append(meanU(result));
                     });
    int counted = 0;
    QObject::connect(&runner, &SequenceRunner::referenceReanchored,
                     [&counted](int, int) { counted++; });
    runner.run();
    if (reanchors)
        *reanchors = counted;
    return totals;
}

}  // namespace

void TestSequenceRunner::re_anchoring_does_not_change_the_answer_when_it_did_not_need_to()
{
    // ⚑ THE PROPERTY THAT GUARDS THE WHOLE SCHEME, and the reason this can be
    // orchestrated here rather than inside the engine's own tracker without
    // giving up the check that tracker would have provided.
    //
    // Re-anchoring changes what each frame is COMPARED AGAINST. It must not
    // change what each frame REPORTS. Where the original reference still
    // correlates, a re-anchored run and a fixed-reference run are measuring the
    // same physical thing by two routes, so they have to agree -- and if the
    // banking arithmetic is wrong in any way that matters, they will not.
    //
    // Forced on every frame rather than waited for, so the case does not need a
    // specimen that decorrelates: a threshold above any achievable correlation
    // re-anchors unconditionally, which is the harshest version of the same
    // question. Against a fixed reference this sequence reads 0, 3, 3 px; via
    // re-anchoring it is 0, then 0+3, then 3+0, arriving the same way round.
    ReferenceUpdatePolicy fixed;
    fixed.enabled = false;

    ReferenceUpdatePolicy always;
    always.enabled = true;
    always.znccThreshold = 1.1;   // unreachable, so every frame re-anchors
    always.percentile = 1.0;

    int fixedReanchors = 0;
    int alwaysReanchors = 0;
    const QVector<double> withFixed = measureSequence(fixed, &fixedReanchors);
    const QVector<double> withUpdate = measureSequence(always, &alwaysReanchors);

    QCOMPARE(fixedReanchors, 0);
    QCOMPARE(alwaysReanchors, 3);

    QCOMPARE(withFixed.size(), 3);
    QCOMPARE(withUpdate.size(), 3);

    for (int frame = 0; frame < 3; frame++) {
        QVERIFY2(qAbs(withFixed.at(frame) - withUpdate.at(frame)) < 0.05,
                 qPrintable(QStringLiteral("frame %1 reads %2 px against a fixed "
                                           "reference and %3 px with re-anchoring; "
                                           "the route must not change the answer")
                                .arg(frame)
                                .arg(withFixed.at(frame))
                                .arg(withUpdate.at(frame))));
    }

    // And the answers are the ones the fixtures actually encode, so this cannot
    // pass by both routes being equally wrong.
    QVERIFY2(qAbs(withUpdate.at(0)) < 0.05, "frame 0 is the reference against itself");
    QVERIFY2(qAbs(withUpdate.at(1) - 3.0) < 0.1, "frame 1 is displaced 3 px");
    QVERIFY2(qAbs(withUpdate.at(2) - 3.0) < 0.1, "frame 2 is still displaced 3 px");
}

void TestSequenceRunner::a_re_anchor_is_announced_rather_than_done_quietly()
{
    // Changing what a run compares against changes what its numbers mean if
    // anything downstream gets the bookkeeping wrong, so it is not something to
    // do without saying.
    ReferenceUpdatePolicy always;
    always.enabled = true;
    always.znccThreshold = 1.1;
    always.percentile = 1.0;

    SequenceRunner runner(coarseSettings(), RegionOfInterest(),
                          fixture(QStringLiteral("shift_reference.tif")),
                          threeFrames(), always);

    QVector<int> announced;
    QObject::connect(&runner, &SequenceRunner::referenceReanchored,
                     [&announced](int frame, int) { announced.append(frame); });
    runner.run();

    QCOMPARE(announced, QVector<int>({0, 1, 2}));
}

QTEST_MAIN(TestSequenceRunner)
#include "test_sequence_runner.moc"
