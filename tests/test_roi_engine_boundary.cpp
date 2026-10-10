// The seam between SurView's region and the engine's shape.
//
// The grid arithmetic is tested against a stand-in predicate (test_poi_grid),
// which leaves one thing unproven: that the boundary a user draws is the same
// boundary the ENGINE tests points against. This is the half that links the
// real engine and checks the conversion is faithful in both directions --
// SurView's open ring in, the engine's closed ring back out.

#include "core/Correlation.h"
#include "core/Roi.h"
#include "core/RoiDetect.h"

#include <QTest>

#include "opencorr.h"

using namespace opencorr;

namespace {

void setOutline(RegionOfInterest &roi, const QVector<QPoint> &corners)
{
    RegionShape outline;
    outline.points = corners;
    roi.shapes = {outline};
}

void addCut(RegionOfInterest &roi, const QVector<QPoint> &corners)
{
    RegionShape cut;
    cut.subtract = true;
    cut.points = corners;
    roi.shapes.append(cut);
}

std::unique_ptr<Polygon2D> toEnginePolygon(const RegionOfInterest &roi)
{
    // The conversion regionInsideTest() performs for a polygon.
    std::vector<int> vertex_x;
    std::vector<int> vertex_y;
    for (const QPoint &vertex : roi.shapes.first().points) {
        vertex_x.push_back(vertex.x());
        vertex_y.push_back(vertex.y());
    }
    return std::make_unique<Polygon2D>(vertex_x, vertex_y);
}

}  // namespace

class TestRoiEngineBoundary : public QObject
{
    Q_OBJECT

private slots:
    void a_drawn_square_encloses_what_it_looks_like_it_encloses();
    void a_concave_region_is_not_treated_as_its_convex_hull();
    void the_engines_bounding_box_agrees_with_the_regions_own();
    void a_detected_boundary_survives_the_round_trip();
    void a_run_measures_nothing_inside_a_triangular_hole();
    void the_drawn_region_and_the_engine_agree_at_every_pixel();
    void a_run_measures_inside_an_island_and_not_around_it();
};

void TestRoiEngineBoundary::a_drawn_square_encloses_what_it_looks_like_it_encloses()
{
    RegionOfInterest roi;
    setOutline(roi, {QPoint(10, 10), QPoint(20, 10), QPoint(20, 20), QPoint(10, 20)});

    const auto polygon = toEnginePolygon(roi);

    QVERIFY(polygon->contains(15, 15));
    QVERIFY(polygon->contains(10, 10));   // a corner counts as inside
    QVERIFY(!polygon->contains(5, 15));
    QVERIFY(!polygon->contains(25, 15));
    QVERIFY(!polygon->contains(15, 25));
}

void TestRoiEngineBoundary::a_concave_region_is_not_treated_as_its_convex_hull()
{
    // The detector routinely returns concave outlines -- the real one measured
    // on the sample pair had 28 corners and a deep notch. A membership test
    // that quietly used the hull would measure points the user excluded.
    RegionOfInterest roi;
    setOutline(roi, {QPoint(0, 0), QPoint(10, 0), QPoint(10, 5),
                    QPoint(5, 5), QPoint(5, 10), QPoint(0, 10)});

    const auto polygon = toEnginePolygon(roi);

    QVERIFY(polygon->contains(2, 2));
    QVERIFY(polygon->contains(8, 2));
    QVERIFY(polygon->contains(2, 8));
    QVERIFY(!polygon->contains(8, 8));   // the notched-out quadrant
}

void TestRoiEngineBoundary::the_engines_bounding_box_agrees_with_the_regions_own()
{
    // The grid is spanned using RegionOfInterest::bounds() while membership is
    // asked of the engine. If the two disagreed the grid would be laid over the
    // wrong rectangle, and points would be silently dropped at one edge.
    RegionOfInterest roi;
    setOutline(roi, {QPoint(13, 7), QPoint(64, 21), QPoint(40, 55), QPoint(9, 33)});

    const auto polygon = toEnginePolygon(roi);
    const QRect box = roi.bounds();

    QCOMPARE(polygon->getMinX(), box.left());
    QCOMPARE(polygon->getMaxX(), box.right());
    QCOMPARE(polygon->getMinY(), box.top());
    QCOMPARE(polygon->getMaxY(), box.bottom());
}

