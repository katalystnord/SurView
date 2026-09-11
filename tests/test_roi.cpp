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
    void a_subset_that_just_touches_a_hole_is_reported();
    void a_region_with_no_holes_has_no_subset_reaching_one();
    void two_corners_the_same_distance_away_resolve_the_same_way_every_time();
    void a_corner_exactly_at_the_edge_of_reach_is_still_within_it();

    // The crossing test itself, from the sweep of 2026-09-09: which points the
    // run measures at all is decided here, so a wrong answer puts subsets on
    // background that never moves, and background correlates confidently
    // against itself.
    //
    // ⚑ Two mutants in this file survive on purpose, both equivalent by an
    // argument about parity rather than by an input nobody has found yet:
    //
    //   ring.size() < 3 -> < 2      A two-corner ring has two edges that are
    //                               the same segment walked twice, so any
    //                               crossing is counted twice and the parity
    //                               is always even. It can never report
    //                               "inside", guard or no guard.
    //   (yi > y) -> (yi >= y)       A vertex sitting exactly on the scanline
    //                               makes BOTH its edges cross where neither
    //                               did. Two crossings cancel, for the same
    //                               reason. This robustness is why the
    //                               convention is a strict comparison.
    //
    // The same two survive PatternFab's copy of this algorithm, which is where
    // the arguments were first worked out.
    //
    // ⚑ A THIRD IS UNREACHABLE, and it is worth saying exactly why, because a
    // case below carries a name that suggests otherwise. ringContains() returns
    // false for a ring of fewer than three corners, and flipping that to true
    // changes nothing: the function is private to this file, the outer ring
    // reaches it only after isValid() has already required three vertices, and
    // a hole reaches it only after its own size test. So
    // a_ring_of_fewer_than_three_corners_encloses_nothing passes for a
    // different reason than its name gives -- what refuses a two-corner region
    // is isValid(), not the guard inside the crossing test. Both are worth
    // keeping; only one of them is being tested.
    void the_notch_of_a_concave_region_is_outside_it();
    void a_slanted_boundary_is_crossed_where_it_actually_lies();
    void a_point_exactly_on_a_boundary_is_decided_the_same_way_every_time();
    void a_ring_of_fewer_than_three_corners_encloses_nothing();
    void a_subset_of_no_size_reaches_only_what_it_sits_on();
    void moving_a_corner_that_does_not_exist_leaves_the_region_alone();
    void the_first_corner_of_a_region_moves_like_any_other();
    void a_corner_can_be_added_to_an_edge_without_redrawing_the_region();
    void a_corner_can_be_taken_out_unless_it_is_one_of_the_last_three();
    void the_edge_under_the_pointer_is_the_one_a_new_corner_joins();
    void a_subset_reaches_a_hole_above_it_as_readily_as_one_beside_it();
    void a_triangular_hole_is_a_hole_wherever_the_question_is_asked();
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

    // ⚑ AND WHICH IS WHICH, which asking only that they DIFFER cannot see. The
    // two sentences could be swapped wholesale and this case would still pass,
    // while the viewport told a user that the boundary they drew by hand was
    // detected from the speckle pattern - and that the proposal an algorithm
    // made was their own work. That is provenance reported backwards, on the
    // one line that says where the region came from, and a detected region is
    // the one carrying a caveat about what its detector could not guarantee.
    QVERIFY2(drawn.originText().contains(QStringLiteral("drawn"),
                                         Qt::CaseInsensitive),
             qPrintable(drawn.originText()));
    QVERIFY2(detected.originText().contains(QStringLiteral("detected"),
                                            Qt::CaseInsensitive),
             qPrintable(detected.originText()));
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
}


