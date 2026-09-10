// The seam between SurView's region and the engine's shape.
//
// The grid arithmetic is tested against a stand-in predicate (test_poi_grid),
// which leaves one thing unproven: that the boundary a user draws is the same
// boundary the ENGINE tests points against. This is the half that links the
// real engine and checks the conversion is faithful in both directions --
// SurView's open ring in, the engine's closed ring back out.

#include "core/Correlation.h"
#include "core/Roi.h"

#include <QTest>

#include "opencorr.h"

using namespace opencorr;

namespace {

std::unique_ptr<Polygon2D> toEnginePolygon(const RegionOfInterest &roi)
{
    // The same conversion Correlation.cpp performs.
    std::vector<int> vertex_x;
    std::vector<int> vertex_y;
    for (const QPoint &vertex : roi.vertices) {
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
};

void TestRoiEngineBoundary::a_drawn_square_encloses_what_it_looks_like_it_encloses()
{
    RegionOfInterest roi;
    roi.vertices = {QPoint(10, 10), QPoint(20, 10), QPoint(20, 20), QPoint(10, 20)};

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
    roi.vertices = {QPoint(0, 0), QPoint(10, 0), QPoint(10, 5),
                    QPoint(5, 5), QPoint(5, 10), QPoint(0, 10)};

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
    roi.vertices = {QPoint(13, 7), QPoint(64, 21), QPoint(40, 55), QPoint(9, 33)};

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
    original.vertices = {QPoint(4, 4), QPoint(30, 6), QPoint(28, 25), QPoint(6, 22)};

    const auto polygon = toEnginePolygon(original);

    const std::vector<int> &x = polygon->vertexX();
    const std::vector<int> &y = polygon->vertexY();
    QCOMPARE(polygon->numVertices(), int(original.vertices.size()));
    QCOMPARE(int(x.size()), polygon->numVertices() + 1);

    RegionOfInterest readBack;
    readBack.origin = RegionOfInterest::Detected;
    for (int i = 0; i < polygon->numVertices(); i++)
        readBack.vertices.append(QPoint(x[size_t(i)], y[size_t(i)]));

    QCOMPARE(readBack.vertices, original.vertices);
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
    outer.vertices = {QPoint(40, 30), QPoint(200, 30),
                      QPoint(200, 130), QPoint(40, 130)};

    RegionOfInterest holed = outer;
    holed.holes.append({QPoint(60, 50), QPoint(170, 50), QPoint(115, 110)});

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
}

QTEST_MAIN(TestRoiEngineBoundary)
#include "test_roi_engine_boundary.moc"
