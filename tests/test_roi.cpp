// What a region of interest is, before anything measures inside it.
//
// Small surface, but it is the thing every later number is scoped by: a
// bounding box that is one pixel short silently drops a row of measurements
// off the edge of the region a user drew.

#include "core/Roi.h"

#include <QTest>

class TestRoi : public QObject
{
    Q_OBJECT

private slots:
    void a_region_needs_three_corners_to_enclose_anything();
    void bounds_include_the_pixels_the_corners_sit_on();
    void bounds_of_an_empty_region_are_null();
    void a_region_reports_how_it_came_to_exist();
    void a_detected_region_carries_its_makers_limitation();
};

void TestRoi::a_region_needs_three_corners_to_enclose_anything()
{
    RegionOfInterest none;
    QVERIFY(!none.isValid());

    RegionOfInterest one;
    one.vertices = {QPoint(5, 5)};
    QVERIFY(!one.isValid());

    RegionOfInterest two;
    two.vertices = {QPoint(5, 5), QPoint(9, 5)};
    QVERIFY(!two.isValid());

    RegionOfInterest three;
    three.vertices = {QPoint(5, 5), QPoint(9, 5), QPoint(9, 9)};
    QVERIFY(three.isValid());
}

void TestRoi::bounds_include_the_pixels_the_corners_sit_on()
{
    RegionOfInterest roi;
    roi.vertices = {QPoint(10, 20), QPoint(30, 20), QPoint(30, 50), QPoint(10, 50)};

    const QRect box = roi.bounds();
    QCOMPARE(box.left(), 10);
    QCOMPARE(box.top(), 20);
    QCOMPARE(box.right(), 30);
    QCOMPARE(box.bottom(), 50);

    // Inclusive of both edges: corners at x=10 and x=30 span 21 pixels, not 20.
    // Reported to the user as the region's size, so an off-by-one here is a
    // wrong number on screen.
    QCOMPARE(box.width(), 21);
    QCOMPARE(box.height(), 31);
}

void TestRoi::bounds_of_an_empty_region_are_null()
{
    QVERIFY(RegionOfInterest().bounds().isNull());
}

void TestRoi::a_region_reports_how_it_came_to_exist()
{
    // Provenance is carried, not inferred later: a boundary a person drew and
    // one an algorithm proposed are different kinds of claim.
    RegionOfInterest drawn;
    drawn.origin = RegionOfInterest::Drawn;
    RegionOfInterest detected;
    detected.origin = RegionOfInterest::Detected;

    QVERIFY(!drawn.originText().isEmpty());
    QVERIFY(!detected.originText().isEmpty());
    QVERIFY(drawn.originText() != detected.originText());
}

void TestRoi::a_detected_region_carries_its_makers_limitation()
{
    // A drawn region has nothing to qualify; a detected one must be able to
    // carry what its detector could not guarantee, all the way to the reader.
    RegionOfInterest drawn;
    QVERIFY(drawn.limitation.isEmpty());

    RegionOfInterest detected;
    detected.origin = RegionOfInterest::Detected;
    detected.limitation = QStringLiteral("single outline, no holes");
    QCOMPARE(detected.limitation, QStringLiteral("single outline, no holes"));
}

QTEST_MAIN(TestRoi)
#include "test_roi.moc"