void TestRoi::a_subset_that_just_touches_a_hole_is_reported()
{
    // ⚑ The case above tests a subset 10 px into a hole and one 30 px clear,
    // and neither can see the subset's own SIZE: it is a square of side
    // 2r + 1 about the point, and both of those answers survive that becoming
    // 2r or 2r + 2. The boundary is the only place the width shows, and six
    // mutants sat on it.
    //
    // The hole spans x 40..60. A point at x = 23 with a 16 px subset reaches
    // x = 39, one pixel short. At x = 24 it reaches exactly 40, the hole's own
    // edge, and touching is reaching: those pixels are background.
    RegionOfInterest region;
    region.vertices = {QPoint(0, 0), QPoint(100, 0), QPoint(100, 100), QPoint(0, 100)};
    region.holes.append({QPoint(40, 40), QPoint(60, 40), QPoint(60, 60), QPoint(40, 60)});

    QVERIFY2(!subsetReachesAHole(region, 23, 50, 16),
             "a subset ending one pixel short of the hole does not reach it");
    QVERIFY2(subsetReachesAHole(region, 24, 50, 16),
             "a subset whose edge lands exactly on the hole's edge does");
}

void TestRoi::a_region_with_no_holes_has_no_subset_reaching_one()
{
    // ⚑ This is what the case above MEANT to check and never did. Its third
    // assertion repeated the second call verbatim, region and all, under a
    // message about a region with no holes -- so the no-holes path had never
    // been run. Found by the mutation sweep, not by reading the file.
    RegionOfInterest region;
    region.vertices = {QPoint(0, 0), QPoint(100, 0), QPoint(100, 100), QPoint(0, 100)};

    QVERIFY2(!subsetReachesAHole(region, 50, 50, 16),
             "a region with no holes has nothing for a subset to reach into");
}

void TestRoi::two_corners_the_same_distance_away_resolve_the_same_way_every_time()
{
    // ⚑ Roi.cpp states this rule in a comment -- "the first of two equally
    // close corners wins and the answer does not depend on the order they
    // happen to be stored in" -- and nothing enforced it. A comment may say
    // WHY a mechanism is what it is; it may not assert what the design
    // requires unless a test of that name enforces it, and now one does.
    //
    // The existing case puts the pointer 1 px from one corner and 5 from the
    // other, so it never meets a tie at all.
    RegionOfInterest roi;
    roi.vertices << QPoint(10, 10) << QPoint(20, 10) << QPoint(15, 40);

    // Exactly between the first two, and 5 px from each.
    QCOMPARE(cornerNear(roi, QPoint(15, 10), 8.0), 0);

    // The same two corners stored the other way round still hand back index 0,
    // which is the point: the FIRST of the tied pair wins, so the answer is a
    // property of the list rather than of floating-point luck.
    RegionOfInterest swapped;
    swapped.vertices << QPoint(20, 10) << QPoint(10, 10) << QPoint(15, 40);
    QCOMPARE(cornerNear(swapped, QPoint(15, 10), 8.0), 0);
}

void TestRoi::a_corner_exactly_at_the_edge_of_reach_is_still_within_it()
{
    // The boundary of the grab radius, which no existing case lands on: at
    // exactly `reach` the corner is grabbable, a pixel further out it is not.
    RegionOfInterest roi;
    roi.vertices << QPoint(10, 10) << QPoint(90, 10) << QPoint(90, 70);

    QCOMPARE(cornerNear(roi, QPoint(18, 10), 8.0), 0);
    QCOMPARE(cornerNear(roi, QPoint(19, 10), 8.0), -1);
}


void TestRoi::the_notch_of_a_concave_region_is_outside_it()
{
    // ⚑ A square is its OWN bounding box, so almost any broken crossing test
    // still answers one correctly -- and every region fixture in this file is
    // a square. An L has its notch inside the bounding box and outside the
    // shape, which is the smallest fixture that can tell a real crossing test
    // from a box test.
    //
    // What is at stake: this decides which points the run measures at all. A
    // region read as its own bounding box puts subsets on background that
    // never moves, and those correlate confidently against themselves.
    RegionOfInterest ell;
    ell.vertices = {QPoint(0, 0), QPoint(100, 0), QPoint(100, 40),
                    QPoint(40, 40), QPoint(40, 100), QPoint(0, 100)};

    QVERIFY2(regionContains(ell, 20, 20), "the corner of the L is inside it");
    QVERIFY2(regionContains(ell, 80, 20), "the foot of the L is inside it");
    QVERIFY2(regionContains(ell, 20, 80), "the upright of the L is inside it");
    QVERIFY2(!regionContains(ell, 80, 80),
             "the notch is outside the region even though it is inside its bounding box");
}

