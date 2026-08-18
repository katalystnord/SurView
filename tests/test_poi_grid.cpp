// Where a run puts its measurement points.
//
// This is the arithmetic that decides what gets measured, and every way it can
// be wrong produces a field that still looks like a field: one step out, a row
// short, or a subset hanging off the image edge reading pixels that do not
// exist. None of that is visible in a picture, which is why it is tested here
// rather than by looking at one.

#include "core/PoiGrid.h"

#include <QTest>

namespace {

// Membership for a solid axis-aligned rectangle, inclusive of its edges. The
// production caller hands in the engine's own polygon test; this stands in for
// it where the point of the test is the GRID, not the shape.
PoiInsideTest insideRect(int left, int top, int right, int bottom)
{
    return [=](int x, int y) {
        return x >= left && x <= right && y >= top && y <= bottom;
    };
}

RegionOfInterest rectRoi(int left, int top, int right, int bottom)
{
    RegionOfInterest roi;
    roi.vertices = {QPoint(left, top), QPoint(right, top),
                    QPoint(right, bottom), QPoint(left, bottom)};
    return roi;
}

}  // namespace

class TestPoiGrid : public QObject
{
    Q_OBJECT

private slots:
    void the_whole_image_grid_starts_one_subset_radius_in();
    void no_point_lets_its_subset_leave_the_image();
    void a_grid_step_larger_than_one_still_lands_inside();
    void a_region_keeps_only_the_points_inside_it();
    void a_region_reaching_past_the_edge_is_held_to_the_safe_margin();
    void every_cell_index_maps_back_to_its_own_row_and_column();
    void an_excluded_cell_leaves_a_gap_not_a_shifted_point();
    void a_subset_too_large_for_the_image_is_refused_in_words();
    void a_region_entirely_in_the_border_margin_is_refused_in_words();
    void a_region_that_falls_between_grid_lines_is_refused_in_words();
    void a_refusal_never_pretends_to_be_an_empty_measurement();
};

void TestPoiGrid::the_whole_image_grid_starts_one_subset_radius_in()
{
    const PoiGrid grid = buildPoiGrid(100, 80, 16, 1, RegionOfInterest(), {});

    QVERIFY(grid.isValid());
    QVERIFY(!grid.restricted);
    QCOMPARE(grid.originX, 16);
    QCOMPARE(grid.originY, 16);
    // Last usable pixel is width-1-radius = 83, so 83-16+1 = 68 columns.
    QCOMPARE(grid.columns, 68);
    QCOMPARE(grid.rows, 48);
    QCOMPARE(grid.cells.size(), 68 * 48);
}

void TestPoiGrid::no_point_lets_its_subset_leave_the_image()
{
    // The invariant that matters most: a subset reads a square of pixels
    // around its centre, and one that reaches outside the image correlates
    // against pixels that were never photographed.
    const int width = 137, height = 91, radius = 12, step = 7;
    const PoiGrid grid =
        buildPoiGrid(width, height, radius, step, RegionOfInterest(), {});

    QVERIFY(grid.isValid());
    QVERIFY(!grid.cells.isEmpty());
    for (const PoiGridCell &cell : grid.cells) {
        QVERIFY(cell.x - radius >= 0);
        QVERIFY(cell.y - radius >= 0);
        QVERIFY(cell.x + radius <= width - 1);
        QVERIFY(cell.y + radius <= height - 1);
    }
}

void TestPoiGrid::a_grid_step_larger_than_one_still_lands_inside()
{
    // A step that does not divide the span evenly must stop short rather than
    // round up past the margin.
    const PoiGrid grid = buildPoiGrid(100, 100, 10, 7, RegionOfInterest(), {});

    QVERIFY(grid.isValid());
    QCOMPARE(grid.originX, 10);
    // Span 10..89 is 80 px; (89-10)/7 + 1 = 12 columns, last at 10+11*7 = 87.
    QCOMPARE(grid.columns, 12);
    const PoiGridCell &last = grid.cells.last();
    QCOMPARE(last.x, 87);
    QVERIFY(last.x + 10 <= 99);
}

void TestPoiGrid::a_region_keeps_only_the_points_inside_it()
{
    const RegionOfInterest roi = rectRoi(40, 30, 60, 50);
    const PoiGrid grid =
        buildPoiGrid(200, 200, 5, 1, roi, insideRect(40, 30, 60, 50));

    QVERIFY(grid.isValid());
    QVERIFY(grid.restricted);
    QCOMPARE(grid.cells.size(), 21 * 21);
    for (const PoiGridCell &cell : grid.cells) {
        QVERIFY(cell.x >= 40 && cell.x <= 60);
        QVERIFY(cell.y >= 30 && cell.y <= 50);
    }
}

void TestPoiGrid::a_region_reaching_past_the_edge_is_held_to_the_safe_margin()
{
    // Drawing a boundary right up to the image border is normal and allowed;
    // it must cost the points that would hang off, not the whole run.
    const RegionOfInterest roi = rectRoi(0, 0, 99, 99);
    const PoiGrid grid =
        buildPoiGrid(100, 100, 16, 1, roi, insideRect(0, 0, 99, 99));

    QVERIFY(grid.isValid());
    QVERIFY(grid.restricted);
    QCOMPARE(grid.originX, 16);
    QCOMPARE(grid.originY, 16);
    for (const PoiGridCell &cell : grid.cells) {
        QVERIFY(cell.x - 16 >= 0);
        QVERIFY(cell.x + 16 <= 99);
    }
}

