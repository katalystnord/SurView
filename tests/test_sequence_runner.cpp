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

#include <cmath>

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
    void a_re_anchor_announces_what_it_lost_rather_than_what_was_lost_before();
    void after_a_re_anchor_the_cuts_are_carried_with_the_specimen_and_measured_clear_of();
    void re_anchoring_does_not_change_the_answer_on_a_specimen_that_deforms();
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
    // ⚑ NAMED THE WAY THE REST OF THE WINDOW NAMES IT: one-based, so the
    // second target is "Frame 2", which is what the project tree and the run
    // log call the same picture. Asking only whether the message contains a
    // "2" was not enough to see that: "Frame 1 of 2" contains one too, and
    // three mutants on that `frame + 1` lived behind it. Two numbers for one
    // frame on one screen is worse than none.
    QVERIFY2(failure.contains(QStringLiteral("Frame 2 of 2")),
             qPrintable(QStringLiteral("the failure does not name the frame the "
                                       "way the window does: %1")
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

void TestSequenceRunner::a_re_anchor_announces_what_it_lost_rather_than_what_was_lost_before()
{
    // ⚑ THE NUMBER BESIDE A RE-ANCHOR IS WHAT THAT RE-ANCHOR COST. A point with
    // no increment on the frame the reference moves is LOST: its position in
    // the new reference is unknown, so it cannot be measured again. The run log
    // says how many, and a reader uses it to decide whether the re-anchor was
    // worth it -- so a running TOTAL there would read as a single catastrophic
    // step rather than as the cost of this one.
    //
    // The case above ignores that number entirely, and on a clean sequence it
    // could not have seen it anyway: nothing is ever lost, so "how many now"
    // and "how many now plus how many before" are both zero. So the second
    // and third frames are tests/fixtures/shift_target_occluded.tif, the shift
    // target with a 48 x 48 block painted flat, where points really are lost
    // and the second re-anchor has a non-zero history behind it.
    //
    // ⚑ Until 2026-10-09 this used the clean frames with one iteration against
    // a strict threshold, credited here with failing "plenty of points". Every
    // one of them was in the grid's own unmeasurable edge row and column; once
    // the grid was fixed, that setting lost nothing at all.
    CorrelationSettings hard = coarseSettings();
    hard.recovery.enabled = false;

    ReferenceUpdatePolicy always;
    always.enabled = true;
    always.znccThreshold = 1.1;   // unreachable, so every frame re-anchors
    always.percentile = 1.0;

    SequenceRunner runner(hard, RegionOfInterest(),
                          fixture(QStringLiteral("shift_reference.tif")),
                          {fixture(QStringLiteral("shift_reference.tif")),
                           fixture(QStringLiteral("shift_target_occluded.tif")),
                           fixture(QStringLiteral("shift_target_occluded.tif"))},
                          always);

    QVector<int> announced;
    QVector<int> lostAfterEachFrame;
    QObject::connect(&runner, &SequenceRunner::referenceReanchored,
                     [&announced](int, int lost) { announced.append(lost); });
    QObject::connect(&runner, &SequenceRunner::frameFinished,
                     [&lostAfterEachFrame](int, const CorrelationResult &field) {
                         int lost = 0;
                         for (const CorrelationPoint &point : field.points) {
                             if (!point.converged)
                                 lost++;
                         }
                         lostAfterEachFrame.append(lost);
                     });

    runner.run();

    QCOMPARE(announced.size(), 3);
    QCOMPARE(lostAfterEachFrame.size(), 3);

    // The case only means something if something really was lost before the
    // last re-anchor: with nothing lost, a total and a difference agree.
    QVERIFY2(lostAfterEachFrame.at(1) > 0,
             qPrintable(QStringLiteral("no point was lost by the second frame, "
                                       "so a running total and this frame's own "
                                       "cost cannot be told apart: %1")
                            .arg(lostAfterEachFrame.at(1))));

    // Each announcement is at most what the field lost in total by that frame,
    // and the last one is smaller than the running total that a wrong sign
    // would report.
    for (int frame = 0; frame < announced.size(); frame++) {
        QVERIFY2(announced.at(frame) <= lostAfterEachFrame.at(frame),
                 qPrintable(QStringLiteral("frame %1 announced %2 points lost "
                                           "where the whole field has lost %3")
                                .arg(frame + 1)
                                .arg(announced.at(frame))
                                .arg(lostAfterEachFrame.at(frame))));
    }
}


// The hole sequence (tools/make-hole-fixture.py): the specimen moves AND
// deforms, x = X + t + G (X - C) with C = (110, 95), every gradient term
// non-zero, so each frame's exact displacement relative to the original
// reference is known at every point.
QPointF holeSequenceTruth(int frame, double X, double Y)
{
    const double t[3][2] = {{6.5, -4.2}, {5.5, -3.6}, {3.1, -1.9}};
    const double g[3][2][2] = {{{-0.010, -0.008}, {-0.006, 0.010}},
                               {{0.030, 0.022}, {0.024, -0.026}},
                               {{0.012, 0.034}, {0.028, -0.010}}};
    const double dx = X - 110.0, dy = Y - 95.0;
    return {t[frame][0] + g[frame][0][0] * dx + g[frame][0][1] * dy,
            t[frame][1] + g[frame][1][0] * dx + g[frame][1][1] * dy};
}

QStringList holeSequence()
{
    return {fixture(QStringLiteral("hole_sequence_1.tif")),
            fixture(QStringLiteral("hole_sequence_2.tif")),
            fixture(QStringLiteral("hole_sequence_3.tif"))};
}

void TestSequenceRunner::after_a_re_anchor_the_cuts_are_carried_with_the_specimen_and_measured_clear_of()
{
    // The cuts are drawn on the ORIGINAL reference. Once the run re-anchors to a
    // later frame the hole has moved and deformed with the specimen, so the cut
    // is carried there by what the run banked, and the frame is masked against
    // the hole where it now is. Re-anchoring is forced on every frame, as above,
    // so the second frame is measured against the first.
    ReferenceUpdatePolicy always;
    always.enabled = true;
    always.znccThreshold = 1.1;
    always.percentile = 1.0;

    RegionOfInterest roi;
    RegionShape specimen;
    specimen.kind = RegionShape::Rectangle;
    specimen.points = {QPoint(6, 9), QPoint(226, 184)};
    RegionShape hole;
    hole.kind = RegionShape::Ellipse;
    hole.subtract = true;
    hole.points = {QPoint(100, 80), QPoint(140, 120)};
    roi.shapes << specimen << hole;

    CorrelationSettings settings = coarseSettings();
    settings.gridStep = 8;
    settings.maxIterations = 20;
    const QStringList targets = holeSequence();

    const auto measure = [&](bool mask) {
        CorrelationSettings s = settings;
        s.maskAtCuts = mask;
        SequenceRunner runner(s, roi, fixture(QStringLiteral("hole_reference.tif")),
                              targets, always);
        QVector<CorrelationResult> frames;
        QObject::connect(&runner, &SequenceRunner::frameFinished,
                         [&frames](int, const CorrelationResult &r) { frames.append(r); });
        runner.run();
        return frames;
    };

    // Mean error over the points whose subset reaches the hole as drawn,
    // against the exact displacement relative to the original reference.
    const auto errorBesideTheHole = [&](const CorrelationResult &frame, int index,
                                        int *counted) {
        double sum = 0.0;
        int n = 0;
        for (const CorrelationPoint &p : frame.points) {
            if (!p.converged || !subsetReachesACut(roi, qRound(p.x), qRound(p.y), 16))
                continue;
            const QPointF truth = holeSequenceTruth(index, p.x, p.y);
            sum += std::hypot(double(p.u) - truth.x(), double(p.v) - truth.y());
            n++;
        }
        *counted = n;
        return n > 0 ? sum / n : 1e9;
    };

    const QVector<CorrelationResult> masked = measure(true);
    QCOMPARE(masked.size(), 3);
    QVERIFY2(masked.at(0).maskingApplied && !masked.at(0).maskCarried,
             "the first frame, against the original, was not masked where drawn");

    const QVector<CorrelationResult> unmasked = measure(false);
    QCOMPARE(unmasked.size(), 3);

    // Both later frames, the second re-anchored twice: carried from the cuts as
    // drawn by the whole banked movement each time, never from the last carry.
    for (int frame = 1; frame < 3; frame++) {
        QVERIFY2(masked.at(frame).maskingApplied, qPrintable(masked.at(frame).maskingNote));
        QVERIFY(masked.at(frame).maskCarried);
        QCOMPARE(masked.at(frame).maskPullIn, kMaskPullIn);
        QVERIFY(masked.at(frame).maskedPoints > 40);

        int withMask = 0, without = 0;
        const double carriedError = errorBesideTheHole(masked.at(frame), frame, &withMask);
        const double unmaskedError = errorBesideTheHole(unmasked.at(frame), frame, &without);
        qInfo("frame %d, re-anchored, beside the hole: %.4f px masked over %d points, "
              "%.4f px unmasked over %d", frame + 1, carriedError, withMask,
              unmaskedError, without);
        QVERIFY2(withMask > 40, qPrintable(QString::number(withMask)));
        QVERIFY2(unmaskedError > 0.08, qPrintable(QString::number(unmaskedError)));
        QVERIFY2(carriedError < 0.03, qPrintable(QString::number(carriedError)));
    }
}

void TestSequenceRunner::re_anchoring_does_not_change_the_answer_on_a_specimen_that_deforms()
{
    // The same property as re_anchoring_does_not_change_the_answer_when_it_did_
    // not_need_to, on a specimen that stretches as well as moves. That case
    // moves by a whole 3 px, so every followed point lands on a whole pixel;
    // here it lands between pixels, which is where a point followed through a
    // real test always is.
    //
    // ⚑ Found 2026-10-11: the engine reads a reference subset at the TRUNCATED
    // position of a point and warps the target from the exact one, so every
    // point measured after a re-anchor was off by the fraction it sat past a
    // pixel -- half a pixel on average, in a field that still correlated
    // perfectly.
    ReferenceUpdatePolicy always;
    always.enabled = true;
    always.znccThreshold = 1.1;
    always.percentile = 1.0;
    const ReferenceUpdatePolicy never;

    // The specimen without the hole: this case is about re-anchoring alone.
    RegionOfInterest roi;
    RegionShape specimen;
    specimen.kind = RegionShape::Rectangle;
    specimen.points = {QPoint(6, 9), QPoint(226, 184)};
    RegionShape hole;
    hole.kind = RegionShape::Ellipse;
    hole.subtract = true;
    hole.points = {QPoint(80, 60), QPoint(160, 140)};
    roi.shapes << specimen << hole;

    CorrelationSettings settings = coarseSettings();
    settings.gridStep = 12;
    settings.maxIterations = 20;
    settings.strainEnabled = true;
    settings.strainRadius = 30.0;
    const QStringList targets = holeSequence();

    const auto measure = [&](const ReferenceUpdatePolicy &policy) {
        SequenceRunner runner(settings, roi, fixture(QStringLiteral("hole_reference.tif")),
                              targets, policy);
        QVector<CorrelationResult> frames;
        QObject::connect(&runner, &SequenceRunner::frameFinished,
                         [&frames](int, const CorrelationResult &r) { frames.append(r); });
        runner.run();
        return frames;
    };
    const QVector<CorrelationResult> anchored = measure(always);
    const QVector<CorrelationResult> fixed = measure(never);
    QCOMPARE(anchored.size(), 3);
    QCOMPARE(fixed.size(), 3);

    // The later frames, each measured against the one before it.
    for (int frame = 1; frame < 3; frame++) {
        double worst = 0.0, worstStrain = 0.0, sum = 0.0;
        int compared = 0, strainCompared = 0;
        for (const CorrelationPoint &a : anchored.at(frame).points) {
            if (!a.converged)
                continue;
            for (const CorrelationPoint &f : fixed.at(frame).points) {
                if (f.gridIndex != a.gridIndex || !f.converged)
                    continue;
                const double d = std::hypot(double(a.u - f.u), double(a.v - f.v));
                worst = std::max(worst, d);
                sum += d;
                compared++;
                if (a.strainFitted && f.strainFitted) {
                    worstStrain = std::max({worstStrain, double(std::abs(a.exx - f.exx)),
                                            double(std::abs(a.eyy - f.eyy)),
                                            double(std::abs(a.exy - f.exy))});
                    strainCompared++;
                }
            }
        }
        qInfo("frame %d, re-anchored against fixed: mean %.4f px, worst %.4f px over "
              "%d points; worst strain difference %.2e over %d", frame + 1,
              sum / compared, worst, compared, worstStrain, strainCompared);
        QVERIFY(compared > 150);
        // Strain must actually have been fitted on the re-anchored frame:
        // two fields with no strain anywhere agree perfectly about it.
        QVERIFY2(strainCompared > 100, qPrintable(QString::number(strainCompared)));
        QVERIFY2(sum / compared < 0.005, qPrintable(QString::number(sum / compared)));
        QVERIFY2(worst < 0.03, qPrintable(QString::number(worst)));
        QVERIFY2(worstStrain < 5e-4, qPrintable(QString::number(worstStrain)));
    }
}

QTEST_MAIN(TestSequenceRunner)
#include "test_sequence_runner.moc"