void TestRoi::a_slanted_boundary_is_crossed_where_it_actually_lies()
{
    // The hypotenuse of this triangle runs from (0, 100) to (100, 0), so at
    // y = 50 it sits at x = 50. Points either side of that are the only thing
    // that can catch the edge interpolation computed with the wrong
    // difference, and a region whose every edge is axis-aligned never
    // exercises the arithmetic at all.
    RegionOfInterest triangle;
    triangle.vertices = {QPoint(0, 0), QPoint(100, 0), QPoint(0, 100)};

    QVERIFY2(regionContains(triangle, 10, 10), "well inside the triangle is inside");
    QVERIFY2(regionContains(triangle, 45, 50), "just inside the slanted edge is inside");
    QVERIFY2(!regionContains(triangle, 55, 50), "just outside the slanted edge is outside");
    QVERIFY2(!regionContains(triangle, 90, 90),
             "beyond the hypotenuse is outside, which only the closing edge can decide");
}

void TestRoi::a_point_exactly_on_a_boundary_is_decided_the_same_way_every_time()
{
    // ⚑ A grid lands on round numbers, so a boundary at a round number is a
    // place points really sit. The rule is half-open -- the region owns one of
    // each pair of opposite edges and not the other -- which is what stops two
    // abutting regions from either double-counting a point or dropping it.
    // Which way round it falls is a fact about this code, checked here rather
    // than assumed, and it is the opposite of what a first reading suggests.
    RegionOfInterest square;
    square.vertices = {QPoint(0, 0), QPoint(100, 0), QPoint(100, 100), QPoint(0, 100)};

    QVERIFY2(regionContains(square, 0, 50), "a point on the left boundary is inside");
    QVERIFY2(!regionContains(square, 100, 50), "a point on the right boundary is not");
}

void TestRoi::a_ring_of_fewer_than_three_corners_encloses_nothing()
{
    // Two corners are a line, and a line has no inside. Asked anyway -- which
    // happens while a region is still being drawn -- the answer is no, for
    // every position, rather than whatever the crossing count of a degenerate
    // ring happens to come to.
    RegionOfInterest line;
    line.vertices = {QPoint(0, 0), QPoint(100, 0)};
    QVERIFY2(!regionContains(line, 50, 0), "a two-corner ring contains nothing, not even its own line");
    QVERIFY2(!regionContains(line, 50, 50), "nor anything else");

    RegionOfInterest single;
    single.vertices = {QPoint(0, 0)};
    QVERIFY2(!regionContains(single, 0, 0), "and one corner contains nothing either");

    const RegionOfInterest nothing;
    QVERIFY2(!regionContains(nothing, 0, 0), "and an empty region contains nothing");
}

void TestRoi::a_subset_of_no_size_reaches_only_what_it_sits_on()
{
    // ⚑ A radius of zero is one pixel, not none: the subset is 2r + 1 across,
    // so at r = 0 it is the point itself. The existing cases use a 16 px
    // radius, where a square of 32 and one of 33 both reach a hole 10 px away
    // and neither the width nor its "+ 1" can be seen.
    RegionOfInterest region;
    region.vertices = {QPoint(0, 0), QPoint(100, 0), QPoint(100, 100), QPoint(0, 100)};
    region.holes.append({QPoint(40, 40), QPoint(60, 40), QPoint(60, 60), QPoint(40, 60)});

    QVERIFY2(subsetReachesAHole(region, 40, 50, 0),
             "a subset of no radius sitting exactly on the hole's edge reaches it");
    QVERIFY2(!subsetReachesAHole(region, 39, 50, 0),
             "and one pixel outside it does not");

    // A negative radius is not a subset at all, and must not be turned into
    // one by the arithmetic that follows.
    QVERIFY2(!subsetReachesAHole(region, 50, 50, -1),
             "a negative radius reaches nothing, even sitting inside the hole");
}

