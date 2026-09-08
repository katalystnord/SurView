// What the camera recorded at one pixel.
//
// ⚑ The two rules this file exists for, both of which produce a perfectly
// plausible readout when broken: the number reported must be the FILE's, not
// the stretched one on screen, and a pixel sitting at an extreme must be
// reported as a warning rather than as a measurement. A clipped pixel reads as
// a very bright one, and the difference is the whole question a person is
// asking when they point at an over-exposed corner.
//
// NEGATIVE CHECK (2026-09-08): five breaks, each reverted after. Four landed on
// the cases named for them -- mapping the value through the display window,
// dropping the extreme test, leaving out the share of the image, and emptying
// the panel for a position off the picture. The fifth landed nowhere and is
// written up at the case concerned rather than left looking like coverage.

#include "core/ImageRecord.h"
#include "core/PixelReadout.h"

#include <QTest>

#include <vtkType.h>

namespace {

// An 8-bit grey image whose pixels run 0 to 240, displayed stretched -- the
// ordinary case for the speckle images that ship.
ImageRecord eightBitGrey()
{
    ImageRecord record;
    record.width = 640;
    record.height = 480;
    record.components = 1;
    record.scalarType = VTK_UNSIGNED_CHAR;
    record.dataMin = 0.0;
    record.dataMax = 240.0;
    record.extremesCounted = true;
    record.pixelCount = 640 * 480;
    record.pixelsAtDataMin = 25779;
    record.pixelsAtDataMax = 12;
    record.displayed = true;
    record.displayMin = 0.0;
    record.displayMax = 240.0;
    return record;
}

QString allText(const QVector<ReadoutLine> &lines)
{
    QString all;
    for (const ReadoutLine &line : lines)
        all += line.label + QLatin1Char(' ') + line.value + QLatin1Char(' ') + line.note
               + QLatin1Char('\n');
    return all;
}

bool anyWarning(const QVector<ReadoutLine> &lines)
{
    for (const ReadoutLine &line : lines) {
        if (line.warning)
            return true;
    }
    return false;
}

}  // namespace

class TestPixelReadout : public QObject
{
    Q_OBJECT

private slots:
    void the_value_reported_is_the_one_in_the_file();
    void the_readout_says_the_picture_on_screen_is_stretched();
    void the_reading_names_the_pixel_it_is_about();

    void a_pixel_at_the_highest_value_in_the_image_is_a_warning_not_a_reading();
    void a_pixel_at_the_lowest_value_is_warned_about_the_same_way();
    void an_ordinary_pixel_carries_no_warning_at_all();
    void the_warning_says_how_much_of_the_image_shares_that_value();
    void a_type_limit_is_a_stronger_statement_than_an_image_extreme();
    void a_float_image_claims_nothing_about_a_type_limit_it_does_not_have();

    void every_channel_of_a_colour_pixel_is_reported_and_none_is_invented();
    void a_position_off_the_picture_reads_as_absent_rather_than_as_zero();
};

void TestPixelReadout::the_value_reported_is_the_one_in_the_file()
{
    // ⚑ NOT the brightness on screen. A 16-bit image sitting in 0-5000 is
    // stretched to black..white for display, so the pixel a reader sees as
    // mid-grey holds 2500 and not 32767 -- and correlation uses the file's
    // number. A readout of the rendering would be reporting on the viewport.
    //
    // NEGATIVE CHECK: mapping the value through the display window reads 145
    // here against the file's 137, and this goes red.
    const ImageRecord record = eightBitGrey();
    const PixelReading reading = pixelReading(record, 100, 50, {137.0});
    QVERIFY(reading.valid);
    QCOMPARE(reading.components.size(), 1);
    QCOMPARE(reading.components.at(0), 137.0);

    const QVector<ReadoutLine> lines = pixelReadoutLines(reading, record);
    QVERIFY2(allText(lines).contains(QStringLiteral("137")), qPrintable(allText(lines)));
}

void TestPixelReadout::the_readout_says_the_picture_on_screen_is_stretched()
{
    // The mapping is stated rather than left implicit, for the same reason the
    // Record panel states it: a reader comparing the number against what they
    // can see needs to know the two are not the same scale.
    ImageRecord record = eightBitGrey();
    record.dataMax = 5000.0;
    record.scalarType = VTK_UNSIGNED_SHORT;
    record.displayMin = 0.0;
    record.displayMax = 5000.0;

    const QVector<ReadoutLine> lines =
        pixelReadoutLines(pixelReading(record, 10, 10, {2500.0}), record);
    const QString said = allText(lines);
    QVERIFY2(said.contains(QStringLiteral("65535")), qPrintable(said));
    QVERIFY2(said.contains(QStringLiteral("stretch"), Qt::CaseInsensitive)
                 || said.contains(QStringLiteral("display"), Qt::CaseInsensitive),
             qPrintable(said));
}

void TestPixelReadout::the_reading_names_the_pixel_it_is_about()
{
    const ImageRecord record = eightBitGrey();
    const QString said = allText(pixelReadoutLines(pixelReading(record, 123, 45, {10.0}),
                                                   record));
    QVERIFY2(said.contains(QStringLiteral("123")) && said.contains(QStringLiteral("45")),
             qPrintable(said));
}