void TestPoiGrid::every_cell_index_maps_back_to_its_own_row_and_column()
{
    // The index is what a renderer uses to put a value in the right place. If
    // it disagrees with the cell's own coordinates the field is drawn shifted,
    // and a shifted field is still a smooth, believable picture.
    const RegionOfInterest roi = rectRoi(20, 20, 70, 60);
    const PoiGrid grid =
        buildPoiGrid(200, 200, 8, 3, roi, insideRect(20, 20, 70, 60));

    QVERIFY(grid.isValid());
    QVERIFY(!grid.cells.isEmpty());
    for (const PoiGridCell &cell : grid.cells) {
        const int row = cell.gridIndex / grid.columns;
        const int column = cell.gridIndex % grid.columns;
        QCOMPARE(grid.originX + column * grid.step, cell.x);
        QCOMPARE(grid.originY + row * grid.step, cell.y);
        QVERIFY(cell.gridIndex >= 0);
        QVERIFY(cell.gridIndex < grid.cellCount());
    }
}

void TestPoiGrid::an_excluded_cell_leaves_a_gap_not_a_shifted_point()
{
    // A diagonal half of the bounding box: the cells outside it must be absent
    // from the list while the geometry still spans the whole rectangle.
    const RegionOfInterest roi = rectRoi(0, 0, 40, 40);
    const PoiInsideTest lowerTriangle = [](int x, int y) { return y >= x; };

    const PoiGrid grid = buildPoiGrid(100, 100, 1, 1, roi, lowerTriangle);

    QVERIFY(grid.isValid());
    QVERIFY(grid.cells.size() < grid.cellCount());
    for (const PoiGridCell &cell : grid.cells)
        QVERIFY(cell.y >= cell.x);

    // Indices must remain strictly increasing: gaps, never renumbering.
    for (int i = 1; i < grid.cells.size(); i++)
        QVERIFY(grid.cells[i].gridIndex > grid.cells[i - 1].gridIndex);
}

void TestPoiGrid::a_subset_too_large_for_the_image_is_refused_in_words()
{
    const PoiGrid grid = buildPoiGrid(20, 20, 30, 1, RegionOfInterest(), {});

    QVERIFY(!grid.isValid());
    QVERIFY(grid.cells.isEmpty());
    // The refusal has to name the numbers involved, because the fix is to
    // change one of them.
    QVERIFY(grid.refusal.contains(QStringLiteral("30")));
    QVERIFY(grid.refusal.contains(QStringLiteral("20")));
}

void TestPoiGrid::a_region_entirely_in_the_border_margin_is_refused_in_words()
{
    const RegionOfInterest roi = rectRoi(0, 0, 5, 5);
    const PoiGrid grid = buildPoiGrid(200, 200, 20, 1, roi, insideRect(0, 0, 5, 5));

    QVERIFY(!grid.isValid());
    QVERIFY(grid.refusal.contains(QStringLiteral("region of interest")));
    QVERIFY(grid.refusal.contains(QStringLiteral("20")));
}

void TestPoiGrid::a_region_that_falls_between_grid_lines_is_refused_in_words()
{
    // Reachable only through SHAPE, not through size -- which is worth knowing,
    // and is why this case is written the way it is. The grid is anchored to the
    // region's own bounding box, so its first lattice point IS the box corner;
    // a rectangle therefore always keeps at least that one point however small
    // it is. What can still come out empty is a shape that occupies the middle
    // of a bounding box the step only samples at the edges -- a small blob with
    // a coarse step, which is an ordinary thing for a user to ask for.
    const RegionOfInterest roi = rectRoi(20, 20, 60, 60);   // box the grid spans
    const PoiInsideTest centralBlob = [](int x, int y) {
        return x >= 30 && x <= 50 && y >= 30 && y <= 50;    // shape inside it
    };

    // Step 40 over a 20..60 box samples only x = 20 and 60, y = 20 and 60 --
    // all four corners, none of them in the blob.
    const PoiGrid grid = buildPoiGrid(200, 200, 5, 40, roi, centralBlob);

    QVERIFY(!grid.isValid());
    QVERIFY(grid.cells.isEmpty());
    // The refusal names the step, because reducing it is the fix.
    QVERIFY(grid.refusal.contains(QStringLiteral("40")));
}

void TestPoiGrid::a_refusal_never_pretends_to_be_an_empty_measurement()
{
    // The distinction the whole type exists for: "nothing could be set up" is
    // not "everything was measured and nothing was found". A caller that
    // cannot tell them apart reports a clean run over zero points.
    const PoiGrid refused = buildPoiGrid(0, 0, 16, 1, RegionOfInterest(), {});
    QVERIFY(!refused.isValid());
    QVERIFY(!refused.refusal.isEmpty());

    const PoiGrid built = buildPoiGrid(100, 100, 16, 1, RegionOfInterest(), {});
    QVERIFY(built.isValid());
    QVERIFY(built.refusal.isEmpty());
    QVERIFY(!built.cells.isEmpty());
}

QTEST_MAIN(TestPoiGrid)
#include "test_poi_grid.moc"