void TestRoiEngineBoundary::a_detected_boundary_survives_the_round_trip()
{
    // What auto-detection actually does: the engine builds a polygon, SurView
    // reads its vertices back to draw and re-use them. The engine stores the
    // ring CLOSED (first vertex repeated), SurView's own is open, and getting
    // that wrong adds a duplicate corner every time a region is re-read.
    RegionOfInterest original;
    setOutline(original, {QPoint(4, 4), QPoint(30, 6), QPoint(28, 25), QPoint(6, 22)});

    const auto polygon = toEnginePolygon(original);

    const std::vector<int> &x = polygon->vertexX();
    const std::vector<int> &y = polygon->vertexY();
    QCOMPARE(polygon->numVertices(), int(original.shapes.first().points.size()));
    QCOMPARE(int(x.size()), polygon->numVertices() + 1);

    RegionOfInterest readBack;
    readBack.origin = RegionOfInterest::Detected;
    readBack.shapes = {RegionShape()};
    for (int i = 0; i < polygon->numVertices(); i++)
        readBack.shapes.first().points.append(QPoint(x[size_t(i)], y[size_t(i)]));

    QCOMPARE(readBack.shapes.first().points, original.shapes.first().points);
    QCOMPARE(readBack.bounds(), original.bounds());

    // And the rebuilt region must select the same pixels.
    const auto rebuilt = toEnginePolygon(readBack);
    for (int py = 0; py < 40; py++) {
        for (int px = 0; px < 40; px++)
            QCOMPARE(rebuilt->contains(px, py), polygon->contains(px, py));
    }
}

void TestRoiEngineBoundary::a_run_measures_nothing_inside_a_triangular_hole()
{
    // ⚑ A HOLE WITH THREE CORNERS IS A HOLE. Three is the smallest ring that
    // encloses anything, and the run drops any hole with fewer -- a rule whose
    // boundary nothing tested here, because every hole in the suite is a
    // rectangle, and to a rectangle "three or more" and "more than three" are
    // the same rule. Under the stricter one a triangular hole is dropped on the
    // way into the engine, the run measures straight across a void the user
    // drew round, and those points correlate background against itself and
    // report confidently that nothing moved -- a cold spot exactly where a
    // specimen concentrates its strain.
    //
    // This is also the first case anywhere that runs a real correlation over a
    // region with a hole at all. The holes have been tested as geometry; the
    // open-hole tension example that ships depends on them reaching the solver.
    RegionOfInterest outer;
    setOutline(outer, {QPoint(40, 30), QPoint(200, 30),
                      QPoint(200, 130), QPoint(40, 130)});

    // ⚑ BOTH SHAPES, and the rectangle is not the afterthought it looks like.
    // A triangle is the BOUNDARY of the rule; a rectangle is what a user
    // actually draws and what the open-hole tension example ships. Testing only
    // the boundary leaves "three or more corners" and "fewer than four corners"
    // indistinguishable, and the second of those honours a triangle while
    // silently dropping every rectangular hole there is. Found by the sweep of
    // 2026-09-10, against this very case.
    RegionOfInterest holed = outer;
    addCut(holed, {QPoint(60, 50), QPoint(170, 50), QPoint(115, 110)});

    RegionOfInterest squareHoled = outer;
    addCut(squareHoled, {QPoint(95, 55), QPoint(140, 55),
                              QPoint(140, 100), QPoint(95, 100)});

    CorrelationSettings settings;
    settings.subsetRadius = 8;
    settings.gridStep = 6;
    settings.maxIterations = 15;
    settings.convergence = 0.001;
    settings.strainEnabled = false;
    settings.recovery.enabled = false;

    // Well inside the triangle at every edge: its sides pass x = 87 and x = 142
    // at the lowest row of this box, so nothing here is a near miss.
    const QRect insideTheHole(105, 60, 20, 20);

    const auto measure = [&settings](const RegionOfInterest &roi) {
        CorrelationRunner runner(settings, roi,
                                 QStringLiteral(SURVIEW_TEST_FIXTURES "/shift_reference.tif"),
                                 QStringLiteral(SURVIEW_TEST_FIXTURES "/shift_target.tif"));
        CorrelationResult result;
        QObject::connect(&runner, &CorrelationRunner::finished,
                         [&result](const CorrelationResult &r) { result = r; });
        runner.run();
        return result;
    };

    const auto countIn = [&insideTheHole](const CorrelationResult &result) {
        int inside = 0;
        for (const CorrelationPoint &point : result.points) {
            if (insideTheHole.contains(QPoint(qRound(point.x), qRound(point.y))))
                inside++;
        }
        return inside;
    };

    // Without the hole the same region puts plenty of points there, which is
    // what stops the case below from passing for want of a grid.
    QVERIFY2(countIn(measure(outer)) > 4,
             "the region without the hole measured almost nothing where the "
             "hole would be, so this case asks nothing");

    QCOMPARE(countIn(measure(holed)), 0);
    QCOMPARE(countIn(measure(squareHoled)), 0);
}

