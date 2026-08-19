// What a sequence will measure, and in what order.
//
// WHY ORDER IS A CORRECTNESS PROBLEM AND NOT A PRESENTATION ONE. A sequence is
// a time axis. Sorted the way a computer sorts strings, `frame_10.tif` comes
// before `frame_2.tif`, so a twelve-frame test would be measured in the order
// 1, 10, 11, 12, 2, 3 ... and every field would be correct while the sequence
// they form is nonsense. Nothing downstream can detect it: each frame solves
// perfectly, the displacements are real, and the animation simply shows the
// specimen jumping about. This is the failure this file exists to prevent.
//
// The second half is which targets can be measured at all. That judgement
// already exists in core/ImagePairing.h and is not repeated here -- what is
// new is that a sequence must carry the ones it SKIPPED and why, because
// "measured 9 frames" reads very differently when 12 were imported.
//
// NEGATIVE CHECK (2026-08-19), three, all watched red:
//   - plain string comparison instead of natural: 5 of the 11 cases failed,
//     which is the bug this module exists to prevent.
//   - comparing whole paths instead of file names: the two-folder case failed.
//   - leading zeros left in place: f_002 stopped preceding f_10.

#include "core/ImageRecord.h"
#include "core/Sequence.h"

#include <QTest>

namespace {

// A record that can pair with the reference below.
ImageRecord frame(const QString &name)
{
    ImageRecord record;
    record.filePath = QStringLiteral("/frames/") + name;
    record.fileName = name;
    record.width = 240;
    record.height = 160;
    record.components = 1;
    record.scalarType = 3;   // VTK_UNSIGNED_CHAR
    return record;
}

ImageRecord reference()
{
    return frame(QStringLiteral("ref.tif"));
}

QStringList namesOf(const QVector<ImageRecord> &records, const QVector<int> &order)
{
    QStringList names;
    for (int index : order)
        names << records.at(index).fileName;
    return names;
}

}  // namespace

class TestSequence : public QObject
{
    Q_OBJECT

private slots:
    void frame_ten_comes_after_frame_two();
    void leading_zeros_do_not_change_the_order();
    void names_without_numbers_keep_their_alphabetical_order();
    void a_number_late_in_the_name_is_still_compared_as_a_number();
    void only_the_file_name_decides_the_order_not_the_folder();
    void the_plan_measures_every_target_that_can_pair();
    void a_target_that_cannot_pair_is_skipped_with_its_reason_kept();
    void a_target_that_could_not_be_read_is_skipped_with_its_reason_kept();
    void a_plan_with_nothing_to_measure_says_so();
};

void TestSequence::frame_ten_comes_after_frame_two()
{
    // The case the whole file is named for.
    QVERIFY(precedesInSequence(QStringLiteral("frame_2.tif"),
                               QStringLiteral("frame_10.tif")));
    QVERIFY(!precedesInSequence(QStringLiteral("frame_10.tif"),
                                QStringLiteral("frame_2.tif")));

    const QStringList sorted =
        sortIntoSequenceOrder({QStringLiteral("frame_10.tif"),
                               QStringLiteral("frame_2.tif"),
                               QStringLiteral("frame_1.tif"),
                               QStringLiteral("frame_11.tif")});
    QCOMPARE(sorted, QStringList({QStringLiteral("frame_1.tif"),
                                  QStringLiteral("frame_2.tif"),
                                  QStringLiteral("frame_10.tif"),
                                  QStringLiteral("frame_11.tif")}));
}

void TestSequence::leading_zeros_do_not_change_the_order()
{
    // Zero-padded names sort correctly as strings, which is exactly why a
    // codebase can carry this bug for years and only meet it on the one
    // sequence that was not padded. Both must work, and must agree.
    QVERIFY(precedesInSequence(QStringLiteral("f_002.tif"),
                               QStringLiteral("f_010.tif")));
    QVERIFY(precedesInSequence(QStringLiteral("f_002.tif"),
                               QStringLiteral("f_10.tif")));
    QVERIFY(precedesInSequence(QStringLiteral("f_2.tif"),
                               QStringLiteral("f_010.tif")));
}

void TestSequence::names_without_numbers_keep_their_alphabetical_order()
{
    QVERIFY(precedesInSequence(QStringLiteral("after.tif"),
                               QStringLiteral("before.tif")));
    QVERIFY(!precedesInSequence(QStringLiteral("before.tif"),
                                QStringLiteral("after.tif")));

    // Case must not split the order into two alphabets, which would put every
    // capitalised name before every lowercase one.
    const QStringList sorted = sortIntoSequenceOrder(
        {QStringLiteral("b.tif"), QStringLiteral("A.tif"), QStringLiteral("c.tif")});
    QCOMPARE(sorted, QStringList({QStringLiteral("A.tif"), QStringLiteral("b.tif"),
                                  QStringLiteral("c.tif")}));
}