void TestRoi::moving_a_corner_that_does_not_exist_leaves_the_region_alone()
{
    // Both ends of the index check, on their own. An index past the end and a
    // negative one are separate conditions, and a region handed either must
    // come back unchanged rather than gaining a corner or losing one.
    RegionOfInterest roi;
    roi.vertices << QPoint(10, 10) << QPoint(90, 10) << QPoint(90, 70);

    const RegionOfInterest tooLarge = withCornerMoved(roi, 3, QPoint(0, 0));
    QCOMPARE(tooLarge.vertices, roi.vertices);

    const RegionOfInterest negative = withCornerMoved(roi, -1, QPoint(0, 0));
    QCOMPARE(negative.vertices, roi.vertices);

    // And the last real corner still moves, so the check is not simply
    // refusing everything.
    const RegionOfInterest moved = withCornerMoved(roi, 2, QPoint(5, 5));
    QCOMPARE(moved.vertices.at(2), QPoint(5, 5));
}

void TestRoi::the_first_corner_of_a_region_moves_like_any_other()
{
    // ⚑ The case above moves the LAST corner, to show the index check is not
    // simply refusing everything. It cannot see the check tightening at the
    // other end: `corner < 0` narrowed to `corner <= 0` refuses corner ZERO,
    // and a user dragging the first corner of a region they drew would find it
    // silently immovable while every other corner obeyed. Nothing on screen
    // would say why. Found standing after the sweep of 2026-09-09.
    RegionOfInterest roi;
    roi.vertices << QPoint(10, 10) << QPoint(90, 10) << QPoint(90, 70);

    const RegionOfInterest moved = withCornerMoved(roi, 0, QPoint(5, 5));
    QCOMPARE(moved.vertices.at(0), QPoint(5, 5));
    QCOMPARE(moved.vertices.at(1), roi.vertices.at(1));
    QCOMPARE(moved.vertices.at(2), roi.vertices.at(2));
}

void TestRoi::a_subset_reaches_a_hole_above_it_as_readily_as_one_beside_it()
{
    // ⚑ EVERY CASE IN THIS FILE PROBES ALONG X. The subset is built as a
    // rectangle with a width and a height, written as the same expression
    // twice, and three mutants lived in the SECOND copy: a square one pixel
    // short, one pixel over, or a whole pixel narrower in y only. A hole
    // directly above the point is the only thing that asks about its height.
    //
    // The same hole, spanning y 40..60. A point at y = 23 with a 16 px subset
    // reaches y = 39, one pixel short; at y = 24 it reaches exactly 40, and
    // touching is reaching.
    RegionOfInterest region;
    region.vertices = {QPoint(0, 0), QPoint(100, 0), QPoint(100, 100), QPoint(0, 100)};
    region.holes.append({QPoint(40, 40), QPoint(60, 40), QPoint(60, 60), QPoint(40, 60)});

    QVERIFY2(!subsetReachesAHole(region, 50, 23, 16),
             "a subset ending one pixel above the hole does not reach it");
    QVERIFY2(subsetReachesAHole(region, 50, 24, 16),
             "and one that ends exactly on its edge does");

    // And at a radius of zero, where the height's own "+ 1" is the whole
    // difference between a one-pixel subset and no subset at all.
    QVERIFY2(subsetReachesAHole(region, 50, 40, 0),
             "a subset of no radius sitting on the hole's top edge reaches it");
    QVERIFY2(!subsetReachesAHole(region, 50, 39, 0),
             "and one pixel above it does not");
}