void TestRoiEngineBoundary::the_drawn_region_and_the_engine_agree_at_every_pixel()
{
    // ⚑ THE SEAM ITSELF, pixel by pixel. What is drawn on screen, the subset
    // overlay and the speckle estimate all ask the engine-free regionContains();
    // the run asks the engine through regionInsideTest(). Any pixel on which
    // they differ is a place the picture says one thing and the measurement
    // does another. Before 2026-10-10 they differed along the right and bottom
    // edge of every polygon, and no case looked at every pixel to see it.
    //
    // One region holding every shape kind, both signs, a concave polygon and a
    // slanted edge, overlapping so the order matters, on lopsided sizes and
    // odd-width boxes whose ellipse centres on half pixels.
    //
    // NEGATIVE CHECKS (2026-10-10), each red at the first disagreeing pixel:
    // the mirror's boundary check removed (the defect this case found) fails at
    // (5, 4), a corner; the mirror's ellipse centre rounded fails at (60, 20);
    // the engine applying shapes first to last fails at (100, 15), and the
    // island case below fails on a point measured inside the cut.
    RegionOfInterest roi;
    setOutline(roi, {QPoint(5, 4), QPoint(117, 9), QPoint(121, 83), QPoint(60, 50),
                     QPoint(9, 91)});
    RegionShape ellipse;
    ellipse.kind = RegionShape::Ellipse;
    ellipse.subtract = true;
    ellipse.points = {QPoint(30, 20), QPoint(91, 61)};
    roi.shapes.append(ellipse);
    RegionShape island;
    island.kind = RegionShape::Rectangle;
    island.points = {QPoint(47, 33), QPoint(70, 44)};
    roi.shapes.append(island);
    addCut(roi, {QPoint(100, 15), QPoint(115, 40), QPoint(96, 30)});
    RegionShape patch;
    patch.kind = RegionShape::Ellipse;
    patch.points = {QPoint(130, 60), QPoint(150, 97)};
    roi.shapes.append(patch);

    const auto engine = regionInsideTest(roi);
    int inside = 0;
    for (int y = -2; y < 105; y++) {
        for (int x = -2; x < 160; x++) {
            const bool ours = regionContains(roi, x, y);
            if (ours != engine(x, y)) {
                QFAIL(qPrintable(QStringLiteral("at (%1, %2) the drawn region says %3 "
                                                "and the engine says %4")
                                     .arg(x).arg(y)
                                     .arg(ours ? "inside" : "outside")
                                     .arg(ours ? "outside" : "inside")));
            }
            inside += ours ? 1 : 0;
        }
    }
    // And the region is not empty or everything, so agreeing meant something.
    QVERIFY2(inside > 2000 && inside < 15000, qPrintable(QString::number(inside)));
}

void TestRoiEngineBoundary::a_run_measures_inside_an_island_and_not_around_it()
{
    // The ordered rule reaching the solver: a rectangle, an ellipse cut out of
    // it, and a smaller rectangle put back inside the cut. Points land on the
    // island and in the outer band, and none in the ring of cut between them.
    RegionOfInterest roi;
    RegionShape outer;
    outer.kind = RegionShape::Rectangle;
    outer.points = {QPoint(30, 25), QPoint(210, 135)};
    RegionShape cut;
    cut.kind = RegionShape::Ellipse;
    cut.subtract = true;
    cut.points = {QPoint(60, 40), QPoint(180, 120)};
    RegionShape island;
    island.kind = RegionShape::Rectangle;
    island.points = {QPoint(105, 70), QPoint(135, 90)};
    roi.shapes = {outer, cut, island};

    CorrelationSettings settings;
    settings.subsetRadius = 8;
    settings.gridStep = 6;
    settings.maxIterations = 15;
    settings.convergence = 0.001;
    settings.strainEnabled = false;
    settings.recovery.enabled = false;
    CorrelationRunner runner(settings, roi,
                             QStringLiteral(SURVIEW_TEST_FIXTURES "/shift_reference.tif"),
                             QStringLiteral(SURVIEW_TEST_FIXTURES "/shift_target.tif"));
    CorrelationResult result;
    QObject::connect(&runner, &CorrelationRunner::finished,
                     [&result](const CorrelationResult &r) { result = r; });
    runner.run();

    int onIsland = 0;
    int inBand = 0;
    for (const CorrelationPoint &point : result.points) {
        const int x = qRound(point.x);
        const int y = qRound(point.y);
        const bool island_ = x >= 105 && x <= 135 && y >= 70 && y <= 90;
        const double dx = (x - 120.0) / 60.0;
        const double dy = (y - 80.0) / 40.0;
        const bool inCut = dx * dx + dy * dy <= 1.0;
        QVERIFY2(island_ || !inCut,
                 qPrintable(QStringLiteral("a point at (%1, %2) is in the cut and not "
                                           "on the island").arg(x).arg(y)));
        onIsland += island_ ? 1 : 0;
        inBand += !inCut ? 1 : 0;
    }
    QVERIFY2(onIsland >= 4, qPrintable(QStringLiteral("only %1 points on the island").arg(onIsland)));
    QVERIFY2(inBand >= 20, qPrintable(QStringLiteral("only %1 points in the band").arg(inBand)));
}

QTEST_MAIN(TestRoiEngineBoundary)
#include "test_roi_engine_boundary.moc"
