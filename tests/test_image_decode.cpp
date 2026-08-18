// ⚑ The row-order regression.
//
// WHY THIS EXISTS. VTK's image readers disagree about which way up they hand
// the rows over: vtkTIFFReader emits the file's rows in file order, while the
// PNG, JPEG and BMP readers emit them bottom-up. Nothing said so, so TIFFs --
// the usual speckle format -- were displayed vertically mirrored, and a
// measured field drawn in engine coordinates lined up with a PNG and not with a
// TIFF. Neither is visible on a speckle pattern, which is exactly why it
// survived: it was found by reading a known image through each reader, not by
// looking at the screen.
//
// The fixtures are committed rather than generated, because generating them
// needs a writer whose own row convention would then be the thing under test.
// tests/fixtures/row_order_marker.{tif,png} are 8x8, bright 3x3 block at the
// picture's TOP-LEFT and a single mid-grey pixel at the TOP-RIGHT -- asymmetric
// in both axes, so a flip in either direction, or a 180-degree rotation, is
// distinguishable.

#include "core/ImageDecode.h"
#include "core/ImageRecord.h"

#include <QTest>

#include <vtkImageData.h>

namespace {

constexpr int kMarker = 255;   // top-left 3x3
constexpr int kTell   = 128;   // single pixel, top-right

int sampleAt(vtkImageData *image, int x, int y)
{
    return int(image->GetScalarComponentAsFloat(x, y, 0, 0));
}

QString fixture(const QString &name)
{
    return QStringLiteral(SURVIEW_TEST_FIXTURES "/") + name;
}

}  // namespace

class TestImageDecode : public QObject
{
    Q_OBJECT

private slots:
    void a_tiff_arrives_with_the_pictures_top_row_first();
    void a_png_arrives_with_the_pictures_top_row_first();
    void both_formats_agree_pixel_for_pixel();
    void the_record_says_whether_the_decoder_had_to_be_corrected();
    void the_origin_is_reset_after_a_flip();
    void an_unreadable_file_still_leaves_provenance();
};

// Shown to fail without the fix, which is the only thing that makes the green
// mean anything: with the normalisation removed, both_formats_agree_pixel_for_
// pixel, a_png_arrives_with_the_pictures_top_row_first and
// the_record_says_whether_the_decoder_had_to_be_corrected all fail.
//
// Honest limit of this fixture: a_tiff_arrives_... still PASSES without the fix,
// because this TIFF carries a TOPLEFT orientation tag and vtkTIFFReader's
// default already honours it. The explicit pin guards against a TIFF that
// declares something else; no such fixture exists here yet, so that specific
// guard is asserted by construction rather than by a red-to-green test.
void TestImageDecode::a_tiff_arrives_with_the_pictures_top_row_first()
{
    ImageRecord record;
    auto image = decodeImage(fixture(QStringLiteral("row_order_marker.tif")), record);

    QVERIFY(image);
    QCOMPARE(record.width, 8);
    QCOMPARE(record.height, 8);

    // Index (x=1, y=1) must be INSIDE the bright block, i.e. row 0 of the file
    // is row 0 of the decoded image.
    QCOMPARE(sampleAt(image, 1, 1), kMarker);
    QCOMPARE(sampleAt(image, 1, 6), 0);
    QCOMPARE(sampleAt(image, 7, 0), kTell);
}

void TestImageDecode::a_png_arrives_with_the_pictures_top_row_first()
{
    ImageRecord record;
    auto image = decodeImage(fixture(QStringLiteral("row_order_marker.png")), record);

    QVERIFY(image);
    QCOMPARE(sampleAt(image, 1, 1), kMarker);
    QCOMPARE(sampleAt(image, 1, 6), 0);
    QCOMPARE(sampleAt(image, 7, 0), kTell);
}

void TestImageDecode::both_formats_agree_pixel_for_pixel()
{
    // The point of the normalisation: the same picture in two formats must be
    // the same array afterwards. This is the assertion that fails outright
    // against the old behaviour, where the two came out mirrored relative to
    // each other.
    ImageRecord tiffRecord;
    ImageRecord pngRecord;
    auto tiff = decodeImage(fixture(QStringLiteral("row_order_marker.tif")), tiffRecord);
    auto png  = decodeImage(fixture(QStringLiteral("row_order_marker.png")), pngRecord);

    QVERIFY(tiff);
    QVERIFY(png);
    QCOMPARE(tiffRecord.width, pngRecord.width);
    QCOMPARE(tiffRecord.height, pngRecord.height);

    for (int y = 0; y < tiffRecord.height; y++) {
        for (int x = 0; x < tiffRecord.width; x++) {
            QCOMPARE(sampleAt(tiff, x, y), sampleAt(png, x, y));
        }
    }
}

void TestImageDecode::the_record_says_whether_the_decoder_had_to_be_corrected()
{
    // A reordering is not a conversion, but it IS a change to what the decoder
    // produced, so the record states it rather than leaving it implicit.
    ImageRecord tiffRecord;
    decodeImage(fixture(QStringLiteral("row_order_marker.tif")), tiffRecord);
    QVERIFY(!tiffRecord.rowsReversedByDecoder);
    QCOMPARE(tiffRecord.decoderClass, QStringLiteral("vtkTIFFReader"));

    ImageRecord pngRecord;
    decodeImage(fixture(QStringLiteral("row_order_marker.png")), pngRecord);
    QVERIFY(pngRecord.rowsReversedByDecoder);
    QCOMPARE(pngRecord.decoderClass, QStringLiteral("vtkPNGReader"));
}

void TestImageDecode::the_origin_is_reset_after_a_flip()
{
    // vtkImageFlip mirrors about the input's centre and leaves the origin at
    // -(height-1). Carried forward, every later coordinate -- a region's
    // corners, a measured point -- would inherit that offset.
    ImageRecord record;
    auto png = decodeImage(fixture(QStringLiteral("row_order_marker.png")), record);

    QVERIFY(png);
    double origin[3] = {-1.0, -1.0, -1.0};
    png->GetOrigin(origin);
    QCOMPARE(origin[0], 0.0);
    QCOMPARE(origin[1], 0.0);
}

void TestImageDecode::an_unreadable_file_still_leaves_provenance()
{
    // A file we could not decode was still named and looked for; saying so
    // beats reporting nothing at all.
    ImageRecord record;
    auto image = decodeImage(fixture(QStringLiteral("no_such_file.tif")), record);

    QVERIFY(!image);
    QVERIFY(!record.isValid());
    QCOMPARE(record.fileName, QStringLiteral("no_such_file.tif"));
}

QTEST_MAIN(TestImageDecode)
#include "test_image_decode.moc"