void TestSequence::a_number_late_in_the_name_is_still_compared_as_a_number()
{
    // Real sequences carry more than one number: a date, a specimen id, then
    // the frame. Only the run that differs decides.
    QVERIFY(precedesInSequence(QStringLiteral("2026-08-19_spec3_frame2.tif"),
                               QStringLiteral("2026-08-19_spec3_frame10.tif")));
    QVERIFY(precedesInSequence(QStringLiteral("2026-08-19_spec3_frame9.tif"),
                               QStringLiteral("2026-08-19_spec10_frame1.tif")));
}

void TestSequence::only_the_file_name_decides_the_order_not_the_folder()
{
    // Frames gathered from more than one folder still form one sequence, and
    // the directory a file happens to sit in is not part of its position in it.
    QVERIFY(precedesInSequence(QStringLiteral("/z/last/frame_2.tif"),
                               QStringLiteral("/a/first/frame_10.tif")));
}

void TestSequence::the_plan_measures_every_target_that_can_pair()
{
    QVector<ImageRecord> targets;
    targets << frame(QStringLiteral("f_10.tif"))
            << frame(QStringLiteral("f_2.tif"))
            << frame(QStringLiteral("f_1.tif"));

    const SequencePlan plan = planSequence(reference(), targets);

    QCOMPARE(plan.order.size(), 3);
    QCOMPARE(namesOf(targets, plan.order),
             QStringList({QStringLiteral("f_1.tif"), QStringLiteral("f_2.tif"),
                          QStringLiteral("f_10.tif")}));
    QVERIFY(plan.skipped.isEmpty());
    QVERIFY(!plan.isEmpty());
}

void TestSequence::a_target_that_cannot_pair_is_skipped_with_its_reason_kept()
{
    // Skipped, never silently dropped: "measured 2 frames" reads very
    // differently when 3 were imported, and the reason is already known.
    QVector<ImageRecord> targets;
    targets << frame(QStringLiteral("f_1.tif"))
            << frame(QStringLiteral("f_2.tif"))
            << frame(QStringLiteral("f_3.tif"));
    targets[1].width = 999;   // a different pixel grid

    const SequencePlan plan = planSequence(reference(), targets);

    QCOMPARE(plan.order.size(), 2);
    QCOMPARE(namesOf(targets, plan.order),
             QStringList({QStringLiteral("f_1.tif"), QStringLiteral("f_3.tif")}));

    QCOMPARE(plan.skipped.size(), 1);
    QVERIFY(plan.skipped.contains(1));
    QVERIFY2(plan.skipped.value(1).contains(QStringLiteral("dimensions"),
                                            Qt::CaseInsensitive),
             qPrintable(QStringLiteral("the reason kept was: %1")
                            .arg(plan.skipped.value(1))));
}

void TestSequence::a_target_that_could_not_be_read_is_skipped_with_its_reason_kept()
{
    // An entry that holds provenance but no pixels. It is in the project
    // because the user named it, and the plan has to account for it rather
    // than behave as though it were never mentioned.
    QVector<ImageRecord> targets;
    targets << frame(QStringLiteral("f_1.tif"));
    ImageRecord unreadable;
    unreadable.filePath = QStringLiteral("/frames/f_2.tif");
    unreadable.fileName = QStringLiteral("f_2.tif");
    targets << unreadable;

    const SequencePlan plan = planSequence(reference(), targets);

    QCOMPARE(plan.order.size(), 1);
    QCOMPARE(plan.skipped.size(), 1);
    QVERIFY(plan.skipped.contains(1));
    QVERIFY2(!plan.skipped.value(1).isEmpty(),
             "a skipped target was given no reason");
}

void TestSequence::a_plan_with_nothing_to_measure_says_so()
{
    QVERIFY(planSequence(reference(), {}).isEmpty());

    QVector<ImageRecord> targets;
    targets << frame(QStringLiteral("f_1.tif"));
    targets[0].height = 999;
    const SequencePlan plan = planSequence(reference(), targets);
    QVERIFY(plan.isEmpty());
    QCOMPARE(plan.skipped.size(), 1);
}

QTEST_MAIN(TestSequence)
#include "test_sequence.moc"
