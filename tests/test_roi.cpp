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

    // Regions with holes: an outer boundary minus the places not to measure.
    void a_region_without_holes_is_unchanged_by_the_hole_machinery();
    void a_point_inside_a_hole_is_outside_the_region();
    void a_hole_needs_three_corners_like_any_other_ring();
    void the_regions_bounds_are_the_outer_boundarys_alone();
    void a_subset_reaching_into_a_hole_is_reported_not_hidden();
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

void TestRoi::a_region_without_holes_is_unchanged_by_the_hole_machinery()
{
    // Every region drawn before holes existed must behave exactly as it did.
    RegionOfInterest square;
    square.vertices = {QPoint(0, 0), QPoint(100, 0), QPoint(100, 100), QPoint(0, 100)};

    QVERIFY(square.isValid());
    QVERIFY(!square.hasHoles());
    QVERIFY(regionContains(square, 50, 50));
    QVERIFY(!regionContains(square, 150, 50));
}

void TestRoi::a_point_inside_a_hole_is_outside_the_region()
{
    // ⚑ THE WHOLE POINT. A specimen with a hole through it shows BACKGROUND
    // there, and background does not move with the specimen. A point measured
    // in a hole correlates the picture behind the specimen against itself and
    // reports, confidently, that nothing moved -- which on a strain map is a
    // cold spot exactly where the stress concentrates.
    RegionOfInterest region;
    region.vertices = {QPoint(0, 0), QPoint(100, 0), QPoint(100, 100), QPoint(0, 100)};
    region.holes.append({QPoint(40, 40), QPoint(60, 40), QPoint(60, 60), QPoint(40, 60)});

    QVERIFY(region.hasHoles());
    QVERIFY2(regionContains(region, 10, 10), "outside the hole is still inside");
    QVERIFY2(!regionContains(region, 50, 50), "inside the hole is outside the region");
    QVERIFY2(!regionContains(region, 150, 50), "outside the outer boundary is outside");
}

void TestRoi::a_hole_needs_three_corners_like_any_other_ring()
{
    // A hole of two corners encloses nothing, and a ring that encloses nothing
    // cannot exclude anything. Dropped rather than carried, so nothing
    // downstream has to keep asking whether a hole is real.
    RegionOfInterest region;
    region.vertices = {QPoint(0, 0), QPoint(100, 0), QPoint(100, 100), QPoint(0, 100)};
    region.holes.append({QPoint(40, 40), QPoint(60, 40)});

    QVERIFY2(!region.hasHoles(), "a two-corner hole is not a hole");
    QVERIFY2(regionContains(region, 50, 50), "and it excludes nothing");
}

void TestRoi::the_regions_bounds_are_the_outer_boundarys_alone()
{
    // A hole is inside the outer ring by construction, so it cannot enlarge the
    // bounds -- and a bounds that grew to include one would put the grid's
    // origin somewhere no point can be placed.
    RegionOfInterest region;
    region.vertices = {QPoint(10, 10), QPoint(90, 10), QPoint(90, 90), QPoint(10, 90)};
    const QRect withoutHole = region.bounds();

    region.holes.append({QPoint(40, 40), QPoint(60, 40), QPoint(60, 60), QPoint(40, 60)});
    QCOMPARE(region.bounds(), withoutHole);
}

void TestRoi::a_subset_reaching_into_a_hole_is_reported_not_hidden()
{
    // ⚑ Excluding a point whose CENTRE is in a hole does not stop a point just
    // outside one from correlating over a subset that reaches in. Those pixels
    // are background, and they drag the answer toward no movement -- a
    // plausible number, which is the dangerous kind.
    //
    // Not silently excluded, because the same is already true and accepted at
    // the outer boundary, and quietly applying a stricter rule to holes would
    // make two boundaries of one region behave differently for no stated
    // reason. Counted instead, so the run can say so and a reader can widen the
    // hole or accept it.
    RegionOfInterest region;
    region.vertices = {QPoint(0, 0), QPoint(100, 0), QPoint(100, 100), QPoint(0, 100)};
    region.holes.append({QPoint(40, 40), QPoint(60, 40), QPoint(60, 60), QPoint(40, 60)});

    QVERIFY2(subsetReachesAHole(region, 30, 50, 16),
             "a point 10 px from the hole with a 16 px subset reaches into it");
    QVERIFY2(!subsetReachesAHole(region, 10, 50, 16),
             "a point 30 px away with a 16 px subset does not");
    QVERIFY2(!subsetReachesAHole(region, 10, 50, 16),
             "and a region with no holes reaches none");
}

QTEST_MAIN(TestRoi)
#include "test_roi.moc"
