// Whether a target can be correlated against the reference at all.
//
// This is knowable the moment both files are read, and saying so then is the
// difference between an honest refusal and a confusing failure inside the
// solver twenty seconds later.

#include "core/ImagePairing.h"
#include "core/ImageRecord.h"

#include <QTest>

#include <vtkType.h>

namespace {

ImageRecord image(int width, int height, int components, int scalarType)
{
    ImageRecord record;
    record.width = width;
    record.height = height;
    record.components = components;
    record.scalarType = scalarType;
    return record;
}

ImageRecord reference()
{
    return image(2048, 1536, 1, VTK_UNSIGNED_SHORT);
}

}  // namespace

class TestImagePairing : public QObject
{
    Q_OBJECT

private slots:
    void an_identical_target_matches();
    void differing_dimensions_are_reported_with_both_sizes();
    void differing_channel_counts_are_reported();
    void differing_pixel_types_are_reported_even_though_both_decode();
    void every_disagreement_is_listed_not_just_the_first();
    void nothing_is_claimed_before_there_is_something_to_compare();
};

void TestImagePairing::an_identical_target_matches()
{
    const PairCompatibility pairing = compareToReference(reference(), reference());
    QVERIFY(pairing.matches());
    QVERIFY(pairing.mismatches.isEmpty());
}

void TestImagePairing::differing_dimensions_are_reported_with_both_sizes()
{
    const PairCompatibility pairing =
        compareToReference(reference(), image(1024, 768, 1, VTK_UNSIGNED_SHORT));

    QVERIFY(!pairing.matches());
    QCOMPARE(pairing.mismatches.size(), 1);
    // Both sides named: "they differ" without the numbers leaves the user to
    // go and look them up.
    QVERIFY(pairing.mismatches.first().contains(QStringLiteral("2048")));
    QVERIFY(pairing.mismatches.first().contains(QStringLiteral("1024")));
}

void TestImagePairing::differing_channel_counts_are_reported()
{
    const PairCompatibility pairing =
        compareToReference(reference(), image(2048, 1536, 3, VTK_UNSIGNED_SHORT));

    QVERIFY(!pairing.matches());
    QCOMPARE(pairing.mismatches.size(), 1);
    QVERIFY(pairing.mismatches.first().contains(QStringLiteral("channels")));
}

void TestImagePairing::differing_pixel_types_are_reported_even_though_both_decode()
{
    // Both files open perfectly well. Correlating an 8-bit target against a
    // 16-bit reference compares two different intensity scales, which is worth
    // saying precisely because nothing else will complain.
    const PairCompatibility pairing =
        compareToReference(reference(), image(2048, 1536, 1, VTK_UNSIGNED_CHAR));

    QVERIFY(!pairing.matches());
    QCOMPARE(pairing.mismatches.size(), 1);
    QVERIFY(pairing.mismatches.first().contains(QStringLiteral("pixel type")));
}

void TestImagePairing::every_disagreement_is_listed_not_just_the_first()
{
    // Stopping at the first mismatch would send someone to fix one problem and
    // meet the next one on the following attempt.
    const PairCompatibility pairing =
        compareToReference(reference(), image(640, 480, 3, VTK_UNSIGNED_CHAR));

    QVERIFY(!pairing.matches());
    QCOMPARE(pairing.mismatches.size(), 3);
}

void TestImagePairing::nothing_is_claimed_before_there_is_something_to_compare()
{
    // No reference imported yet, or a target whose pixels could not be read:
    // there is no disagreement to report, and inventing one would refuse a
    // pairing nobody has established is wrong.
    QVERIFY(compareToReference(ImageRecord(), reference()).matches());
    QVERIFY(compareToReference(reference(), ImageRecord()).matches());
    QVERIFY(compareToReference(ImageRecord(), ImageRecord()).matches());
}

QTEST_MAIN(TestImagePairing)
#include "test_image_pairing.moc"