void TestRoi::a_triangular_hole_is_a_hole_wherever_the_question_is_asked()
{
    // ⚑ THREE CORNERS IS THE SMALLEST RING THAT ENCLOSES ANYTHING, and three
    // separate places apply that rule: whether a region has holes at all,
    // whether a point falls inside one, and whether a subset reaches one. Every
    // hole in this file is a rectangle, and to a rectangle "three or more" and
    // "more than three" are the same rule -- so all three could be tightened
    // and nothing went red.
    //
    // What it costs is not a wrong number but a hole that stops existing:
    // hasHoles() answers false, the run never builds a region with holes at
    // all, and the void the user drew round is measured straight across.
    RegionOfInterest region;
    region.vertices = {QPoint(0, 0), QPoint(100, 0), QPoint(100, 100), QPoint(0, 100)};
    region.holes.append({QPoint(30, 30), QPoint(70, 30), QPoint(50, 70)});

    QVERIFY2(region.hasHoles(),
             "a region whose only hole is a triangle reported no holes at all");

    QVERIFY2(!regionContains(region, 50, 40),
             "a point well inside a triangular hole was reported inside the region");
    QVERIFY2(regionContains(region, 10, 10),
             "and a point clear of it is still inside");

    QVERIFY2(subsetReachesAHole(region, 20, 40, 12),
             "a subset reaching a triangular hole was not reported");

    // And a ring of two corners still encloses nothing, in all three, which is
    // the rule the one above must not be confused with.
    RegionOfInterest twoCorners;
    twoCorners.vertices = region.vertices;
    twoCorners.holes.append({QPoint(30, 30), QPoint(70, 30)});
    QVERIFY2(!twoCorners.hasHoles(), "a two-corner ring was counted as a hole");
    QVERIFY2(regionContains(twoCorners, 50, 30),
             "a two-corner ring excluded a point from the region");
    QVERIFY2(!subsetReachesAHole(twoCorners, 50, 30, 12),
             "a subset was said to reach a hole that encloses nothing");
}

void TestRoi::a_corner_can_be_added_to_an_edge_without_redrawing_the_region()
{
    // ⚑ ONE CORNER TOO FEW COST THE WHOLE BOUNDARY. A region could be drawn and
    // its corners moved, and that was all: following a curve a little better,
    // or working round a fixture that turned out to be in shot, meant placing
    // every corner again from the first. On a specimen outline of a dozen
    // corners that is the difference between an adjustment and a redraw.
    //
    // The new corner goes AFTER the edge's first corner, which is what keeps
    // the ring in order: inserted anywhere else the boundary crosses itself,
    // and a self-crossing ring is not a region at all - its inside is decided
    // by a parity rule that no longer means what the reader drew.
    RegionOfInterest square;
    square.vertices = {QPoint(0, 0), QPoint(100, 0), QPoint(100, 100), QPoint(0, 100)};

    const RegionOfInterest five = withCornerInserted(square, 1, QPoint(100, 50));
    QCOMPARE(five.vertices.size(), 5);
    QCOMPARE(five.vertices.at(2), QPoint(100, 50));

    // The corners either side of it are the ones the edge joined, in the order
    // they were in.
    QCOMPARE(five.vertices.at(1), QPoint(100, 0));
    QCOMPARE(five.vertices.at(3), QPoint(100, 100));

    // The shape is unchanged by a corner placed ON its edge: a point inside
    // stays inside, one outside stays outside.
    QVERIFY(regionContains(five, 50, 50));
    QVERIFY(!regionContains(five, 150, 50));

    // An index that names no edge leaves the region alone rather than growing
    // it somewhere arbitrary.
    QCOMPARE(withCornerInserted(square, -1, QPoint(50, 50)).vertices, square.vertices);
    QCOMPARE(withCornerInserted(square, 9, QPoint(50, 50)).vertices, square.vertices);

    // ⚑ And an adjusted region is no longer the detector's proposal, the same
    // rule moving a corner already follows.
    RegionOfInterest detected = square;
    detected.origin = RegionOfInterest::Detected;
    detected.limitation = QStringLiteral("single outline, no holes");
    const RegionOfInterest adjusted = withCornerInserted(detected, 0, QPoint(50, 0));
    QVERIFY(adjusted.origin == RegionOfInterest::Drawn);
    QVERIFY(adjusted.limitation.isEmpty());
}

