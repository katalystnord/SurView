// Auto-detection: proposing a region by segmenting the speckled part of an image.
//
// Written because coverage said this file was at 0% -- the feature shipped with
// a button, a log line and a stated limitation, and nothing that checked any of
// it. The interesting half is not that it finds a region; it is that it DECLINES
// when there is nothing to find, and says why in terms a user can act on. A
// detector that manufactures a plausible region from sensor noise is worse than
// one that finds nothing, because the result looks like an answer.

#include "core/Roi.h"
#include "core/RoiDetect.h"

#include <QTest>

namespace {

QString fixture(const QString &name)
{
    return QStringLiteral(SURVIEW_TEST_FIXTURES "/") + name;
}

}  // namespace

class TestRoiDetect : public QObject
{
    Q_OBJECT

private slots:
    void a_speckled_patch_on_a_plain_background_is_found();
    void the_found_region_covers_the_patch_and_not_the_whole_frame();
    void a_detected_region_is_marked_as_detected_not_drawn();
    void a_detected_region_carries_the_detectors_own_limitation();
    void a_blank_frame_is_declined_rather_than_guessed_at();
    void a_refusal_explains_what_was_looked_for();
    void an_unreadable_file_is_declined_not_crashed();
    void the_time_the_pass_took_is_reported();
};

void TestRoiDetect::a_speckled_patch_on_a_plain_background_is_found()
{
    const RoiDetection detection =
        detectSpeckleRegion(fixture(QStringLiteral("speckle_patch.tif")));

    QVERIFY2(detection.found, qPrintable(detection.reason));
    QVERIFY(detection.roi.isValid());
    QVERIFY(detection.roi.vertices.size() >= 3);
    QVERIFY(detection.reason.isEmpty());
}

void TestRoiDetect::the_found_region_covers_the_patch_and_not_the_whole_frame()
{
    // The fixture's speckle occupies rows 40..159 and columns 60..209 of a
    // 260x200 frame. A detector that simply returned the whole image would pass
    // "it found something" while being useless, so the region is checked
    // against where the speckle actually is.
    const RoiDetection detection =
        detectSpeckleRegion(fixture(QStringLiteral("speckle_patch.tif")));
    QVERIFY2(detection.found, qPrintable(detection.reason));

    const QRect box = detection.roi.bounds();

    // Inside the frame at all.
    QVERIFY(box.left() >= 0);
    QVERIFY(box.top() >= 0);
    QVERIFY(box.right() < 260);
    QVERIFY(box.bottom() < 200);

    // Genuinely narrower than the frame in both axes -- it found a REGION.
    QVERIFY2(box.width() < 240, qPrintable(QString::number(box.width())));
    QVERIFY2(box.height() < 190, qPrintable(QString::number(box.height())));

    // And it is where the speckle is, within the slack a morphological cleanup
    // and a contour simplification are entitled to.
    QVERIFY2(qAbs(box.left() - 60) < 25, qPrintable(QString::number(box.left())));
    QVERIFY2(qAbs(box.top() - 40) < 25, qPrintable(QString::number(box.top())));
    QVERIFY2(qAbs(box.right() - 209) < 25, qPrintable(QString::number(box.right())));
    QVERIFY2(qAbs(box.bottom() - 159) < 25, qPrintable(QString::number(box.bottom())));
}

void TestRoiDetect::a_detected_region_is_marked_as_detected_not_drawn()
{
    // Provenance has to survive the trip: a boundary an algorithm proposed and
    // one a person drew are different kinds of claim, and the difference is
    // reported to the user.
    const RoiDetection detection =
        detectSpeckleRegion(fixture(QStringLiteral("speckle_patch.tif")));
    QVERIFY(detection.found);
    QCOMPARE(detection.roi.origin, RegionOfInterest::Detected);
    QVERIFY(detection.roi.originText() != RegionOfInterest().originText());
}

void TestRoiDetect::a_detected_region_carries_the_detectors_own_limitation()
{
    // The detector cannot represent a hole or a second patch. That is carried
    // with the region rather than mentioned once and forgotten, because the
    // region outlives the moment it was proposed.
    const RoiDetection detection =
        detectSpeckleRegion(fixture(QStringLiteral("speckle_patch.tif")));
    QVERIFY(detection.found);
    QVERIFY(!detection.roi.limitation.isEmpty());
    QVERIFY(detection.roi.limitation.contains(QStringLiteral("hole"),
                                              Qt::CaseInsensitive));
}

void TestRoiDetect::a_blank_frame_is_declined_rather_than_guessed_at()
{
    // ⚑ The case that matters most. The segmentation normalises contrast before
    // thresholding, which turns pure sensor noise into a full-range image, so
    // "there is nothing here" is a real opportunity to report a confident,
    // meaningless region. It must refuse.
    const RoiDetection detection =
        detectSpeckleRegion(fixture(QStringLiteral("blank_frame.tif")));

    QVERIFY2(!detection.found,
             "a frame with no speckle produced a region anyway");
    QVERIFY(!detection.roi.isValid());
    QVERIFY(detection.roi.vertices.isEmpty());
}

void TestRoiDetect::a_refusal_explains_what_was_looked_for()
{
    // A bare "detection failed" leaves nothing to act on. The detector declines
    // for several distinct reasons and does not report which, so the message
    // states its conditions and names the alternative.
    const RoiDetection detection =
        detectSpeckleRegion(fixture(QStringLiteral("blank_frame.tif")));

    QVERIFY(!detection.found);
    QVERIFY(!detection.reason.isEmpty());
    QVERIFY(detection.reason.length() > 40);
    QVERIFY(detection.reason.contains(QStringLiteral("hand"), Qt::CaseInsensitive));
}

void TestRoiDetect::an_unreadable_file_is_declined_not_crashed()
{
    const RoiDetection detection =
        detectSpeckleRegion(fixture(QStringLiteral("no_such_image.tif")));

    QVERIFY(!detection.found);
    QVERIFY(!detection.reason.isEmpty());
}

void TestRoiDetect::the_time_the_pass_took_is_reported()
{
    // Reported because it is a whole-image gradient and segmentation pass that
    // blocks the window, and a wait the user sat through should be accounted
    // for rather than silent.
    const RoiDetection detection =
        detectSpeckleRegion(fixture(QStringLiteral("speckle_patch.tif")));
    QVERIFY(detection.found);
    QVERIFY(detection.secondsElapsed >= 0.0);
    QVERIFY(detection.secondsElapsed < 60.0);
}

QTEST_MAIN(TestRoiDetect)
#include "test_roi_detect.moc"
