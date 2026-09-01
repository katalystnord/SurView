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

    // Adjusting a region that already exists, rather than drawing it again.
    void the_corner_under_the_pointer_is_the_one_that_gets_grabbed();
    void a_pointer_far_from_every_corner_grabs_none_of_them();
    void the_nearest_corner_wins_when_two_are_within_reach();
    void moving_a_corner_leaves_every_other_corner_alone();
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

void TestRoi::the_corner_under_the_pointer_is_the_one_that_gets_grabbed()
{
    // Until now a region could only be redrawn from scratch. Getting one corner
    // slightly wrong meant placing all of them again, which is why the corners
    // are grabbable at all.
    RegionOfInterest roi;
    roi.vertices << QPoint(10, 10) << QPoint(90, 10)
                 << QPoint(90, 70) << QPoint(10, 70);

    QCOMPARE(cornerNear(roi, QPoint(10, 10), 6.0), 0);
    QCOMPARE(cornerNear(roi, QPoint(88, 12), 6.0), 1);
    QCOMPARE(cornerNear(roi, QPoint(11, 69), 6.0), 3);
}

void TestRoi::a_pointer_far_from_every_corner_grabs_none_of_them()
{
    RegionOfInterest roi;
    roi.vertices << QPoint(10, 10) << QPoint(90, 10)
                 << QPoint(90, 70) << QPoint(10, 70);

    // The middle of the region is not a corner. Grabbing the nearest one
    // regardless of distance would mean a click anywhere inside the boundary
    // silently moved a corner the user was nowhere near.
    QCOMPARE(cornerNear(roi, QPoint(50, 40), 6.0), -1);
    // On an edge, between two corners, is also not a corner.
    QCOMPARE(cornerNear(roi, QPoint(50, 10), 6.0), -1);

    QCOMPARE(cornerNear(RegionOfInterest(), QPoint(0, 0), 6.0), -1);
}

void TestRoi::the_nearest_corner_wins_when_two_are_within_reach()
{
    RegionOfInterest roi;
    // Deliberately close together, as happens when a region is drawn small or
    // the view is zoomed out.
    roi.vertices << QPoint(10, 10) << QPoint(16, 10) << QPoint(13, 40);

    QCOMPARE(cornerNear(roi, QPoint(11, 10), 8.0), 0);
    QCOMPARE(cornerNear(roi, QPoint(15, 10), 8.0), 1);
}

void TestRoi::moving_a_corner_leaves_every_other_corner_alone()
{
    RegionOfInterest roi;
    roi.vertices << QPoint(10, 10) << QPoint(90, 10)
                 << QPoint(90, 70) << QPoint(10, 70);
    roi.origin = RegionOfInterest::Detected;
    roi.limitation = QStringLiteral("a detector's caveat");

    const RegionOfInterest moved = withCornerMoved(roi, 1, QPoint(95, 5));

    QCOMPARE(moved.vertices.size(), 4);
    QCOMPARE(moved.vertices.at(1), QPoint(95, 5));
    QCOMPARE(moved.vertices.at(0), roi.vertices.at(0));
    QCOMPARE(moved.vertices.at(2), roi.vertices.at(2));
    QCOMPARE(moved.vertices.at(3), roi.vertices.at(3));

    // ⚑ A region a person has adjusted is no longer the region the detector
    // proposed, and it must stop claiming to be: the origin decides what the
    // project says about it and what an exported file records, and the
    // detector's own caveat about holes no longer describes this shape.
    QCOMPARE(moved.origin, RegionOfInterest::Drawn);
    QVERIFY2(moved.limitation.isEmpty(), qPrintable(moved.limitation));

    // An index nobody has returns the region untouched rather than growing it.
    QCOMPARE(withCornerMoved(roi, 9, QPoint(0, 0)).vertices, roi.vertices);
    QCOMPARE(withCornerMoved(roi, -1, QPoint(0, 0)).vertices, roi.vertices);
}

QTEST_MAIN(TestRoi)
#include "test_roi.moc"