void TestRoi::a_corner_can_be_taken_out_unless_it_is_one_of_the_last_three()
{
    // The other half: a corner placed by mistake, or one left over from a
    // boundary that has been adjusted past needing it.
    RegionOfInterest five;
    five.vertices = {QPoint(0, 0), QPoint(50, 0), QPoint(100, 0),
                     QPoint(100, 100), QPoint(0, 100)};

    const RegionOfInterest four = withCornerRemoved(five, 1);
    QCOMPARE(four.vertices.size(), 4);
    QCOMPARE(four.vertices.at(0), QPoint(0, 0));
    QCOMPARE(four.vertices.at(1), QPoint(100, 0));
    QVERIFY(regionContains(four, 50, 50));

    // ⚑ THREE CORNERS IS THE FLOOR, and the rule is the same one the drawing
    // mode states: fewer than three enclose nothing. Taking the fourth corner
    // out of a triangle would leave a region that cannot be measured in, so it
    // is refused and the region comes back as it was - rather than accepted,
    // leaving a reader with a boundary that has silently stopped being one.
    RegionOfInterest triangle;
    triangle.vertices = {QPoint(0, 0), QPoint(100, 0), QPoint(50, 100)};
    QCOMPARE(withCornerRemoved(triangle, 1).vertices, triangle.vertices);

    // An index that names no corner leaves it alone, both ends.
    QCOMPARE(withCornerRemoved(five, -1).vertices, five.vertices);
    QCOMPARE(withCornerRemoved(five, 5).vertices, five.vertices);

    // ⚑ The FIRST corner comes out like any other, which the cases above this
    // file have twice found to be where an index check is tightened by mistake.
    const RegionOfInterest withoutFirst = withCornerRemoved(five, 0);
    QCOMPARE(withoutFirst.vertices.size(), 4);
    QCOMPARE(withoutFirst.vertices.at(0), QPoint(50, 0));

    RegionOfInterest detected = five;
    detected.origin = RegionOfInterest::Detected;
    const RegionOfInterest adjusted = withCornerRemoved(detected, 1);
    QVERIFY(adjusted.origin == RegionOfInterest::Drawn);
}

void TestRoi::the_edge_under_the_pointer_is_the_one_a_new_corner_joins()
{
    // The viewport has to turn a click into an edge before it can add a corner
    // to it, and "nearest corner" is the wrong question: a click halfway along
    // a side is far from both of its ends.
    RegionOfInterest square;
    square.vertices = {QPoint(0, 0), QPoint(100, 0), QPoint(100, 100), QPoint(0, 100)};

    // Halfway down the right-hand side: the edge from corner 1 to corner 2.
    QCOMPARE(edgeNear(square, QPoint(100, 50), 6.0), 1);
    // Halfway along the top: the edge from corner 0 to corner 1.
    QCOMPARE(edgeNear(square, QPoint(50, 0), 6.0), 0);
    // ⚑ The CLOSING edge, from the last corner back to the first, which is the
    // one an implementation walking pairs of vertices forgets.
    QCOMPARE(edgeNear(square, QPoint(0, 50), 6.0), 3);

    // Well clear of every edge is no edge at all, rather than the least bad one.
    QCOMPARE(edgeNear(square, QPoint(50, 50), 6.0), -1);
    QCOMPARE(edgeNear(square, QPoint(200, 200), 6.0), -1);

    // A ring that encloses nothing has no edge to offer.
    RegionOfInterest line;
    line.vertices = {QPoint(0, 0), QPoint(100, 0)};
    QCOMPARE(edgeNear(line, QPoint(50, 0), 6.0), -1);
}

QTEST_MAIN(TestRoi)
#include "test_roi.moc"
