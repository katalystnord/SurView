// What SurView says it knows about a decoded image.
//
// The record is the foundation the measurement stands on, so its arithmetic is
// worth pinning: a clipping share computed against the wrong denominator, or a
// type range claimed for a floating-point image, is a confident statement about
// evidence that does not support it.

#include "core/ImageRecord.h"

#include <QTest>

#include <vtkType.h>

namespace {

ImageRecord sixteenBitImage()
{
    ImageRecord record;
    record.width = 100;
    record.height = 50;
    record.components = 1;
    record.scalarType = VTK_UNSIGNED_SHORT;
    record.dataMin = 0.0;
    record.dataMax = 4095.0;    // 12-bit sensor data in a 16-bit file
    return record;
}

}  // namespace

class TestImageRecord : public QObject
{
    Q_OBJECT

private slots:
    void an_image_with_no_pixels_is_not_valid();
    void integer_pixels_have_a_type_range_and_float_pixels_do_not();
    void range_use_is_measured_against_the_types_range_not_the_datas();
    void clipping_shares_are_counted_against_the_pixels_actually_present();
    void an_uncounted_image_reports_no_share_rather_than_zero_percent();
    void the_pixel_type_is_named_in_words();
    void the_channel_count_is_named_in_words();
    void provenance_survives_a_file_that_cannot_be_decoded();
};

void TestImageRecord::an_image_with_no_pixels_is_not_valid()
{
    QVERIFY(!ImageRecord().isValid());

    ImageRecord partial;
    partial.width = 10;
    QVERIFY(!partial.isValid());

    QVERIFY(sixteenBitImage().isValid());
}

void TestImageRecord::integer_pixels_have_a_type_range_and_float_pixels_do_not()
{
    ImageRecord record = sixteenBitImage();
    QVERIFY(record.hasTypeRange());
    QCOMPARE(record.typeMin(), 0.0);
    QCOMPARE(record.typeMax(), 65535.0);

    record.scalarType = VTK_UNSIGNED_CHAR;
    QVERIFY(record.hasTypeRange());
    QCOMPARE(record.typeMax(), 255.0);

    // No fixed range to measure against, so none is claimed.
    record.scalarType = VTK_FLOAT;
    QVERIFY(!record.hasTypeRange());
    record.scalarType = VTK_DOUBLE;
    QVERIFY(!record.hasTypeRange());
}

void TestImageRecord::range_use_is_measured_against_the_types_range_not_the_datas()
{
    // 12-bit data in a 16-bit container uses 4095/65535 of what the file could
    // have held. That gap is the whole point of reporting it.
    const ImageRecord record = sixteenBitImage();
    QVERIFY(qAbs(record.rangeUtilization() - (4095.0 / 65535.0)) < 1e-9);
    QVERIFY(record.rangeUtilization() < 0.07);
}

void TestImageRecord::clipping_shares_are_counted_against_the_pixels_actually_present()
{
    ImageRecord record = sixteenBitImage();
    record.extremesCounted = true;
    record.pixelCount = 5000;
    record.pixelsAtDataMin = 50;
    record.pixelsAtDataMax = 500;

    QCOMPARE(record.fractionAtDataMin(), 0.01);
    QCOMPARE(record.fractionAtDataMax(), 0.10);

    // The extreme present (4095) is NOT the type's limit (65535). Counting at
    // the type limit would report a badly clipped image as perfectly clean,
    // which is the mistake this whole distinction exists to prevent.
    QVERIFY(record.dataMax != record.typeMax());
}

void TestImageRecord::an_uncounted_image_reports_no_share_rather_than_zero_percent()
{
    ImageRecord record = sixteenBitImage();
    QVERIFY(!record.extremesCounted);
    QCOMPARE(record.pixelCount, qint64(0));
    // Guarded against dividing by nothing; the caller distinguishes "not
    // counted" from "counted and found none" via extremesCounted.
    QCOMPARE(record.fractionAtDataMin(), 0.0);
    QCOMPARE(record.fractionAtDataMax(), 0.0);
}

void TestImageRecord::the_pixel_type_is_named_in_words()
{
    ImageRecord record = sixteenBitImage();
    QVERIFY(record.pixelTypeName().contains(QStringLiteral("16-bit")));

    record.scalarType = VTK_UNSIGNED_CHAR;
    QVERIFY(record.pixelTypeName().contains(QStringLiteral("8-bit")));

    record.scalarType = VTK_FLOAT;
    QVERIFY(!record.pixelTypeName().isEmpty());
}

void TestImageRecord::the_channel_count_is_named_in_words()
{
    ImageRecord record = sixteenBitImage();
    QVERIFY(record.channelsText().contains(QStringLiteral("1")));

    record.components = 3;
    QVERIFY(record.channelsText().contains(QStringLiteral("3")));
}

void TestImageRecord::provenance_survives_a_file_that_cannot_be_decoded()
{
    // fromFile fills the provenance half only; the pixel half stays empty
    // until something decodes the file. A record with provenance and no pixels
    // is a legitimate state, not a failure to be discarded.
    const ImageRecord record =
        ImageRecord::fromFile(QStringLiteral("/nonexistent/path/speckle_0042.tif"));

    QCOMPARE(record.fileName, QStringLiteral("speckle_0042.tif"));
    QVERIFY(record.filePath.endsWith(QStringLiteral("speckle_0042.tif")));
    QVERIFY(!record.isValid());
    QVERIFY(record.decoderClass.isEmpty());
}

QTEST_MAIN(TestImageRecord)
#include "test_image_record.moc"