void TestPixelReadout::a_pixel_at_the_highest_value_in_the_image_is_a_warning_not_a_reading()
{
    // ⚑ Where the sensor ran out of range the speckle is GONE, not merely
    // bright: every clipped pixel holds the same number, so a subset over them
    // has no gradient at all and correlation has nothing to lock onto. Reported
    // as 240 with no more said, it reads as an ordinary bright pixel.
    //
    // NEGATIVE CHECK: dropping the extreme test leaves this line unwarned and
    // the case red.
    const ImageRecord record = eightBitGrey();
    const PixelReading reading = pixelReading(record, 3, 4, {240.0});
    QVERIFY(reading.atDataCeiling);
    QVERIFY(!reading.atDataFloor);

    const QVector<ReadoutLine> lines = pixelReadoutLines(reading, record);
    QVERIFY2(anyWarning(lines), qPrintable(allText(lines)));
    // And it says what the consequence IS, not merely that the value is high.
    QVERIFY2(allText(lines).contains(QStringLiteral("gradient"), Qt::CaseInsensitive),
             qPrintable(allText(lines)));
}

void TestPixelReadout::a_pixel_at_the_lowest_value_is_warned_about_the_same_way()
{
    const ImageRecord record = eightBitGrey();
    const PixelReading reading = pixelReading(record, 3, 4, {0.0});
    QVERIFY(reading.atDataFloor);
    QVERIFY(anyWarning(pixelReadoutLines(reading, record)));
}

void TestPixelReadout::an_ordinary_pixel_carries_no_warning_at_all()
{
    // Without this every case above passes against a readout that warns on
    // everything, which would train a reader to ignore the warning.
    const ImageRecord record = eightBitGrey();
    const QVector<ReadoutLine> lines =
        pixelReadoutLines(pixelReading(record, 3, 4, {137.0}), record);
    QVERIFY2(!anyWarning(lines), qPrintable(allText(lines)));
}

void TestPixelReadout::the_warning_says_how_much_of_the_image_shares_that_value()
{
    // ⚑ A handful of pixels at the extreme is ordinary; a large share means the
    // exposure is wrong. The bare fact "this pixel is at the maximum" cannot
    // tell those apart, and the record already counts the share.
    //
    // NEGATIVE CHECK: leaving the share out leaves the warning identical for a
    // dozen pixels and for a third of the image.
    const ImageRecord record = eightBitGrey();   // 25,779 px at the floor, 8.39%
    const QString said =
        allText(pixelReadoutLines(pixelReading(record, 3, 4, {0.0}), record));
    QVERIFY2(said.contains(QStringLiteral("8.4"))
                 || said.contains(QStringLiteral("8.39"))
                 || said.contains(QStringLiteral("25,779"))
                 || said.contains(QStringLiteral("25779")),
             qPrintable(said));
}

void TestPixelReadout::a_type_limit_is_a_stronger_statement_than_an_image_extreme()
{
    // The image's own highest value MIGHT be where the sensor gave up; the
    // type's own limit certainly is. Both are worth saying and they are not the
    // same statement.
    ImageRecord record = eightBitGrey();
    record.dataMax = 255.0;

    const PixelReading atLimit = pixelReading(record, 3, 4, {255.0});
    QVERIFY(atLimit.atDataCeiling);
    QVERIFY(atLimit.atTypeCeiling);

    const PixelReading belowLimit = pixelReading(eightBitGrey(), 3, 4, {240.0});
    QVERIFY(belowLimit.atDataCeiling);
    QVERIFY2(!belowLimit.atTypeCeiling,
             "240 of a possible 255 was called the type's own limit");
}

void TestPixelReadout::a_float_image_claims_nothing_about_a_type_limit_it_does_not_have()
{
    // ⚑ NEGATIVE-CHECKED AND FOUND TO PROVE LESS THAN IT LOOKS. Removing the
    // hasTypeRange() guard leaves this case GREEN: a float type's range is
    // plus or minus 3.4e38, so no pixel of a real image ever reaches it. The
    // guard stays because it states the rule rather than relying on that
    // accident, but what this case pins is the property, not the mechanism.
    ImageRecord record = eightBitGrey();
    record.scalarType = VTK_FLOAT;
    QVERIFY(!record.hasTypeRange());

    const PixelReading reading = pixelReading(record, 3, 4, {240.0});
    QVERIFY(reading.atDataCeiling);       // the image's own extreme still holds
    QVERIFY2(!reading.atTypeCeiling, "a float image was given a type limit");
}

void TestPixelReadout::every_channel_of_a_colour_pixel_is_reported_and_none_is_invented()
{
    // ⚑ No luminance. A single number for a colour pixel is one the file does
    // not contain, and which of the several conventions produced it would be
    // invisible.
    ImageRecord record = eightBitGrey();
    record.components = 3;

    const PixelReading reading = pixelReading(record, 3, 4, {137.0, 92.0, 40.0});
    QCOMPARE(reading.components.size(), 3);

    const QString said = allText(pixelReadoutLines(reading, record));
    for (const QString &channel : {QStringLiteral("137"), QStringLiteral("92"),
                                   QStringLiteral("40")}) {
        QVERIFY2(said.contains(channel), qPrintable(said));
    }
}

void TestPixelReadout::a_position_off_the_picture_reads_as_absent_rather_than_as_zero()
{
    // ⚑ The standing rule of this code base, one readout further out: zero is a
    // real, dark, perfectly plausible pixel value.
    const ImageRecord record = eightBitGrey();
    const PixelReading reading = pixelReading(record, 3, 4, {});
    QVERIFY(!reading.valid);
    QVERIFY(reading.components.isEmpty());

    const QVector<ReadoutLine> lines = pixelReadoutLines(reading, record);
    QVERIFY2(!lines.isEmpty(), "an absent reading emptied the panel instead of "
                               "saying so");
    QVERIFY2(!allText(lines).contains(QStringLiteral("0 of")), qPrintable(allText(lines)));
}

QTEST_MAIN(TestPixelReadout)
#include "test_pixel_readout.moc"
